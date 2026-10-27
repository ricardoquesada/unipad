// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// RP2350 PIO-accelerated Commodore 64 SID Potentiometer & Sync subsystem:
//   - Commodore 1351 Proportional Analog Mouse (6-bit modulo-64, 0 CPU interrupts)
//   - Commodore 1311/1312 Analog Paddles (8-bit 0..255, 0 CPU interrupts)
//   - C64GS / Cheetah Annihilator 2nd & 3rd POT buttons (3-button / 5-button modes)
//   - Zero-interrupt C64 SID Rumble pulse detection & PAL/NTSC clock auto-calibration

#ifndef UNIPAD_C64_PIO_H
#define UNIPAD_C64_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "unipad_c64_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-port hardware operating mode for a C64 Control Port's POTX/POTY lines.
 *
 * Each port (`UNIPAD_PORT_1` and `UNIPAD_PORT_2`) operates independently so a user
 * can pair a Bluetooth Mouse (1351 proportional PIO mode on Port 1) simultaneously
 * with a Bluetooth Gamepad (3-button, 5-button, Rumble, or Paddle mode on Port 2).
 */
typedef enum {
    UNIPAD_POT_HW_BUTTONS = 0,    /**< SIO digital POT buttons (unpressed=$FF, pressed=~$03) */
    UNIPAD_POT_HW_RUMBLE = 1,     /**< SIO Hi-Z + PIO sync pulse detector for C64 Rumble */
    UNIPAD_POT_HW_1351_MOUSE = 2, /**< PIO `c64_sid_pot` Commodore 1351 proportional mouse */
    UNIPAD_POT_HW_PADDLE = 3,     /**< PIO `c64_sid_pot` Commodore 1311/1312 analog paddles */
} unipad_pot_hw_mode_t;

/**
 * @brief C64 Phi2 clock standard selection for SID POT timing calculation.
 */
typedef enum {
    UNIPAD_TV_AUTO = 0, /**< Auto-detect PAL (985.248 kHz) vs. NTSC (1.022727 MHz) from SID sync */
    UNIPAD_TV_PAL = 1,  /**< Force PAL C64 Phi2 = 985,248 Hz (1.01497 us / cycle) */
    UNIPAD_TV_NTSC = 2, /**< Force NTSC C64 Phi2 = 1,022,727 Hz (0.97778 us / cycle) */
} unipad_tv_standard_t;

/**
 * @brief Diagnostic snapshot of the PIO SID subsystem for `status` / `version` CLI output.
 */
typedef struct {
    bool initialized;
    uint8_t pot_pio_index;
    uint8_t sync_pio_index;
    unipad_tv_standard_t tv_setting;
    bool tv_auto_locked;
    uint32_t active_phi2_hz;
    uint32_t last_sync_low_us[UNIPAD_PORT_COUNT];
    unipad_pot_hw_mode_t port_mode[UNIPAD_PORT_COUNT];
    uint8_t mouse_pos_x[UNIPAD_PORT_COUNT];
    uint8_t mouse_pos_y[UNIPAD_PORT_COUNT];
    uint8_t paddle_x[UNIPAD_PORT_COUNT];
    uint8_t paddle_y[UNIPAD_PORT_COUNT];
} unipad_pio_status_t;

/**
 * @brief Claims PIO state machines, loads `c64_sid_pot` and `c64_sid_sync_measure`,
 *        and performs a non-intrusive boot probe for PAL/NTSC SID clock auto-calibration.
 *
 * @return True if all 6 PIO state machines (4 POT + 2 SYNC) were claimed and initialized.
 */
bool unipad_c64_pio_init(void);

/**
 * @brief Configures the hardware POT operating mode for a specific C64 Control Port.
 *
 * Cleanly transitions `POTX`/`POTY` between RP2350 SIO digital output and PIO hardware
 * waveform generation without glitching the C64 SID bus.
 *
 * @param port Target C64 Control Port (`UNIPAD_PORT_1` or `UNIPAD_PORT_2`).
 * @param mode Desired POT hardware mode.
 */
