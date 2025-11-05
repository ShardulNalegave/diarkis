cmake_minimum_required(VERSION 3.15)
project(diarkis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build test programs" OFF)

find_package(spdlog REQUIRED)
find_package(Protobuf REQUIRED)
find_library(BRPC_LIB NAMES brpc)
find_library(BRAFT_LIB NAMES braft)
find_library(LEVELDB_LIB NAMES leveldb)
find_library(GFLAGS_LIB NAMES gflags)

if(NOT BRPC_LIB OR NOT BRAFT_LIB)
    message(FATAL_ERROR "braft and brpc libraries are required")
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

set(DIARKIS_SOURCES
    src/fs_operations.cpp
    src/local_storage.cpp
    src/raft_fs_service.cpp
    src/fs_client.cpp
)

add_library(diarkis STATIC ${DIARKIS_SOURCES})

target_link_libraries(diarkis
    PUBLIC 
        spdlog::spdlog
        ${BRAFT_LIB}
        ${BRPC_LIB}
        ${PROTOBUF_LIBRARIES}
        ${LEVELDB_LIB}
        ${GFLAGS_LIB}
        ssl
        crypto
        dl
        z
        pthread
)

target_include_directories(diarkis
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

# Example executable
add_executable(fs_example examples/example_usage.cpp)
target_link_libraries(fs_example diarkis ${GFLAGS_LIB})
