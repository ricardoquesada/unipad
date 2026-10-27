// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Atomic C64 Control Port digital GPIO, status LED, autofire, and debounced
// pushbutton hardware abstraction layer for Unipad C64 (Rev. A).

#ifndef UNIPAD_C64_GPIO_H
#define UNIPAD_C64_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical C64 Control Port identifier on Unipad C64.
 */
typedef enum {
    UNIPAD_PORT_1 = 0, /**< C64 Control Port #1 (J2, GP2..GP6 digital, GP7/GP8 POT, GP16 SYNC) */
    UNIPAD_PORT_2 = 1, /**< C64 Control Port #2 (J3, GP9..GP13 digital, GP14/GP15 POT, GP17 SYNC) */
    UNIPAD_PORT_COUNT = 2,
} unipad_port_t;

/**
 * @brief 5-bit digital C64 joystick line state (1 = asserted/pulled LOW on C64 DB9, 0 = released/Hi-Z).
 */
typedef struct {
    bool up;        /**< DB9 Pin 1 ($DC00/$DC01 bit 0) */
    bool down;      /**< DB9 Pin 2 ($DC00/$DC01 bit 1) */
    bool left;      /**< DB9 Pin 3 ($DC00/$DC01 bit 2) */
    bool right;     /**< DB9 Pin 4 ($DC00/$DC01 bit 3) */
    bool fire;      /**< DB9 Pin 6 ($DC00/$DC01 bit 4) */
    bool auto_fire; /**< Virtual shoulder autofire trigger */
} unipad_digital_joy_t;

/**
 * @brief Callback invoked on the BTstack main thread when an onboard tactile switch is pressed.
 */
typedef void (*unipad_button_callback_t)(void);

/**
 * @brief Early boot GPIO initialization (safe to call BEFORE `cyw43_arch_init()`).
 *
 * Immediately places all 74HCT05 open-drain outputs into Hi-Z (`GPIO LOW`),
 * disables internal pulls on the 5V fail-safe `GP16`/`GP17` SID sync dividers,
 * illuminates Port 1 & Port 2 LEDs during boot self-test, and configures the
 * `MODE` (`GP21`) and `SWAP` (`GP22`) pushbuttons.
 */
void unipad_c64_gpio_init_early(void);

/**
 * @brief Starts BTstack run-loop timers for non-blocking pushbutton debouncing and autofire.
 *
 * Must be called from the BTstack main thread (e.g. inside `on_init_complete()`).
 *
 * @param on_mode_pressed Callback fired on debounced `MODE` (`GP21`) button press.
 * @param on_swap_pressed Callback fired on debounced `SWAP` (`GP22`) button press.
 * @param autofire_cps    Initial autofire frequency in clicks-per-second (1..62).
 */
void unipad_c64_gpio_start_timers(unipad_button_callback_t on_mode_pressed,
                                  unipad_button_callback_t on_swap_pressed,
                                  uint8_t autofire_cps);

/**
 * @brief Atomically updates all 5 digital lines (`UP`, `DOWN`, `LEFT`, `RIGHT`, `FIRE`) of a C64 port.
 *
 * Uses RP2350 single-cycle `gpio_put_masked()` so C64 CIA1 keyboard/joystick reads
 * never observe torn intermediate bit transitions.
 *
 * @param port Target C64 Control Port (`UNIPAD_PORT_1` or `UNIPAD_PORT_2`).
 * @param joy  Desired digital direction, fire, and autofire state.
 */
void unipad_c64_gpio_set_digital_port(unipad_port_t port, const unipad_digital_joy_t* joy);

/**
 * @brief Releases all 5 digital lines (`UP`, `DOWN`, `LEFT`, `RIGHT`, `FIRE`) and disables autofire on a port.
 *
 * @param port Target C64 Control Port.
 */
void unipad_c64_gpio_clear_digital_port(unipad_port_t port);

/**
 * @brief Updates the active autofire rate in clicks-per-second (1..62 CPS).
 *
 * @param cps Clicks per second (e.g., 7 = Quickgun, 13 = Zipstick, 29 = Quickshot, 62 = Competition Pro).
 */
void unipad_c64_gpio_set_autofire_cps(uint8_t cps);

/**
 * @brief Returns the currently active autofire rate in clicks-per-second.
 */
uint8_t unipad_c64_gpio_get_autofire_cps(void);

/**
 * @brief Sets the Port 1 (Green, `GP19`) and Port 2 (Red, `GP20`) status LEDs.
 *
 * @param port1_on True to illuminate Port 1 Green LED (`D5`).
 * @param port2_on True to illuminate Port 2 Red LED (`D6`).
 */
void unipad_c64_gpio_set_port_leds(bool port1_on, bool port2_on);

/**
 * @brief Sets the steady-state Bluetooth discovery LED (`GP18` Blue LED `D4` + Pico 2 W onboard LED).
 *
 * If an asynchronous blink sequence is currently running, the new steady state is
 * cached and restored automatically once the blink sequence finishes.
 *
 * @param enabled True when Bluetooth scanning/discovery is active.
 */
void unipad_c64_gpio_set_bt_led(bool enabled);

/**
 * @brief Asynchronously blinks the Bluetooth LED (`GP18`) `times` pulses without blocking BTstack.
 *
 * @param times Number of 100ms-OFF / 100ms-ON pulses (1 = Normal, 2 = Mouse, 3 = Twin-Stick).
 */
void unipad_c64_gpio_blink_bt_led(int times);

/**
 * @brief Reads the raw (un-debounced) instantaneous state of the `MODE` pushbutton (`GP21`).
 *
 * Used at boot completion to check whether the user held `MODE` during power-up
 * to erase stored Bluetooth pairing keys.
 *
 * @return True if `MODE` (`GP21`) is currently held pressed (active-LOW).
 */
bool unipad_c64_gpio_is_mode_pressed_raw(void);

#ifdef __cplusplus
}
#endif

#endif  // UNIPAD_C64_GPIO_H
