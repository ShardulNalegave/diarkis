cmake_minimum_required(VERSION 3.15)
project(diarkis_example VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog REQUIRED)

add_executable(diarkis_example example.cc)

target_link_libraries(diarkis_example
    PRIVATE
        diarkis_client
        spdlog::spdlog
)
