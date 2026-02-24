cmake_minimum_required(VERSION 3.16)

project(firmware C CXX ASM)

message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Linker: ${CMAKE_LINKER}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 23)

set(CMAKE_VERBOSE_MAKEFILE ON)


# set(CMAKE_C_COMPILER_TARGET "arm-none-eabi")
# set(CMAKE_CXX_COMPILER_TARGET "arm-none-eabi")

add_compile_options(-mcpu=cortex-m3 -mthumb)
add_compile_options(-ffunction-sections -fdata-sections -fno-common -fmessage-length=0)

add_compile_definitions(STM32F103xB)

add_compile_options(-Werror -Wall -Wno-pedantic)

# add_compile_options(-nostdlib -nodefaultlibs)


if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message(STATUS "Maximum optimization for speed")
    add_compile_options(-O3 -ffast-math)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    message(STATUS "Maximum optimization for speed, debug info included")
    add_compile_options(-O3 -ffast-math -g)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
    message(STATUS "Maximum optimization for size")
    add_compile_options(-Os -g)
else ()
    message(STATUS "Minimal optimization, debug info included")
    add_compile_options(-O0 -g -fno-omit-frame-pointer)
    add_compile_definitions(DEBUG)
endif ()


set(CUBE_DIR "${CMAKE_SOURCE_DIR}/STM32CubeF1")

include_directories(
    "${CMAKE_SOURCE_DIR}/app"
    "${CUBE_DIR}/Drivers/CMSIS/Include"
    "${CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include"
    "${CUBE_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc"
)

file(GLOB_RECURSE SOURCES
    "app/*.c"
    "app/*.cpp"
    "${CUBE_DIR}/*.c"
    "${CUBE_DIR}/*.s"
)

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32F103CBTX_FLASH.ld)

add_link_options(-mcpu=cortex-m3 -mthumb)

add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map)
add_link_options(-T ${LINKER_SCRIPT})

# link_directories(/usr/lib/gcc/arm-none-eabi/14.2.0/)

add_executable(${PROJECT_NAME} ${SOURCES} ${LINKER_SCRIPT})

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wno-unused-command-line-argument>)

set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE}
Building ${BIN_FILE}")
