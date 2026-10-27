// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Bluepad32 custom `uni_platform` implementation for Unipad C64 (Pico 2 W / RP2350).

#include "unipad_c64_platform.h"

#include <stddef.h>
#include <string.h>

#include <btstack_run_loop.h>
#include <hardware/clocks.h>
#include <pico/unique_id.h>
#include <uni.h>

#include "hid_usage.h"
#include "uni_common.h"
#include "uni_version.h"
#include "unipad_board_id.h"
#include "unipad_c64_console.h"
#include "unipad_c64_gpio.h"
#include "unipad_c64_pins.h"
#include "unipad_c64_pio.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Unipad C64 requires CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

#ifndef UNIPAD_FIRMWARE_VERSION
#define UNIPAD_FIRMWARE_VERSION "1.0.0"
#endif

// Unijoysticle / Unipad property keys (sorted to match uni_property_idx_t order)
#define UNI_PROPERTY_NAME_UNI_AUTOFIRE_CPS      "bp.uni.autofire"
#define UNI_PROPERTY_NAME_UNI_C64_POT_MODE      "bp.uni.c64pot"
#define UNI_PROPERTY_NAME_UNI_MODEL             "bp.uni.model"
#define UNI_PROPERTY_NAME_UNI_MOUSE_EMULATION   "bp.uni.mouseemu"
#define UNI_PROPERTY_NAME_UNI_SERIAL_NUMBER     "bp.uni.serial"
#define UNI_PROPERTY_NAME_UNI_VENDOR            "bp.uni.vendor"

// Default autofire presets (clicks per second):
//   7 CPS = Quickgun Turbo Pro (~4 PAL frames)
//  13 CPS = Zipstick (~2 PAL frames)
//  29 CPS = Quickshot 128F (~1 PAL frame)
//  62 CPS = Competition Pro (~1/2 PAL frame)
#define AUTOFIRE_CPS_DEFAULT                    7u

// Analog stick -> 1351 mouse continuous velocity divisor & deadzone
#define GAMEPAD_MOUSE_AXIS_DEADZONE             48
#define GAMEPAD_MOUSE_AXIS_DIVISOR              64
#define PERIODIC_TICK_INTERVAL_MS               16u  // ~62.5 Hz (1 C64 frame)

// Analog trigger override threshold in Paddle mode (0..1023)
#define PADDLE_TRIGGER_ACTIVE_THRESHOLD         24

// Keyboard debouncer bit flags inside `ins->debouncer` (distinct from misc_buttons bits)
#define KB_DEBOUNCE_BIT_ESC                     (1u << 16)
#define KB_DEBOUNCE_BIT_TAB                     (1u << 17)

/**
 * @brief Per-HID-device platform state stored inside `d->platform_data[]`.
 */
typedef struct {
    unipad_gamepad_mode_t gamepad_mode;
    uni_gamepad_seat_t seat;
    uni_gamepad_seat_t prev_seat;
    bool swap_ports_in_twinstick;
    uni_balance_board_state_t bb_state;
    uint32_t debouncer;

    // Cached analog stick deflection for smooth 62.5 Hz 1351 cursor motion when held steady
    int16_t cached_mouse_dx;
    int16_t cached_mouse_dy;
} unipad_device_instance_t;

_Static_assert(sizeof(unipad_device_instance_t) < HID_DEVICE_MAX_PLATFORM_DATA,
               "unipad_device_instance_t exceeds HID_DEVICE_MAX_PLATFORM_DATA");

// Persistent platform property definitions matching `UNI_PROPERTY_IDX_LAST .. UNI_PROPERTY_IDX_UNI_LAST`
static const uni_property_t s_unipad_properties[] = {
    {UNI_PROPERTY_IDX_UNI_AUTOFIRE_CPS, UNI_PROPERTY_NAME_UNI_AUTOFIRE_CPS, UNI_PROPERTY_TYPE_U8,
     .default_value.u8 = AUTOFIRE_CPS_DEFAULT},
    {UNI_PROPERTY_IDX_UNI_C64_POT_MODE, UNI_PROPERTY_NAME_UNI_C64_POT_MODE, UNI_PROPERTY_TYPE_U8,
     .default_value.u8 = UNIPAD_C64_POT_MODE_3BUTTONS},
    {UNI_PROPERTY_IDX_UNI_MODEL, UNI_PROPERTY_NAME_UNI_MODEL, UNI_PROPERTY_TYPE_STRING,
     .default_value.str = "Unipad C64 (Rev. A)", .flags = UNI_PROPERTY_FLAG_READ_ONLY},
    {UNI_PROPERTY_IDX_UNI_MOUSE_EMULATION, UNI_PROPERTY_NAME_UNI_MOUSE_EMULATION, UNI_PROPERTY_TYPE_U8,
     .default_value.u8 = 0},
    {UNI_PROPERTY_IDX_UNI_SERIAL_NUMBER, UNI_PROPERTY_NAME_UNI_SERIAL_NUMBER, UNI_PROPERTY_TYPE_U32,
     .default_value.u32 = 0, .flags = UNI_PROPERTY_FLAG_READ_ONLY},
    {UNI_PROPERTY_IDX_UNI_VENDOR, UNI_PROPERTY_NAME_UNI_VENDOR, UNI_PROPERTY_TYPE_STRING,
     .default_value.str = "Retro Moe (retro.moe)", .flags = UNI_PROPERTY_FLAG_READ_ONLY},
};

_Static_assert(ARRAY_SIZE(s_unipad_properties) == (UNI_PROPERTY_IDX_UNI_LAST - UNI_PROPERTY_IDX_LAST),
               "Unipad property table size mismatch with uni_property_idx_t");

// Runtime platform state
static unipad_c64_pot_mode_t s_pot_mode = UNIPAD_C64_POT_MODE_3BUTTONS;
static float s_mouse_scale = 1.0f;
static bool s_auto_enable_bt = true;
static bool s_internal_bt_toggle_pending = false;
static btstack_timer_source_t s_periodic_timer;

// ---------------------------------------------------------------------------
// Forward Declarations
// ---------------------------------------------------------------------------
static unipad_device_instance_t* get_device_instance(const uni_hid_device_t* d);
static void reconcile_all_port_modes_and_leds(void);
static void maybe_update_bt_scanning(bool enable);
static void try_swap_ports_from_controller(uni_hid_device_t* d);

// ---------------------------------------------------------------------------
// String Lookup Helpers
// ---------------------------------------------------------------------------

const char* unipad_c64_gamepad_mode_to_str(unipad_gamepad_mode_t mode) {
    switch (mode) {
        case UNIPAD_GAMEPAD_MODE_NORMAL:
            return "normal";
        case UNIPAD_GAMEPAD_MODE_MOUSE:
            return "mouse";
        case UNIPAD_GAMEPAD_MODE_TWINSTICK:
            return "twinstick";
        default:
            return "none";
    }
}

