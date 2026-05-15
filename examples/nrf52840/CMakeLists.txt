cmake_minimum_required(VERSION 3.16)

project(firmware C CXX ASM)

message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Linker: ${CMAKE_LINKER}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 23)

set(CMAKE_VERBOSE_MAKEFILE ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fno-omit-frame-pointer")
    add_compile_definitions(DEBUG)
endif()

set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

add_compile_options(${CPU_FLAGS})
add_compile_options(-ffunction-sections -fdata-sections -fno-common -fmessage-length=0)

add_compile_definitions(NRF52840_XXAA)

add_compile_options(-Werror -Wall -Wno-pedantic)

add_compile_definitions(BUILDER=${CMAKE_CXX_COMPILER_ID}_${CMAKE_BUILD_TYPE})


if (NOT DEFINED SDK_DIR)
    set(SDK_DIR "${CMAKE_SOURCE_DIR}/NRF5_SDK")
endif()

include_directories(
    "${CMAKE_SOURCE_DIR}/app"
    "${CMAKE_SOURCE_DIR}/app/nrf"
    "${SDK_DIR}/modules/nrfx"
    "${SDK_DIR}/modules/nrfx/mdk"
    "${SDK_DIR}/modules/nrfx/hal"
    "${SDK_DIR}/modules/nrfx/drivers/include"
    "${SDK_DIR}/components/toolchain/cmsis/include"
    "${SDK_DIR}/components/libraries/util"
    "${SDK_DIR}/components/drivers_nrf/nrf_soc_nosd"
)

file(GLOB_RECURSE SOURCES
    "app/*.c"
    "app/*.cpp"
    "${SDK_DIR}/*.c"
    "${SDK_DIR}/*.s"
    "${SDK_DIR}/*.S"
)

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/nrf52840.ld)

add_link_options(${CPU_FLAGS})

add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map,--sort-section=alignment)
add_link_options(-T ${LINKER_SCRIPT})

add_executable(${PROJECT_NAME} ${SOURCES} ${LINKER_SCRIPT})

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-unwind-tables>)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics -fno-use-cxa-atexit>)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wno-unused-command-line-argument>)


add_subdirectory("${CMAKE_SOURCE_DIR}/../../modules/etl" "${CMAKE_BINARY_DIR}/etl")

set(PULSE_PORT ARM_CM4)
add_subdirectory("${CMAKE_SOURCE_DIR}/../../src" "${CMAKE_BINARY_DIR}/pulse")
target_link_libraries(${PROJECT_NAME} PRIVATE pulse::pulse)


set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE}
Building ${BIN_FILE}")
