//
// routes.cpp — API aligned with Metrics Spec (v0)
//
#include "routes.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"
#include "config.h"
#include "metrics/metric_key.h"
#include "store/memory_store.h"

#include <chrono>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using json   = nlohmann::json;
using Clock  = std::chrono::steady_clock;

// ----------------------------- registry -------------------------------------

struct MetricDesc {
    const char* unit;                       // required unit
    std::vector<std::string> labels;        // allowed labels for this metric
};

// Core Metrics (v0) — exactly as in your spec
static const std::unordered_map<std::string, MetricDesc> kRegistry = {
        // CPU
        {"cpu.total_pct",   {"%",        {"host"}}},
        {"cpu.core_pct",    {"%",        {"host","core"}}},

        // Memory
        {"mem.used_bytes",  {"bytes",    {"host"}}},
        {"mem.free_bytes",  {"bytes",    {"host"}}},

        // Disk I/O (bytes/sec per spec; your sampler writes read/write per second)
        {"disk.read_bytes", {"bytes/sec",{"host","dev"}}},
        {"disk.write_bytes",{"bytes/sec",{"host","dev"}}},
        {"disk.util_pct",   {"%",        {"host","dev"}}}, // optional

        // Network (bytes/sec per spec)
        {"net.rx_bytes",    {"bytes/sec",{"host","iface"}}},
        {"net.tx_bytes",    {"bytes/sec",{"host","iface"}}},

        // Processes (summary) — timeseries only for count; top is table via another route later
        {"proc.count",      {"count",    {"host"}}}
};

// Allowed label universe (guardrail)
static const std::unordered_set<std::string> kAllowedLabelUniverse = {
        "host","core","dev","iface","pid","comm"
};

// ----------------------------- helpers --------------------------------------

static const auto kStartedAt = Clock::now();

static void set_cors(httplib::Server& svr) {
    svr.set_default_headers({
                                    {"Access-Control-Allow-Origin",  "*"},
                                    {"Access-Control-Allow-Methods", "GET, OPTIONS"},
                                    {"Access-Control-Allow-Headers", "Content-Type"}
                            });
    svr.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res){
        res.status = 204;
    });
}

