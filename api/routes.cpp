// routes.cpp — HTTP API bindings for the monitoring dashboard.
// Exposes JSON/CSV endpoints that surface metrics collected in MemoryStore.

#include "routes.h"

#include "config.h"
#include "metrics/metric_key.h"
#include "store/memory_store.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct MetricDesc {
    const char* unit;
    std::vector<std::string> labels;
};

struct MetricSelectorParts {
    std::string metric;
    std::unordered_map<std::string, std::string> labels;
};

const std::unordered_map<std::string, MetricDesc> kMetricRegistry = {
        {"cpu.total_pct", {"%", {"host"}}},
        {"cpu.core_pct", {"%", {"host", "core"}}},
        {"mem.used", {"bytes", {"host"}}},
        {"mem.free", {"bytes", {"host"}}},
        {"disk.read", {"bytes/sec", {"host", "dev"}}},
        {"disk.write", {"bytes/sec", {"host", "dev"}}},
        {"net.rx", {"bytes/sec", {"host", "iface"}}},
        {"net.tx", {"bytes/sec", {"host", "iface"}}},
};

const std::unordered_set<std::string> kPermittedLabelUniverse = {
        "host", "core", "dev", "iface", "pid", "comm"
};

const auto kStartedAt = Clock::now();

/**
 * Configure permissive CORS headers so that the dashboard UI can query the API
 * directly from any origin.
 */
void configure_cors(httplib::Server& server) {
    server.set_default_headers({
            {"Access-Control-Allow-Origin", "*"},
            {"Access-Control-Allow-Methods", "GET, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type"}
    });
    server.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });
}

/**
 * Serialize JSON and status code into the outgoing response.
 */
void write_json_response(httplib::Response& res, const json& payload, int status = 200) {
    res.status = status;
    res.set_content(payload.dump(), "application/json");
}

/**
 * Emit a uniform error structure ({"error": {code, message}}).
 */
void write_error_response(httplib::Response& res, int status, std::string message) {
    write_json_response(res, json{{"error", {{"code", status}, {"message", std::move(message)}}}}, status);
}

/**
 * Parse stringified integers (base 10). Returns nullopt on failure.
 */