const char* unipad_c64_pot_mode_to_str(unipad_c64_pot_mode_t mode) {
    switch (mode) {
        case UNIPAD_C64_POT_MODE_3BUTTONS:
            return "3buttons";
        case UNIPAD_C64_POT_MODE_5BUTTONS:
            return "5buttons";
        case UNIPAD_C64_POT_MODE_RUMBLE:
            return "rumble";
        case UNIPAD_C64_POT_MODE_PADDLE:
            return "paddle";
        default:
            return "invalid";
    }
}

// ---------------------------------------------------------------------------
// Instance & Device Enumeration Helpers
// ---------------------------------------------------------------------------

static unipad_device_instance_t* get_device_instance(const uni_hid_device_t* d) {
    return (unipad_device_instance_t*)&d->platform_data[0];
}

static int count_physical_connected_devices(void) {
    int count = 0;
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL) {
            continue;
        }
        if (uni_bt_conn_is_connected(&d->conn) && !uni_hid_device_is_virtual_device(d)) {
            count++;
        }
    }
    return count;
}

static uni_hid_device_t* find_primary_gamepad(void) {
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
            continue;
        }
        if (!uni_hid_device_is_mouse(d) && !uni_hid_device_is_virtual_device(d)) {
            return d;
        }
    }
    return NULL;
}

static bool seat_maps_to_port(uni_gamepad_seat_t seat, unipad_port_t port) {
    if (port == UNIPAD_PORT_1) {
        return (seat & GAMEPAD_SEAT_A) != 0;
    }
    return (seat & GAMEPAD_SEAT_B) != 0;
}

/**
 * @brief Determines the hardware POT mode for a specific C64 Control Port based on
 *        what device currently occupies that port plus the user's `c64_pot_mode` setting.
 */
static unipad_pot_hw_mode_t resolve_hw_pot_mode_for_port(unipad_port_t port) {
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
            continue;
        }
        const unipad_device_instance_t* ins = get_device_instance(d);
        if (!seat_maps_to_port(ins->seat, port)) {
            continue;
        }
        if (uni_hid_device_is_mouse(d) || ins->gamepad_mode == UNIPAD_GAMEPAD_MODE_MOUSE) {
            return UNIPAD_POT_HW_1351_MOUSE;
        }
    }

    switch (s_pot_mode) {
        case UNIPAD_C64_POT_MODE_RUMBLE:
            return UNIPAD_POT_HW_RUMBLE;
        case UNIPAD_C64_POT_MODE_PADDLE:
            return UNIPAD_POT_HW_PADDLE;
        case UNIPAD_C64_POT_MODE_3BUTTONS:
        case UNIPAD_C64_POT_MODE_5BUTTONS:
        default:
            return UNIPAD_POT_HW_BUTTONS;
    }
}

static void reconcile_all_port_modes_and_leds(void) {
    uni_gamepad_seat_t occupied_seats = GAMEPAD_SEAT_NONE;

    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
            continue;
        }
        occupied_seats |= get_device_instance(d)->seat;
    }

    unipad_c64_gpio_set_port_leds((occupied_seats & GAMEPAD_SEAT_A) != 0,
                                  (occupied_seats & GAMEPAD_SEAT_B) != 0);

    for (uint p = 0u; p < UNIPAD_PORT_COUNT; p++) {
        const unipad_port_t port = (unipad_port_t)p;
        const unipad_pot_hw_mode_t desired_hw_mode = resolve_hw_pot_mode_for_port(port);
        if (unipad_c64_pio_get_port_mode(port) != desired_hw_mode) {
            unipad_c64_pio_set_port_mode(port, desired_hw_mode);
        }
    }
}

static void notify_controller_seat_feedback(uni_hid_device_t* d, uni_gamepad_seat_t seat) {
    bool visual_set = false;

    if (d->report_parser.set_lightbar_color != NULL) {
        // Match physical Unipad C64 LED colors: Port 1 = Green (D5), Port 2 = Red (D6), Both = Amber.
        const uint8_t red = (seat & GAMEPAD_SEAT_B) ? 0xFFu : 0x00u;
        const uint8_t green = (seat & GAMEPAD_SEAT_A) ? 0xFFu : 0x00u;
        d->report_parser.set_lightbar_color(d, red, green, 0x00u);
        visual_set = true;
    }

    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, seat);
        visual_set = true;
    }

    if (!visual_set && d->report_parser.play_dual_rumble != NULL) {
        d->report_parser.play_dual_rumble(d, 0 /* delay ms */, 100 /* duration ms */,
                                          0x00 /* weak */, 0xA0 /* strong */);
    }
}

static void assign_device_seat(uni_hid_device_t* d, uni_gamepad_seat_t seat) {
    unipad_device_instance_t* ins = get_device_instance(d);
    ins->seat = seat;

    logi("unipad_c64: Device %s assigned seat 0x%02x (%s)\n",
         bd_addr_to_str(d->conn.btaddr), seat,
         (seat == (GAMEPAD_SEAT_A | GAMEPAD_SEAT_B)) ? "Port 1+2 Twin-Stick"
         : (seat == GAMEPAD_SEAT_A)                  ? "Port 1"
         : (seat == GAMEPAD_SEAT_B)                  ? "Port 2"
                                                     : "None");

    reconcile_all_port_modes_and_leds();
    notify_controller_seat_feedback(d, seat);
}

static void clear_both_ports_outputs(void) {
    unipad_c64_gpio_clear_digital_port(UNIPAD_PORT_1);
    unipad_c64_gpio_clear_digital_port(UNIPAD_PORT_2);
    unipad_c64_pio_set_buttons(UNIPAD_PORT_1, false, false);
    unipad_c64_pio_set_buttons(UNIPAD_PORT_2, false, false);
}

// ---------------------------------------------------------------------------
// Input Processing: Joystick, 1351 Mouse, 1311/1312 Paddles, 5-Button, Keyboard, Balance Board
// ---------------------------------------------------------------------------

static void apply_uni_joystick_to_port(unipad_port_t port, const uni_joystick_t* joy) {
    const unipad_digital_joy_t digital = {
        .up = (joy->up != 0),
        .down = (joy->down != 0),
        .left = (joy->left != 0),
        .right = (joy->right != 0),
        .fire = (joy->fire != 0),
        .auto_fire = (joy->auto_fire != 0),
    };
    unipad_c64_gpio_set_digital_port(port, &digital);
    unipad_c64_pio_set_buttons(port, joy->button2 != 0, joy->button3 != 0);
}

static void route_uni_joystick_to_seat(uni_gamepad_seat_t seat, const uni_joystick_t* joy) {
    if (seat == GAMEPAD_SEAT_A) {
        apply_uni_joystick_to_port(UNIPAD_PORT_1, joy);
        return;
    }
    if (seat == GAMEPAD_SEAT_B) {
        apply_uni_joystick_to_port(UNIPAD_PORT_2, joy);
        return;
    }
    loge("unipad_c64: Invalid seat %d for single joystick update\n", seat);
}

