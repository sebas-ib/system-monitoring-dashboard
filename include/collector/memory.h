//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_MEMORY_H
#define SYSTEM_MONITORING_DASHBOARD_MEMORY_H

#pragma once
#include <cstdint>

struct MemBytes { uint64_t used_bytes=0, free_bytes=0, total_bytes=0;}; // free = available

bool get_system_memory_bytes(MemBytes& mb);


#endif //SYSTEM_MONITORING_DASHBOARD_MEMORY_H
