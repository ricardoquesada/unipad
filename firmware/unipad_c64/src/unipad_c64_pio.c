// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// RP2350 PIO-accelerated Commodore 64 SID Potentiometer & Sync subsystem.

#include "unipad_c64_pio.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <pico/time.h>
#include <uni.h>

#include "c64_sid_pot.pio.h"
#include "unipad_c64_pins.h"

// Exact C64 Phi2 clock frequencies
#define C64_PHI2_PAL_HZ             985248u
#define C64_PHI2_NTSC_HZ            1022727u

// Expected LOW duration on GP16/GP17 during an un-extended 256-Phi2 SID discharge:
//   - PAL : 256 / 985,248 Hz  = 259.83 us + ~3.8 us RC rise to V_IH = ~263.6 us
//   - NTSC: 256 / 1,022,727 Hz = 250.31 us + ~3.8 us RC rise to V_IH = ~254.1 us
//   - Midpoint discriminator: 258.8 us (258,800 ns)
//   - Valid noise-rejection window for genuine SID discharge pulses: [180 us .. 330 us]
#define SID_SYNC_VALID_MIN_NS       180000ULL
#define SID_SYNC_VALID_MAX_NS       330000ULL
#define SID_SYNC_CALIB_MIN_NS       240000ULL
#define SID_SYNC_CALIB_MAX_NS       280000ULL
#define SID_SYNC_PAL_THRESHOLD_NS   258800ULL

// RC charge time (~3.5 us = 3500 ns) from 74HCT05 Hi-Z release (0.1V) across 10k pull-up
// into the C64 SID 470pF integration capacitor up to the ~2.4V SID threshold.
#define SID_POT_RC_RISE_NS          3500ULL
#define SID_POT_PIO_OVERHEAD_CYCLES 8u

// Maximum SID counts in Paddle mode:
//   - POTX (GP7/GP14) does not share the POTY sync divider, safe up to 246.
//   - POTY (GP8/GP15) drives the same POTY line sensed by GP16/GP17; capping at 238
//     leaves 18 Phi2 cycles (~18 us > 4.5 * tau_Y) for POTY to recharge above 2.62V
//     before Cycle 512 so `wait 1 pin 0` -> `wait 0 pin 0` never misses the next sync edge.
#define PADDLE_MAX_SID_X            246u
#define PADDLE_MAX_SID_Y            238u

// Maximum per-packet 1351 delta step (+/- 31 counts modulo 64 avoids 180-degree aliasing
// in C64 MoveCheck routines if a high-DPI mouse moves violently within a single 520us frame).
#define MOUSE_1351_MAX_STEP         24

typedef struct {
    uint potx_pin;
    uint poty_pin;
    uint sync_pin;
    uint sm_potx;
    uint sm_poty;
    uint sm_sync;

    unipad_pot_hw_mode_t mode;

    // Cached button states (UNIPAD_POT_HW_BUTTONS)
    bool btn2_pressed;
    bool btn3_pressed;

    // Commodore 1351 6-bit positions (0..63) & sub-count fractional accumulators
    uint8_t mouse_x6;
    uint8_t mouse_y6;
    float mouse_rem_x;
    float mouse_rem_y;

    // Commodore 1311/1312 8-bit paddle positions
    uint8_t paddle_x;
    uint8_t paddle_y;

    // Diagnostic last measured sync pulse width in microseconds
    uint32_t last_sync_low_us;
} port_pio_channel_t;

static bool s_initialized = false;
static PIO s_pot_pio = NULL;
static uint s_pot_offset = 0u;
static PIO s_sync_pio = NULL;
static uint s_sync_offset = 0u;

static unipad_tv_standard_t s_tv_setting = UNIPAD_TV_AUTO;
static bool s_tv_auto_locked = false;
static uint32_t s_active_phi2_hz = C64_PHI2_PAL_HZ;