/**
 * @brief Processes true Bluetooth Mouse HID reports OR Gamepad-in-Mouse-mode buttons/deltas
 *        into Commodore 1351 Proportional Analog Mouse signals (`$D419`/`$D41A` + CIA1 buttons).
 *
 * Verified Commodore 1351 button wiring:
 *   - Left Click   -> DB9 Pin 6 (`JOY_FIRE`, `$DC00`/`$DC01` bit 4)
 *   - Right Click  -> DB9 Pin 1 (`JOY_UP`,   `$DC00`/`$DC01` bit 0)
 *   - Middle Click -> DB9 Pin 2 (`JOY_DOWN`, `$DC00`/`$DC01` bit 1, Micromys/VICE 3-button convention)
 */
static void process_1351_mouse_input(uni_gamepad_seat_t seat,
                                     int32_t delta_x,
                                     int32_t delta_y,
                                     bool left_btn,
                                     bool right_btn,
                                     bool middle_btn) {
    const unipad_digital_joy_t mouse_buttons = {
        .up = right_btn,
        .down = middle_btn,
        .left = false,
        .right = false,
        .fire = left_btn,
        .auto_fire = false,
    };

    if (seat & GAMEPAD_SEAT_A) {
        unipad_c64_pio_update_1351_mouse(UNIPAD_PORT_1, delta_x, delta_y, s_mouse_scale);
        unipad_c64_gpio_set_digital_port(UNIPAD_PORT_1, &mouse_buttons);
    }
    if (seat & GAMEPAD_SEAT_B) {
        unipad_c64_pio_update_1351_mouse(UNIPAD_PORT_2, delta_x, delta_y, s_mouse_scale);
        unipad_c64_gpio_set_digital_port(UNIPAD_PORT_2, &mouse_buttons);
    }
}

static uint8_t axis_to_paddle_byte(int32_t axis_val) {
    // Bluepad32 axis spans [-512 .. +511].
    // Moving stick RIGHT (+511) decreases C64 SID resistance -> 0 (clockwise C64 paddle turn).
    // Moving stick LEFT  (-512) increases C64 SID resistance -> 255 (counter-clockwise turn).
    int32_t normalized = (axis_val + 512) >> 2;
    if (normalized < 0) {
        normalized = 0;
    } else if (normalized > 255) {
        normalized = 255;
    }
    return (uint8_t)(255 - normalized);
}

static uint8_t trigger_to_paddle_byte(int32_t trigger_val) {
    // Bluepad32 brake/throttle spans [0 .. 1023].
    int32_t inverted = (1023 - trigger_val) >> 2;
    if (inverted < 0) {
        inverted = 0;
    } else if (inverted > 255) {
        inverted = 255;
    }
    return (uint8_t)inverted;
}

/**
 * @brief Processes a Bluetooth Gamepad in Commodore 1311/1312 Paddle Mode (`c64_pot_mode paddle`):
 *   - Paddle X (`$D419`): Left Analog Stick X (`axis_x`) or Left Trigger (`brake` if pulled)
 *   - Paddle Y (`$D41A`): Right Analog Stick X (`axis_rx`) or Right Trigger (`throttle` if pulled)
 *   - Paddle X Fire (`JOY_LEFT`, DB9 Pin 3): `Button A`, `L1`, or `D-Pad Left`
 *   - Paddle Y Fire (`JOY_RIGHT`, DB9 Pin 4): `Button B`, `R1`, or `D-Pad Right`
 *   - Menu / Intro Navigation: `D-Pad Up/Down` + `Button X`/`Button Y` (`JOY_FIRE`)
 */
static void process_gamepad_as_paddles(uni_gamepad_seat_t seat, const uni_gamepad_t* gp) {
    const uint8_t pad_x = (gp->brake > PADDLE_TRIGGER_ACTIVE_THRESHOLD)
                              ? trigger_to_paddle_byte(gp->brake)
                              : axis_to_paddle_byte(gp->axis_x);
    const uint8_t pad_y = (gp->throttle > PADDLE_TRIGGER_ACTIVE_THRESHOLD)
                              ? trigger_to_paddle_byte(gp->throttle)
                              : axis_to_paddle_byte(gp->axis_rx);

    const unipad_digital_joy_t digital = {
        .up = (gp->dpad & DPAD_UP) != 0,
        .down = (gp->dpad & DPAD_DOWN) != 0,
        .left = ((gp->dpad & DPAD_LEFT) != 0) || ((gp->buttons & (BUTTON_A | BUTTON_SHOULDER_L)) != 0),
        .right = ((gp->dpad & DPAD_RIGHT) != 0) || ((gp->buttons & (BUTTON_B | BUTTON_SHOULDER_R)) != 0),
        .fire = (gp->buttons & (BUTTON_X | BUTTON_Y)) != 0,
        .auto_fire = false,
    };

    if (seat & GAMEPAD_SEAT_A) {
        unipad_c64_pio_set_paddles(UNIPAD_PORT_1, pad_x, pad_y);
        unipad_c64_gpio_set_digital_port(UNIPAD_PORT_1, &digital);
    }
    if (seat & GAMEPAD_SEAT_B) {
        unipad_c64_pio_set_paddles(UNIPAD_PORT_2, pad_x, pad_y);
        unipad_c64_gpio_set_digital_port(UNIPAD_PORT_2, &digital);
    }
}

/**
 * @brief Applies CrystalCT 5-Button C64 chords (`Select` = `UP+DOWN`, `Start` = `LEFT+RIGHT`)
 *        atomically onto the digital joystick struct prior to `gpio_put_masked()`.
 */
static void overlay_crystalct_5button_chords(uint8_t misc_buttons, uni_joystick_t* io_joy) {
    if (misc_buttons & MISC_BUTTON_SELECT) {
        io_joy->up = 1;
        io_joy->down = 1;
    }
    if (misc_buttons & MISC_BUTTON_START) {
        io_joy->left = 1;
        io_joy->right = 1;
    }
}

static bool test_misc_button_rising_edge(unipad_device_instance_t* ins,
                                         uint8_t misc_buttons,
                                         uint32_t mask) {
    const bool currently_down = (misc_buttons & mask) != 0;
    const bool was_down = (ins->debouncer & mask) != 0;

    if (!currently_down) {
        ins->debouncer &= ~mask;
        return false;
    }
    if (was_down) {
        return false;
    }
    ins->debouncer |= mask;
    return true;
}

static bool test_keyboard_key_rising_edge(unipad_device_instance_t* ins,
                                          const uni_keyboard_t* kb,
                                          uint8_t usage_key,
                                          uint32_t debounce_bit) {
    bool found = false;
    for (int i = 0; i < UNI_KEYBOARD_PRESSED_KEYS_MAX; i++) {
        if (kb->pressed_keys[i] < HID_USAGE_KB_ERROR_UNDEFINED) {
            break;
        }
        if (kb->pressed_keys[i] == usage_key) {
            found = true;
            break;
        }
    }

    const bool was_down = (ins->debouncer & debounce_bit) != 0;
    if (!found) {
        ins->debouncer &= ~debounce_bit;
        return false;
    }
    if (was_down) {
        return false;
    }
    ins->debouncer |= debounce_bit;
    return true;
}

