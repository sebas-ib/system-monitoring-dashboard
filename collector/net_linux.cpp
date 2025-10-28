//
// Created by Sebastian Ibarra on 10/9/25.
//

#include "collector/net.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <iostream>
#include <cctype>
#include "metrics/time.h"

static inline std::string trim(const std::string& s){
    int a = 0 , b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (a < b && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

static bool read_proc_net(NetSnapshot& out){
    std::ifstream f("/proc/net/dev");
    if(!f.is_open()) return false;

    out.clear();
    std::string line;

    // Skip top two header lines
    std::getline(f, line);
    std::getline(f, line);

    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if(colon == std::string::npos) continue;

        std::string iface = trim(line.substr(0, colon));
        std::string rest = line.substr(colon + 1);


        if(iface == "lo") continue;

        std::istringstream iss(rest);
        uint64_t r_bytes, r_pkts, r_errs, r_drop, r_fifo, r_frame, r_comp, r_mcast;
        uint64_t t_bytes, t_pkts, t_errs, t_drop, t_fifo, t_colls, t_carrier, t_comp;

        if (!(iss >> r_bytes >> r_pkts >> r_errs >> r_drop >> r_fifo >> r_frame >> r_comp >> r_mcast
                  >> t_bytes >> t_pkts >> t_errs >> t_drop >> t_fifo >> t_colls >> t_carrier >> t_comp)) {
            continue; // malformed line
        }

        InterfaceCounters curr;

        curr.rx_bytes = r_bytes;
        curr.rx_drop = r_drop;
        curr.rx_errs = r_errs;
        curr.rx_packets = r_pkts;
        curr.tx_bytes = t_bytes;
        curr.tx_drop = t_drop;
        curr.tx_errs = t_errs;
        curr.tx_packets = t_pkts;

        out.emplace(iface, curr);
    }

    return true;
}


bool get_net_stats(std::unordered_map<std::string, InterfaceRates>& out){
    static NetSnapshot prev;
    static bool initialized = false;
    static uint64_t prev_time;

    // Current values
    NetSnapshot curr;
    if(!read_proc_net(curr)){
        curr.clear();
        return false;
    }

    uint64_t time_now = now_ms();

    if(!initialized){
        prev = std::move(curr);
        prev_time = time_now;
        initialized = true;
        out.clear();
    }

    // calculate difference in time
    const double dt_s = (time_now > prev_time)
                        ? static_cast<double>(time_now - prev_time) / 1000
                        : 0.0;

    if(dt_s <= 0){
        prev = std::move(curr);
        prev_time = time_now;
        out.clear();
        return true;
    }

    out.clear();
    for (const auto& [iface, ccurr] : curr) {
        const auto it = prev.find(iface);
        if(it == prev.end()) continue;

        const auto& cprev = it->second;

        InterfaceRates rates;

        const double drx = (ccurr.rx_bytes >= cprev.rx_bytes) ? (double)(ccurr.rx_bytes - cprev.rx_bytes) : 0;
        const double dtx = (ccurr.tx_bytes >= cprev.tx_bytes) ? (double)(ccurr.tx_bytes - cprev.tx_bytes) : 0;

        rates.rx_bytes_per_s = drx / dt_s;
        rates.tx_bytes_per_s = dtx / dt_s;

        out.emplace(iface, rates);
    }

    prev = std::move(curr);
    prev_time = time_now;
    return true;
}