static void write_json(httplib::Response& res, const json& j, int status=200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

static void write_error(httplib::Response& res, int code, std::string msg) {
    write_json(res, json{
            {"error", {{"code", code}, {"message", std::move(msg)}}}
    }, code);
}

static std::optional<long long> parse_ll(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

static std::unordered_map<std::string, std::string>
parse_label_filters(const std::string& s) {
    // key:value,key2:value2
    std::unordered_map<std::string, std::string> out;
    if (s.empty()) return out;
    std::istringstream ss(s);
    std::string kv;
    while (std::getline(ss, kv, ',')) {
        auto pos = kv.find(':');
        if (pos == std::string::npos) continue;
        std::string k = kv.substr(0, pos);
        std::string v = kv.substr(pos + 1);
        if (!k.empty() && !v.empty()) out.emplace(std::move(k), std::move(v));
    }
    return out;
}

static std::string build_selector(const std::string& name,
                                  const std::unordered_map<std::string, std::string>& labels) {
    if (labels.empty()) return name;
    std::ostringstream os;
    os << name << "{";
    bool first = true;
    for (const auto& [k, v] : labels) {
        if (!first) os << ",";
        first = false;
        os << k << "=" << v;
    }
    os << "}";
    return os.str();
}

static bool validate_metric_and_labels(const std::string& metric_name,
                                       const std::unordered_map<std::string, std::string>& labels,
                                       std::string& err_msg) {
    auto it = kRegistry.find(metric_name);
    if (it == kRegistry.end()) {
        err_msg = "Unknown metric '" + metric_name + "'";
        return false;
    }
    const auto& allowed = it->second.labels;

    // 1) ensure keys are in allowed list for this metric
    const std::unordered_set<std::string> allowed_set(allowed.begin(), allowed.end());
    for (const auto& [k, _] : labels) {
        if (!allowed_set.count(k)) {
            err_msg = "Label '" + k + "' not allowed for metric '" + metric_name + "'";
            return false;
        }
        if (!kAllowedLabelUniverse.count(k)) {
            err_msg = "Label '" + k + "' is not in the global allowed label set";
            return false;
        }
    }
    return true;
}

static const char* unit_for_metric(const std::string& metric_name) {
    auto it = kRegistry.find(metric_name);
    if (it != kRegistry.end()) return it->second.unit;
    // Fallback (shouldn’t happen if validation is on the happy path)
    if (metric_name.find("pct") != std::string::npos) return "%";
    if (metric_name.find("bytes") != std::string::npos) {
        // Prefer /sec when ambiguous (disk/net names in v0 are bytes/sec)
        if (metric_name.find("read") != std::string::npos ||
            metric_name.find("write") != std::string::npos ||
            metric_name.find("rx") != std::string::npos ||
            metric_name.find("tx") != std::string::npos) {
            return "bytes/sec";
        }
        return "bytes";
    }
    if (metric_name.find("count") != std::string::npos) return "count";
    return "value";
}

static void write_csv(httplib::Response& res,
                      const std::vector<Sample>& data,
                      const std::string& filename = "export.csv") {
    std::ostringstream os;
    os << "timestamp,value\n";
    for (const auto& s : data) os << s.ts_ms << "," << s.value << "\n";
    res.status = 200;
    res.set_content(os.str(), "text/csv");
    res.set_header("Content-Disposition", ("attachment; filename=\"" + filename + "\"").c_str());
}

// ------------------------------- routes -------------------------------------

void bind_routes(httplib::Server& svr, MemoryStore& store) {
    set_cors(svr);

    // /api/status
    svr.Get("/api/status", [&store](const httplib::Request&, httplib::Response& res){
        const auto uptime_s =
                std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - kStartedAt).count();

        // If you don’t have these methods yet, stub to 0 / 0.0 or remove.
        json j{
                {"status", "ok"},
                {"uptime_s", uptime_s},
                {"metrics_collected", 0}, // implement or stub
                {"store_size_mb", 0}           // implement or stub
        };
        write_json(res, j);
    });

    // /api/metrics — derived from the registry
    svr.Get("/api/metrics", [](const httplib::Request&, httplib::Response& res){
        json arr = json::array();
        for (const auto& [name, desc] : kRegistry) {
            json labels = json::array();
            for (const auto& l : desc.labels) labels.push_back(l);
            arr.push_back({
                                  {"name",   name},
                                  {"unit",   desc.unit},
                                  {"labels", labels}
                          });
        }
        write_json(res, json{{"metrics", arr}});
    });

    // /api/presets — as spec
    svr.Get("/api/presets", [](const httplib::Request&, httplib::Response& res){
        write_json(res, json{
                {"presets", json::array({
                                                json{{"name","15m"},{"seconds",900}},
                                                json{{"name","1h"},{"seconds",3600}},
                                                json{{"name","24h"},{"seconds",86400}}
                                        })}
        });
    });

    // /api/timeseries
    svr.Get("/api/timeseries", [&store](const httplib::Request& req, httplib::Response& res){
        const std::string metric_name = req.get_param_value("metric");
        const std::string from_str    = req.get_param_value("from");
        const std::string to_str      = req.get_param_value("to");

        if (metric_name.empty())
            return write_error(res, 400, "Missing required parameter 'metric'");
        if (from_str.empty())
            return write_error(res, 400, "Missing required parameter 'from'");
        if (to_str.empty())
            return write_error(res, 400, "Missing required parameter 'to'");

        const auto from_ms = parse_ll(from_str);
        const auto to_ms   = parse_ll(to_str);
        if (!from_ms.has_value() || !to_ms.has_value())
            return write_error(res, 400, "Parameters 'from' and 'to' must be epoch milliseconds (integers)");
        if (*from_ms > *to_ms)
            return write_error(res, 400, "'from' must be <= 'to'");

        // labels (key:value,...) + default host
        auto labels = parse_label_filters(req.get_param_value("labels"));
        if (labels.find("host") == labels.end() && !cfg::HOST_LABEL.empty())
            labels.emplace("host", cfg::HOST_LABEL);

        // validate metric + labels against registry
        std::string err;
        if (!validate_metric_and_labels(metric_name, labels, err)) {
            return write_error(res, 422, err);
        }

        // optional echo-throughs
        const std::string rollup = req.has_param("rollup") ? req.get_param_value("rollup") : "raw";
        const auto step_opt  = parse_ll(req.get_param_value("step"));   // seconds; not applied in v0
        const auto limit_opt = parse_ll(req.get_param_value("limit"));
        const long long limit = (limit_opt && *limit_opt > 0) ? *limit_opt : 10000;

        const auto selector = build_selector(metric_name, labels);

        std::vector<Sample> data = store.query(selector, *from_ms, *to_ms); // raw only in v0
        if (static_cast<long long>(data.size()) > limit) {
            data.erase(data.begin(), data.end() - static_cast<size_t>(limit));
        }

        json samples = json::array();
        for (const auto& s : data) samples.push_back({s.ts_ms, s.value});

        json labels_json = json::object();
        for (const auto& [k, v] : labels) labels_json[k] = v;

        write_json(res, json{
                {"metric",  metric_name},
                {"unit",    unit_for_metric(metric_name)},
                {"rollup",  rollup},
                {"labels",  labels_json},
                {"samples", samples}
        });
    });

    // /api/export — same query params as /timeseries + format
    svr.Get("/api/export", [&store](const httplib::Request& req, httplib::Response& res){
        const std::string metric_name = req.get_param_value("metric");
        const std::string from_str    = req.get_param_value("from");
        const std::string to_str      = req.get_param_value("to");
        const std::string format      = req.get_param_value("format");

        if (metric_name.empty())
            return write_error(res, 400, "Missing required parameter 'metric'");
        if (from_str.empty() || to_str.empty())
            return write_error(res, 400, "Missing required parameter 'from' or 'to'");
        if (format != "csv" && format != "json")
            return write_error(res, 400, "Parameter 'format' must be 'csv' or 'json'");

        const auto from_ms = parse_ll(from_str);
        const auto to_ms   = parse_ll(to_str);
        if (!from_ms.has_value() || !to_ms.has_value())
            return write_error(res, 400, "Parameters 'from' and 'to' must be epoch milliseconds (integers)");
        if (*from_ms > *to_ms)
            return write_error(res, 400, "'from' must be <= 'to'");

        auto labels = parse_label_filters(req.get_param_value("labels"));
        if (labels.find("host") == labels.end() && !cfg::HOST_LABEL.empty())
            labels.emplace("host", cfg::HOST_LABEL);

        std::string err;
        if (!validate_metric_and_labels(metric_name, labels, err)) {
            return write_error(res, 422, err);
        }

        const auto limit_opt = parse_ll(req.get_param_value("limit"));
        const long long limit = (limit_opt && *limit_opt > 0) ? *limit_opt : std::numeric_limits<long long>::max();

        const auto selector = build_selector(metric_name, labels);

        std::vector<Sample> data = store.query(selector, *from_ms, *to_ms);
        if (static_cast<long long>(data.size()) > limit) {
            data.erase(data.begin(), data.end() - static_cast<size_t>(limit));
        }

        if (format == "csv") {
            return write_csv(res, data, "export.csv");
        }

        // format == json — mirror /timeseries payload
        json samples = json::array();
        for (const auto& s : data) samples.push_back({s.ts_ms, s.value});

        json labels_json = json::object();
        for (const auto& [k, v] : labels) labels_json[k] = v;

        write_json(res, json{
                {"metric",  metric_name},
                {"unit",    unit_for_metric(metric_name)},
                {"rollup",  "raw"},
                {"labels",  labels_json},
                {"samples", samples}
        });
    });
}
