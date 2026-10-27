// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Non-blocking interactive USB CDC / UART0 serial console for Unipad C64.

#ifndef UNIPAD_C64_CONSOLE_H
#define UNIPAD_C64_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the non-blocking BTstack timer polling USB CDC & UART0 stdin for CLI commands.
 */
void unipad_c64_console_init(void);

#ifdef __cplusplus
}
#endif

#endif  // UNIPAD_C64_CONSOLE_H