void unipad_c64_pio_set_port_mode(unipad_port_t port, unipad_pot_hw_mode_t mode);

/**
 * @brief Returns the active hardware POT mode for a C64 Control Port.
 */
unipad_pot_hw_mode_t unipad_c64_pio_get_port_mode(unipad_port_t port);

/**
 * @brief Updates the digital 2nd (`POTX`) and 3rd (`POTY`) button states in `UNIPAD_POT_HW_BUTTONS` mode.
 *
 * Follows exact C64GS / Cheetah Annihilator / Unijoysticle 2 C64 polarity:
 *   - `pressed == false` -> RP2350 GPIO HIGH -> 74HCT05 clamps POT LOW -> SID reads `$FF`
 *   - `pressed == true`  -> RP2350 GPIO LOW  -> 74HCT05 Hi-Z, 10k pull-up to +5V -> SID reads `~$03`
 *
 * Ignored safely if `port` is currently in Rumble, 1351 Mouse, or Paddle mode.
 *
 * @param port           Target C64 Control Port.
 * @param button2_potx   True if Button 2 (POTX, DB9 Pin 9) is pressed.
 * @param button3_poty   True if Button 3 (POTY, DB9 Pin 5) is pressed.
 */
void unipad_c64_pio_set_buttons(unipad_port_t port, bool button2_potx, bool button3_poty);

/**
 * @brief Applies relative X/Y motion deltas to the Commodore 1351 Proportional Mouse on `port`.
 *
 * Integrates scaled HID movement modulo 64 into SID bits `6..1` (`64 + (pos6 << 1)`,
 * spanning `[64..190]` Phi2 cycles) with sub-pixel fractional accumulation and
 * verified Commodore 1351 `MoveCheck` axis polarity (`+dx` -> `+POTX`, `+dy` down -> `-POTY`).
 *
 * @param port    Target C64 Control Port (must be in `UNIPAD_POT_HW_1351_MOUSE` mode).
 * @param delta_x Raw HID X displacement (+ = right, - = left).
 * @param delta_y Raw HID Y displacement (+ = down,  - = up).
 * @param scale   Sensitivity multiplier (e.g. `1.0f` from `bp.mouse.scale`).
 */
void unipad_c64_pio_update_1351_mouse(unipad_port_t port,
                                      int32_t delta_x,
                                      int32_t delta_y,
                                      float scale);

/**
 * @brief Updates the 8-bit Commodore 1311/1312 Paddle X (`$D419`) and Paddle Y (`$D41A`) values on `port`.
 *
 * @param port     Target C64 Control Port (must be in `UNIPAD_POT_HW_PADDLE` mode).
 * @param paddle_x Desired `$D419` reading (`0..255`).
 * @param paddle_y Desired `$D41A` reading (`0..255`, internally clamped to `238` to preserve sync recovery).
 */
void unipad_c64_pio_set_paddles(unipad_port_t port, uint8_t paddle_x, uint8_t paddle_y);

/**
 * @brief Polls the zero-interrupt PIO sync measurement FIFOs for C64 Rumble activity and PAL/NTSC calibration.
 *
 * Should be called periodically (~15 ms) from a BTstack run-loop timer.
 *
 * @param out_rumble_port1 Set to true if valid C64 SID discharge pulses were observed on Port 1 (when in Rumble mode).
 * @param out_rumble_port2 Set to true if valid C64 SID discharge pulses were observed on Port 2 (when in Rumble mode).
 */
void unipad_c64_pio_poll_sync(bool* out_rumble_port1, bool* out_rumble_port2);

/**
 * @brief Configures the C64 Phi2 TV clock standard (`AUTO`, `PAL`, or `NTSC`) and refreshes active PIO timings.
 */
void unipad_c64_pio_set_tv_standard(unipad_tv_standard_t standard);

/**
 * @brief Populates a diagnostic snapshot of the PIO subsystem state.
 */
void unipad_c64_pio_get_status(unipad_pio_status_t* out_status);

#ifdef __cplusplus
}
#endif

#endif  // UNIPAD_C64_PIO_H
