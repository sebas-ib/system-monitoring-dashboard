//
// Created by Sebastian Ibarra on 11/14/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_SYSTEM_INFO_H
#define SYSTEM_MONITORING_DASHBOARD_SYSTEM_INFO_H

#include "string"

struct SystemInfo {
    int cpu_cores;
    long mem_total_bytes;
    std::string hostname;
    std::string os_name;
    std::string kernel_version;
};
SystemInfo collect_system_info();

#endif //SYSTEM_MONITORING_DASHBOARD_SYSTEM_INFO_H
