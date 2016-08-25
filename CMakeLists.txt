cmake_minimum_required(VERSION 3.6)
project(dTCP)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_subdirectory(clogger)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY /usr/local/bin)

set(SOURCE_FILES main.c)
add_executable(dtcp ${SOURCE_FILES})
target_link_libraries(dtcp LINK_PUBLIC clogger)