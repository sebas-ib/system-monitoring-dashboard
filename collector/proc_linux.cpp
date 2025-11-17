//
// Created by Sebastian Ibarra on 11/4/25.
//
#include "collector/proc.h"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

namespace procmon {

// ================================================================
// Helpers
// ================================================================

    // Return kernel clock ticks per second (HZ).
    static inline int clk_tck() {
        static int v = int(sysconf(_SC_CLK_TCK));
        return v > 0 ? v : 100;
    }

    // Read the first "cpu" line from /proc/stat and sum all fields into total_jiffies_out.
    static bool read_first_cpu_line(uint64_t &total_jiffies_out) {
        std::ifstream f("/proc/stat");
        // Return false if can't open
        if (!f.is_open()) return false;

        // If empty return false
        std::string line;
        if (!std::getline(f, line)) return false;

        // If first part is not == to "cpu" return false
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag != "cpu") return false;

        // cpu user nice system idle iowait irq softirq steal guest guest_nice
        uint64_t val = 0, sum = 0;
        while (iss >> val) sum += val;
        total_jiffies_out = sum;
        return true;
    }

    // Return MemTotal from /proc/meminfo in kB.
    static uint64_t read_memtotal_kb() {
        // Open file
        std::ifstream f("/proc/meminfo");
        if (!f.is_open()) return 0;

        // Extract the value if it's key is "MemTotal"
        std::string key;
        uint64_t value = 0;
        while (f >> key >> value) {
            if (key == "MemTotal:") return value; // already kB
            std::string rest;
            std::getline(f, rest); // consume remainder of the line (units)
        }
        return 0;
    }

    // Read argv from /proc/[pid]/cmdline, convert NULs to spaces.
    static std::string read_cmdline(int pid) {

        // Opens file proc/[PID]/cmdline in binary mode
        std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline", std::ios::in | std::ios::binary);
        if (!f.is_open()) return {};

        // Reads whole file into buf
        std::string buf((std::istreambuf_iterator<char>(f)), {});
        if (buf.empty()) return {};


        // For every char in buf, if the char is null char, then set the char to a space (' ')
        for (char &c: buf) if (c == '\0') c = ' ';
        // If the last elem of buf is a ' ' then pop it off.
        if (!buf.empty() && buf.back() == ' ') buf.pop_back();

        // return the clean commandline command
        return buf;
    }

    /**
     * Parse the subset of /proc/[pid]/stat we need.
     * Notes:
     *  - comm may contain spaces and is wrapped in parentheses.
     *  - After state(3) and ppid(4), skip fields 5..13 (9 fields), then read utime(14) and stime(15).
     */
    static bool read_stat(int pid, ProcSample &s) {

        // Open /proc/[PID]/stat
        std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
        if (!f.is_open()) return false;

        // Get first line
        std::string all;
        std::getline(f, all);
        if (all.empty()) return false;

        // Locate "(comm)"
        size_t l = all.find('(');
        size_t r = all.rfind(')');
        if (l == std::string::npos || r == std::string::npos || r <= l) return false;

        // Initialize process with PID
        s.pid = pid;
        s.comm = all.substr(l + 1, r - l - 1);

        // Parse fields after ") "
        std::istringstream right(all.substr(r + 2));

        // 3: state, 4: ppid
        char state = '?';
        int ppid = 0;
        right >> state >> ppid;

        // Skip 5..13 (9 fields): pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt
        for (int i = 0; i < 9; ++i) {
            uint64_t tmp;
            right >> tmp;
        }

        // 14: utime, 15: stime
        uint64_t utime = 0, stime = 0;
        right >> utime >> stime;

        // 16..17: cutime, cstime (ignored but consumed)
        {
            uint64_t tmp16, tmp17;
            right >> tmp16 >> tmp17;
        }

        // 18: priority, 19: nice
        int priority = 0, nice = 0;
        right >> priority >> nice;

        // 20..21: num_threads, itrealvalue (ignored)
        {
            uint64_t thr_ign, itrv_ign;
            right >> thr_ign >> itrv_ign;
        }

        // 22: starttime
        uint64_t starttime = 0;
        right >> starttime;

        s.state = state;
        s.ppid = ppid;
        s.utime_ticks = utime;
        s.stime_ticks = stime;
        s.priority = priority;
        s.nice = nice;
        s.starttime_ticks = starttime;
        return true;
    }

    /** Read selected fields from /proc/[pid]/status. */
    static void read_status(int pid, ProcSample &s) {

        // Open "/proc/[PID]/status"
        std::ifstream f("/proc/" + std::to_string(pid) + "/status");
        if (!f.is_open()) return;

        // Read fields in "/proc/[PID]/status"
        std::string key;
        while (f >> key) {
            if (key == "Uid:") {
                unsigned ruid;
                f >> ruid;
                s.uid = ruid;
            } else if (key == "Threads:") {
                uint32_t th;
                f >> th;
                s.threads = th;
            } else if (key == "voluntary_ctxt_switches:") {
                uint64_t v;
                f >> v;
                s.ctx_switches += v;
            } else if (key == "nonvoluntary_ctxt_switches:") {
                uint64_t nv;
                f >> nv;
                s.ctx_switches += nv;
            }
            std::string rest;
            std::getline(f, rest); // consume rest of line
        }
    }

