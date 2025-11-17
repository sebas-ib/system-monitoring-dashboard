//
// Created by Sebastian Ibarra on 11/14/25.
//
#include <unistd.h>
#include <sys/utsname.h>
#include "collector/memory.h"
#include "store/system_info.h"

SystemInfo collect_system_info() {
    SystemInfo info{};

    info.cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    MemBytes mb;
    if (get_system_memory_bytes(mb)) {
        info.mem_total_bytes = mb.total_bytes;
    }

    char hostbuf[256];
    gethostname(hostbuf, sizeof(hostbuf));
    info.hostname = hostbuf;

    struct utsname u{};
    if (uname(&u) == 0) {
        info.os_name = u.sysname;
        info.kernel_version = u.release;
    }

    return info;
}