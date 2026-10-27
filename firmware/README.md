# Unipad™ Firmware

This directory contains the board-specific RP2350 (Raspberry Pi Pico 2 W) firmware, Programmable I/O (PIO) hardware state machines, and [Bluepad32](https://github.com/ricardoquesada/bluepad32) platform integration for **Unipad** adapters.

## Directory Layout

```text
firmware/
├── external/               # Third-party / upstream git submodules (isolated from 1st-party code)
│   └── bluepad32/          # Bluepad32 Bluetooth gamepad/mouse stack (git submodule)
├── common/                 # Shared RP2350 drivers & utilities across Unipad boards
│   └── include/
└── unipad_c64/             # Unipad C64 target (PICO_BOARD=pico2_w)
    ├── include/            # Pinout definitions matching hardware/unipad_c64/pico2w.kicad_sch
    ├── pio/                # RP2350 .pio state machine programs (SID 1351 mouse, paddles, clock cal)
    └── src/                # Bluepad32 custom uni_platform implementation for Unipad C64
```

## Adding / Initializing the Bluepad32 Submodule

To register `bluepad32` inside `firmware/external/bluepad32` for the first time:

```bash
git submodule add https://github.com/ricardoquesada/bluepad32.git firmware/external/bluepad32
git submodule update --init --recursive
```

When cloning `unipad` on a new machine:

```bash
git clone --recurse-submodules git@github.com:ricardoquesada/unipad.git
```

## Flashing `.uf2` Firmware to Unipad

1. Hold down the **`BOOTSEL`** button on the Pico 2 W while plugging a Micro-USB cable into your computer.
2. Release `BOOTSEL` — the board mounts as a USB mass-storage drive named **`RP2350`**.
3. Drag and drop the compiled `unipad_c64.uf2` binary onto the `RP2350` drive; the board reboots automatically into the new firmware.
