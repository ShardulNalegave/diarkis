cmake_minimum_required(VERSION 3.15)
project(diarkis_client VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog REQUIRED)
find_package(msgpack-cxx REQUIRED)
find_package(Threads REQUIRED)

set(DIARKIS_CLIENT_SRCS
    src/client.cc
    src/rpc.cc
    src/tcp.cc)

add_library(diarkis_client STATIC ${DIARKIS_CLIENT_SRCS})
target_include_directories(diarkis_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(diarkis_client
    PUBLIC
        diarkis_commands
    PRIVATE
        spdlog::spdlog
        msgpack-cxx
        Threads::Threads
)