static int16_t compute_stick_mouse_velocity(int32_t primary_axis, int32_t secondary_axis) {
    int32_t v1 = (primary_axis > GAMEPAD_MOUSE_AXIS_DEADZONE || primary_axis < -GAMEPAD_MOUSE_AXIS_DEADZONE)
                     ? (primary_axis / GAMEPAD_MOUSE_AXIS_DIVISOR)
                     : 0;
    int32_t v2 = (secondary_axis > GAMEPAD_MOUSE_AXIS_DEADZONE || secondary_axis < -GAMEPAD_MOUSE_AXIS_DEADZONE)
                     ? (secondary_axis / GAMEPAD_MOUSE_AXIS_DIVISOR)
                     : 0;
    return (int16_t)(v1 + v2);
}

static void process_gamepad(uni_hid_device_t* d, const uni_gamepad_t* gp) {
    unipad_device_instance_t* ins = get_device_instance(d);

    // Clear cached stick-mouse velocity whenever not in Mouse emulation mode.
    if (ins->gamepad_mode != UNIPAD_GAMEPAD_MODE_MOUSE) {
        ins->cached_mouse_dx = 0;
        ins->cached_mouse_dy = 0;
    }

    switch (ins->gamepad_mode) {
        case UNIPAD_GAMEPAD_MODE_NORMAL: {
            if (s_pot_mode == UNIPAD_C64_POT_MODE_PADDLE) {
                process_gamepad_as_paddles(ins->seat, gp);
                break;
            }

            uni_joystick_t joy = {0};
            if (d->controller_type == CONTROLLER_TYPE_WiiController &&
                d->controller_subtype == CONTROLLER_SUBTYPE_WIIMOTE_ACCEL) {
                uni_joy_to_single_from_wii_accel(gp, &joy);
            } else {
                uni_joy_to_single_joy_from_gamepad(gp, &joy, false /* C64: Button B = Jump/Up */);
            }

            if (s_pot_mode == UNIPAD_C64_POT_MODE_5BUTTONS) {
                overlay_crystalct_5button_chords(gp->misc_buttons, &joy);
            }
            route_uni_joystick_to_seat(ins->seat, &joy);
            break;
        }

        case UNIPAD_GAMEPAD_MODE_TWINSTICK: {
            if (s_pot_mode == UNIPAD_C64_POT_MODE_PADDLE) {
                process_gamepad_as_paddles(ins->seat, gp);
                break;
            }

            uni_joystick_t joy_a = {0};
            uni_joystick_t joy_b = {0};
            uni_joy_to_twinstick_from_gamepad(gp, &joy_a, &joy_b);

            if (s_pot_mode == UNIPAD_C64_POT_MODE_5BUTTONS) {
                overlay_crystalct_5button_chords(gp->misc_buttons, &joy_a);
                overlay_crystalct_5button_chords(gp->misc_buttons, &joy_b);
            }

            if (ins->swap_ports_in_twinstick) {
                route_uni_joystick_to_seat(GAMEPAD_SEAT_B, &joy_a);
                route_uni_joystick_to_seat(GAMEPAD_SEAT_A, &joy_b);
            } else {
                route_uni_joystick_to_seat(GAMEPAD_SEAT_A, &joy_a);
                route_uni_joystick_to_seat(GAMEPAD_SEAT_B, &joy_b);
            }
            break;
        }

        case UNIPAD_GAMEPAD_MODE_MOUSE: {
            ins->cached_mouse_dx = compute_stick_mouse_velocity(gp->axis_x, gp->axis_rx);
            ins->cached_mouse_dy = compute_stick_mouse_velocity(gp->axis_y, gp->axis_ry);

            // Update buttons immediately; continuous 62.5 Hz cursor motion is stepped by
            // `periodic_timer_handler` so holding a stick steady glides smoothly in GEOS.
            process_1351_mouse_input(ins->seat, 0, 0,
                                     (gp->buttons & BUTTON_A) != 0,
                                     (gp->buttons & BUTTON_B) != 0,
                                     (gp->buttons & BUTTON_X) != 0);
            break;
        }

        default:
            break;
    }

    // In 5-button mode, Select & Start belong exclusively to the C64 game (Buttons 4 & 5);
    // port swap and mode cycling remain accessible via the physical onboard buttons & System button.
    if (s_pot_mode == UNIPAD_C64_POT_MODE_5BUTTONS) {
        return;
    }

    if (test_misc_button_rising_edge(ins, gp->misc_buttons, MISC_BUTTON_SELECT)) {
        try_swap_ports_from_controller(d);
    }
    if (test_misc_button_rising_edge(ins, gp->misc_buttons, MISC_BUTTON_START)) {
        unipad_c64_platform_next_gamepad_mode();
    }
}

static void process_keyboard(uni_hid_device_t* d, const uni_keyboard_t* kb) {
    unipad_device_instance_t* ins = get_device_instance(d);
    uni_joystick_t joy_a = {0};
    uni_joystick_t joy_b = {0};

    if (ins->gamepad_mode == UNIPAD_GAMEPAD_MODE_TWINSTICK) {
        uni_joy_to_twinstick_from_keyboard(kb, &joy_a, &joy_b);
        if (ins->swap_ports_in_twinstick) {
            route_uni_joystick_to_seat(GAMEPAD_SEAT_B, &joy_a);
            route_uni_joystick_to_seat(GAMEPAD_SEAT_A, &joy_b);
        } else {
            route_uni_joystick_to_seat(GAMEPAD_SEAT_A, &joy_a);
            route_uni_joystick_to_seat(GAMEPAD_SEAT_B, &joy_b);
        }
    } else {
        uni_joy_to_single_joy_from_keyboard(kb, &joy_a);
        route_uni_joystick_to_seat(ins->seat, &joy_a);
    }

    if (test_keyboard_key_rising_edge(ins, kb, HID_USAGE_KB_ESCAPE, KB_DEBOUNCE_BIT_ESC)) {
        try_swap_ports_from_controller(d);
    }
    if (test_keyboard_key_rising_edge(ins, kb, HID_USAGE_KB_TAB, KB_DEBOUNCE_BIT_TAB)) {
        unipad_c64_platform_next_gamepad_mode();
    }
}

static void process_balance_board(uni_hid_device_t* d, const uni_balance_board_t* bb) {
    unipad_device_instance_t* ins = get_device_instance(d);
    uni_joystick_t joy = {0};
    uni_joy_to_single_joy_from_balance_board(bb, &ins->bb_state, &joy);
    route_uni_joystick_to_seat(ins->seat, &joy);
}

