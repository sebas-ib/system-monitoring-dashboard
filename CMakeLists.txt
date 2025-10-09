cmake_minimum_required(VERSION 3.27)
project(system_monitoring_dashboard)

set(CMAKE_CXX_STANDARD 17)

add_executable(system_monitoring_dashboard main.cpp
        include/third_party/httplib.h)
