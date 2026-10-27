# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Ricardo Quesada
#
# Build configuration for Unipad C64 Firmware (Raspberry Pi Pico 2 W / RP2350).

cmake_minimum_required(VERSION 3.13)

# Unipad C64 uses Raspberry Pi Pico 2 W (RP2350 + CYW43439) by default.
# Pass -DPICO_BOARD=pico_w only when testing on a first-generation RP2040 Pico W.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico2_w")
    message(STATUS "PICO_BOARD not specified; defaulting to 'pico2_w' (RP2350)")
else()
    message(STATUS "Building Unipad C64 for PICO_BOARD='${PICO_BOARD}'")
endif()

set(BLUEPAD32_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/bluepad32")
if(NOT EXISTS "${BLUEPAD32_ROOT}/src/components/bluepad32/CMakeLists.txt")
    message(FATAL_ERROR
        "Bluepad32 submodule not found at '${BLUEPAD32_ROOT}'. "
        "Run 'git submodule update --init --recursive' first.")
endif()

# Allow optional override of BTSTACK_ROOT (e.g. -DBTSTACK_ROOT=${BLUEPAD32_ROOT}/external/btstack).
if(DEFINED BTSTACK_ROOT)
    message(STATUS "Using custom BTSTACK_ROOT: ${BTSTACK_ROOT}")
    set(PICO_BTSTACK_PATH "${BTSTACK_ROOT}")
    if(EXISTS "${BTSTACK_ROOT}/src/hci_event_builder.c")
        set(BTSTACK_MISSING_SOURCES "${BTSTACK_ROOT}/src/hci_event_builder.c")
    endif()
else()
    # Pico SDK bundles BTstack under ${PICO_SDK_PATH}/lib/btstack; resolve after import.
    set(USE_PICO_SDK_BTSTACK TRUE)
endif()

# Must be included prior to project()
include(pico_sdk_import.cmake)

if(USE_PICO_SDK_BTSTACK)
    set(BTSTACK_ROOT "${PICO_SDK_PATH}/lib/btstack")
endif()

project(unipad_c64 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED UNIPAD_FIRMWARE_VERSION)
    set(UNIPAD_FIRMWARE_VERSION "1.0.0")
endif()

pico_sdk_init()

set(UNIPAD_C64_SOURCES
    src/main.c
    src/unipad_c64_gpio.c
    src/unipad_c64_pio.c
    src/unipad_c64_platform.c
    src/unipad_c64_console.c
)

add_executable(${PROJECT_NAME}
    ${UNIPAD_C64_SOURCES}
    ${BTSTACK_MISSING_SOURCES}
)

# Generate RP2350/RP2040 C header from our cycle-exact C64 SID POT & Sync PIO programs.
pico_generate_pio_header(${PROJECT_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/pio/c64_sid_pot.pio
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNIPAD_FIRMWARE_VERSION="${UNIPAD_FIRMWARE_VERSION}"
)

option(UNIPAD_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors on first-party Unipad source files"
    OFF
)

# Enforce strict compiler diagnostics on first-party Unipad C64 source files.
# Note: Pico SDK libraries (pico_stdlib, pico_btstack_*, pico_cyw43_*) are CMake
# INTERFACE libraries that attach third-party .c sources directly to ${PROJECT_NAME},
# so -Werror is applied via set_source_files_properties to keep third-party SDK
# sources immune to toolchain-version warning drift.
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
    -Werror=return-type
)

if(UNIPAD_WARNINGS_AS_ERRORS)
    set_source_files_properties(${UNIPAD_C64_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Werror"
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BLUEPAD32_ROOT}/src/components/bluepad32/include
)

# Expose src/ globally so libbluepad32 resolves our project's sdkconfig.h & btstack_config.h.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include)
include_directories(${BTSTACK_ROOT}/src)

target_link_libraries(${PROJECT_NAME} PUBLIC
    unipad_common
    pico_stdlib
    hardware_pio
    hardware_clocks
    hardware_watchdog
    pico_cyw43_arch_none
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
    bluepad32
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common unipad_common)
add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)

# Enable both Micro-USB CDC serial console and J4 debug header UART0 (GP0/GP1, 115200 8N1).
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 1)

# Emit unipad_c64.uf2, .bin, .hex, .map, and .dis alongside the ELF binary.
pico_add_extra_outputs(${PROJECT_NAME})
