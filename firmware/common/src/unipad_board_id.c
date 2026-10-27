// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Boot-time RP2350 `BOARD_ID[2:0]` (`GP26..GP28`) strap detector and board
// metadata registry shared across all Unipad firmware targets.

#include "unipad_board_id.h"

#include <stdio.h>

#include <hardware/gpio.h>
#include <pico/time.h>

// Internal ~50..80 kOhm pull-up RC settling delay against ~20 pF pad + jumper
// parasitic capacitance (tau ~= 1.6 us -> 25 us provides > 15 time constants).
#define BOARD_ID_PULLUP_SETTLE_US   25u

// Inter-sample delay for 3-sample majority vote deglitching at power-up.
#define BOARD_ID_SAMPLE_GAP_US      1u
#define BOARD_ID_VOTE_SAMPLES       3u
#define BOARD_ID_VOTE_THRESHOLD     2u

static const uint s_strap_pins[UNIPAD_BOARD_ID_PIN_COUNT] = {
    UNIPAD_PIN_BOARD_ID0,  // Bit 0 (LSB): GP26 / Pin 31 / JP1
    UNIPAD_PIN_BOARD_ID1,  // Bit 1:       GP27 / Pin 32 / JP2
    UNIPAD_PIN_BOARD_ID2,  // Bit 2 (MSB): GP28 / Pin 34 / JP3
};

static const unipad_board_info_t s_board_registry[UNIPAD_BOARD_ID_MAX_COUNT] = {
    [UNIPAD_BOARD_RESERVED_0] = {
        .id = UNIPAD_BOARD_RESERVED_0,
        .slug = "reserved_000",
        .model_name = "Unipad Reserved (0b000)",
        .target_hosts = "Reserved / Extended ADC Escape",
        .port_count = 0u,
        .is_allocated = false,
    },
    [UNIPAD_BOARD_RESERVED_1] = {
        .id = UNIPAD_BOARD_RESERVED_1,
        .slug = "reserved_001",
        .model_name = "Unipad Reserved (0b001)",
        .target_hosts = "Reserved Future Retro Platform",
        .port_count = 0u,
        .is_allocated = false,
    },
    [UNIPAD_BOARD_SINGLE_PORT] = {
        .id = UNIPAD_BOARD_SINGLE_PORT,
        .slug = "unipad_singleport",
        .model_name = "Unipad Single Port",
        .target_hosts = "Single-Port DB9 (Amiga / Atari / C64)",
        .port_count = 1u,
        .is_allocated = true,
    },
    [UNIPAD_BOARD_MSX] = {
        .id = UNIPAD_BOARD_MSX,
        .slug = "unipad_msx",
        .model_name = "Unipad MSX",
        .target_hosts = "MSX / MSX2 / MSX2+",
        .port_count = 2u,
        .is_allocated = true,
    },
    [UNIPAD_BOARD_ATARIST] = {
        .id = UNIPAD_BOARD_ATARIST,
        .slug = "unipad_atarist",
        .model_name = "Unipad Atari ST",
        .target_hosts = "Atari ST / STE / Falcon",
        .port_count = 2u,
        .is_allocated = true,
    },
    [UNIPAD_BOARD_800XL] = {
        .id = UNIPAD_BOARD_800XL,
        .slug = "unipad_800xl",
        .model_name = "Unipad 800XL",
        .target_hosts = "Atari 800XL / 65XE / 130XE / 2600 / 7800",
        .port_count = 2u,
        .is_allocated = true,
    },
    [UNIPAD_BOARD_A500] = {
        .id = UNIPAD_BOARD_A500,
        .slug = "unipad_a500",
        .model_name = "Unipad A500",
        .target_hosts = "Commodore Amiga 500 / 1200 / 2000",
        .port_count = 2u,
        .is_allocated = true,
    },
    [UNIPAD_BOARD_C64] = {
        .id = UNIPAD_BOARD_C64,
        .slug = "unipad_c64",
        .model_name = "Unipad C64",
        .target_hosts = "Commodore 64 / 128",
        .port_count = 2u,
        .is_allocated = true,
    },
};

static const unipad_board_info_t s_unknown_board_fallback = {
    .id = UNIPAD_BOARD_RESERVED_0,
    .slug = "unknown",
    .model_name = "Unipad Unknown",
    .target_hosts = "Unknown",
    .port_count = 0u,
    .is_allocated = false,
};

