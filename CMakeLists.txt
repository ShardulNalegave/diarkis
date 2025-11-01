cmake_minimum_required(VERSION 3.15)
project(diarkis VERSION 0.1.0 LANGUAGES CXX)

add_subdirectory(vendor/spdlog)

set(DIARKIS_SOURCES
    src/main.cpp
    src/fs.cpp)

add_executable(diarkis ${DIARKIS_SOURCES})
target_include_directories(diarkis PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(diarkis PRIVATE spdlog::spdlog)