/** Prefer /proc/[pid]/statm resident pages*pagesize; fallback to VmRSS from /proc/[pid]/status. */
    static uint64_t rss_kb_from_proc(int pid) {
        // Try statm first
        {
            std::ifstream f("/proc/" + std::to_string(pid) + "/statm");
            if (f.is_open()) {
                uint64_t size_pages = 0, resident_pages = 0;
                if (f >> size_pages >> resident_pages) {
                    long ps = sysconf(_SC_PAGESIZE);
                    if (ps <= 0) ps = 4096;
                    return (resident_pages * (uint64_t) ps) / 1024;
                }
            }
        }
        // Fallback: VmRSS
        {
            std::ifstream s("/proc/" + std::to_string(pid) + "/status");
            std::string key;
            uint64_t val = 0;
            while (s >> key >> val) {
                if (key == "VmRSS:") return val; // kB
                std::string rest;
                std::getline(s, rest);
            }
        }
        return 0;
    }

    /** Resolve username from uid. */
    static std::string username_from_uid(unsigned uid) {
        if (auto *pw = getpwuid(uid); pw && pw->pw_name) return std::string(pw->pw_name);
        return std::to_string(uid);
    }


    bool read_proc_snapshot(ProcSnapshot &out) {
        out.by_pid.clear();
        out.hz = clk_tck();

        if (!read_first_cpu_line(out.total_jiffies)) return false;
        out.memtotal_kb = read_memtotal_kb();

        DIR *d = opendir("/proc");
        if (!d) return false;

        if (dirent *e; true) {
            while ((e = readdir(d))) {
                // Only consider directories (or unknown/link types—common on some filesystems)
                if (e->d_type != DT_DIR && e->d_type != DT_LNK && e->d_type != DT_UNKNOWN) continue;

                // Is the name numeric?
                const char *name = e->d_name;
                char *endp = nullptr;
                long pid_long = std::strtol(name, &endp, 10);
                if (!name[0] || *endp != '\0' || pid_long <= 0) continue;

                const int pid = int(pid_long);

                ProcSample s;
                if (!read_stat(pid, s)) continue;
                read_status(pid, s);
                s.rss_kb = rss_kb_from_proc(pid);
                s.cmdline = read_cmdline(pid);

                out.by_pid.emplace(pid, std::move(s));
            }
        }

        closedir(d);
        return true;
    }

    std::vector<ProcRow> compute_proc_rows(const ProcSnapshot &prev,
                                           const ProcSnapshot &cur) {
        std::vector<ProcRow> rows;
        if (prev.hz <= 0 || cur.hz <= 0) return rows;

        // dt inferred from total jiffies across all CPUs.
        double dt = double(cur.total_jiffies > prev.total_jiffies
                           ? (cur.total_jiffies - prev.total_jiffies)
                           : 1) / double(cur.hz);
        if (dt <= 0.0) dt = 1.0;

        const double memtotal_kb = (cur.memtotal_kb > 0 ? double(cur.memtotal_kb) : 0.0);

        rows.reserve(cur.by_pid.size());
        for (const auto &[pid, b]: cur.by_pid) {

            // If process is new (not present in prev), we can only show cumulative CPU time.
            auto it = prev.by_pid.find(pid);
            if (it == prev.by_pid.end()) {
                ProcRow r;
                r.pid = pid;
                r.ppid = b.ppid;
                r.user = username_from_uid(b.uid);
                r.state = b.state;
                r.name = b.cmdline.empty() ? ("[" + b.comm + "]") : b.cmdline;
                r.threads = b.threads;
                r.priority = b.priority;
                r.nice = b.nice;
                r.cpu_time_s = double(b.utime_ticks + b.stime_ticks) / double(cur.hz);
                r.cpu_pct = 0.0;           // no delta yet
                r.wakeups_per_s = 0.0;        // unknown without a previous sample
                r.rss_mb = double(b.rss_kb) / 1024.0;
                r.mem_pct = (memtotal_kb > 0.0) ? (100.0 * double(b.rss_kb) / memtotal_kb) : 0.0;

                rows.push_back(std::move(r));
                continue;
            }

            // Existing process: compute deltas.
            const ProcSample &a = it->second;

            int64_t dut = (int64_t) b.utime_ticks - (int64_t) a.utime_ticks;
            int64_t dst = (int64_t) b.stime_ticks - (int64_t) a.stime_ticks;
            if (dut < 0) dut = 0;
            if (dst < 0) dst = 0;

            const double dproc_s = double(dut + dst) / double(cur.hz);
            const double pcpu = (dt > 0.0) ? (100.0 * dproc_s / dt) : 0.0;

            int64_t dcs = (int64_t) b.ctx_switches - (int64_t) a.ctx_switches;
            if (dcs < 0) dcs = 0;

            ProcRow r;
            r.pid = pid;
            r.ppid = b.ppid;
            r.user = username_from_uid(b.uid);
            r.state = b.state;
            r.name = b.cmdline.empty() ? ("[" + b.comm + "]") : b.cmdline;
            r.threads = b.threads;
            r.priority = b.priority;
            r.nice = b.nice;

            r.cpu_pct = pcpu;
            r.cpu_time_s = double(b.utime_ticks + b.stime_ticks) / double(cur.hz);
            r.wakeups_per_s = (dt > 0.0) ? (double(dcs) / dt) : 0.0;

            r.rss_mb = double(b.rss_kb) / 1024.0;
            r.mem_pct = (memtotal_kb > 0.0) ? (100.0 * double(b.rss_kb) / memtotal_kb) : 0.0;

            rows.push_back(std::move(r));
        }

        return rows;
    }

    std::vector<ProcRow> top_by_cpu(const ProcSnapshot &prev,
                                    const ProcSnapshot &cur,
                                    size_t limit) {
        auto rows = compute_proc_rows(prev, cur);
        std::stable_sort(rows.begin(), rows.end(),
                         [](const ProcRow &x, const ProcRow &y) { return x.cpu_pct > y.cpu_pct; });
        if (limit && rows.size() > limit) rows.resize(limit);
        return rows;
    }

} // namespace procmon
