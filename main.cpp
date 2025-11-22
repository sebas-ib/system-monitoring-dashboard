#include <atomic>
#include <thread>
#include <cstdlib>
#include <string>
#include <fstream>

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
    constexpr int kDefaultListenPort = 8080;

/**
 * Populate the read-only "system" metadata bucket so the UI can render
 * contextual details.
 */
    void cache_system_metadata(MemoryStore& store) {
        const SystemInfo system_info = collect_system_info();
        store.put_metadata("system", {
                {"cpu_cores",        system_info.cpu_cores},
                {"mem_total_bytes",  system_info.mem_total_bytes},
                {"hostname",         system_info.hostname},
                {"os_name",          system_info.os_name},
                {"kernel_version",   system_info.kernel_version}
        });
    }

/**
 * Resolve listen port from the PORT environment variable, falling back to
 * kDefaultListenPort on missing/invalid values.
 */
    int resolve_listen_port() {
        if (const char* env = std::getenv("PORT")) {
            if (*env) {
                try {
                    int port = std::stoi(env);
                    if (port > 0 && port <= 65535) {
                        return port;
                    }
                } catch (...) {
                    // fall through to default
                }
            }
        }
        return kDefaultListenPort;
    }

/**
 * Resolve the static web root from WEB_ROOT env var.
 * Defaults to "web" (relative to the working directory).
 */
    std::string resolve_web_root() {
        if (const char* env = std::getenv("WEB_ROOT")) {
            if (*env) {
                return std::string(env);
            }
        }
        return std::string("web");
    }

/**
 * Register handlers to serve the SPA frontend from web_root.
 * - Mounts web_root under "/" so that /app.js, /styles.css, etc. work.
 * - Adds a special handler for "/" that returns index.html.
 */
    void bind_static_frontend(httplib::Server& server, const std::string& web_root) {
        // Serve static files from web_root
        server.set_mount_point("/", web_root.c_str());

        // Explicitly map "/" -> index.html (SPA entry point)
        server.Get("/", [web_root](const httplib::Request&, httplib::Response& res) {
            std::string index_path = web_root + "/index.html";
            std::ifstream ifs(index_path, std::ios::binary);
            if (!ifs) {
                res.status = 404;
                return;
            }

            std::string body((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
            res.set_content(body, "text/html; charset=UTF-8");
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

    // Bind API routes (e.g. /api/status, /api/stored, etc.)
    bind_routes(server, store);

    // Bind static frontend (web UI)
    const std::string web_root = resolve_web_root();
    bind_static_frontend(server, web_root);

    // Resolve listen port from environment
    const int listen_port = resolve_listen_port();

    const bool server_ok = server.listen(kListenAddress, listen_port);

    sampler_running = false;
    if (sampler_thread.joinable()) {
        sampler_thread.join();
    }

    return server_ok ? 0 : 1;
}
