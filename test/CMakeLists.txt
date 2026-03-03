cmake_minimum_required(VERSION 3.19)

project(pulse_test CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_VERBOSE_MAKEFILE ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fno-omit-frame-pointer")
    add_definitions(-DDEBUG)
endif()

add_compile_options(-Werror -Wall)

include_directories(${CMAKE_SOURCE_DIR})

file(GLOB SOURCES "*.cpp" "common/*.cpp")

find_package(Catch2 REQUIRED)

add_subdirectory("${CMAKE_SOURCE_DIR}/../modules/etl" "${CMAKE_BINARY_DIR}/etl")

add_subdirectory("${CMAKE_SOURCE_DIR}/../src" "${CMAKE_BINARY_DIR}/pulse")

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE Catch2::Catch2WithMain etl::etl pulse::pulse)

target_include_directories(${PROJECT_NAME} PUBLIC "config")


function(JsonArrayToList jsonVar outVar)
    string(JSON len LENGTH "${${jsonVar}}")
    math(EXPR last "${len} - 1")

    set(result)

    foreach(i RANGE 0 ${last})
        string(JSON value GET "${${jsonVar}}" ${i})
        list(APPEND result "${value}")
    endforeach()

    set(${outVar} "${result}" PARENT_SCOPE)
endfunction()


# Variations for malloc test
# XXX make proper combinations
set(MALLOC_CONFIG_LIST [=[
[
    ["M_GRAN=16", "M_BSZ=1", "M_FIT=1"],
    ["M_GRAN=8", "M_BSZ=1", "M_FIT=1"],
    ["M_GRAN=8", "M_BSZ=2", "M_FIT=0"],
    ["M_GRAN=8", "M_BSZ=4", "M_FIT=0"],
    ["M_GRAN=16", "M_BSZ=2", "M_FIT=1"],
    ["M_GRAN=8", "M_BSZ=8", "M_FIT=0"],
    ["M_GRAN=256", "M_BSZ=4", "M_FIT=0"]
]
]=])

file(GLOB MALLOC_TEST_SOURCES "malloc/*.cpp" "common/*.cpp")

set(MALLOC_TEST_TARGETS)
set(MALLOC_TEST_EXES)

set(index 0)
JsonArrayToList(MALLOC_CONFIG_LIST MALLOC_CONFIG_LIST)
foreach(config IN LISTS MALLOC_CONFIG_LIST)
    set(target_name "malloc_test_${index}")

    add_executable(${target_name} ${MALLOC_TEST_SOURCES})

    JsonArrayToList(config config)
    target_compile_definitions(${target_name} PRIVATE ${config})

    set_target_properties(${target_name} PROPERTIES
        OUTPUT_NAME "malloc_test_${index}"
    )

    list(APPEND MALLOC_TEST_TARGETS ${target_name})
    list(APPEND MALLOC_TEST_EXES "$<TARGET_FILE:${target_name}>")

    target_link_libraries(${target_name} PRIVATE Catch2::Catch2WithMain etl::etl pulse::pulse)
    target_include_directories(${target_name} PUBLIC "malloc")

    math(EXPR index "${index} + 1")
endforeach()

add_custom_target(run_malloc_tests ALL DEPENDS ${MALLOC_TEST_TARGETS})
foreach(malloc_test_exe IN LISTS MALLOC_TEST_EXES)
    add_custom_command(
        TARGET run_malloc_tests
        POST_BUILD
        COMMAND valgrind --leak-check=full ${malloc_test_exe}
        VERBATIM
    )
endforeach()
