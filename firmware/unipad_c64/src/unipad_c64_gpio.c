// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Atomic C64 Control Port digital GPIO, status LED, autofire, and debounced
// pushbutton hardware abstraction layer for Unipad C64 (Rev. A).

#include "unipad_c64_gpio.h"

#include <btstack_run_loop.h>
#include <hardware/gpio.h>
#include <pico/cyw43_arch.h>
#include <pico/time.h>

#include "unipad_c64_pins.h"

// Timing constants
#define BUTTON_POLL_INTERVAL_MS     15u
#define BUTTON_STABLE_SAMPLES       3u       // 3 * 15ms = 45ms integrating debounce
#define BUTTON_COOLDOWN_US          250000ULL
#define LED_BLINK_HALF_PERIOD_MS    100u
#define AUTOFIRE_CPS_MIN            1u
#define AUTOFIRE_CPS_MAX            62u

// Contiguous 5-pin SIO masks for Port 1 (GP2..GP6) and Port 2 (GP9..GP13)
#define PORT1_DIGITAL_MASK          (0x1Fu << UNIPAD_PIN_J1_BASE)
#define PORT2_DIGITAL_MASK          (0x1Fu << UNIPAD_PIN_J2_BASE)

typedef struct {
    unipad_digital_joy_t desired;
} port_digital_state_t;

typedef struct {
    uint gpio;
    uint8_t low_streak;
    bool latched_pressed;
    uint64_t last_trigger_us;
    unipad_button_callback_t callback;
} button_debouncer_t;

static port_digital_state_t s_ports[UNIPAD_PORT_COUNT];

// Autofire state
static btstack_timer_source_t s_autofire_timer;
static bool s_autofire_timer_active = false;
static bool s_autofire_phase = false;
static uint8_t s_autofire_cps = 7u;

// Bluetooth LED blink state
static btstack_timer_source_t s_led_blink_timer;
static bool s_led_blink_timer_active = false;
static int s_led_blink_edges_remaining = 0;
static bool s_bt_led_steady = false;

// Pushbutton polling debouncer state
static btstack_timer_source_t s_button_timer;
static button_debouncer_t s_buttons[2];

// ---------------------------------------------------------------------------
// Internal Helpers
// ---------------------------------------------------------------------------

