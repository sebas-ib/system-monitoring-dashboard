// loop.cpp — orchestrates the background sampler that collects host metrics.
// The sampler loops forever while 'running' stays true and populates MemoryStore
// with CPU, memory, disk, network, and process data for the HTTP API layer.

#include <atomic>
#include <thread>
#include <vector>

#include "collector/loop.h"

#include "collector/cpu.h"
#include "collector/disk.h"
#include "collector/memory.h"
#include "collector/net.h"
#include "collector/proc.h"
#include "config.h"
#include "metrics/metric_key.h"
#include "metrics/time.h"
#include "third_party/json.hpp"

namespace {
using json = nlohmann::json;

constexpr size_t kProcessTableLimit = 128;

std::string selector_for(const std::string& metric_name,
                         const std::initializer_list<std::pair<std::string, std::string>>& labels) {
    return metric_with_labels(metric_name, labels);
}

void sample_cpu_metrics(MemoryStore& store, int64_t timestamp_ms, std::vector<double>& core_percent_buffer) {
    const std::string total_cpu_selector = selector_for("cpu.total_pct", {{"host", cfg::HOST_LABEL}});
    if (double total_percent = get_cpu_total_percent(); total_percent >= 0.0) {
        store.append(total_cpu_selector, timestamp_ms, total_percent);
    }

    const std::string core_cpu_selector = selector_for("cpu.core_pct", {{"host", cfg::HOST_LABEL}});
    if (get_cpu_core_percent(core_percent_buffer)) {
        store.append_vector(core_cpu_selector, timestamp_ms, core_percent_buffer);
    }
}

void sample_memory_metrics(MemoryStore& store, int64_t timestamp_ms) {
    if (MemBytes bytes; get_system_memory_bytes(bytes)) {
        const std::string used_selector = selector_for("mem.used", {{"host", cfg::HOST_LABEL}});
        store.append(used_selector, timestamp_ms, static_cast<double>(bytes.used_bytes));

        const std::string free_selector = selector_for("mem.free", {{"host", cfg::HOST_LABEL}});
        store.append(free_selector, timestamp_ms, static_cast<double>(bytes.free_bytes));
    }
}

void sample_disk_metrics(MemoryStore& store, int64_t timestamp_ms, std::vector<DiskIO>& disk_io_buffer) {
    if (!get_disk_io(disk_io_buffer)) {
        return;
    }

    for (const DiskIO& device_io : disk_io_buffer) {
        const std::string read_selector = selector_for("disk.read", {
                {"host", cfg::HOST_LABEL},
                {"dev", device_io.dev_name}
        });
        store.append(read_selector, timestamp_ms, device_io.bytes_read_per_s);

        const std::string write_selector = selector_for("disk.write", {
                {"host", cfg::HOST_LABEL},
                {"dev", device_io.dev_name}
        });
        store.append(write_selector, timestamp_ms, device_io.bytes_written_per_s);
    }
}

void sample_network_metrics(MemoryStore& store,
                            int64_t timestamp_ms,
                            std::unordered_map<std::string, InterfaceRates>& interface_rates) {
    if (!get_net_stats(interface_rates)) {
        return;
    }

    for (const auto& [interface, rate] : interface_rates) {
        const std::string rx_selector = selector_for("net.rx", {
                {"host", cfg::HOST_LABEL},
                {"iface", interface}
        });
        store.append(rx_selector, timestamp_ms, rate.rx_bytes_per_s);

        const std::string tx_selector = selector_for("net.tx", {
                {"host", cfg::HOST_LABEL},
                {"iface", interface}
        });
        store.append(tx_selector, timestamp_ms, rate.tx_bytes_per_s);
    }
}

json serialize_process_rows(const std::vector<procmon::ProcRow>& rows) {
    json::array_t table;
    table.reserve(rows.size());

    for (const auto& row : rows) {
        table.push_back(json{
                {"pid", row.pid},
                {"ppid", row.ppid},
                {"user", row.user},
                {"name", row.name},
                {"state", std::string(1, row.state)},
                {"cpu_pct", row.cpu_pct},
                {"cpu_time_s", row.cpu_time_s},
                {"threads", row.threads},
                {"idle_wakeups_per_s", row.wakeups_per_s},
                {"rss_mb", row.rss_mb},
                {"mem_pct", row.mem_pct},
                {"priority", row.priority},
                {"nice", row.nice}
        });
    }

    return table;
}

void sample_process_metrics(MemoryStore& store,
                            procmon::ProcSnapshot& previous_snapshot,
                            procmon::ProcSnapshot& current_snapshot,
                            bool& have_previous_snapshot) {
    if (!procmon::read_proc_snapshot(current_snapshot)) {
        return;
    }

    if (have_previous_snapshot) {
        const auto rows = procmon::top_by_cpu(previous_snapshot, current_snapshot, kProcessTableLimit);
        store.put_snapshot("processes", serialize_process_rows(rows));
    }

    previous_snapshot = std::move(current_snapshot);
    have_previous_snapshot = true;
}
} // namespace

/**
 * Launch the detached sampler loop.
 *
 * @param store   Shared MemoryStore receiving metrics.
 * @param running Flag toggled by the caller to stop sampling.
 * @return Joinable std::thread that runs the sampler loop.
 */
std::thread start_sampler(MemoryStore& store, std::atomic<bool>& running) {
    return std::thread([&store, &running]() {
        std::vector<double> core_percent_buffer;
        std::vector<DiskIO> disk_io_buffer;
        std::unordered_map<std::string, InterfaceRates> interface_rates;

        procmon::ProcSnapshot previous_process_snapshot{};
        procmon::ProcSnapshot current_process_snapshot{};
        bool have_previous_process_snapshot = false;

        while (running.load(std::memory_order_relaxed)) {
            const int64_t timestamp_ms = now_ms();

            sample_cpu_metrics(store, timestamp_ms, core_percent_buffer);

            sample_memory_metrics(store, timestamp_ms);

            sample_disk_metrics(store, timestamp_ms, disk_io_buffer);

            sample_network_metrics(store, timestamp_ms, interface_rates);

            sample_process_metrics(store,
                                   previous_process_snapshot,
                                   current_process_snapshot,
                                   have_previous_process_snapshot);

            std::this_thread::sleep_for(std::chrono::seconds(cfg::SAMPLE_PERIOD_S));
        }
    });
}

