// routes.h — declares the HTTP route binder for the monitoring dashboard API.

#ifndef SYSTEM_MONITORING_DASHBOARD_ROUTES_H
#define SYSTEM_MONITORING_DASHBOARD_ROUTES_H

#pragma once

#include "store/memory_store.h"
#include "third_party/httplib.h"

/**
 * Register all /api/* endpoints onto the provided httplib server using data
 * retrieved from the shared MemoryStore instance.
 */
void bind_routes(httplib::Server& svr, MemoryStore& store);

#endif // SYSTEM_MONITORING_DASHBOARD_ROUTES_H
