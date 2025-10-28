//
// Created by Sebastian Ibarra on 10/9/25.
//

#ifndef SYSTEM_MONITORING_DASHBOARD_DISK_H
#define SYSTEM_MONITORING_DASHBOARD_DISK_H

struct DiskInfo{
    u_int64_t bytes_read = 0, bytes_written = 0;
};

struct DiskIO{
    std::string dev_name = "";
    double bytes_read_per_s = 0, bytes_written_per_s = 0;
};

bool get_disk_io(std::vector<DiskIO>& output);

#endif //SYSTEM_MONITORING_DASHBOARD_DISK_H