// ---------------------------------------------------------------------------
// Periodic 62.5 Hz Frame Timer: Zero-Interrupt C64 Rumble + Smooth Stick-Mouse Stepping
// ---------------------------------------------------------------------------

static void trigger_rumble_for_seat_mask(uni_gamepad_seat_t seat_mask) {
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
            continue;
        }
        const unipad_device_instance_t* ins = get_device_instance(d);
        if ((ins->seat & seat_mask) == 0) {
            continue;
        }
        if (d->report_parser.play_dual_rumble != NULL) {
            d->report_parser.play_dual_rumble(d, 0 /* delay ms */, 35 /* duration ms */,
                                              0x80 /* weak */, 0x40 /* strong */);
        }
    }
}

static void periodic_timer_handler(btstack_timer_source_t* ts) {
    // 1. Poll PIO SID sync FIFOs for zero-interrupt C64 Rumble pulses & opportunistic PAL/NTSC lock.
    bool rumble_p1 = false;
    bool rumble_p2 = false;
    unipad_c64_pio_poll_sync(&rumble_p1, &rumble_p2);

    if (rumble_p1) {
        trigger_rumble_for_seat_mask(GAMEPAD_SEAT_A);
    }
    if (rumble_p2) {
        trigger_rumble_for_seat_mask(GAMEPAD_SEAT_B);
    }

    // 2. Step continuous 1351 cursor velocity for any gamepad in Mouse emulation mode.
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
            continue;
        }
        const unipad_device_instance_t* ins = get_device_instance(d);
        if (ins->gamepad_mode != UNIPAD_GAMEPAD_MODE_MOUSE) {
            continue;
        }
        if (ins->cached_mouse_dx == 0 && ins->cached_mouse_dy == 0) {
            continue;
        }
        if (ins->seat & GAMEPAD_SEAT_A) {
            unipad_c64_pio_update_1351_mouse(UNIPAD_PORT_1, ins->cached_mouse_dx, ins->cached_mouse_dy,
                                             s_mouse_scale);
        }
        if (ins->seat & GAMEPAD_SEAT_B) {
            unipad_c64_pio_update_1351_mouse(UNIPAD_PORT_2, ins->cached_mouse_dx, ins->cached_mouse_dy,
                                             s_mouse_scale);
        }
    }

    btstack_run_loop_set_timer(ts, PERIODIC_TICK_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

// ---------------------------------------------------------------------------
// Port Swap, Mode Cycling & Bluetooth Discovery Management
// ---------------------------------------------------------------------------

static void maybe_update_bt_scanning(bool enable) {
    if (!s_auto_enable_bt) {
        return;
    }
    s_internal_bt_toggle_pending = true;
    if (enable) {
        uni_bt_start_scanning_and_autoconnect_safe();
    } else {
        uni_bt_stop_scanning_safe();
    }
}

static void try_swap_ports_from_controller(uni_hid_device_t* d) {
    unipad_device_instance_t* ins = get_device_instance(d);
    if (ins->seat == GAMEPAD_SEAT_NONE) {
        return;
    }

    if (ins->gamepad_mode != UNIPAD_GAMEPAD_MODE_TWINSTICK) {
        // When two physical gamepads are connected simultaneously, require BOTH players
        // to hold System/Select at the same time before swapping (prevents accidental hijacking).
        for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
            uni_hid_device_t* other = uni_hid_device_get_instance_for_idx(i);
            if (other == NULL || other == d || !uni_bt_conn_is_connected(&other->conn) ||
                uni_hid_device_is_virtual_device(other)) {
                continue;
            }
            const unipad_device_instance_t* other_ins = get_device_instance(other);
            if (other->controller.klass == UNI_CONTROLLER_CLASS_GAMEPAD &&
                other_ins->seat != GAMEPAD_SEAT_NONE &&
                (other->controller.gamepad.misc_buttons & (MISC_BUTTON_SYSTEM | MISC_BUTTON_SELECT)) == 0) {
                logi("unipad_c64: Two gamepads connected — press System/Select on both (or press SWAP on board)\n");
                return;
            }
        }
    }

    unipad_c64_platform_swap_ports();
}

static void on_physical_mode_button_pressed(void) {
    logi("unipad_c64: Onboard MODE button pressed\n");
    unipad_c64_platform_next_gamepad_mode();
}

static void on_physical_swap_button_pressed(void) {
    logi("unipad_c64: Onboard SWAP button pressed\n");
    unipad_c64_platform_swap_ports();
}

// ---------------------------------------------------------------------------
// Public Runtime Control APIs
// ---------------------------------------------------------------------------

void unipad_c64_platform_swap_ports(void) {
    bool swapped_any = false;

    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
            continue;
        }
        unipad_device_instance_t* ins = get_device_instance(d);
        if (ins->seat == GAMEPAD_SEAT_NONE) {
            continue;
        }

        if (ins->gamepad_mode == UNIPAD_GAMEPAD_MODE_TWINSTICK) {
            ins->swap_ports_in_twinstick = !ins->swap_ports_in_twinstick;
            clear_both_ports_outputs();
            unipad_c64_gpio_blink_bt_led(1);
            logi("unipad_c64: Swapped primary/secondary sticks in Twin-Stick mode (%s)\n",
                 ins->swap_ports_in_twinstick ? "swapped" : "normal");
            return;
        }

        const uni_gamepad_seat_t new_seat = (~ins->seat) & GAMEPAD_SEAT_AB_MASK;
        assign_device_seat(d, new_seat);
        swapped_any = true;
    }

    clear_both_ports_outputs();
    reconcile_all_port_modes_and_leds();

    if (swapped_any) {
        unipad_c64_gpio_blink_bt_led(1);
    } else {
        logi("unipad_c64: swap_ports — no active controllers connected\n");
    }
}

