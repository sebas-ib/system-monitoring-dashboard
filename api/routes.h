//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_ROUTES_H
#define SYSTEM_MONITORING_DASHBOARD_ROUTES_H

#pragma once
#include "third_party/httplib.h"
#include "store/memory_store.h"

// binds /api/status, /api/metrics, /api/timeseries
void bind_routes(httplib::Server& svr, MemoryStore& store);

#endif //SYSTEM_MONITORING_DASHBOARD_ROUTES_H
