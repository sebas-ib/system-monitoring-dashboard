//
// Created by Sebastian Ibarra on 10/8/25.
//

#include <string>
#include <thread>
#include <atomic>
#include "chrono"
#include "third_party/httplib.h"
#include "store/memory_store.h"

std::atomic<bool> running(true);
static MemoryStore store; // Creating global memory before main runs

void backgroundLoopFunction(){
    while(running){
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        double test_value = 10 + (std::rand() % 90); // Fake CPU readings
        store.append("cpu.total_pct", now, test_value);
        printf("Appended sample: %s %lld -> %.2f\n", "cpu.total_pct", now, test_value);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    httplib::Server svr;
    auto beginning = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::thread background_thread(backgroundLoopFunction);

    // CORS
    svr.set_default_headers({
                                    {"Access-Control-Allow-Origin",  "*"},
                                    {"Access-Control-Allow-Methods", "GET, OPTIONS"},
                                    {"Access-Control-Allow-Methods", "Content-Type"}
                            });

    // Health endpoint
    svr.Get("/api/status", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // metrics test
    svr.Get("/api/metrics", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(
                R"({"metrics": [{ "name": "cpu.total_pct", "unit": "%", "labels": ["host"] },{ "name": "mem.used_bytes", "unit": "bytes", "labels": ["host"] },{ "name": "net.rx_bytes", "unit": "bytes/sec", "labels": ["host","iface"] }]})",
                "application/json");
    });

    svr.Get("/api/timeseries", [](const httplib::Request &, httplib::Response& res) {
        auto data = store.query("cpu.total_pct",0, std::numeric_limits<int64_t>::max());
        std::string out = "Samples: " + std::to_string(data.size()) + "\n";
        res.set_content(out,"text/plain");
    });


    // Listen on all interfaces so other devices on LAN can hit it
    const char *host = "0.0.0.0";
    int port = 8080;

    bool completed = svr.listen(host, port);

    running = false;
    background_thread.join();
    return completed ;
}