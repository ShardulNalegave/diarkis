cmake_minimum_required(VERSION 3.15)
project(diarkis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog REQUIRED)
find_package(msgpack-cxx REQUIRED)
find_library(BRPC_LIB NAMES brpc)
find_library(BRAFT_LIB NAMES braft)
find_library(LEVELDB_LIB NAMES leveldb)

if(NOT BRPC_LIB OR NOT BRAFT_LIB)
    message(FATAL_ERROR "braft and brpc libraries are required")
endif()

set(DIARKIS_SOURCES
    src/main.cc
    src/storage.cc
    src/state_machine.cc
)

add_library(diarkis STATIC ${DIARKIS_SOURCES})

target_link_libraries(diarkis
    PUBLIC 
        msgpackc-cxx
        spdlog::spdlog
        ${BRAFT_LIB}
        ${BRPC_LIB}
        ${PROTOBUF_LIBRARIES}
)

target_include_directories(diarkis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)
