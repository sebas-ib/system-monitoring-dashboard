// loop.h — declaration for the background sampler thread that gathers host data.

#ifndef SYSTEM_MONITORING_DASHBOARD_LOOP_H
#define SYSTEM_MONITORING_DASHBOARD_LOOP_H

#pragma once

#include <atomic>
#include <thread>

#include "store/memory_store.h"

/**
 * Launch a background worker that captures CPU, memory, disk, network, and
 * process metrics at cfg::SAMPLE_PERIOD_S intervals.
 *
 * @param store   Shared MemoryStore instance populated with samples.
 * @param running Atomic flag toggled by the caller to stop the loop.
 * @return Joinable std::thread running the sampler.
 */
std::thread start_sampler(MemoryStore& store, std::atomic<bool>& running);

#endif // SYSTEM_MONITORING_DASHBOARD_LOOP_H
