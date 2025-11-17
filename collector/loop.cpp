//
// Created by Sebastian Ibarra on 10/9/25.
//

#include <atomic>
#include <thread>
#include <vector>
#include "collector/loop.h"
#include "collector/cpu.h"
#include "collector/memory.h"
#include "collector/net.h"
#include "metrics/metric_key.h"
#include "metrics/time.h"
#include "config.h"
#include "collector/disk.h"
#include <iostream>

#include "collector/proc.h"
#include "third_party/json.hpp"

using json   = nlohmann::json;


std::thread start_sampler(MemoryStore& store, std::atomic<bool>& running) {
    return std::thread([&store, &running]() {
        std::vector<double> core_pct;
        std::vector<DiskIO> disk_ios;
        std::unordered_map<std::string, InterfaceRates> rates;

        procmon::ProcSnapshot prev_proc{}, cur_proc{};
        bool have_prev_proc = false;

        while (running.load(std::memory_order_relaxed)) {
            auto t = now_ms();

            if (double total = get_cpu_total_percent(); total >= 0.0) {
                store.append(metric_with_labels("cpu.total_pct", {{"host", cfg::HOST_LABEL}}), t, total);
            }

            if (get_cpu_core_percent(core_pct)) {
                    store.append_vector(metric_with_labels("cpu.core_pct",{{"host", cfg::HOST_LABEL}}), t, core_pct);
            }

            if (MemBytes mb; get_system_memory_bytes(mb)) {
                store.append(metric_with_labels("mem.used", {{"host", cfg::HOST_LABEL}}), t, (double)mb.used_bytes);
                store.append(metric_with_labels("mem.free", {{"host", cfg::HOST_LABEL}}), t, (double)mb.free_bytes);
            }

            if (get_disk_io(disk_ios)) {
                for (const DiskIO& io : disk_ios) {
                    store.append(metric_with_labels("disk.read",  {{"host", cfg::HOST_LABEL}, {"dev", io.dev_name}}), t, io.bytes_read_per_s);
                    store.append(metric_with_labels("disk.write", {{"host", cfg::HOST_LABEL}, {"dev", io.dev_name}}), t, io.bytes_written_per_s);
                }
            }

            if (get_net_stats(rates)) {
                for (const auto& [iface, rate] : rates) {
                    store.append(metric_with_labels("net.rx", {{"host", cfg::HOST_LABEL}, {"iface", iface}}), t, rate.rx_bytes_per_s);
                    store.append(metric_with_labels("net.tx", {{"host", cfg::HOST_LABEL}, {"iface", iface}}), t, rate.tx_bytes_per_s);
                }
            }

            if (procmon::read_proc_snapshot(cur_proc)) {
                if (have_prev_proc) {
                    auto rows = procmon::top_by_cpu(prev_proc, cur_proc, /*limit*/ 128); // or 0 for all

                    nlohmann::json::array_t arr;
                    arr.reserve(rows.size());

                    for (const auto& r : rows) {
                        arr.push_back(nlohmann::json{
                                {"pid", r.pid},
                                {"ppid", r.ppid},
                                {"user", r.user},
                                {"name", r.name},
                                {"state", std::string(1, r.state)},
                                {"cpu_pct", r.cpu_pct},
                                {"cpu_time_s", r.cpu_time_s},
                                {"threads", r.threads},
                                {"idle_wakeups_per_s", r.wakeups_per_s},
                                {"rss_mb", r.rss_mb},
                                {"mem_pct", r.mem_pct},
                                {"priority", r.priority},
                                {"nice", r.nice}
                        });
                    }
                    nlohmann::json table = std::move(arr);
                    store.put_snapshot("processes", table);
                }
                prev_proc = std::move(cur_proc);
                have_prev_proc = true;
            }

            std::this_thread::sleep_for(std::chrono::seconds(cfg::SAMPLE_PERIOD_S));
        }
    });
}

