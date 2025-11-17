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
        {"mem.used",  {"bytes",    {"host"}}},
        {"mem.free",  {"bytes",    {"host"}}},

        {"disk.read", {"bytes/sec",{"host","dev"}}},
        {"disk.write",{"bytes/sec",{"host","dev"}}},

        // Network (bytes/sec per spec)
        {"net.rx",    {"bytes/sec",{"host","iface"}}},
        {"net.tx",    {"bytes/sec",{"host","iface"}}},

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


    svr.Get("/api/info", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string key = req.get_param_value("key");

        // Case 1: no key -> return all metadata
        if (key.empty()) {
            return write_json(res, store.all_metadata());
        }

        // Case 2: key provided -> return metadata[key]
        json data = store.get_metadata(key);
        if (data.is_null() || data.empty()) {
            return write_error(res, 400, "No key found");
        }

        return write_json(res, data);
    });

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


    // /api/stored — enumerate all stored metrics and the observed label values
    svr.Get("/api/stored", [&store](const httplib::Request&, httplib::Response& res) {

        using json = nlohmann::json;

        // ---- Helper: parse "metric{key=val,key2=val2}" ----
        struct Parts {
            std::string metric;
            std::unordered_map<std::string, std::string> labels;
        };

        auto parse_selector = [&](const std::string& sel) -> Parts {
            Parts p;

            auto brace = sel.find('{');
            if (brace == std::string::npos) {
                p.metric = sel;
                return p;
            }

            p.metric = sel.substr(0, brace);

            auto close = sel.find('}', brace);
            if (close == std::string::npos) return p;

            std::string inside = sel.substr(brace + 1, close - brace - 1);
            std::istringstream ss(inside);
            std::string kv;

            while (std::getline(ss, kv, ',')) {
                auto pos = kv.find('=');
                if (pos == std::string::npos) continue;
                std::string k = kv.substr(0, pos);
                std::string v = kv.substr(pos + 1);
                p.labels[k] = v;
            }

            return p;
        };

        // -----------------------------
        // 1) Get all stored selectors
        // -----------------------------
        std::vector<std::string> selectors = store.list_series_keys();

        // maps metric → (label → set<label-values>)
        std::unordered_map<std::string,
                std::unordered_map<std::string, std::unordered_set<std::string>>
        > agg;

        // also track whether the metric is stored as vector or scalar
        std::unordered_map<std::string, std::string> metric_kind;

        for (const auto& sel : selectors) {
            Parts p = parse_selector(sel);
            if (p.metric.empty()) continue;

            // detect kind by naming convention
            // scalar: single-value metrics
            // vector: multi-value metrics (like "cpu.core_pct")
            if (p.metric == "cpu.core_pct") {
                metric_kind[p.metric] = "vector";
            } else {
                metric_kind[p.metric] = "scalar";
            }

            auto& label_map = agg[p.metric];

            for (const auto& [k, v] : p.labels) {
                label_map[k].insert(v);
            }
        }

        // -----------------------------
        // 2) Build JSON output
        // -----------------------------
        json out_metrics = json::array();

        for (const auto& [metric, labels_map] : agg) {

            json label_obj = json::object();
            for (const auto& [label_key, value_set] : labels_map) {
                std::vector<std::string> vals(value_set.begin(), value_set.end());
                std::sort(vals.begin(), vals.end());
                label_obj[label_key] = vals;
            }

            // unit inference by simple naming convention
            std::string unit;
            if (metric.find("pct") != std::string::npos)       unit = "%";
            else if (metric.find("bytes") != std::string::npos) unit = "bytes";
            else                                                unit = "value";

            out_metrics.push_back({
                                          {"name",  metric},
                                          {"kind",  metric_kind[metric]},
                                          {"unit",  unit},
                                          {"labels", label_obj}
                                  });
        }

        // sort metrics alphabetically for stable output
        std::sort(out_metrics.begin(), out_metrics.end(),
                  [](const json& a, const json& b){
                      return a["name"].get<std::string>() < b["name"].get<std::string>();
                  });

        write_json(res, json{
                {"metrics", out_metrics}
        });
    });



    // /api/query
    svr.Get("/api/query", [&store](const httplib::Request& req, httplib::Response& res) {
        const std::string metric_name = req.get_param_value("metric");
        if (metric_name.empty())
            return write_error(res, 400, "Missing ?metric");

        // Parse optional params
        const auto from_opt = parse_ll(req.get_param_value("from"));
        const auto to_opt   = parse_ll(req.get_param_value("to"));
        long long from_ms = from_opt.value_or(0);
        long long to_ms   = to_opt.value_or(std::numeric_limits<long long>::max());

        auto labels = parse_label_filters(req.get_param_value("labels"));
        if (labels.find("host") == labels.end() && !cfg::HOST_LABEL.empty())
            labels.emplace("host", cfg::HOST_LABEL);

        // Validate against registry
        std::string err;
        if (!validate_metric_and_labels(metric_name, labels, err))
            return write_error(res, 422, err);

        const auto selector = build_selector(metric_name, labels);

        // detect vector metric by checking if it exists in vec store
        bool is_vector = store.vec_series_exists(selector);

        json samples = json::array();

        if (is_vector) {
            auto rows = store.query_vector(selector, from_ms, to_ms);
            for (auto &r : rows)
                samples.push_back({ r.ts_ms, r.vals });
        } else {
            auto rows = store.query(selector, from_ms, to_ms);
            for (auto &r : rows)
                samples.push_back({ r.ts_ms, r.value });
        }

        json labels_json = json::object();
        for (auto &[k,v] : labels) labels_json[k] = v;


        write_json(res, json{
                {"metric", metric_name},
                {"unit",   unit_for_metric(metric_name)},
                {"labels", labels_json},
                {"samples", samples},
                {"vector", is_vector}
        });
    });

    // in your route binder
    svr.Get("/api/processes", [&store](const httplib::Request&, httplib::Response& res){
        nlohmann::json j = store.get_snapshot("processes");
        if (j.is_null()) j = nlohmann::json::array();
        write_json(res, j);
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
