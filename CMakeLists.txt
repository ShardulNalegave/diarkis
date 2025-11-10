cmake_minimum_required(VERSION 3.15)
project(diarkis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog REQUIRED)
find_package(msgpack-cxx REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

find_library(BRPC_LIB NAMES brpc)
find_library(BRAFT_LIB NAMES braft)
find_library(LEVELDB_LIB NAMES leveldb)
find_library(GFLAGS_LIB NAMES gflags)

if(NOT BRPC_LIB OR NOT BRAFT_LIB)
    message(FATAL_ERROR "braft and brpc libraries are required")
endif()

add_subdirectory(commands)
add_subdirectory(client/cpp)
add_subdirectory(examples)

set(DIARKIS_SOURCES
    src/main.cc
    src/storage.cc
    src/state_machine.cc
    src/tcp.cc
    src/rpc.cc
    src/config.cc
)

add_executable(diarkis ${DIARKIS_SOURCES})

target_link_libraries(diarkis
    PUBLIC
        diarkis_commands

        msgpack-cxx
        spdlog::spdlog
        yaml-cpp
        Threads::Threads
        ${BRAFT_LIB}
        ${BRPC_LIB}
        ${PROTOBUF_LIBRARIES}
        ${GFLAGS_LIB}
)

target_include_directories(diarkis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)
