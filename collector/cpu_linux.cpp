//
// Created by Sebastian Ibarra on 10/9/25.
//
// Provides functions to read CPU usage statistics directly from the linux system file '/proc/stat'
// and calculate the percentage of cpu time that was activaly used vs idle for total CPU and per core
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include "collector/cpu.h"

// Cpu times struct to store all cpu usage types
struct CpuTimes {
    uint64_t user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;
};


// Reads file 'proc/stat' and extracts CPU stats
// Each line starting with "cpu" corresponds to either
//  - "cpu"  -> aggregate of all cores
//  - "cpu0" -> core 0
//
// The function fills two outputs
//  - per_cpu   -> a vecotr of CpuTimes for each core
//  - total_out -> a CpuTimes for the cpu (all cores combined)
static bool read_proc_stat(std::vector<CpuTimes>& per_cpu, CpuTimes& total_out) {
    // Open '/proc/stat' file for reading
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return false;


    per_cpu.clear();
    bool have_total = false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("cpu", 0) != 0) break; // stop at first non-cpu line


        std::istringstream iss(line);
        std::string label;
        CpuTimes t{};

        // Initialize CpuTimes object "t" with all values from line read
        iss >> label
            >> t.user >> t.nice >> t.system >> t.idle >> t.iowait >> t.irq >> t.softirq >> t.steal;


        // "cpu" line (w/ out the number) is the total aggregate usage
        if (label == "cpu") {
            total_out = t;
            have_total = true;
        } else if (label.size() > 3 && label[0]=='c' && label[1]=='p' && label[2]=='u') {
            per_cpu.push_back(t); // cpu0, cpu1, ...
        }
    }
    return have_total;
}


// Returns the active cpu time (time where cpu actually was doing work)
static inline uint64_t active_time(const CpuTimes& t) {
    return t.user + t.nice + t.system + t.irq + t.softirq + t.steal;
}

// Returns active + idle + iowait cpu times, aka total time
static inline uint64_t total_time(const CpuTimes& t) {
    return active_time(t) + t.idle + t.iowait;
}


// Gets cpu usage per core
bool get_cpu_core_percent(std::vector<double>& out_core_pct) {
    static std::vector<CpuTimes> last_per_cpu; // Stores previous cputimes
    static bool initialized = false;

    std::vector<CpuTimes> cur_per_cpu;
    CpuTimes ignore_total{};
    // Reads the cpu statistics
    if (!read_proc_stat(cur_per_cpu, ignore_total) || cur_per_cpu.empty()) return false;

    out_core_pct.resize(cur_per_cpu.size(), 0.0);

    // On the first run, initialize snapshot and return true
    if (!initialized) {
        last_per_cpu = cur_per_cpu;
        initialized = true;
        // First call: we have no deltas yet -> return 0s
        return true;
    }

    // Compute usage for each core
    for (size_t i = 0; i < cur_per_cpu.size(); ++i) {
        const CpuTimes& a = last_per_cpu[i];
        const CpuTimes& b = cur_per_cpu[i];

        uint64_t a_active = active_time(a), b_active = active_time(b);
        uint64_t a_total  = total_time(a),  b_total  = total_time(b);

        uint64_t d_active = (b_active >= a_active) ? (b_active - a_active) : 0ULL;
        uint64_t d_total  = (b_total  >= a_total)  ? (b_total  - a_total)  : 0ULL;

        // Save percent usage to 'out_core_pct'
        out_core_pct[i] = (d_total == 0) ? 0.0 : (100.0 * (double)d_active / (double)d_total);
    }

    // Save current cpu usage for next calculation
    last_per_cpu = std::move(cur_per_cpu);
    return true;
}


// Gets total cpu usage
double get_cpu_total_percent() {
    static CpuTimes last_total{}; // Stores last cpu usage information
    static bool initialized = false;

    std::vector<CpuTimes> ignore_per_cpu;
    CpuTimes cur_total{};

    // Reads current cpu usage statistics
    if (!read_proc_stat(ignore_per_cpu, cur_total)) return -1.0;

    // If first read, initialize last_total and return 0
    if (!initialized) {
        last_total = cur_total;
        initialized = true;
        return 0.0; // no delta yet
    }

    // Calculate difference between last_total adn curr_total
    uint64_t a_active = active_time(last_total), b_active = active_time(cur_total);
    uint64_t a_total  = total_time(last_total),  b_total  = total_time(cur_total);

    uint64_t d_active = (b_active >= a_active) ? (b_active - a_active) : 0ULL;
    uint64_t d_total  = (b_total  >= a_total)  ? (b_total  - a_total)  : 0ULL;

    // Save curr_total usage statistics as last_total for next calculations
    last_total = cur_total;
    if (d_total == 0) return 0.0;
    return 100.0 * (double)d_active / (double)d_total;
}