static port_pio_channel_t s_channels[UNIPAD_PORT_COUNT] = {
    [UNIPAD_PORT_1] = {
        .potx_pin = UNIPAD_PIN_J1_POTX,
        .poty_pin = UNIPAD_PIN_J1_POTY,
        .sync_pin = UNIPAD_PIN_SYNC_J1,
        .mode = UNIPAD_POT_HW_BUTTONS,
        .mouse_x6 = 32u,
        .mouse_y6 = 32u,
        .paddle_x = 128u,
        .paddle_y = 128u,
    },
    [UNIPAD_PORT_2] = {
        .potx_pin = UNIPAD_PIN_J2_POTX,
        .poty_pin = UNIPAD_PIN_J2_POTY,
        .sync_pin = UNIPAD_PIN_SYNC_J2,
        .mode = UNIPAD_POT_HW_BUTTONS,
        .mouse_x6 = 32u,
        .mouse_y6 = 32u,
        .paddle_x = 128u,
        .paddle_y = 128u,
    },
};

// ---------------------------------------------------------------------------
// Internal Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Claims `needed_sms` unused state machines on a single PIO block that can fit `program`.
 */
static bool claim_pio_group(const pio_program_t* program,
                            uint needed_sms,
                            PIO* out_pio,
                            uint* out_offset,
                            uint* out_sms) {
    for (uint pio_idx = 0u; pio_idx < NUM_PIOS; pio_idx++) {
        PIO candidate = pio_get_instance(pio_idx);
        if (!pio_can_add_program(candidate, program)) {
            continue;
        }

        uint claimed = 0u;
        for (uint i = 0u; i < needed_sms; i++) {
            int sm = pio_claim_unused_sm(candidate, false);
            if (sm < 0) {
                break;
            }
            out_sms[claimed++] = (uint)sm;
        }

        if (claimed == needed_sms) {
            int offset = pio_add_program(candidate, program);
            if (offset >= 0) {
                *out_pio = candidate;
                *out_offset = (uint)offset;
                return true;
            }
        }

        // Roll back partial SM claims on this candidate block before trying the next PIO block.
        for (uint i = 0u; i < claimed; i++) {
            pio_sm_unclaim(candidate, out_sms[i]);
        }
    }
    return false;
}

/**
 * @brief Converts a desired 8-bit SID `$D419`/`$D41A` reading (`0..246`) into an exact
 *        RP2350 `clk_sys` PIO loop iteration count.
 */
static uint32_t sid_value_to_pio_cycles(uint8_t sid_value) {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint64_t total_phi2 = 256ULL + (uint64_t)sid_value;
    const uint64_t target_cycles = (total_phi2 * (uint64_t)sys_hz) / (uint64_t)s_active_phi2_hz;
    const uint64_t rc_rise_cycles = (SID_POT_RC_RISE_NS * (uint64_t)sys_hz) / 1000000000ULL;
    const uint64_t subtract = rc_rise_cycles + SID_POT_PIO_OVERHEAD_CYCLES;

    if (target_cycles <= subtract) {
        return 1u;
    }
    return (uint32_t)(target_cycles - subtract);
}

/**
 * @brief Flushes any unconsumed stale target in the SM's TX FIFO and enqueues the newest cycle count.
 *
 * Ensures zero queue lag even if the C64 4066 analog mux is temporarily pointing at the other port.
 */
static void pio_push_latest_cycles(uint sm, uint32_t hold_cycles) {
    pio_sm_clear_fifos(s_pot_pio, sm);
    pio_sm_put(s_pot_pio, sm, hold_cycles);
}

static uint8_t encode_1351_sid_byte(uint8_t pos6) {
    // Commodore 1351 encodes the 6-bit modulo-64 position in bits 6..1 with a +64 (0x40) base
    // offset, keeping physical SID counts strictly inside [64 .. 190] Phi2 cycles.
    return (uint8_t)(64u + ((pos6 & 0x3Fu) << 1));
}

