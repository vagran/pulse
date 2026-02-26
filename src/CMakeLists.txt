cmake_minimum_required(VERSION 3.16)

project(pulse CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB_RECURSE SOURCES "common/*.cpp")

add_library(${PROJECT_NAME} INTERFACE)
add_library(pulse::pulse ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} INTERFACE "include")

target_sources(${PROJECT_NAME} INTERFACE ${SOURCES})

target_link_libraries(${PROJECT_NAME} INTERFACE etl::etl)
