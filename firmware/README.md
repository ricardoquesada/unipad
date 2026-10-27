# Unipad™ Firmware

This directory contains the board-specific **Raspberry Pi Pico 2 W (`RP2350` + `CYW43439`)** firmware, cycle-exact Programmable I/O (`PIO`) hardware state machines, and [Bluepad32](https://github.com/ricardoquesada/bluepad32) platform integration for **Unipad** retro computer adapters.

## Directory Layout

```text
firmware/
├── external/               # Third-party / upstream git submodules (isolated from 1st-party code)
│   └── bluepad32/          # Bluepad32 Bluetooth Classic + BLE HID stack (git submodule)
├── common/                 # Shared RP2350 drivers & utilities across Unipad boards
│   └── include/
└── unipad_c64/             # Unipad C64 (Rev. A) target — defaults to PICO_BOARD=pico2_w
    ├── CMakeLists.txt      # Pico SDK 2.x + Bluepad32 + hardware_pio build definition
    ├── pico_sdk_import.cmake
    ├── include/
    │   ├── unipad_c64_pins.h       # Exact RP2350 pinout matching hardware/unipad_c64/pico2w.kicad_sch
    │   ├── unipad_c64_gpio.h       # Atomic 5-bit SIO ports, LEDs, autofire & pushbutton debouncer
    │   ├── unipad_c64_pio.h        # PIO SID 1351 mouse, 1311/1312 paddles, POT buttons & Rumble/Sync
    │   ├── unipad_c64_platform.h   # Bluepad32 custom uni_platform & runtime mode/seat control
    │   └── unipad_c64_console.h    # Non-blocking USB CDC & UART0 interactive CLI
    ├── pio/
    │   └── c64_sid_pot.pio         # RP2350 PIO state machines (c64_sid_pot & c64_sid_sync_measure)
    └── src/
        ├── btstack_config.h        # Dual-mode Classic BR/EDR + BLE + Flash TLV configuration
        ├── sdkconfig.h             # Bluepad32 PICO_W + custom platform configuration
        ├── main.c                  # RP2350 boot sequence & BTstack event loop
        ├── unipad_c64_gpio.c
        ├── unipad_c64_pio.c
        ├── unipad_c64_platform.c
        └── unipad_c64_console.c
```

## Prerequisites

1. **Raspberry Pi Pico SDK (`>= 2.0.0`, tested with `2.3.1` for RP2350 `pico2_w`)**:
   Set `PICO_SDK_PATH` in your shell environment (e.g., `export PICO_SDK_PATH=~/src/rpi/pico-sdk`).
2. **ARM Embedded Toolchain & CMake**:
   ```bash
   brew install cmake ninja arm-none-eabi-gcc
   ```
3. **Submodules Initialized**:
   ```bash
   git submodule update --init --recursive
   ```

## Building `unipad_c64.uf2`

```bash
cmake -S firmware/unipad_c64 -B firmware/unipad_c64/build
cmake --build firmware/unipad_c64/build -j8
```

> **Note:** `PICO_BOARD` defaults automatically to `pico2_w` (`RP2350` + `CYW43439`). The compiled firmware image is generated at `firmware/unipad_c64/build/unipad_c64.uf2`.

## Flashing `.uf2` Firmware to Unipad

1. Hold down the **`BOOTSEL`** button on the Pico 2 W while plugging a Micro-USB cable into your computer (ensure the C64 is **powered OFF** or Unipad is unplugged from the C64 unless `D3` Schottky USB isolation is populated, which is standard on Unipad C64 Rev. A).
2. Release `BOOTSEL` — the board mounts as a USB mass-storage volume named **`RP2350`**.
3. Copy `firmware/unipad_c64/build/unipad_c64.uf2` onto the `RP2350` volume; the Pico 2 W reboots automatically into Unipad C64.