static bool s_detected = false;
static uint8_t s_cached_raw_mask = (uint8_t)UNIPAD_BOARD_C64;
static unipad_board_id_t s_cached_board_id = UNIPAD_BOARD_C64;

/**
 * @brief Arms internal pull-ups and Schmitt trigger hysteresis on all 3 strap pins.
 */
static void arm_strap_inputs_with_pullups(void) {
    for (uint i = 0u; i < UNIPAD_BOARD_ID_PIN_COUNT; i++) {
        const uint pin = s_strap_pins[i];
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        gpio_set_input_hysteresis_enabled(pin, true);
    }
}

/**
 * @brief Disables internal pulls (`PUE=0, PDE=0`) and digital input buffers (`IE=0`)
 *        so grounded straps consume `0.0 uA` and floating pads cannot oscillate.
 */
static void isolate_strap_pads_post_boot(void) {
    for (uint i = 0u; i < UNIPAD_BOARD_ID_PIN_COUNT; i++) {
        const uint pin = s_strap_pins[i];
        gpio_disable_pulls(pin);
        gpio_set_input_enabled(pin, false);
    }
}

/**
 * @brief Samples the 3 strap pins using a 3-read majority vote per bit.
 */
static uint8_t sample_deglitched_strap_mask(void) {
    uint8_t high_votes[UNIPAD_BOARD_ID_PIN_COUNT] = {0u, 0u, 0u};

    for (uint sample = 0u; sample < BOARD_ID_VOTE_SAMPLES; sample++) {
        if (sample > 0u) {
            busy_wait_us_32(BOARD_ID_SAMPLE_GAP_US);
        }
        for (uint bit = 0u; bit < UNIPAD_BOARD_ID_PIN_COUNT; bit++) {
            if (gpio_get(s_strap_pins[bit])) {
                high_votes[bit]++;
            }
        }
    }

    uint8_t mask = 0u;
    for (uint bit = 0u; bit < UNIPAD_BOARD_ID_PIN_COUNT; bit++) {
        if (high_votes[bit] >= BOARD_ID_VOTE_THRESHOLD) {
            mask |= (uint8_t)(1u << bit);
        }
    }
    return mask;
}

unipad_board_id_t unipad_board_id_detect(void) {
    if (s_detected) {
        return s_cached_board_id;
    }

    arm_strap_inputs_with_pullups();
    busy_wait_us_32(BOARD_ID_PULLUP_SETTLE_US);

    const uint8_t mask = sample_deglitched_strap_mask();
    isolate_strap_pads_post_boot();

    s_cached_raw_mask = mask;
    s_cached_board_id = (unipad_board_id_t)mask;
    s_detected = true;

    return s_cached_board_id;
}

unipad_board_id_t unipad_board_id_get(void) {
    if (!s_detected) {
        return unipad_board_id_detect();
    }
    return s_cached_board_id;
}

uint8_t unipad_board_id_get_raw_mask(void) {
    if (!s_detected) {
        (void)unipad_board_id_detect();
    }
    return s_cached_raw_mask;
}

const unipad_board_info_t* unipad_board_id_get_info(unipad_board_id_t id) {
    const uint32_t idx = (uint32_t)id;
    if (idx >= UNIPAD_BOARD_ID_MAX_COUNT) {
        return &s_unknown_board_fallback;
    }
    return &s_board_registry[idx];
}

const char* unipad_board_id_name(unipad_board_id_t id) {
    return unipad_board_id_get_info(id)->model_name;
}

bool unipad_board_id_matches(unipad_board_id_t expected_id) {
    return unipad_board_id_get() == expected_id;
}

const char* unipad_board_id_format_straps(uint8_t raw_mask, char* out_buf, size_t buf_size) {
    if (out_buf == NULL || buf_size == 0u) {
        return "";
    }

    const unsigned b2 = (raw_mask >> 2u) & 1u;
    const unsigned b1 = (raw_mask >> 1u) & 1u;
    const unsigned b0 = (raw_mask >> 0u) & 1u;

    (void)snprintf(out_buf, buf_size, "0b%u%u%u (GP28=%u, GP27=%u, GP26=%u)",
                   b2, b1, b0, b2, b1, b0);
    return out_buf;
}
