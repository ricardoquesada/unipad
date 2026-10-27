// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Firmware entry point for Unipad C64 (Rev. A) — Raspberry Pi Pico 2 W (RP2350).

#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "sdkconfig.h"
#include "unipad_c64_gpio.h"
#include "unipad_c64_platform.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Unipad C64 must be built with CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

int main(void) {
    // 1. Place all 74HCT05 open-drain drivers into Hi-Z immediately on reset (within microseconds)
    //    so the C64 CIA1 keyboard/joystick bus never sees boot transients while CYW43439 loads FW.
    unipad_c64_gpio_init_early();

    // 2. Initialize USB CDC stdio and J4 Debug Header UART0 (GP0/GP1, 115200 8N1).
    stdio_init_all();

    // 3. Initialize CYW43439 wireless driver (claims 1 PIO SM on PIO2 for gSPI and boots BT HCI).
    if (cyw43_arch_init() != 0) {
        loge("unipad_c64: FATAL — failed to initialize cyw43_arch\n");
        return -1;
    }

    // Illuminate Pico 2 W onboard LED alongside Port 1/2 LEDs until BTstack finishes startup.
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // 4. Register the Unipad C64 platform prior to calling uni_init().
    uni_platform_set_custom(unipad_c64_get_platform());

    // 5. Initialize Bluepad32 (properties, HID parsers, Classic + BLE HCI, Balance Board).
    uni_init(0, NULL);

    // 6. Enter the bare-metal BTstack event loop (does not return).
    btstack_run_loop_execute();

    return 0;
}
