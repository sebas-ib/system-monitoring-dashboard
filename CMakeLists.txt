cmake_minimum_required(VERSION 3.15)
project(system_monitoring_dashboard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include path (for third_party/httplib.h)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Minimal server target
add_executable(smd_server
        api/status_server.cpp
        store/memory_store.cpp
)

# On macOS you don’t need extra libs for httplib’s default HTTP (no OpenSSL).
# If you later enable HTTPS, you’d link OpenSSL.