std::optional<long long> parse_int64(const std::string& candidate) {
    if (candidate.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(candidate.c_str(), &end, 10);
    if (errno != 0 || end == candidate.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return value;
}

/**
 * Parse `key:value,key2:value2` label filters used by query/export endpoints.
 */
std::unordered_map<std::string, std::string> parse_label_filters(const std::string& encoded) {
    std::unordered_map<std::string, std::string> labels;
    if (encoded.empty()) {
        return labels;
    }

    std::istringstream encoded_stream(encoded);
    std::string token;
    while (std::getline(encoded_stream, token, ',')) {
        const auto separator = token.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        std::string key = token.substr(0, separator);
        std::string value = token.substr(separator + 1);
        if (!key.empty() && !value.empty()) {
            labels.emplace(std::move(key), std::move(value));
        }
    }

    return labels;
}

/**
 * Render the fully-qualified selector `metric{label=value}` used as the
 * MemoryStore key.
 */
std::string build_selector(const std::string& metric_name,
                           const std::unordered_map<std::string, std::string>& labels) {
    if (labels.empty()) {
        return metric_name;
    }

    std::ostringstream selector;
    selector << metric_name << "{";
    bool first = true;
    for (const auto& [key, value] : labels) {
        if (!first) {
            selector << ",";
        }
        first = false;
        selector << key << "=" << value;
    }
    selector << "}";
    return selector.str();
}

/**
 * Validate that the requested metric exists and supplied labels are allowed.
 */
bool validate_metric_and_labels(const std::string& metric_name,
                                const std::unordered_map<std::string, std::string>& labels,
                                std::string& error_message) {
    const auto registry_it = kMetricRegistry.find(metric_name);
    if (registry_it == kMetricRegistry.end()) {
        error_message = "Unknown metric '" + metric_name + "'";
        return false;
    }

    const std::unordered_set<std::string> allowed_labels(registry_it->second.labels.begin(),
                                                         registry_it->second.labels.end());
    for (const auto& [label_key, _] : labels) {
        if (!allowed_labels.count(label_key)) {
            error_message = "Label '" + label_key + "' not allowed for metric '" + metric_name + "'";
            return false;
        }
        if (!kPermittedLabelUniverse.count(label_key)) {
            error_message = "Label '" + label_key + "' is not in the allowed label universe";
            return false;
        }
    }

    return true;
}

/**
 * Determine the preferred unit string for a metric name.
 */
const char* infer_unit_for_metric(const std::string& metric_name) {
    if (const auto registry_it = kMetricRegistry.find(metric_name); registry_it != kMetricRegistry.end()) {
        return registry_it->second.unit;
    }

    if (metric_name.find("pct") != std::string::npos) {
        return "%";
    }
    if (metric_name.find("bytes") != std::string::npos) {
        if (metric_name.find("read") != std::string::npos ||
            metric_name.find("write") != std::string::npos ||
            metric_name.find("rx") != std::string::npos ||
            metric_name.find("tx") != std::string::npos) {
            return "bytes/sec";
        }
        return "bytes";
    }
    if (metric_name.find("count") != std::string::npos) {
        return "count";
    }
    return "value";
}

/**
 * Export samples to CSV, matching the column order expected by the UI.
 */
void write_csv_response(httplib::Response& res,
                        const std::vector<Sample>& samples,
                        const std::string& filename = "export.csv") {
    std::ostringstream csv;
    csv << "timestamp,value\n";
    for (const auto& sample : samples) {
        csv << sample.ts_ms << "," << sample.value << "\n";
    }
    res.status = 200;
    res.set_content(csv.str(), "text/csv");
    res.set_header("Content-Disposition", ("attachment; filename=\"" + filename + "\"").c_str());
}

/**
 * Parse selectors such as `metric{key=value}` from stored series keys.
 */
MetricSelectorParts parse_selector(const std::string& selector) {
    MetricSelectorParts parts;

    const auto open_brace = selector.find('{');
    if (open_brace == std::string::npos) {
        parts.metric = selector;
        return parts;
    }

    parts.metric = selector.substr(0, open_brace);
    const auto close_brace = selector.find('}', open_brace);
    if (close_brace == std::string::npos) {
        return parts;
    }

    std::string inside = selector.substr(open_brace + 1, close_brace - open_brace - 1);
    std::istringstream inside_stream(inside);
    std::string token;
    while (std::getline(inside_stream, token, ',')) {
        const auto equals = token.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        std::string key = token.substr(0, equals);
        std::string value = token.substr(equals + 1);
        parts.labels[key] = value;
    }

    return parts;
}

/**
 * Convert metric selectors stored in MemoryStore into a user-friendly summary.
 */
json describe_stored_metrics(const MemoryStore& store) {
    const std::vector<std::string> selectors = store.list_series_keys();

    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_set<std::string>>> values_by_metric;
    std::unordered_map<std::string, std::string> metric_kind;

    for (const auto& selector : selectors) {
        MetricSelectorParts parts = parse_selector(selector);
        if (parts.metric.empty()) {
            continue;
        }

        metric_kind[parts.metric] = (parts.metric == "cpu.core_pct") ? "vector" : "scalar";
        auto& label_map = values_by_metric[parts.metric];
        for (const auto& [label_key, label_value] : parts.labels) {
            label_map[label_key].insert(label_value);
        }
    }

    json metrics_array = json::array();
    for (const auto& [metric, labels_map] : values_by_metric) {
        json label_values = json::object();
        for (const auto& [label_key, value_set] : labels_map) {
            std::vector<std::string> values(value_set.begin(), value_set.end());
            std::sort(values.begin(), values.end());
            label_values[label_key] = values;
        }

        std::string unit;
        if (metric.find("pct") != std::string::npos) {
            unit = "%";
        } else if (metric.find("bytes") != std::string::npos) {
            unit = "bytes";
        } else {
            unit = "value";
        }

        metrics_array.push_back({
                {"name", metric},
                {"kind", metric_kind[metric]},
                {"unit", unit},
                {"labels", label_values}
        });
    }

    std::sort(metrics_array.begin(), metrics_array.end(), [](const json& lhs, const json& rhs) {
        return lhs["name"].get<std::string>() < rhs["name"].get<std::string>();
    });

    return json{{"metrics", metrics_array}};
}

/**
 * Convert label map to JSON object for responses.
 */
json labels_to_json(const std::unordered_map<std::string, std::string>& labels) {
    json labels_json = json::object();
    for (const auto& [key, value] : labels) {
        labels_json[key] = value;
    }
    return labels_json;
}
} // namespace

// ------------------------------- routes -------------------------------------

/**
 * Bind all HTTP routes exposed by the monitoring API.
 */
void bind_routes(httplib::Server& svr, MemoryStore& store) {
    configure_cors(svr);

    svr.Get("/api/info", [&store](const httplib::Request& req, httplib::Response& res) {
        const std::string key = req.get_param_value("key");
        if (key.empty()) {
            return write_json_response(res, store.all_metadata());
        }

        const json data = store.get_metadata(key);
        if (data.is_null() || data.empty()) {
            return write_error_response(res, 400, "No key found");
        }
        return write_json_response(res, data);
    });

    svr.Get("/api/status", [&store](const httplib::Request&, httplib::Response& res) {
        const auto uptime_seconds =
                std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - kStartedAt).count();

        json payload{{"status", "ok"},
                     {"uptime_s", uptime_seconds},
                     {"metrics_collected", 0},
                     {"store_size_mb", 0}};
        write_json_response(res, payload);
    });

    svr.Get("/api/metrics", [](const httplib::Request&, httplib::Response& res) {
        json registry_array = json::array();
        for (const auto& [name, desc] : kMetricRegistry) {
            json allowed_labels = json::array();
            for (const auto& label : desc.labels) {
                allowed_labels.push_back(label);
            }
            registry_array.push_back({
                    {"name", name},
                    {"unit", desc.unit},
                    {"labels", allowed_labels}
            });
        }
        write_json_response(res, json{{"metrics", registry_array}});
    });

    svr.Get("/api/stored", [&store](const httplib::Request&, httplib::Response& res) {
        write_json_response(res, describe_stored_metrics(store));
    });

    svr.Get("/api/query", [&store](const httplib::Request& req, httplib::Response& res) {
        const std::string metric_name = req.get_param_value("metric");
        if (metric_name.empty()) {
            return write_error_response(res, 400, "Missing ?metric");
        }

        const auto from_ms = parse_int64(req.get_param_value("from")).value_or(0);
        const auto to_ms = parse_int64(req.get_param_value("to")).value_or(std::numeric_limits<long long>::max());

        auto labels = parse_label_filters(req.get_param_value("labels"));
        if (!cfg::HOST_LABEL.empty() && labels.find("host") == labels.end()) {
            labels.emplace("host", cfg::HOST_LABEL);
        }

        std::string error_message;
        if (!validate_metric_and_labels(metric_name, labels, error_message)) {
            return write_error_response(res, 422, error_message);
        }

        const std::string selector = build_selector(metric_name, labels);
        const bool is_vector_metric = store.vec_series_exists(selector);

        json samples = json::array();
        if (is_vector_metric) {
            for (const auto& sample : store.query_vector(selector, from_ms, to_ms)) {
                samples.push_back({sample.ts_ms, sample.vals});
            }
        } else {
            for (const auto& sample : store.query(selector, from_ms, to_ms)) {
                samples.push_back({sample.ts_ms, sample.value});
            }
        }

        write_json_response(res, json{{"metric", metric_name},
                                      {"unit", infer_unit_for_metric(metric_name)},
                                      {"labels", labels_to_json(labels)},
                                      {"samples", samples},
                                      {"vector", is_vector_metric}});
    });

    svr.Get("/api/processes", [&store](const httplib::Request&, httplib::Response& res) {
        json snapshot = store.get_snapshot("processes");
        if (snapshot.is_null()) {
            snapshot = json::array();
        }
        write_json_response(res, snapshot);
    });

    svr.Get("/api/export", [&store](const httplib::Request& req, httplib::Response& res) {
        const std::string metric_name = req.get_param_value("metric");
        const std::string from_str = req.get_param_value("from");
        const std::string to_str = req.get_param_value("to");
        const std::string format = req.get_param_value("format");

        if (metric_name.empty()) {
            return write_error_response(res, 400, "Missing required parameter 'metric'");
        }
        if (from_str.empty() || to_str.empty()) {
            return write_error_response(res, 400, "Missing required parameter 'from' or 'to'");
        }
        if (format != "csv" && format != "json") {
            return write_error_response(res, 400, "Parameter 'format' must be 'csv' or 'json'");
        }

        const auto from_ms = parse_int64(from_str);
        const auto to_ms = parse_int64(to_str);
        if (!from_ms.has_value() || !to_ms.has_value()) {
            return write_error_response(res, 400, "Parameters 'from' and 'to' must be epoch milliseconds (integers)");
        }
        if (*from_ms > *to_ms) {
            return write_error_response(res, 400, "'from' must be <= 'to'");
        }

        auto labels = parse_label_filters(req.get_param_value("labels"));
        if (!cfg::HOST_LABEL.empty() && labels.find("host") == labels.end()) {
            labels.emplace("host", cfg::HOST_LABEL);
        }

        std::string error_message;
        if (!validate_metric_and_labels(metric_name, labels, error_message)) {
            return write_error_response(res, 422, error_message);
        }

        const auto limit_opt = parse_int64(req.get_param_value("limit"));
        const long long limit = (limit_opt && *limit_opt > 0) ? *limit_opt : std::numeric_limits<long long>::max();

        const std::string selector = build_selector(metric_name, labels);
        std::vector<Sample> rows = store.query(selector, *from_ms, *to_ms);
        if (static_cast<long long>(rows.size()) > limit) {
            rows.erase(rows.begin(), rows.end() - static_cast<size_t>(limit));
        }

        if (format == "csv") {
            return write_csv_response(res, rows, "export.csv");
        }

        json samples = json::array();
        for (const auto& row : rows) {
            samples.push_back({row.ts_ms, row.value});
        }

        write_json_response(res, json{{"metric", metric_name},
                                      {"unit", infer_unit_for_metric(metric_name)},
                                      {"rollup", "raw"},
                                      {"labels", labels_to_json(labels)},
                                      {"samples", samples}});
    });
}
