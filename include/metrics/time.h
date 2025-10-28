//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_TIME_H
#define SYSTEM_MONITORING_DASHBOARD_TIME_H

#pragma once
#include <chrono>
#include <cstdint>

inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

#endif //SYSTEM_MONITORING_DASHBOARD_TIME_H
