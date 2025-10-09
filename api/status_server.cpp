//
// Created by Sebastian Ibarra on 10/8/25.
//

#include <string>
#include "third_party/httplib.h"

int main() {
    httplib::Server svr;

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


    // Optional OPTIONS preflight
    svr.Options("api/status", [](const httplib::Request &, httplib::Response &res) {
        res.status = 204;
    });

    // Listen on all interfaces so other devices on LAN can hit it
    const char *host = "0.0.0.0";
    int port = 8080;
    return svr.listen(host, port) ? 0 : 1;
}