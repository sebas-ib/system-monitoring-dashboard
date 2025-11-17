//
// Created by Sebastian Ibarra on 10/9/25.
//
// Created by Sebastian Ibarra on 10/9/25.
#include "collector/memory.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

/*
*   Static function that returns true if /proc/meminfo is read successfully
*   Takes in an unordered map where keys are strings like "MemTotal" and values are 64-bit unsigned integers (bytes)
*/
static bool read_meminfo(std::unordered_map<std::string, uint64_t>& kv) {

    // Opens '/proc/meminfo'
    // Returns false if file couldn't be opened
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return false;

    // Reads file line by line until end of file
    std::string line;
    while (std::getline(f, line)) {
        // Lines look like: "MemTotal:       16333780 kB"

        // Parses each line into the parts
        // Key, value, unit
        // if one is missing then line is malformed
        std::istringstream iss(line);
        std::string key, unit;
        uint64_t val_kb = 0;
        if (!(iss >> key >> val_kb >> unit)) continue;

        // Removes trailing ':' from key
        if (!key.empty() && key.back() == ':') key.pop_back();

        // Converts kB to bytes
        uint64_t val_bytes = val_kb * 1024ULL;
        kv[key] = val_bytes;
    }
    return true;
}

// Function that initializes
bool get_system_memory_bytes(MemBytes& mb) {
    // Declare and initialize unordered map with memory statistics
    std::unordered_map<std::string, uint64_t> mi;
    if (!read_meminfo(mi)) return false;

    // Find "MemTotal" in the map,
    // If not present return false since it is a required field
    auto itTotal = mi.find("MemTotal");
    if (itTotal == mi.end()) return false;

    // Total memory present
    const uint64_t total = itTotal->second;

    // Prefer MemAvailable if present (best estimate of readily available memory)
    auto itAvail = mi.find("MemAvailable");
    if (itAvail != mi.end()) {
        const uint64_t avail = itAvail->second;
        mb.free_bytes = avail;                                      // MemAvailable is free memory
        mb.used_bytes = (total > avail) ? (total - avail) : 0ULL;   // Used is total - free
        mb.total_bytes = total;
        return true;
    }

    // Fallback if MemAvailable is missing (older kernels):
    // avail ≈ MemFree + Buffers + Cached - Shmem
    const uint64_t memfree = mi.count("MemFree") ? mi["MemFree"] : 0ULL;
    const uint64_t buffers = mi.count("Buffers") ? mi["Buffers"] : 0ULL;
    const uint64_t cached  = mi.count("Cached")  ? mi["Cached"]  : 0ULL;
    const uint64_t shmem   = mi.count("Shmem")   ? mi["Shmem"]   : 0ULL;

    uint64_t avail = memfree + buffers + cached;
    if (avail > shmem) avail -= shmem;

    mb.free_bytes = avail;
    mb.used_bytes = (total > avail) ? (total - avail) : 0ULL;
    mb.total_bytes = total;
    return true;
}
