//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_METRICS_KEY_H
#define SYSTEM_MONITORING_DASHBOARD_METRICS_KEY_H

#pragma once
#include <string>
#include <initializer_list>
#include <utility>

inline std::string metric_with_labels(
        const std::string& name,
        std::initializer_list<std::pair<std::string,std::string>> kvs)
{
    std::string out = name + "{";
    bool first = true;
    for (const auto& kv : kvs) {
        if (!first) out += ",";
        out += kv.first + "=" + kv.second;
        first = false;
    }
    out += "}";
    return out;
}

#endif //SYSTEM_MONITORING_DASHBOARD_METRICS_KEY_H
