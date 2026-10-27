# Unipad™

[![Build Firmware](https://github.com/ricardoquesada/unipad/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/ricardoquesada/unipad/actions/workflows/build-firmware.yml)

Wireless Bluetooth Gamepad, Paddle & Mouse adapters for vintage 8-bit and 16-bit computers — by the creator of [Unijoysticle](https://github.com/ricardoquesada/unijoysticle2) and [Bluepad32](https://github.com/ricardoquesada/bluepad32).

## Models

| Model | Target Systems | Wireless Inputs | Power Draw | Hardware Folder | Firmware Folder |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Unipad C64** *(Rev. A)* | Commodore 64 / 128 | Bluetooth Gamepads, **1351 Analog Mouse**, **1311/1312 Paddles**, Rumble | `< 45 mA` (CIA/SID-protected) | [`hardware/unipad_c64/`](hardware/unipad_c64/) | [`firmware/unipad_c64/`](firmware/unipad_c64/) |

## Repository Structure

This monorepo houses the complete hardware, firmware, 3D enclosure, and documentation stack for the **Unipad** product family:

```text
unipad/
├── hardware/               # KiCad 10 schematics, 4-layer PCBs, shared libraries & JLCPCB bundles
│   ├── libs/Unipad.pretty/ # Shared KiCad footprints across all Unipad boards
│   └── unipad_c64/         # Unipad C64 (Rev. A) KiCad 10 project & JLCPCB Gerbers/BOM/CPL
├── firmware/               # RP2350 (Pico 2 W) firmware + PIO state machines
│   ├── external/           # Upstream git submodules (e.g. bluepad32)
│   ├── common/             # Shared Unipad RP2350 drivers & helpers
│   └── unipad_c64/         # Unipad C64 platform target, GPIO map & PIO programs
├── case/                   # 3D-printable enclosures (STEP / STL)
│   └── unipad_c64/         # Unipad C64 enclosure specs & CAD files
└── docs/                   # User guides & exported schematic PDFs
    └── schematics/         # Exported PDF schematics (e.g. schematic_unipad_c64.pdf)
```

## Key Highlights (Unipad C64)

- **3-in-1 C64 Control Port Support:** Use modern Bluetooth gamepads (DualSense, Xbox, Switch Pro, 8BitDo, etc.), Bluetooth mice (**Commodore 1351 analog mode**), and analog sticks/controllers as **Commodore 1311/1312 Paddles** across both Control Ports.
- **Deterministic RP2350 PIO Timing:** Dedicated 150 MHz Programmable I/O (PIO) hardware state machines synchronize directly to the C64 SID `POTX`/`POTY` discharge cycle with zero CPU interrupt jitter and automatic PAL/NTSC clock calibration.
- **Low-Power & Vintage-Safe:** Draws `< 45 mA` from the C64 `+5V` rail (less than half of ESP32-based adapters), protected by a `250 mA` resettable PTC polyfuse, ferrite Pi-filter, zero-drop `DMG2305UX` P-FET ideal diode (preventing USB `VBUS` back-feeding into the C64), `74HCT05` open-drain buffers, and 5V fail-safe SID `SYNC` dividers.
- **5-Second Drag-and-Drop Updates:** Hold `BOOTSEL`, plug into USB, and drop a `.uf2` firmware file onto the drive — no serial drivers required.

## License

All hardware designs (`hardware/`), 3D enclosures (`case/`), and documentation (`docs/`) are licensed under **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International ([CC BY-NC-SA 4.0](LICENSE))**. Third-party submodules under `firmware/external/` retain their respective upstream licenses.
