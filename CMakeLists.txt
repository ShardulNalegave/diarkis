cmake_minimum_required(VERSION 3.15)
project(diarkis VERSION 0.1.0 LANGUAGES CXX)

add_subdirectory(vendor/spdlog)
add_subdirectory(vendor/CLI11)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

find_library(LEVELDB_LIBRARY NAMES leveldb)
if(NOT LEVELDB_LIBRARY)
    message(FATAL_ERROR "leveldb library not found")
endif()

find_library(BRPC_LIBRARY NAMES brpc)
find_path(BRPC_INCLUDE_DIR NAMES brpc/server.h)
if(NOT BRPC_LIBRARY)
    message(FATAL_ERROR "brpc library not found. Make sure brpc is installed.")
endif()

find_library(BRAFT_LIBRARY NAMES braft)
find_path(BRAFT_INCLUDE_DIR NAMES braft/raft.h)
if(NOT BRAFT_LIBRARY)
    message(FATAL_ERROR "braft library not found. Make sure braft is installed.")
endif()

message(STATUS "Found brpc: ${BRPC_LIBRARY}")
message(STATUS "Found braft: ${BRAFT_LIBRARY}")

set(DIARKIS_SOURCES
    src/main.cpp
    src/events.cpp
    src/fs_watcher.cpp
    src/state_machine.cpp
    src/raft_node.cpp)

add_executable(diarkis ${DIARKIS_SOURCES})
target_link_libraries(diarkis
    PRIVATE
        spdlog::spdlog
        CLI11::CLI11
        Threads::Threads
        ${BRAFT_LIBRARY}
        ${BRPC_LIBRARY}
        ${PROTOBUF_LIBRARIES}
        ${LEVELDB_LIBRARY}
)

target_include_directories(diarkis
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${BRPC_INCLUDE_DIR}
        ${BRAFT_INCLUDE_DIR}
        ${PROTOBUF_INCLUDE_DIRS}
)
