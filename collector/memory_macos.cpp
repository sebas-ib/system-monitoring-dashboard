//
// Created by Sebastian Ibarra on 10/9/25.
//
#include <mach/mach.h>
#include "collector/memory.h"

bool get_system_memory_bytes(MemBytes& mb) {
    vm_statistics64_data_t vmStats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info_t)&vmStats, &count) != KERN_SUCCESS)
        return false;
    uint64_t page = (uint64_t)vm_page_size;
    uint64_t available = (vmStats.free_count + vmStats.inactive_count + vmStats.speculative_count) * page;
    uint64_t used      = (vmStats.active_count + vmStats.wire_count + vmStats.compressor_page_count) * page;
    mb.used_bytes = used;
    mb.free_bytes = available;
    return true;
}
