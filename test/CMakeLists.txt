cmake_minimum_required(VERSION 3.16)

project(pulse_test CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_options(-Werror -Wall)

include_directories(${CMAKE_SOURCE_DIR})

file(GLOB_RECURSE SOURCES "*.cpp")

find_package(Catch2 REQUIRED)

add_subdirectory("${CMAKE_SOURCE_DIR}/../src/modules/etl" "${CMAKE_BINARY_DIR}/etl")

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE Catch2::Catch2WithMain etl::etl)
