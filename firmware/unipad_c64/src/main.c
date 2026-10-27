// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Firmware entry point for Unipad C64 (Rev. A) — Raspberry Pi Pico 2 W (RP2350).

#include <stdio.h>

#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdio_usb.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "sdkconfig.h"
#include "unipad_board_id.h"
#include "unipad_c64_gpio.h"
#include "unipad_c64_platform.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Unipad C64 must be built with CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

/**
 * @brief Fail-safe interlock invoked when `unipad_c64.uf2` is flashed onto a
 *        non-C64 Unipad carrier PCB (`BOARD_ID[2:0] != 0b111`).
 *
 * Keeps all external carrier GPIOs (`GP0..GP28`) locked in Hi-Z so foreign DB9
 * circuits cannot experience bus contention, brings up USB CDC serial to report
 * the mismatch, and blinks a 3-flash SOS cadence on the Pico 2 W onboard LED.
 */
static void halt_on_board_id_mismatch(unipad_board_id_t detected_id) {
    stdio_usb_init();
    const bool cyw43_ok = (cyw43_arch_init() == 0);

    char strap_str[40];
    unipad_board_id_format_straps(unipad_board_id_get_raw_mask(), strap_str, sizeof(strap_str));
    const unipad_board_info_t* info = unipad_board_id_get_info(detected_id);

    while (true) {
        printf("unipad_c64: FATAL BOARD ID MISMATCH — firmware expects '%s' (0b111), "
               "detected '%s' [%s]. External GPIOs locked in Hi-Z.\n",
               unipad_board_id_name(UNIPAD_BOARD_C64), info->model_name, strap_str);

        if (!cyw43_ok) {
            sleep_ms(2000);
            continue;
        }

        for (uint pulse = 0u; pulse < 3u; pulse++) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(120);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(120);
        }
        sleep_ms(1280);
    }
}

int main(void) {
    // 1. Sample `BOARD_ID[2:0]` (`GP26..GP28`) in ~28 us and isolate the strap pads (`0 uA`).
    //    If this `.uf2` was dropped onto a non-C64 Unipad board, refuse to drive `GP0..GP22`!
    const unipad_board_id_t board_id = unipad_board_id_detect();
    if (!unipad_board_id_matches(UNIPAD_BOARD_C64)) {
        halt_on_board_id_mismatch(board_id);
    }

    // 2. Place all 74HCT05 open-drain drivers into Hi-Z immediately on reset (within microseconds)
    //    so the C64 CIA1 keyboard/joystick bus never sees boot transients while CYW43439 loads FW.
    unipad_c64_gpio_init_early();

    // 3. Initialize USB CDC stdio and J4 Debug Header UART0 (GP0/GP1, 115200 8N1).
    stdio_init_all();

    // 4. Initialize CYW43439 wireless driver (claims 1 PIO SM on PIO2 for gSPI and boots BT HCI).
    if (cyw43_arch_init() != 0) {
        loge("unipad_c64: FATAL — failed to initialize cyw43_arch\n");
        return -1;
    }

    // Illuminate Pico 2 W onboard LED alongside Port 1/2 LEDs until BTstack finishes startup.
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // 5. Register the Unipad C64 platform prior to calling uni_init().
    uni_platform_set_custom(unipad_c64_get_platform());

    // 6. Initialize Bluepad32 (properties, HID parsers, Classic + BLE HCI, Balance Board).
    uni_init(0, NULL);

    // 7. Enter the bare-metal BTstack event loop (does not return).
    btstack_run_loop_execute();

    return 0;
}
