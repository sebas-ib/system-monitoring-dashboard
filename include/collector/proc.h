//
// Created by Sebastian Ibarra on 11/4/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_PROC_H
#define SYSTEM_MONITORING_DASHBOARD_PROC_H

#pragma once
// Linux /proc process sampler.
// Computes per-process %CPU using deltas between two snapshots.
// Also returns CPU time, threads, context-switches/sec (idle-wakeups proxy),
// memory RSS and %MEM, PID/PPID, user, name, and state.

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace procmon {

    struct ProcSample {
        int pid = 0;
        int ppid = 0;
        uint64_t utime_ticks = 0;     // /proc/[pid]/stat fields 14
        uint64_t stime_ticks = 0;     // field 15
        uint64_t starttime_ticks = 0; // field 22
        uint64_t rss_kb = 0;          // from statm/status
        uint64_t ctx_switches = 0;    // voluntary+nonvoluntary (status)
        uint32_t threads = 0;         // status Threads
        int priority = 0;             // stat field 18
        int nice = 0;                 // stat field 19
        unsigned uid = 0;             // status Uid
        char state = '?';             // stat field 3
        std::string comm;             // stat comm (name)
        std::string cmdline;          // full argv (may be empty)
    };

    struct ProcSnapshot {
        uint64_t total_jiffies = 0;                      // sum from /proc/stat (all CPUs)
        std::unordered_map<int, ProcSample> by_pid;      // pid -> sample
        uint64_t memtotal_kb = 0;                        // from /proc/meminfo
        int hz = 100;                                    // CLK_TCK
    };

// A computed row suitable for UI tables.
    struct ProcRow {
        int pid = 0;
        int ppid = 0;
        std::string user;         // resolved from uid
        std::string name;         // cmdline or [comm]
        char state = '?';

        double cpu_pct = 0.0;     // over the snapshot interval
        double cpu_time_s = 0.0;  // cumulative (utime+stime)/HZ
        uint32_t threads = 0;

        double wakeups_per_s = 0.0; // Δ(context switches)/Δt (proxy for idle wakeups)
        double rss_mb = 0.0;
        double mem_pct = 0.0;

        int priority = 0;
        int nice = 0;
    };

// Take a point-in-time snapshot of /proc for later diffing.
    bool read_proc_snapshot(ProcSnapshot& out);

// Compute per-process deltas between two snapshots taken Δt seconds apart.
// The function infers Δt from total_jiffies/HZ of snapshots.
    std::vector<ProcRow> compute_proc_rows(const ProcSnapshot& prev,
                                           const ProcSnapshot& cur);

// Convenience: return rows sorted by descending CPU%.
    std::vector<ProcRow> top_by_cpu(const ProcSnapshot& prev,
                                    const ProcSnapshot& cur,
                                    size_t limit = 0);

} // namespace procmon


#endif //SYSTEM_MONITORING_DASHBOARD_PROC_H
