cmake_minimum_required(VERSION 3.15)
project(insole_ble LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SimpleBLE CONFIG REQUIRED)

add_executable(insole_ble main.cpp)
target_link_libraries(insole_ble PRIVATE simpleble::simpleble)

# Platform-specific networking libraries
if(WIN32)
    target_link_libraries(insole_ble PRIVATE ws2_32 wsock32)
endif()
