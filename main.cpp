#include <atomic>
#include <thread>

#include "api/routes.h"
#include "collector/loop.h"
#include "config.h"
#include "store/memory_store.h"
#include "store/system_info.h"
#include "third_party/httplib.h"

// main.cpp — entry point for the monitoring daemon.
// Initializes storage, launches the sampler thread, and exposes the HTTP API.

namespace {
constexpr char kListenAddress[] = "0.0.0.0";
constexpr int kListenPort = 8080;

/**
 * Populate the read-only "system" metadata bucket so the UI can render
 * contextual details such as total memory, CPU core count, and hostname.
 */
void cache_system_metadata(MemoryStore& store) {
    const SystemInfo system_info = collect_system_info();
    store.put_metadata("system", {
            {"cpu_cores", system_info.cpu_cores},
            {"mem_total_bytes", system_info.mem_total_bytes},
            {"hostname", system_info.hostname},
            {"os_name", system_info.os_name},
            {"kernel_version", system_info.kernel_version}
    });
}
} // namespace

/**
 * Application entry point.
 *
 * @returns 0 when the HTTP server is bound successfully, 1 otherwise.
 * @sideeffects Starts a background sampler thread and an HTTP server.
 */
int main() {
    std::atomic<bool> sampler_running(true);
    MemoryStore store(cfg::KEEP_SECONDS, cfg::SAMPLE_PERIOD_S);

    cache_system_metadata(store);

    std::thread sampler_thread = start_sampler(store, sampler_running);

    httplib::Server server;
    bind_routes(server, store);
    const bool server_ok = server.listen(kListenAddress, kListenPort);

    sampler_running = false;
    if (sampler_thread.joinable()) {
        sampler_thread.join();
    }

    return server_ok ? 0 : 1;
}