static void drive_bt_led_hardware(bool on) {
    gpio_put(UNIPAD_PIN_LED_BT, on);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

static uint32_t compose_port_bits(const unipad_digital_joy_t* joy, uint base_pin) {
    const bool effective_fire = joy->auto_fire ? s_autofire_phase : joy->fire;
    uint32_t bits = 0u;

    if (joy->up) {
        bits |= (1u << 0);
    }
    if (joy->down) {
        bits |= (1u << 1);
    }
    if (joy->left) {
        bits |= (1u << 2);
    }
    if (joy->right) {
        bits |= (1u << 3);
    }
    if (effective_fire) {
        bits |= (1u << 4);
    }
    return bits << base_pin;
}

static void apply_digital_port_atomic(unipad_port_t port) {
    if (port == UNIPAD_PORT_1) {
        const uint32_t val = compose_port_bits(&s_ports[UNIPAD_PORT_1].desired, UNIPAD_PIN_J1_BASE);
        gpio_put_masked(PORT1_DIGITAL_MASK, val);
        return;
    }

    if (port == UNIPAD_PORT_2) {
        const uint32_t val = compose_port_bits(&s_ports[UNIPAD_PORT_2].desired, UNIPAD_PIN_J2_BASE);
        gpio_put_masked(PORT2_DIGITAL_MASK, val);
    }
}

static uint32_t compute_autofire_half_period_ms(uint8_t cps) {
    const uint32_t clamped = (cps < AUTOFIRE_CPS_MIN) ? AUTOFIRE_CPS_MIN
                           : (cps > AUTOFIRE_CPS_MAX) ? AUTOFIRE_CPS_MAX
                                                      : cps;
    const uint32_t half_ms = 1000u / (clamped * 2u);
    return (half_ms > 0u) ? half_ms : 1u;
}

static void autofire_timer_handler(btstack_timer_source_t* ts) {
    const bool any_active =
        s_ports[UNIPAD_PORT_1].desired.auto_fire || s_ports[UNIPAD_PORT_2].desired.auto_fire;

    if (!any_active) {
        s_autofire_timer_active = false;
        s_autofire_phase = false;
        apply_digital_port_atomic(UNIPAD_PORT_1);
        apply_digital_port_atomic(UNIPAD_PORT_2);
        return;
    }

    s_autofire_phase = !s_autofire_phase;
    if (s_ports[UNIPAD_PORT_1].desired.auto_fire) {
        apply_digital_port_atomic(UNIPAD_PORT_1);
    }
    if (s_ports[UNIPAD_PORT_2].desired.auto_fire) {
        apply_digital_port_atomic(UNIPAD_PORT_2);
    }

    btstack_run_loop_set_timer(ts, compute_autofire_half_period_ms(s_autofire_cps));
    btstack_run_loop_add_timer(ts);
}

static void ensure_autofire_timer_running(void) {
    const bool any_active =
        s_ports[UNIPAD_PORT_1].desired.auto_fire || s_ports[UNIPAD_PORT_2].desired.auto_fire;

    if (!any_active || s_autofire_timer_active) {
        return;
    }

    s_autofire_timer_active = true;
    s_autofire_phase = true;
    apply_digital_port_atomic(UNIPAD_PORT_1);
    apply_digital_port_atomic(UNIPAD_PORT_2);

    btstack_run_loop_set_timer_handler(&s_autofire_timer, autofire_timer_handler);
    btstack_run_loop_set_timer(&s_autofire_timer, compute_autofire_half_period_ms(s_autofire_cps));
    btstack_run_loop_add_timer(&s_autofire_timer);
}

static void led_blink_timer_handler(btstack_timer_source_t* ts) {
    if (s_led_blink_edges_remaining <= 0) {
        s_led_blink_timer_active = false;
        drive_bt_led_hardware(s_bt_led_steady);
        return;
    }

    // Even remaining count -> OFF half-cycle; Odd remaining count -> ON half-cycle.
    const bool drive_on = ((s_led_blink_edges_remaining & 1) != 0);
    drive_bt_led_hardware(drive_on);
    s_led_blink_edges_remaining--;

    if (s_led_blink_edges_remaining == 0) {
        s_led_blink_timer_active = false;
        drive_bt_led_hardware(s_bt_led_steady);
        return;
    }

    btstack_run_loop_set_timer(ts, LED_BLINK_HALF_PERIOD_MS);
    btstack_run_loop_add_timer(ts);
}

static void poll_single_button(button_debouncer_t* btn, uint64_t now_us) {
    const bool raw_pressed = !gpio_get(btn->gpio);

    if (!raw_pressed) {
        btn->low_streak = 0u;
        btn->latched_pressed = false;
        return;
    }

    if (btn->low_streak < BUTTON_STABLE_SAMPLES) {
        btn->low_streak++;
    }

    if (btn->low_streak < BUTTON_STABLE_SAMPLES || btn->latched_pressed) {
        return;
    }

    btn->latched_pressed = true;
    if ((now_us - btn->last_trigger_us) < BUTTON_COOLDOWN_US) {
        return;
    }

    btn->last_trigger_us = now_us;
    if (btn->callback != NULL) {
        btn->callback();
    }
}

static void button_poll_timer_handler(btstack_timer_source_t* ts) {
    const uint64_t now_us = time_us_64();
    poll_single_button(&s_buttons[0], now_us);
    poll_single_button(&s_buttons[1], now_us);

    btstack_run_loop_set_timer(ts, BUTTON_POLL_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

static void init_output_pin_low(uint pin) {
    gpio_init(pin);
    gpio_put(pin, false);
    gpio_set_dir(pin, GPIO_OUT);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void unipad_c64_gpio_init_early(void) {
    // 1. Initialize all 10 digital joystick lines (GP2..GP6, GP9..GP13) to LOW (74HCT05 Hi-Z).
    for (uint pin = UNIPAD_PIN_J1_BASE; pin < (UNIPAD_PIN_J1_BASE + UNIPAD_J1_PIN_COUNT); pin++) {
        init_output_pin_low(pin);
    }
    for (uint pin = UNIPAD_PIN_J2_BASE; pin < (UNIPAD_PIN_J2_BASE + UNIPAD_J2_PIN_COUNT); pin++) {
        init_output_pin_low(pin);
    }

    // 2. Initialize all 4 POT lines (GP7, GP8, GP14, GP15) to LOW (74HCT05 Hi-Z) during boot
    //    so GP16/GP17 can observe unperturbed C64 SID discharge pulses for PAL/NTSC calibration.
    init_output_pin_low(UNIPAD_PIN_J1_POTX);
    init_output_pin_low(UNIPAD_PIN_J1_POTY);
    init_output_pin_low(UNIPAD_PIN_J2_POTX);
    init_output_pin_low(UNIPAD_PIN_J2_POTY);

    // 3. Configure 5V fail-safe SID sync inputs (GP16, GP17) WITHOUT internal pulls
    //    so the external 10k/22k voltage dividers maintain exact 2.62V / <=0.45V thresholds.
    gpio_init(UNIPAD_PIN_SYNC_J1);
    gpio_set_dir(UNIPAD_PIN_SYNC_J1, GPIO_IN);
    gpio_disable_pulls(UNIPAD_PIN_SYNC_J1);
    gpio_set_input_hysteresis_enabled(UNIPAD_PIN_SYNC_J1, true);

    gpio_init(UNIPAD_PIN_SYNC_J2);
    gpio_set_dir(UNIPAD_PIN_SYNC_J2, GPIO_IN);
    gpio_disable_pulls(UNIPAD_PIN_SYNC_J2);
    gpio_set_input_hysteresis_enabled(UNIPAD_PIN_SYNC_J2, true);

    // 4. Configure Status LEDs (GP18=BT Blue, GP19=Port 1 Green, GP20=Port 2 Red).
    //    Illuminate Port 1 + Port 2 LEDs during boot initialization (matches Unijoysticle 2 UX).
    init_output_pin_low(UNIPAD_PIN_LED_BT);
    init_output_pin_low(UNIPAD_PIN_LED_PORT1);
    init_output_pin_low(UNIPAD_PIN_LED_PORT2);
    gpio_put(UNIPAD_PIN_LED_PORT1, true);
    gpio_put(UNIPAD_PIN_LED_PORT2, true);

    // 5. Configure active-LOW tactile pushbuttons (GP21=MODE, GP22=SWAP).
    gpio_init(UNIPAD_PIN_SWITCH_MODE);
    gpio_set_dir(UNIPAD_PIN_SWITCH_MODE, GPIO_IN);
    gpio_pull_up(UNIPAD_PIN_SWITCH_MODE);
    gpio_set_input_hysteresis_enabled(UNIPAD_PIN_SWITCH_MODE, true);

    gpio_init(UNIPAD_PIN_SWITCH_SWAP);
    gpio_set_dir(UNIPAD_PIN_SWITCH_SWAP, GPIO_IN);
    gpio_pull_up(UNIPAD_PIN_SWITCH_SWAP);
    gpio_set_input_hysteresis_enabled(UNIPAD_PIN_SWITCH_SWAP, true);
}

void unipad_c64_gpio_start_timers(unipad_button_callback_t on_mode_pressed,
                                  unipad_button_callback_t on_swap_pressed,
                                  uint8_t autofire_cps) {
    unipad_c64_gpio_set_autofire_cps(autofire_cps);

    // Seed `latched_pressed` if the user is still holding MODE from boot key-reset
    // so releasing or holding through boot does not accidentally cycle gamepad mode.
    const bool mode_held_at_start = !gpio_get(UNIPAD_PIN_SWITCH_MODE);
    const bool swap_held_at_start = !gpio_get(UNIPAD_PIN_SWITCH_SWAP);

    s_buttons[0] = (button_debouncer_t){
        .gpio = UNIPAD_PIN_SWITCH_MODE,
        .low_streak = mode_held_at_start ? BUTTON_STABLE_SAMPLES : 0u,
        .latched_pressed = mode_held_at_start,
        .last_trigger_us = 0u,
        .callback = on_mode_pressed,
    };
    s_buttons[1] = (button_debouncer_t){
        .gpio = UNIPAD_PIN_SWITCH_SWAP,
        .low_streak = swap_held_at_start ? BUTTON_STABLE_SAMPLES : 0u,
        .latched_pressed = swap_held_at_start,
        .last_trigger_us = 0u,
        .callback = on_swap_pressed,
    };

    btstack_run_loop_set_timer_handler(&s_button_timer, button_poll_timer_handler);
    btstack_run_loop_set_timer(&s_button_timer, BUTTON_POLL_INTERVAL_MS);
    btstack_run_loop_add_timer(&s_button_timer);
}

void unipad_c64_gpio_set_digital_port(unipad_port_t port, const unipad_digital_joy_t* joy) {
    if (port >= UNIPAD_PORT_COUNT || joy == NULL) {
        return;
    }

    s_ports[port].desired = *joy;
    apply_digital_port_atomic(port);

    if (joy->auto_fire) {
        ensure_autofire_timer_running();
    }
}

void unipad_c64_gpio_clear_digital_port(unipad_port_t port) {
    if (port >= UNIPAD_PORT_COUNT) {
        return;
    }

    s_ports[port].desired = (unipad_digital_joy_t){0};
    apply_digital_port_atomic(port);
}

void unipad_c64_gpio_set_autofire_cps(uint8_t cps) {
    if (cps < AUTOFIRE_CPS_MIN) {
        s_autofire_cps = AUTOFIRE_CPS_MIN;
        return;
    }
    if (cps > AUTOFIRE_CPS_MAX) {
        s_autofire_cps = AUTOFIRE_CPS_MAX;
        return;
    }
    s_autofire_cps = cps;
}

uint8_t unipad_c64_gpio_get_autofire_cps(void) {
    return s_autofire_cps;
}

void unipad_c64_gpio_set_port_leds(bool port1_on, bool port2_on) {
    gpio_put(UNIPAD_PIN_LED_PORT1, port1_on);
    gpio_put(UNIPAD_PIN_LED_PORT2, port2_on);
}

void unipad_c64_gpio_set_bt_led(bool enabled) {
    s_bt_led_steady = enabled;
    if (!s_led_blink_timer_active) {
        drive_bt_led_hardware(enabled);
    }
}

void unipad_c64_gpio_blink_bt_led(int times) {
    if (times <= 0) {
        return;
    }
    if (times > 10) {
        times = 10;
    }

    if (s_led_blink_timer_active) {
        btstack_run_loop_remove_timer(&s_led_blink_timer);
    }

    // Each blink has 2 edges (OFF -> ON), plus 1 final edge to restore steady state.
    s_led_blink_edges_remaining = (times * 2) + 1;
    s_led_blink_timer_active = true;

    // Immediately drive first edge (OFF) and schedule remaining edges.
    drive_bt_led_hardware(false);
    s_led_blink_edges_remaining--;

    btstack_run_loop_set_timer_handler(&s_led_blink_timer, led_blink_timer_handler);
    btstack_run_loop_set_timer(&s_led_blink_timer, LED_BLINK_HALF_PERIOD_MS);
    btstack_run_loop_add_timer(&s_led_blink_timer);
}

bool unipad_c64_gpio_is_mode_pressed_raw(void) {
    return !gpio_get(UNIPAD_PIN_SWITCH_MODE);
}
