cmake_minimum_required(VERSION 3.16)

project(pulse CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB_RECURSE SOURCES "common/*.cpp")

add_library(${PROJECT_NAME} INTERFACE)
add_library(pulse::pulse ALIAS ${PROJECT_NAME})

if (DEFINED PULSE_PORT)
    set(PULSE_PORT_DIR "${PROJECT_SOURCE_DIR}/ports/${PULSE_PORT}")
    target_include_directories(${PROJECT_NAME} INTERFACE "${PULSE_PORT_DIR}/include")
    file(GLOB_RECURSE PORT_SOURCES "${PULSE_PORT_DIR}/impl/*.cpp")
endif()

target_include_directories(${PROJECT_NAME} INTERFACE "include")

target_sources(${PROJECT_NAME} INTERFACE ${SOURCES} ${PORT_SOURCES})

target_link_libraries(${PROJECT_NAME} INTERFACE etl::etl)
