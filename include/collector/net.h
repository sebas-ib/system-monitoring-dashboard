//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_NET_H
#define SYSTEM_MONITORING_DASHBOARD_NET_H

#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

struct InterfaceRates {
    double rx_bytes_per_s = 0.0;
    double tx_bytes_per_s = 0.0;
};

struct InterfaceCounters {
    uint64_t rx_bytes=0, rx_packets=0, rx_errs=0, rx_drop=0;
    uint64_t tx_bytes=0, tx_packets=0, tx_errs=0, tx_drop=0;
};

// Full type must be known *here* (not just a forward-declare).
using NetSnapshot = std::unordered_map<std::string, InterfaceCounters>;

bool get_net_stats(std::unordered_map<std::string, InterfaceRates>& out);

#endif //SYSTEM_MONITORING_DASHBOARD_NET_H
