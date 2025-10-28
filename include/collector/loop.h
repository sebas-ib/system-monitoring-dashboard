//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_LOOP_H
#define SYSTEM_MONITORING_DASHBOARD_LOOP_H

#pragma once
#include <atomic>
#include <thread>
#include "store/memory_store.h"

// Starts background sampler thread; returns a joinable std::thread
std::thread start_sampler(MemoryStore& store, std::atomic<bool>& running);

#endif //SYSTEM_MONITORING_DASHBOARD_LOOP_H