bool unipad_c64_platform_set_gamepad_mode(unipad_gamepad_mode_t mode) {
    uni_hid_device_t* d = find_primary_gamepad();
    if (d == NULL) {
        loge("unipad_c64: Cannot change gamepad mode — no gamepad connected\n");
        return false;
    }

    unipad_device_instance_t* ins = get_device_instance(d);
    const int num_physical = count_physical_connected_devices();

    if (ins->gamepad_mode == mode) {
        logi("unipad_c64: Already in gamepad mode '%s'\n", unipad_c64_gamepad_mode_to_str(mode));
        return true;
    }

    clear_both_ports_outputs();

    switch (mode) {
        case UNIPAD_GAMEPAD_MODE_TWINSTICK:
            if (num_physical != 1) {
                logi("unipad_c64: Twin-Stick requires 1 physical device (connected=%d); reverting to Normal\n",
                     num_physical);
                ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_NORMAL;
                reconcile_all_port_modes_and_leds();
                unipad_c64_gpio_blink_bt_led(1);
                return false;
            }
            ins->prev_seat = ins->seat;
            ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_TWINSTICK;
            assign_device_seat(d, GAMEPAD_SEAT_A | GAMEPAD_SEAT_B);
            unipad_c64_gpio_blink_bt_led(3);
            maybe_update_bt_scanning(false);
            logi("unipad_c64: Gamepad mode -> twinstick\n");
            return true;

        case UNIPAD_GAMEPAD_MODE_MOUSE:
            if (ins->gamepad_mode == UNIPAD_GAMEPAD_MODE_TWINSTICK) {
                ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_MOUSE;
                assign_device_seat(d, ins->prev_seat ? ins->prev_seat : GAMEPAD_SEAT_A);
                maybe_update_bt_scanning(num_physical < 2);
            } else {
                ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_MOUSE;
                reconcile_all_port_modes_and_leds();
            }
            unipad_c64_gpio_blink_bt_led(2);
            logi("unipad_c64: Gamepad mode -> mouse (PIO Commodore 1351)\n");
            return true;

        case UNIPAD_GAMEPAD_MODE_NORMAL:
            if (ins->gamepad_mode == UNIPAD_GAMEPAD_MODE_TWINSTICK) {
                ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_NORMAL;
                assign_device_seat(d, ins->prev_seat ? ins->prev_seat : GAMEPAD_SEAT_B);
                maybe_update_bt_scanning(num_physical < 2);
            } else {
                ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_NORMAL;
                reconcile_all_port_modes_and_leds();
            }
            unipad_c64_gpio_blink_bt_led(1);
            logi("unipad_c64: Gamepad mode -> normal\n");
            return true;

        default:
            loge("unipad_c64: Unsupported gamepad mode %d\n", mode);
            return false;
    }
}

void unipad_c64_platform_next_gamepad_mode(void) {
    uni_hid_device_t* d = find_primary_gamepad();
    if (d == NULL) {
        logi("unipad_c64: MODE — no gamepad connected\n");
        return;
    }

    const unipad_device_instance_t* ins = get_device_instance(d);
    const int num_physical = count_physical_connected_devices();

    // Cycle order: Normal -> Mouse (1351) -> Twin-Stick (when 1 device connected) -> Normal
    switch (ins->gamepad_mode) {
        case UNIPAD_GAMEPAD_MODE_NORMAL:
            unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_MOUSE);
            break;
        case UNIPAD_GAMEPAD_MODE_MOUSE:
            if (num_physical == 1) {
                unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_TWINSTICK);
            } else {
                unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_NORMAL);
            }
            break;
        case UNIPAD_GAMEPAD_MODE_TWINSTICK:
        default:
            unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_NORMAL);
            break;
    }
}

unipad_gamepad_mode_t unipad_c64_platform_get_gamepad_mode(void) {
    uni_hid_device_t* d = find_primary_gamepad();
    if (d == NULL) {
        return UNIPAD_GAMEPAD_MODE_NONE;
    }
    return get_device_instance(d)->gamepad_mode;
}

bool unipad_c64_platform_set_pot_mode(unipad_c64_pot_mode_t mode) {
    if (mode <= UNIPAD_C64_POT_MODE_INVALID || mode >= UNIPAD_C64_POT_MODE_COUNT) {
        return false;
    }

    s_pot_mode = mode;

    uni_property_value_t val = {0};
    val.u8 = (uint8_t)mode;
    uni_property_set(UNI_PROPERTY_IDX_UNI_C64_POT_MODE, val);

    clear_both_ports_outputs();
    reconcile_all_port_modes_and_leds();
    logi("unipad_c64: C64 POT mode -> %s\n", unipad_c64_pot_mode_to_str(mode));
    return true;
}

unipad_c64_pot_mode_t unipad_c64_platform_get_pot_mode(void) {
    return s_pot_mode;
}

bool unipad_c64_platform_set_autofire_cps(uint8_t cps) {
    if (cps < 1u || cps > 62u) {
        return false;
    }

    unipad_c64_gpio_set_autofire_cps(cps);

    uni_property_value_t val = {0};
    val.u8 = cps;
    uni_property_set(UNI_PROPERTY_IDX_UNI_AUTOFIRE_CPS, val);
    logi("unipad_c64: Autofire rate -> %u CPS\n", (unsigned)cps);
    return true;
}

uint8_t unipad_c64_platform_get_autofire_cps(void) {
    return unipad_c64_gpio_get_autofire_cps();
}

bool unipad_c64_platform_set_mouse_scale(float scale) {
    if (scale < 0.1f || scale > 5.0f) {
        return false;
    }

    s_mouse_scale = scale;

    uni_property_value_t val = {0};
    val.f32 = scale;
    uni_property_set(UNI_PROPERTY_IDX_MOUSE_SCALE, val);
    logi("unipad_c64: 1351 mouse sensitivity scale -> %.2f\n", (double)scale);
    return true;
}

float unipad_c64_platform_get_mouse_scale(void) {
    return s_mouse_scale;
}

void unipad_c64_platform_set_bt_scanning(bool enabled) {
    s_auto_enable_bt = enabled;
    s_internal_bt_toggle_pending = true;
    if (enabled) {
        uni_bt_start_scanning_and_autoconnect_safe();
    } else {
        uni_bt_stop_scanning_safe();
    }
}

