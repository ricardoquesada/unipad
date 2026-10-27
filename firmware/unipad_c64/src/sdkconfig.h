// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Bluepad32 compile-time configuration for Unipad C64 (Pico 2 W / RP2350).

#ifndef UNIPAD_C64_SDKCONFIG_H
#define UNIPAD_C64_SDKCONFIG_H

// Support up to 4 simultaneous HID slots (2 physical C64 seats + transient/virtual handoff)
#define CONFIG_BLUEPAD32_MAX_DEVICES 4
#define CONFIG_BLUEPAD32_MAX_ALLOWLIST 4
#define CONFIG_BLUEPAD32_GAP_SECURITY 1
#define CONFIG_BLUEPAD32_ENABLE_BLE_BY_DEFAULT 1

// Enable DualShock 4 / DualSense touchpad virtual mouse support
#define CONFIG_BLUEPAD32_ENABLE_VIRTUAL_DEVICE_BY_DEFAULT 1

// Target selection for libbluepad32
#define CONFIG_BLUEPAD32_PLATFORM_CUSTOM 1
#define CONFIG_TARGET_PICO_W 1

// Log verbosity: 2 == Info (0=None, 1=Error, 2=Info, 3=Debug)
#define CONFIG_BLUEPAD32_LOG_LEVEL 2

#endif  // UNIPAD_C64_SDKCONFIG_H
