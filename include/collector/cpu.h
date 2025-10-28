//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_CPU_H
#define SYSTEM_MONITORING_DASHBOARD_CPU_H

#pragma once
#include <vector>

// Returns per-logical-CPU utilization (0..100). true if ok.
bool get_cpu_core_percent(std::vector<double>& out_core_pct);

// Returns total CPU utilization (0..100). <0 on error.
double get_cpu_total_percent();


#endif //SYSTEM_MONITORING_DASHBOARD_CPU_H