static void refresh_channel_pio_targets(unipad_port_t port) {
    port_pio_channel_t* ch = &s_channels[port];

    if (ch->mode == UNIPAD_POT_HW_1351_MOUSE) {
        pio_push_latest_cycles(ch->sm_potx, sid_value_to_pio_cycles(encode_1351_sid_byte(ch->mouse_x6)));
        pio_push_latest_cycles(ch->sm_poty, sid_value_to_pio_cycles(encode_1351_sid_byte(ch->mouse_y6)));
        return;
    }

    if (ch->mode == UNIPAD_POT_HW_PADDLE) {
        const uint8_t clamped_x = (ch->paddle_x > PADDLE_MAX_SID_X) ? PADDLE_MAX_SID_X : ch->paddle_x;
        const uint8_t clamped_y = (ch->paddle_y > PADDLE_MAX_SID_Y) ? PADDLE_MAX_SID_Y : ch->paddle_y;
        pio_push_latest_cycles(ch->sm_potx, sid_value_to_pio_cycles(clamped_x));
        pio_push_latest_cycles(ch->sm_poty, sid_value_to_pio_cycles(clamped_y));
    }
}

static void switch_pot_pins_to_sio(port_pio_channel_t* ch, bool drive_potx_high, bool drive_poty_high) {
    if (s_initialized) {
        pio_sm_set_enabled(s_pot_pio, ch->sm_potx, false);
        pio_sm_set_enabled(s_pot_pio, ch->sm_poty, false);
    }

    gpio_init(ch->potx_pin);
    gpio_put(ch->potx_pin, drive_potx_high);
    gpio_set_dir(ch->potx_pin, GPIO_OUT);

    gpio_init(ch->poty_pin);
    gpio_put(ch->poty_pin, drive_poty_high);
    gpio_set_dir(ch->poty_pin, GPIO_OUT);
}

static void switch_pot_pins_to_pio(unipad_port_t port) {
    if (!s_initialized) {
        return;
    }

    port_pio_channel_t* ch = &s_channels[port];
    c64_sid_pot_program_init(s_pot_pio, ch->sm_potx, s_pot_offset, ch->potx_pin, ch->sync_pin);
    c64_sid_pot_program_init(s_pot_pio, ch->sm_poty, s_pot_offset, ch->poty_pin, ch->sync_pin);

    // Seed initial delay words into TX FIFOs BEFORE enabling state machines so the very first
    // `pull noblock` loads OSR and persistent shadow register Y deterministically.
    refresh_channel_pio_targets(port);

    pio_sm_set_enabled(s_pot_pio, ch->sm_potx, true);
    pio_sm_set_enabled(s_pot_pio, ch->sm_poty, true);
}

/**
 * @brief Drains all pending pulse-width samples from one `c64_sid_sync_measure` RX FIFO.
 *
 * @param port                Port being inspected.
 * @param allow_calibration   True only when POTY is NOT being clamped/extended by our 74HCT05.
 * @return True if at least one valid C64 SID discharge pulse was detected.
 */
