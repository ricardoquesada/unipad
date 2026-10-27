# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Ricardo Quesada
#
# Shared RP2350 hardware drivers and board-identification contracts across all
# Unipad retro computer adapters.

add_library(unipad_common STATIC
    src/unipad_board_id.c
)

target_include_directories(unipad_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(unipad_common PRIVATE
    -Wall
    -Wextra
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
    -Werror=return-type
)

target_link_libraries(unipad_common PUBLIC
    pico_stdlib
    hardware_gpio
)
