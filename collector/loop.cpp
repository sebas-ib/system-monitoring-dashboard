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

std::thread start_sampler(MemoryStore& store, std::atomic<bool>& running) {
    return std::thread([&store, &running](){
        std::vector<double> core_pct;
        std::vector<DiskIO> disk_ios;
        std::unordered_map<std::string, InterfaceRates> rates;

        while (running.load(std::memory_order_relaxed)) {
            auto t = now_ms();

            // cpu.total_pct
            if (double total = get_cpu_total_percent(); total >= 0.0) {
                store.append(metric_with_labels("cpu.total_pct", {{"host", cfg::HOST_LABEL}}), t, total);
            }

            // cpu.core_pct
            if (get_cpu_core_percent(core_pct)) {
                for (size_t i=0;i<core_pct.size();++i) {
                    store.append(metric_with_labels("cpu.core_pct",
                                                    {{"host", cfg::HOST_LABEL}, {"core", std::to_string(i)}}),
                                 t, core_pct[i]);
                }
            }
            // mem.*
            if (MemBytes mb; get_system_memory_bytes(mb)) {
                store.append(metric_with_labels("mem.used_bytes", {{"host", cfg::HOST_LABEL}}), t, (double)mb.used_bytes);
                store.append(metric_with_labels("mem.free_bytes", {{"host", cfg::HOST_LABEL}}), t, (double)mb.free_bytes);
            }

            //disk
            if (get_disk_io(disk_ios)) {
                for (const DiskIO& io : disk_ios) {
                    store.append(
                            metric_with_labels("disk.read_bytes",
                                               {{"host", cfg::HOST_LABEL}, {"dev", io.dev_name}}),
                            t, io.bytes_read_per_s
                    );

                    store.append(
                            metric_with_labels("disk.write_bytes",
                                               {{"host", cfg::HOST_LABEL}, {"dev", io.dev_name}}),
                            t, io.bytes_written_per_s
                    );
                }
            }

            if (get_net_stats(rates)) {
                for (const auto& [iface, rate] : rates) {
                    // RX bytes per second
                    store.append(
                            metric_with_labels("net.rx_bytes",
                                               {{"host", cfg::HOST_LABEL}, {"iface", iface}}),
                            t, rate.rx_bytes_per_s
                    );

                    // TX bytes per second
                    store.append(
                            metric_with_labels("net.tx_bytes",
                                               {{"host", cfg::HOST_LABEL}, {"iface", iface}}),
                            t, rate.tx_bytes_per_s
                    );
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(cfg::SAMPLE_PERIOD_S));
        }
    });
}
