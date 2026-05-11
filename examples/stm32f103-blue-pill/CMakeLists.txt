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

add_compile_options(-mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
add_compile_options(-ffunction-sections -fdata-sections -fno-common -fmessage-length=0)

add_compile_definitions(STM32F103xB)

add_compile_options(-Werror -Wall -Wno-pedantic)

add_compile_definitions(BUILDER=${CMAKE_CXX_COMPILER_ID}_${CMAKE_BUILD_TYPE})


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

add_link_options(-mcpu=cortex-m3 -mthumb -mfloat-abi=soft)

add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map,--sort-section=alignment)
add_link_options(-T ${LINKER_SCRIPT})

add_executable(${PROJECT_NAME} ${SOURCES} ${LINKER_SCRIPT})

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-unwind-tables>)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics -fno-use-cxa-atexit>)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wno-unused-command-line-argument>)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-c++-static-destructors>)

    # Need libgcc for software FP functions
    find_program(ARM_GCC arm-none-eabi-gcc REQUIRED)
    execute_process(
        COMMAND ${ARM_GCC} -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -print-libgcc-file-name
        OUTPUT_VARIABLE LIBGCC_FILE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    get_filename_component(LIBGCC_DIR "${LIBGCC_FILE}" DIRECTORY)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LIBGCC_DIR} -lgcc")

    include_directories(${CMAKE_SOURCE_DIR}/app/clang)
endif()


add_subdirectory("${CMAKE_SOURCE_DIR}/../../modules/etl" "${CMAKE_BINARY_DIR}/etl")

set(PULSE_PORT ARM_CM3)
add_subdirectory("${CMAKE_SOURCE_DIR}/../../src" "${CMAKE_BINARY_DIR}/pulse")
target_link_libraries(${PROJECT_NAME} PRIVATE pulse::pulse)


set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE}
Building ${BIN_FILE}")
