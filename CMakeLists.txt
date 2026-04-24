cmake_minimum_required(VERSION 3.10)
project(raw_marketdata_capture LANGUAGES CXX)
 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
include_directories(include)
 
add_executable(raw_marketdata_capture
    src/config_parser.cpp
    src/socket.cpp
    src/capture.cpp
    src/main.cpp
)
 
target_link_libraries(raw_marketdata_capture pthread)