static bool drain_sync_fifo_for_port(unipad_port_t port, bool allow_calibration) {
    port_pio_channel_t* ch = &s_channels[port];
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    bool valid_pulse_seen = false;

    while (!pio_sm_is_rx_fifo_empty(s_sync_pio, ch->sm_sync)) {
        const uint32_t raw_iters = pio_sm_get(s_sync_pio, ch->sm_sync);
        // Each loop of `measure_low` takes 2 PIO clk_sys cycles (`jmp pin` + `jmp x--`).
        const uint64_t low_cycles = (uint64_t)raw_iters * 2ULL;
        const uint64_t low_ns = (low_cycles * 1000000000ULL) / (uint64_t)sys_hz;

        if (low_ns < SID_SYNC_VALID_MIN_NS || low_ns > SID_SYNC_VALID_MAX_NS) {
            continue;
        }

        valid_pulse_seen = true;
        ch->last_sync_low_us = (uint32_t)(low_ns / 1000ULL);

        if (!allow_calibration || s_tv_setting != UNIPAD_TV_AUTO || s_tv_auto_locked) {
            continue;
        }

        if (low_ns >= SID_SYNC_CALIB_MIN_NS && low_ns <= SID_SYNC_CALIB_MAX_NS) {
            const uint32_t detected_hz =
                (low_ns >= SID_SYNC_PAL_THRESHOLD_NS) ? C64_PHI2_PAL_HZ : C64_PHI2_NTSC_HZ;
            s_active_phi2_hz = detected_hz;
            s_tv_auto_locked = true;
            logi("unipad_pio: Auto-calibrated C64 SID clock on Port %u (%u us low) -> %s (%u Hz)\n",
                 (unsigned)(port + 1u), (unsigned)ch->last_sync_low_us,
                 (detected_hz == C64_PHI2_PAL_HZ) ? "PAL" : "NTSC", (unsigned)detected_hz);
        }
    }

    return valid_pulse_seen;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool unipad_c64_pio_init(void) {
    if (s_initialized) {
        return true;
    }

    uint pot_sms[4] = {0};
    if (!claim_pio_group(&c64_sid_pot_program, 4u, &s_pot_pio, &s_pot_offset, pot_sms)) {
        loge("unipad_pio: Failed to claim 4 PIO state machines for c64_sid_pot\n");
        return false;
    }

    uint sync_sms[2] = {0};
    if (!claim_pio_group(&c64_sid_sync_measure_program, 2u, &s_sync_pio, &s_sync_offset, sync_sms)) {
        loge("unipad_pio: Failed to claim 2 PIO state machines for c64_sid_sync_measure\n");
        for (uint i = 0u; i < 4u; i++) {
            pio_sm_unclaim(s_pot_pio, pot_sms[i]);
        }
        pio_remove_program(s_pot_pio, &c64_sid_pot_program, s_pot_offset);
        return false;
    }

    s_channels[UNIPAD_PORT_1].sm_potx = pot_sms[0];
    s_channels[UNIPAD_PORT_1].sm_poty = pot_sms[1];
    s_channels[UNIPAD_PORT_2].sm_potx = pot_sms[2];
    s_channels[UNIPAD_PORT_2].sm_poty = pot_sms[3];

    s_channels[UNIPAD_PORT_1].sm_sync = sync_sms[0];
    s_channels[UNIPAD_PORT_2].sm_sync = sync_sms[1];

    // Start the 2 zero-interrupt SID sync pulse measurement state machines.
    c64_sid_sync_measure_program_init(s_sync_pio, s_channels[UNIPAD_PORT_1].sm_sync, s_sync_offset,
                                      UNIPAD_PIN_SYNC_J1);
    c64_sid_sync_measure_program_init(s_sync_pio, s_channels[UNIPAD_PORT_2].sm_sync, s_sync_offset,
                                      UNIPAD_PIN_SYNC_J2);
    pio_sm_set_enabled(s_sync_pio, s_channels[UNIPAD_PORT_1].sm_sync, true);
    pio_sm_set_enabled(s_sync_pio, s_channels[UNIPAD_PORT_2].sm_sync, true);

    s_initialized = true;

    logi("unipad_pio: Claimed PIO%u (4 SMs: SID POT) + PIO%u (2 SMs: SID Sync/Rumble) @ clk_sys=%u MHz\n",
         (unsigned)PIO_NUM(s_pot_pio), (unsigned)PIO_NUM(s_sync_pio),
         (unsigned)(clock_get_hz(clk_sys) / 1000000u));

    // Brief 6ms (~11 SID cycles) non-intrusive boot probe while POT pins are still Hi-Z
    // to lock onto PAL vs. NTSC immediately if the C64 is already powered on.
    sleep_ms(6);
    drain_sync_fifo_for_port(UNIPAD_PORT_1, true);
    drain_sync_fifo_for_port(UNIPAD_PORT_2, true);

    // Place both ports into default 3-button unpressed state (74HCT05 clamping POT LOW -> SID $FF).
    unipad_c64_pio_set_port_mode(UNIPAD_PORT_1, UNIPAD_POT_HW_BUTTONS);
    unipad_c64_pio_set_port_mode(UNIPAD_PORT_2, UNIPAD_POT_HW_BUTTONS);

    return true;
}

void unipad_c64_pio_set_port_mode(unipad_port_t port, unipad_pot_hw_mode_t mode) {
    if (port >= UNIPAD_PORT_COUNT) {
        return;
    }

    port_pio_channel_t* ch = &s_channels[port];
    ch->mode = mode;

    switch (mode) {
        case UNIPAD_POT_HW_BUTTONS:
            // Unpressed POT button -> drive 74HCT05 HIGH (clamps POT LOW -> SID $FF).
            // Pressed POT button   -> drive 74HCT05 LOW  (Hi-Z -> 10k pull-up -> SID ~$03).
            switch_pot_pins_to_sio(ch, !ch->btn2_pressed, !ch->btn3_pressed);
            break;

        case UNIPAD_POT_HW_RUMBLE:
            // Release 74HCT05 to Hi-Z (GPIO LOW) so C64 SID discharge pulses reach GP16/GP17 freely.
            switch_pot_pins_to_sio(ch, false, false);
            if (s_initialized) {
                pio_sm_clear_fifos(s_sync_pio, ch->sm_sync);
            }
            break;

        case UNIPAD_POT_HW_1351_MOUSE:
        case UNIPAD_POT_HW_PADDLE:
            ch->mouse_rem_x = 0.0f;
            ch->mouse_rem_y = 0.0f;
            switch_pot_pins_to_pio(port);
            break;
    }
}

unipad_pot_hw_mode_t unipad_c64_pio_get_port_mode(unipad_port_t port) {
    if (port >= UNIPAD_PORT_COUNT) {
        return UNIPAD_POT_HW_BUTTONS;
    }
    return s_channels[port].mode;
}

void unipad_c64_pio_set_buttons(unipad_port_t port, bool button2_potx, bool button3_poty) {
    if (port >= UNIPAD_PORT_COUNT) {
        return;
    }

    port_pio_channel_t* ch = &s_channels[port];
    ch->btn2_pressed = button2_potx;
    ch->btn3_pressed = button3_poty;

    if (ch->mode != UNIPAD_POT_HW_BUTTONS) {
        return;
    }

    // Active-LOW at C64 SID input (unpressed = GPIO HIGH -> 74HCT05 clamp LOW -> $FF;
    // pressed = GPIO LOW -> 74HCT05 Hi-Z -> 10k pull-up charges cap -> ~$03).
    gpio_put(ch->potx_pin, !button2_potx);
    gpio_put(ch->poty_pin, !button3_poty);
}

void unipad_c64_pio_update_1351_mouse(unipad_port_t port,
                                      int32_t delta_x,
                                      int32_t delta_y,
                                      float scale) {
    if (!s_initialized || port >= UNIPAD_PORT_COUNT) {
        return;
    }

    port_pio_channel_t* ch = &s_channels[port];
    if (ch->mode != UNIPAD_POT_HW_1351_MOUSE) {
        return;
    }

    if (scale <= 0.05f) {
        scale = 1.0f;
    }

    const float scaled_x = ((float)delta_x * scale) + ch->mouse_rem_x;
    const float scaled_y = ((float)delta_y * scale) + ch->mouse_rem_y;

    int32_t step_x = (int32_t)scaled_x;
    int32_t step_y = (int32_t)scaled_y;

    ch->mouse_rem_x = scaled_x - (float)step_x;
    ch->mouse_rem_y = scaled_y - (float)step_y;

    if (step_x == 0 && step_y == 0) {
        return;
    }

    // Clamp single-update step to +/-24 modulo-64 counts to prevent MoveCheck sign aliasing.
    if (step_x > MOUSE_1351_MAX_STEP) {
        step_x = MOUSE_1351_MAX_STEP;
    } else if (step_x < -MOUSE_1351_MAX_STEP) {
        step_x = -MOUSE_1351_MAX_STEP;
    }

    if (step_y > MOUSE_1351_MAX_STEP) {
        step_y = MOUSE_1351_MAX_STEP;
    } else if (step_y < -MOUSE_1351_MAX_STEP) {
        step_y = -MOUSE_1351_MAX_STEP;
    }

    // Verified Commodore 1351 MoveCheck polarity:
    //   - Moving RIGHT (+step_x) INCREASES $D419 (POTX).
    //   - Moving DOWN  (+step_y) DECREASES $D41A (POTY); moving UP (-step_y) INCREASES $D41A.
    ch->mouse_x6 = (uint8_t)((int32_t)ch->mouse_x6 + step_x) & 0x3Fu;
    ch->mouse_y6 = (uint8_t)((int32_t)ch->mouse_y6 - step_y) & 0x3Fu;

    refresh_channel_pio_targets(port);
}

void unipad_c64_pio_set_paddles(unipad_port_t port, uint8_t paddle_x, uint8_t paddle_y) {
    if (!s_initialized || port >= UNIPAD_PORT_COUNT) {
        return;
    }

    port_pio_channel_t* ch = &s_channels[port];
    if (ch->paddle_x == paddle_x && ch->paddle_y == paddle_y && ch->mode != UNIPAD_POT_HW_PADDLE) {
        return;
    }

    ch->paddle_x = paddle_x;
    ch->paddle_y = paddle_y;

    if (ch->mode == UNIPAD_POT_HW_PADDLE) {
        refresh_channel_pio_targets(port);
    }
}

void unipad_c64_pio_poll_sync(bool* out_rumble_port1, bool* out_rumble_port2) {
    if (out_rumble_port1 != NULL) {
        *out_rumble_port1 = false;
    }
    if (out_rumble_port2 != NULL) {
        *out_rumble_port2 = false;
    }
    if (!s_initialized) {
        return;
    }

    // Only allow opportunistic PAL/NTSC calibration or rumble triggering when that port's
    // 74HCT05 POTY driver is Hi-Z (`UNIPAD_POT_HW_RUMBLE`), ensuring our own POTY clamp
    // never masquerades as a SID discharge pulse.
    const bool p1_rumble_mode = (s_channels[UNIPAD_PORT_1].mode == UNIPAD_POT_HW_RUMBLE);
    const bool p2_rumble_mode = (s_channels[UNIPAD_PORT_2].mode == UNIPAD_POT_HW_RUMBLE);

    const bool p1_pulsed = drain_sync_fifo_for_port(UNIPAD_PORT_1, p1_rumble_mode);
    const bool p2_pulsed = drain_sync_fifo_for_port(UNIPAD_PORT_2, p2_rumble_mode);

    if (p1_rumble_mode && p1_pulsed && out_rumble_port1 != NULL) {
        *out_rumble_port1 = true;
    }
    if (p2_rumble_mode && p2_pulsed && out_rumble_port2 != NULL) {
        *out_rumble_port2 = true;
    }
}

void unipad_c64_pio_set_tv_standard(unipad_tv_standard_t standard) {
    s_tv_setting = standard;
    if (standard == UNIPAD_TV_PAL) {
        s_active_phi2_hz = C64_PHI2_PAL_HZ;
    } else if (standard == UNIPAD_TV_NTSC) {
        s_active_phi2_hz = C64_PHI2_NTSC_HZ;
    } else {
        s_tv_auto_locked = false;
    }

    if (s_initialized) {
        refresh_channel_pio_targets(UNIPAD_PORT_1);
        refresh_channel_pio_targets(UNIPAD_PORT_2);
    }
}

void unipad_c64_pio_get_status(unipad_pio_status_t* out_status) {
    if (out_status == NULL) {
        return;
    }

    *out_status = (unipad_pio_status_t){
        .initialized = s_initialized,
        .pot_pio_index = s_initialized ? (uint8_t)PIO_NUM(s_pot_pio) : 0xFFu,
        .sync_pio_index = s_initialized ? (uint8_t)PIO_NUM(s_sync_pio) : 0xFFu,
        .tv_setting = s_tv_setting,
        .tv_auto_locked = s_tv_auto_locked,
        .active_phi2_hz = s_active_phi2_hz,
    };

    for (uint i = 0u; i < UNIPAD_PORT_COUNT; i++) {
        out_status->last_sync_low_us[i] = s_channels[i].last_sync_low_us;
        out_status->port_mode[i] = s_channels[i].mode;
        out_status->mouse_pos_x[i] = s_channels[i].mouse_x6;
        out_status->mouse_pos_y[i] = s_channels[i].mouse_y6;
        out_status->paddle_x[i] = s_channels[i].paddle_x;
        out_status->paddle_y[i] = s_channels[i].paddle_y;
    }
}
