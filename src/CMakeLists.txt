cmake_minimum_required(VERSION 3.16)

project(pulse CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB_RECURSE SOURCES "common/*.cpp")

include_directories("include")

add_library(${PROJECT_NAME} STATIC ${SOURCES})

add_library(pulse::pulse ALIAS ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE etl::etl)
