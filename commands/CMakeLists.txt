cmake_minimum_required(VERSION 3.15)
project(diarkis_commands VERSION 0.1.0)

find_package(msgpack-cxx REQUIRED)

add_library(diarkis_commands INTERFACE)
target_link_libraries(diarkis_commands INTERFACE msgpack-cxx)
target_include_directories(diarkis_commands INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
