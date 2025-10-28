//
// Created by Sebastian Ibarra on 10/9/25.
//
#include <mach/mach.h>
#include <vector>
#include "collector/cpu.h"

struct CpuCoreTicks { uint64_t user=0, system=0, idle=0, nice=0; };

bool get_cpu_core_percent(std::vector<double>& out_core_pct) {
    processor_info_array_t cpuInfo;
    mach_msg_type_number_t numCpuInfo;
    natural_t numCPU = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                            &numCPU, &cpuInfo, &numCpuInfo) != KERN_SUCCESS || numCPU==0) return false;

    static std::vector<CpuCoreTicks> last(numCPU);
    if (last.size() != numCPU) last.assign(numCPU, CpuCoreTicks{});

    const processor_cpu_load_info_t cpuLoad = (processor_cpu_load_info_t)cpuInfo;
    out_core_pct.resize(numCPU);

    for (natural_t i = 0; i < numCPU; i++) {
        uint64_t u = cpuLoad[i].cpu_ticks[CPU_STATE_USER];
        uint64_t s = cpuLoad[i].cpu_ticks[CPU_STATE_SYSTEM];
        uint64_t id= cpuLoad[i].cpu_ticks[CPU_STATE_IDLE];
        uint64_t n = cpuLoad[i].cpu_ticks[CPU_STATE_NICE];

        uint64_t du = u - last[i].user, ds = s - last[i].system;
        uint64_t di = id - last[i].idle, dn = n - last[i].nice;
        uint64_t total = du + ds + di + dn;
        last[i] = {u,s,id,n};

        out_core_pct[i] = (total==0) ? 0.0 : 100.0 * (double)(du+ds+dn) / (double)total;
    }
    vm_deallocate(mach_task_self(), (vm_address_t)cpuInfo, numCpuInfo * sizeof(integer_t));
    return true;
}

double get_cpu_total_percent() {
    host_cpu_load_info_data_t cpuinfo{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpuinfo, &count) != KERN_SUCCESS) return -1.0;

    static uint64_t lu=0, ls=0, li=0, ln=0;
    uint64_t u = cpuinfo.cpu_ticks[CPU_STATE_USER];
    uint64_t s = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
    uint64_t i = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
    uint64_t n = cpuinfo.cpu_ticks[CPU_STATE_NICE];
    uint64_t du=u-lu, ds=s-ls, di=i-li, dn=n-ln, total=du+ds+di+dn;
    lu=u; ls=s; li=i; ln=n;
    if (!total) return 0.0;
    return 100.0 * (double)(du+ds+dn) / (double)total;
}
