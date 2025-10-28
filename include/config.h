//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_CONFIG_H
#define SYSTEM_MONITORING_DASHBOARD_CONFIG_H

#pragma once
#include <string>
#include <cstdlib>
#include <unistd.h>

namespace cfg {
    inline std::string resolve_host_name(){
        const char* env = std::getenv("HOST_LABEL");
        if(env && *env) return env;

        char buf[256];
        if(gethostname(buf,sizeof(buf)) == 0){
            return buf;
        }

        return "unknown";
    }

    inline constexpr int SAMPLE_PERIOD_S   = 1;
    inline constexpr int KEEP_SECONDS      = 20;   // ring capacity hint
    inline const std::string HOST_LABEL    = resolve_host_name();
}

#endif //SYSTEM_MONITORING_DASHBOARD_CONFIG_H
