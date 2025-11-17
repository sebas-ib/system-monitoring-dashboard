cmake_minimum_required(VERSION 3.16)
project(system_monitoring_dashboard CXX)
set(CMAKE_CXX_STANDARD 17)

# Headers
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)

# Pick collectors by OS
set(COLLECTOR_SRCS collector/loop.cpp
        collector/proc_linux.cpp)
if(APPLE)
    list(APPEND COLLECTOR_SRCS
            collector/cpu_macos.cpp
            collector/memory_macos.cpp
            # collector/disk_macos.cpp
            # collector/net_macos.cpp
    )
else() # Linux
    list(APPEND COLLECTOR_SRCS
            collector/cpu_linux.cpp
            collector/memory_linux.cpp
            collector/disk_linux.cpp
            collector/net_linux.cpp
            collector/proc_linux.cpp
    )
endif()

add_executable(dashboard
        main.cpp
        api/routes.cpp
        store/memory_store.cpp
        store/system_info.cpp
        ${COLLECTOR_SRCS}
)
