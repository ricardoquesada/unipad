// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Bluepad32 custom `uni_platform` implementation for Unipad C64 (Pico 2 W / RP2350).

#ifndef UNIPAD_C64_PLATFORM_H
#define UNIPAD_C64_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include <uni.h>

#include "unipad_c64_pio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gamepad emulation modes supported by Unipad C64.
 *
 * Cycled via the onboard `MODE` button (`GP21`), gamepad `Start` button
 * (when not in `5buttons` mode), keyboard `Tab` key, or `gamepad_mode` CLI command.
 */
typedef enum {
    UNIPAD_GAMEPAD_MODE_NONE = 0,
    UNIPAD_GAMEPAD_MODE_NORMAL = 1,    /**< 1 gamepad -> 1 C64 joystick port (1 BT LED blink) */
    UNIPAD_GAMEPAD_MODE_MOUSE = 2,     /**< Analog sticks -> PIO 1351 mouse (2 BT LED blinks) */
    UNIPAD_GAMEPAD_MODE_TWINSTICK = 3, /**< 1 gamepad -> both C64 ports (3 BT LED blinks) */
} unipad_gamepad_mode_t;

/**
 * @brief User-configurable C64 SID POT modes for joystick/gamepad ports.
 *
 * Numeric values match `uni_platform_unijoysticle_c64_pot_mode_t` for 100%
 * Bluepad32 property (`bp.uni.c64pot`) compatibility.
 * Note: Any port occupied by a Bluetooth Mouse (or a Gamepad in `MOUSE` mode)
 * automatically switches that specific port's PIO channel to `1351` mouse mode
 * while leaving the other port in the user's configured POT mode!
 */
typedef enum {
    UNIPAD_C64_POT_MODE_INVALID = 0,
    UNIPAD_C64_POT_MODE_3BUTTONS = 1, /**< POTX = Button 2, POTY = Button 3 (default) */
    UNIPAD_C64_POT_MODE_5BUTTONS = 2, /**< 3buttons + CrystalCT Select (UP+DOWN) & Start (LEFT+RIGHT) */
    UNIPAD_C64_POT_MODE_RUMBLE = 3,   /**< C64 SID POTY pulses trigger Bluetooth gamepad rumble */
    UNIPAD_C64_POT_MODE_PADDLE = 4,   /**< PIO Commodore 1311/1312 analog paddles (0..255) */
    UNIPAD_C64_POT_MODE_COUNT = 5,
} unipad_c64_pot_mode_t;

/**
 * @brief Returns the singleton Bluepad32 `struct uni_platform` descriptor for Unipad C64.
 */
struct uni_platform* unipad_c64_get_platform(void);

// ---------------------------------------------------------------------------
// Runtime Control & Introspection APIs (invoked from BTstack main thread / CLI)
// ---------------------------------------------------------------------------

/** @brief Swaps Control Port #1 and Control Port #2 assignments across active controllers. */
void unipad_c64_platform_swap_ports(void);

/** @brief Advances the primary connected gamepad to the next emulation mode. */
void unipad_c64_platform_next_gamepad_mode(void);

/** @brief Sets the primary connected gamepad to a specific emulation mode. */
bool unipad_c64_platform_set_gamepad_mode(unipad_gamepad_mode_t mode);

/** @brief Returns the active emulation mode of the primary connected gamepad (`NONE` if none). */
unipad_gamepad_mode_t unipad_c64_platform_get_gamepad_mode(void);

/** @brief Sets and persists the C64 POT mode (`3buttons`, `5buttons`, `rumble`, `paddle`). */
bool unipad_c64_platform_set_pot_mode(unipad_c64_pot_mode_t mode);

/** @brief Returns the active C64 POT mode. */
unipad_c64_pot_mode_t unipad_c64_platform_get_pot_mode(void);

/** @brief Sets and persists the autofire rate in clicks-per-second (1..62). */
bool unipad_c64_platform_set_autofire_cps(uint8_t cps);

/** @brief Returns the active autofire rate in clicks-per-second. */
uint8_t unipad_c64_platform_get_autofire_cps(void);

/** @brief Sets and persists the Commodore 1351 mouse movement sensitivity multiplier (0.1..5.0). */
bool unipad_c64_platform_set_mouse_scale(float scale);

/** @brief Returns the active Commodore 1351 mouse movement sensitivity multiplier. */
float unipad_c64_platform_get_mouse_scale(void);

/** @brief Enables or disables Bluetooth discovery/scanning explicitly from user command. */
void unipad_c64_platform_set_bt_scanning(bool enabled);

/** @brief Prints a comprehensive Unipad C64 hardware, PIO, and Bluetooth status report. */
void unipad_c64_platform_print_status(void);

/** @brief Human-readable name for a `unipad_gamepad_mode_t`. */
const char* unipad_c64_gamepad_mode_to_str(unipad_gamepad_mode_t mode);

/** @brief Human-readable name for a `unipad_c64_pot_mode_t`. */
const char* unipad_c64_pot_mode_to_str(unipad_c64_pot_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif  // UNIPAD_C64_PLATFORM_H
