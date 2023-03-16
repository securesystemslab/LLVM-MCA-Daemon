cmake_minimum_required(VERSION 3.13)
project(example-grpc LANGUAGES CXX)

add_subdirectory(broker)

set(CMAKE_CXX_STANDARD 17)

# find_library(vivbroker MCADVivisectBroker)

add_executable(test_broker "main.cpp")
target_link_libraries(test_broker
    PUBLIC
    MCADVivisectBroker)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/broker)
