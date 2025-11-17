#include <atomic>
#include "third_party/httplib.h"
#include "store/memory_store.h"
#include "collector/loop.h"
#include "api/routes.h"
#include "store/system_info.h"
#include "config.h"

int main() {
    std::atomic<bool> running(true);
    MemoryStore store(cfg::KEEP_SECONDS, cfg::SAMPLE_PERIOD_S);

    SystemInfo sys = collect_system_info();

    store.put_metadata("system", {
            {"cpu_cores", sys.cpu_cores},
            {"mem_total_bytes", sys.mem_total_bytes},
            {"hostname", sys.hostname},
            {"os_name", sys.os_name},
            {"kernel_version", sys.kernel_version}
    });

    // start sampler
    auto bg = start_sampler(store, running);


    // start server
    httplib::Server svr;
    bind_routes(svr, store);
    const char* host = "0.0.0.0";
    int port = 8080;
    bool ok = svr.listen(host, port);

    running = false;
    if (bg.joinable()) bg.join();

    return ok ? 0 : 1;
}

