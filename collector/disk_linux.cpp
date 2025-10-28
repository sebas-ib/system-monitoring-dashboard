//
// Created by Sebastian Ibarra on 10/9/25.
//
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <unordered_map>
#include <stdio.h>
#include "metrics/time.h"
#include <sstream>
#include <collector/disk.h>


constexpr bool AGGREGATE_PARTITIONS = true;

inline bool is_counted_device(const std::string& n){
    // Drop purely virtual or optical devices
    return !(n.rfind("loop",0)==0 || n.rfind("ram",0)==0 ||
             n.rfind("sr",0)==0   || n.rfind("fd",0)==0);
}

static bool is_whole_device(const std::string& name){
    if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 ||
        name.rfind("sr", 0) == 0 || name.rfind("fd", 0) == 0) return false;

    // nvme0n1p1 -> partition (has 'p#') nvme0n1 -> whole device
    if (name.rfind("nvme",0)==0) return name.find('p') == std::string::npos;

    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())))
        return false;

    return true; // sda, vda, ...
}


// Extract the base sysfs directory name for a device (strip partition suffix)
inline std::string base_device_name(const std::string& name) {
    if (name.rfind("nvme", 0) == 0 || name.rfind("mmcblk", 0) == 0) {
        auto pos_p = name.find('p');
        return (pos_p == std::string::npos) ? name : name.substr(0, pos_p);
    }
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back()))) {
        size_t i = name.size();
        while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i-1]))) --i;
        return name.substr(0, i);
    }
    return name;
}

struct Row {
    std::string name;
    uint64_t sectors_read   = 0;
    uint64_t sectors_written = 0;
};

static bool read_diskstats(std::unordered_map<std::string, Row>& rows){
    std::ifstream f("/proc/diskstats");
    if(!f.is_open()) return false;
    std::string line;

    rows.clear();
    while(std::getline(f, line)){
        std::istringstream iss(line);

        int major=0, minor=0;
        std::string name;
        uint64_t reads_completed, reads_merged, sectors_read, ms_reading;
        uint64_t writes_completed, writes_merged, sectors_written, ms_writing;
        uint64_t ios_in_progress, ms_doing_io, weighted_ms_doing_io;

        if (!(iss >> major >> minor >> name)) continue;
        if (!(iss >> reads_completed >> reads_merged >> sectors_read >> ms_reading
                  >> writes_completed >> writes_merged >> sectors_written >> ms_writing
                  >> ios_in_progress >> ms_doing_io >> weighted_ms_doing_io)) continue;


        if (!is_counted_device(name)) continue;

        rows[name] = Row{name, sectors_read, sectors_written};
    }
    return true;
}

bool get_disk_io(std::vector<DiskIO>& output){
    // Keep previous values for calculations
    static std::unordered_map<std::string, DiskInfo> prev_values; // sectors
    static bool initialized = false;
    static uint64_t prev_time;

    // Current values
    std::unordered_map<std::string, Row> curr_rows;
    if(!read_diskstats(curr_rows)){
        output.clear(); // Return false if read doesn't work
        return false;
    }

    const u_int64_t time_now = now_ms();

    std::unordered_map<std::string, DiskInfo> curr_values;
    curr_values.reserve(curr_rows.size()); // We know they'll be the same size

    for(const auto&[name,r]: curr_rows){
        DiskInfo info;
        info.bytes_read = r.sectors_read;
        info.bytes_written = r.sectors_written;
        curr_values.emplace(name, info);
    }

    // First Call, initialize and return
    if(!initialized) {
        prev_values = curr_values;
        prev_time = time_now;
        initialized = true;
        output.clear();
        return true;
    }

    // Since time elapsed might not be exaclty 1000ms, we calculate exact time
    const double dt_s = (time_now > prev_time)
            ? static_cast<double>(time_now - prev_time) / 1000
            : 0.0;

    // If time didn't advance just update values and return
    if(dt_s <= 0){
        prev_values = std::move(curr_values);
        prev_time = time_now;
        output.clear();
        return true;
    }

    // Compute sector deltas per entry
    struct Delta { uint64_t rd=0, wr=0; };
    std::unordered_map<std::string, Delta> deltas;
    for (const auto& [name, curr] : curr_values){
        auto itp = prev_values.find(name);
        if (itp == prev_values.end()) continue;
        const auto& prev = itp->second;

        Delta d;
        if (curr.bytes_read    >= prev.bytes_read)    d.rd = curr.bytes_read    - prev.bytes_read;
        if (curr.bytes_written >= prev.bytes_written) d.wr = curr.bytes_written - prev.bytes_written;
        deltas[name] = d;
    }

    std::unordered_map<std::string, Delta> by_key;
    if (AGGREGATE_PARTITIONS) {
        for (const auto& [name, d] : deltas) {
            auto& g = by_key[base_device_name(name)];
            g.rd += d.rd;
            g.wr += d.wr;
        }
    } else {
        by_key = std::move(deltas); // keep each line (parent + partitions)
    }

    output.clear();
    output.reserve(curr_values.size());

    static constexpr double DISKSTATS_SECTOR_BYTES = 512.0;

    // For each device in curr values calculate bps;
    for (const auto&[key, d]: by_key){
        DiskIO io;
        io.dev_name = key;
        io.bytes_read_per_s = (d.rd * DISKSTATS_SECTOR_BYTES) / dt_s;
        io.bytes_written_per_s = (d.wr * DISKSTATS_SECTOR_BYTES) / dt_s;
        output.push_back(io);
    }

    // Update baselines
    prev_values = std::move(curr_values);
    prev_time = time_now;
    return true;
}