void unipad_c64_platform_print_status(void) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    char board_id_hex[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(board_id_hex, sizeof(board_id_hex));

    bd_addr_t local_addr = {0};
    uni_bt_get_local_bd_addr_safe(local_addr);

    unipad_pio_status_t pio_st;
    unipad_c64_pio_get_status(&pio_st);

    const char* pot_hw_names[] = {"buttons (SIO)", "rumble (PIO sync)", "1351 mouse (PIO)", "paddles (PIO)"};
    const char* tv_names[] = {"auto", "pal", "ntsc"};

    char strap_str[40];
    unipad_board_id_format_straps(unipad_board_id_get_raw_mask(), strap_str, sizeof(strap_str));
    const unipad_board_info_t* board_info = unipad_board_id_get_info(unipad_board_id_get());

    logi("\n=== Unipad C64 Status ===\n");
    logi("  Hardware       : Unipad C64 (Rev. A) — Raspberry Pi Pico 2 W (RP2350)\n");
    logi("  Board ID       : %s -> %s (%s)\n", strap_str, board_info->model_name, board_info->target_hosts);
    logi("  Board UID      : %s\n", board_id_hex);
    logi("  Local BT Addr  : %s (scanning=%s)\n", bd_addr_to_str(local_addr),
         uni_bt_is_scanning() ? "on" : "off");
    logi("  Firmware       : v%s (Bluepad32 v%s, built %s %s)\n",
         UNIPAD_FIRMWARE_VERSION, UNI_VERSION_STRING, __DATE__, __TIME__);
    logi("  System Clock   : %u MHz\n", (unsigned)(clock_get_hz(clk_sys) / 1000000u));
    logi("  PIO Allocation : POT=PIO%u (4 SMs), Sync/Rumble=PIO%u (2 SMs)\n",
         (unsigned)pio_st.pot_pio_index, (unsigned)pio_st.sync_pio_index);
    logi("  C64 SID Clock  : %u Hz (%s, setting=%s%s)\n",
         (unsigned)pio_st.active_phi2_hz,
         (pio_st.active_phi2_hz == 985248u) ? "PAL" : "NTSC",
         tv_names[pio_st.tv_setting],
         pio_st.tv_auto_locked ? ", auto-locked" : "");
    logi("  C64 POT Mode   : %s\n", unipad_c64_pot_mode_to_str(s_pot_mode));
    logi("  Port 1 HW POT  : %s (1351=%u,%u | paddle=%u,%u | last_sync=%u us)\n",
         pot_hw_names[pio_st.port_mode[UNIPAD_PORT_1]],
         (unsigned)pio_st.mouse_pos_x[UNIPAD_PORT_1], (unsigned)pio_st.mouse_pos_y[UNIPAD_PORT_1],
         (unsigned)pio_st.paddle_x[UNIPAD_PORT_1], (unsigned)pio_st.paddle_y[UNIPAD_PORT_1],
         (unsigned)pio_st.last_sync_low_us[UNIPAD_PORT_1]);
    logi("  Port 2 HW POT  : %s (1351=%u,%u | paddle=%u,%u | last_sync=%u us)\n",
         pot_hw_names[pio_st.port_mode[UNIPAD_PORT_2]],
         (unsigned)pio_st.mouse_pos_x[UNIPAD_PORT_2], (unsigned)pio_st.mouse_pos_y[UNIPAD_PORT_2],
         (unsigned)pio_st.paddle_x[UNIPAD_PORT_2], (unsigned)pio_st.paddle_y[UNIPAD_PORT_2],
         (unsigned)pio_st.last_sync_low_us[UNIPAD_PORT_2]);
    logi("  Gamepad Mode   : %s\n", unipad_c64_gamepad_mode_to_str(unipad_c64_platform_get_gamepad_mode()));
    logi("  Autofire Rate  : %u CPS\n", (unsigned)unipad_c64_gpio_get_autofire_cps());
    logi("  1351 Scale     : %.2f\n", (double)s_mouse_scale);
    logi("  Connected Devs : %d physical\n", count_physical_connected_devices());
    logi("=========================\n");
}

// ---------------------------------------------------------------------------
// Bluepad32 `struct uni_platform` Callbacks
// ---------------------------------------------------------------------------

static void unipad_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    char strap_str[40];
    unipad_board_id_format_straps(unipad_board_id_get_raw_mask(), strap_str, sizeof(strap_str));
    logi("unipad_c64: Initializing %s [BOARD_ID=%s] (RP2350 + PIO SID engine)\n",
         unipad_board_id_name(unipad_board_id_get()), strap_str);
    unipad_c64_pio_init();
}

static void unipad_on_init_complete(void) {
    // Turn off boot self-test Port 1 & Port 2 LEDs now that BTstack HCI is up.
    unipad_c64_gpio_set_port_leds(false, false);

    // BTstack Flash TLV is now online (HCI_STATE_WORKING reached); load persisted user settings!
    const uni_property_value_t saved_pot = uni_property_get(UNI_PROPERTY_IDX_UNI_C64_POT_MODE);
    if (saved_pot.u8 > UNIPAD_C64_POT_MODE_INVALID && saved_pot.u8 < UNIPAD_C64_POT_MODE_COUNT) {
        s_pot_mode = (unipad_c64_pot_mode_t)saved_pot.u8;
    } else {
        s_pot_mode = UNIPAD_C64_POT_MODE_3BUTTONS;
    }

    const uni_property_value_t saved_cps = uni_property_get(UNI_PROPERTY_IDX_UNI_AUTOFIRE_CPS);
    const uint8_t initial_cps = (saved_cps.u8 >= 1u && saved_cps.u8 <= 62u) ? saved_cps.u8 : AUTOFIRE_CPS_DEFAULT;

    const uni_property_value_t saved_scale = uni_property_get(UNI_PROPERTY_IDX_MOUSE_SCALE);
    if (saved_scale.f32 >= 0.1f && saved_scale.f32 <= 5.0f) {
        s_mouse_scale = saved_scale.f32;
    }

    reconcile_all_port_modes_and_leds();

    // Check if the user held the physical MODE button (GP21) during boot to clear stored BT keys.
    if (unipad_c64_gpio_is_mode_pressed_raw()) {
        logi("unipad_c64: MODE button held at boot — erasing stored Bluetooth pairing keys!\n");
        uni_bt_del_keys_unsafe();
        unipad_c64_gpio_blink_bt_led(5);
    } else {
        uni_bt_list_keys_unsafe();
    }

    // Start non-blocking BTstack timers for button debouncing, autofire, PIO rumble/mouse tick, and USB CLI.
    unipad_c64_gpio_start_timers(on_physical_mode_button_pressed, on_physical_swap_button_pressed, initial_cps);

    btstack_run_loop_set_timer_handler(&s_periodic_timer, periodic_timer_handler);
    btstack_run_loop_set_timer(&s_periodic_timer, PERIODIC_TICK_INTERVAL_MS);
    btstack_run_loop_add_timer(&s_periodic_timer);

    unipad_c64_console_init();

    // Begin Bluetooth Classic + BLE discovery and auto-reconnection.
    uni_bt_start_scanning_and_autoconnect_unsafe();

    logi("unipad_c64: Ready (POT mode='%s', autofire=%u CPS, 1351 scale=%.2f). Type 'help' on USB serial.\n",
         unipad_c64_pot_mode_to_str(s_pot_mode), (unsigned)initial_cps, (double)s_mouse_scale);
}

static uni_error_t unipad_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    ARG_UNUSED(addr);
    ARG_UNUSED(name);
    ARG_UNUSED(cod);
    ARG_UNUSED(rssi);
    // Accept all Bluepad32-supported HID classes: Gamepads, Mice, Keyboards, and Wii Balance Boards!
    return UNI_ERROR_SUCCESS;
}

static void unipad_on_device_connected(uni_hid_device_t* d) {
    if (d == NULL) {
        return;
    }
    unipad_c64_gpio_blink_bt_led(1);
}

static void unipad_on_device_disconnected(uni_hid_device_t* d) {
    if (d == NULL) {
        return;
    }

    unipad_device_instance_t* ins = get_device_instance(d);
    const uni_gamepad_seat_t released_seat = ins->seat;

    ins->seat = GAMEPAD_SEAT_NONE;
    ins->prev_seat = GAMEPAD_SEAT_NONE;
    ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_NORMAL;
    ins->swap_ports_in_twinstick = false;
    ins->cached_mouse_dx = 0;
    ins->cached_mouse_dy = 0;

    if (released_seat & GAMEPAD_SEAT_A) {
        unipad_c64_gpio_clear_digital_port(UNIPAD_PORT_1);
        unipad_c64_pio_set_buttons(UNIPAD_PORT_1, false, false);
    }
    if (released_seat & GAMEPAD_SEAT_B) {
        unipad_c64_gpio_clear_digital_port(UNIPAD_PORT_2);
        unipad_c64_pio_set_buttons(UNIPAD_PORT_2, false, false);
    }

    reconcile_all_port_modes_and_leds();

    if (count_physical_connected_devices() < 2) {
        maybe_update_bt_scanning(true);
    }
}

static uni_error_t unipad_on_device_ready(uni_hid_device_t* d) {
    if (d == NULL) {
        return UNI_ERROR_INVALID_DEVICE;
    }

    unipad_device_instance_t* ins = get_device_instance(d);
    if (ins->seat != GAMEPAD_SEAT_NONE || !uni_hid_device_has_controller_type(d)) {
        return UNI_ERROR_INVALID_DEVICE;
    }

    // Compute currently occupied C64 seats, allowing a new physical controller to preempt
    // a DualShock 4 / DualSense virtual touchpad mouse when claiming the second physical slot.
    uni_hid_device_t* virtual_d = NULL;
    uint32_t used_seats = 0u;

    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
        uni_hid_device_t* tmp = uni_hid_device_get_instance_for_idx(i);
        if (tmp == NULL || !uni_bt_conn_is_connected(&tmp->conn)) {
            continue;
        }
        if (tmp != d && uni_hid_device_is_virtual_device(tmp)) {
            virtual_d = tmp;
            continue;
        }
        used_seats |= get_device_instance(tmp)->seat;
    }

    if (used_seats == (GAMEPAD_SEAT_A | GAMEPAD_SEAT_B)) {
        return UNI_ERROR_NO_SLOTS;
    }

    // If a second physical controller is joining while a virtual touchpad mouse holds the remaining
    // seat, gracefully detach the virtual touchpad mouse so Player 2 gets the C64 port.
    if (virtual_d != NULL && !uni_hid_device_is_virtual_device(d)) {
        if (virtual_d->parent != NULL) {
            virtual_d->parent->child = NULL;
        }
        uni_hid_device_disconnect(virtual_d);
        uni_hid_device_delete(virtual_d);
        virtual_d = NULL;
    }

    // C64 convention:
    //   - Mice default to Control Port #1 (GAMEPAD_SEAT_A: GEOS, Lemmings, C64 OS, Maniac Mansion)
    //   - Joysticks/Gamepads default to Control Port #2 (GAMEPAD_SEAT_B: ~90% of C64 games)
    uni_gamepad_seat_t wanted_seat = uni_hid_device_is_mouse(d) ? GAMEPAD_SEAT_A : GAMEPAD_SEAT_B;
    if (used_seats & wanted_seat) {
        wanted_seat = (~wanted_seat) & GAMEPAD_SEAT_AB_MASK;
    }

    ins->gamepad_mode = UNIPAD_GAMEPAD_MODE_NORMAL;
    ins->swap_ports_in_twinstick = false;
    ins->debouncer = 0u;
    ins->cached_mouse_dx = 0;
    ins->cached_mouse_dy = 0;

    assign_device_seat(d, wanted_seat);

    if (count_physical_connected_devices() >= 2) {
        maybe_update_bt_scanning(false);
    }

    return UNI_ERROR_SUCCESS;
}

static void unipad_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (d == NULL || ctl == NULL) {
        return;
    }

    const unipad_device_instance_t* ins = get_device_instance(d);
    if (ins->seat == GAMEPAD_SEAT_NONE) {
        return;
    }

    switch (ctl->klass) {
        case UNI_CONTROLLER_CLASS_GAMEPAD:
            process_gamepad(d, &ctl->gamepad);
            break;

        case UNI_CONTROLLER_CLASS_MOUSE:
            process_1351_mouse_input(ins->seat,
                                     ctl->mouse.delta_x,
                                     ctl->mouse.delta_y,
                                     (ctl->mouse.buttons & UNI_MOUSE_BUTTON_LEFT) != 0,
                                     (ctl->mouse.buttons & UNI_MOUSE_BUTTON_RIGHT) != 0,
                                     (ctl->mouse.buttons & UNI_MOUSE_BUTTON_MIDDLE) != 0);
            break;

        case UNI_CONTROLLER_CLASS_KEYBOARD:
            process_keyboard(d, &ctl->keyboard);
            break;

        case UNI_CONTROLLER_CLASS_BALANCE_BOARD:
            process_balance_board(d, &ctl->balance_board);
            break;

        default:
            break;
    }
}

static const uni_property_t* unipad_get_property(uni_property_idx_t idx) {
    if (idx < UNI_PROPERTY_IDX_LAST || idx >= UNI_PROPERTY_IDX_UNI_LAST) {
        return NULL;
    }
    return &s_unipad_properties[idx - UNI_PROPERTY_IDX_LAST];
}

static void unipad_on_oob_event(uni_platform_oob_event_t event, void* data) {
    switch (event) {
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED: {
            const bool enabled = (data != NULL);
            unipad_c64_gpio_set_bt_led(enabled);
            logi("unipad_c64: Bluetooth discovery %s\n", enabled ? "enabled" : "disabled");

            if (!enabled && !s_internal_bt_toggle_pending) {
                s_auto_enable_bt = false;
            }
            s_internal_bt_toggle_pending = false;
            break;
        }

        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON: {
            uni_hid_device_t* d = (uni_hid_device_t*)data;
            if (d != NULL) {
                try_swap_ports_from_controller(d);
            }
            break;
        }

        default:
            break;
    }
}

static void unipad_device_dump(uni_hid_device_t* d) {
    if (d == NULL) {
        return;
    }
    const unipad_device_instance_t* ins = get_device_instance(d);
    if (uni_hid_device_is_mouse(d)) {
        logi("\tunipad_c64: type=1351_mouse%s, seat=0x%02x\n",
             uni_hid_device_is_virtual_device(d) ? " (touchpad)" : "", ins->seat);
        return;
    }
    logi("\tunipad_c64: type=controller, mode=%s%s, seat=0x%02x\n",
         unipad_c64_gamepad_mode_to_str(ins->gamepad_mode),
         (ins->gamepad_mode == UNIPAD_GAMEPAD_MODE_TWINSTICK && ins->swap_ports_in_twinstick) ? " (swapped)" : "",
         ins->seat);
}

struct uni_platform* unipad_c64_get_platform(void) {
    static struct uni_platform s_platform = {
        .name = "Unipad C64",
        .init = unipad_init,
        .on_init_complete = unipad_on_init_complete,
        .on_device_discovered = unipad_on_device_discovered,
        .on_device_connected = unipad_on_device_connected,
        .on_device_disconnected = unipad_on_device_disconnected,
        .on_device_ready = unipad_on_device_ready,
        .on_controller_data = unipad_on_controller_data,
        .get_property = unipad_get_property,
        .on_oob_event = unipad_on_oob_event,
        .device_dump = unipad_device_dump,
    };
    return &s_platform;
}
