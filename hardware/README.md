# Unipad™ Hardware

This directory contains the KiCad 10 hardware schematics, PCB layouts, shared footprint libraries, and turnkey JLCPCB fabrication packages for the **Unipad** product family.

## Directory Layout

- [`libs/Unipad.pretty/`](libs/Unipad.pretty/) — Shared custom KiCad footprints referenced by all Unipad boards via `${KIPRJMOD}/../libs/Unipad.pretty` (prevents duplicating `.pretty` libraries across board variants).
- [`unipad_c64/`](unipad_c64/) — **Unipad C64 (Rev. A)**: 4-layer Commodore 64 / 128 wireless gamepad, 1351 analog mouse, and 1311/1312 paddle adapter powered by Raspberry Pi Pico 2 W (`RP2350` + `CYW43439`).

## Design Tooling & Verification

All projects in `hardware/` use **KiCad 10.0+**. Before generating fabrication outputs, verify zero ERC and DRC errors from the command line:

```bash
kicad-cli sch erc --severity-error --exit-code-violations hardware/unipad_c64/unipad_c64.kicad_sch
kicad-cli pcb drc --schematic-parity --severity-error --exit-code-violations hardware/unipad_c64/unipad_c64.kicad_pcb
```

## Unipad `BOARD_ID[2:0]` PCB Strapping Standard (`GP26..GP28`)

Every Unipad carrier board reserves Raspberry Pi Pico 2 W Pins **31 (`GP26`)**, **32 (`GP27`)**, and **34 (`GP28`)** exclusively as a 3-bit hardware board-identification bus **`BOARD_ID[2:0] = {GP28, GP27, GP26}`**:

| `BOARD_ID[2:0]` | `JP3` (`ID2` / `GP28`) | `JP2` (`ID1` / `GP27`) | `JP1` (`ID0` / `GP26`) | Assigned Carrier Board |
| :---: | :---: | :---: | :---: | :--- |
| `0b111` (`0x7`) | `OPEN` (`1`) | `OPEN` (`1`) | `OPEN` (`1`) | **Unipad C64 (Rev. A+)** *(100% compatible with unstrapped PCBs)* |
| `0b110` (`0x6`) | `OPEN` (`1`) | `OPEN` (`1`) | `GND` (`0`) | *Reserved — Future Unipad Variant (Slot 6)* |
| `0b101` (`0x5`) | `OPEN` (`1`) | `GND` (`0`) | `OPEN` (`1`) | *Reserved — Future Unipad Variant (Slot 5)* |
| `0b100` (`0x4`) | `OPEN` (`1`) | `GND` (`0`) | `GND` (`0`) | *Reserved — Future Unipad Variant (Slot 4)* |
| `0b011` (`0x3`) | `GND` (`0`) | `OPEN` (`1`) | `OPEN` (`1`) | *Reserved — Future Unipad Variant (Slot 3)* |
| `0b010` (`0x2`) | `GND` (`0`) | `OPEN` (`1`) | `GND` (`0`) | *Reserved — Future Unipad Variant (Slot 2)* |
| `0b001` (`0x1`) | `GND` (`0`) | `GND` (`0`) | `OPEN` (`1`) | *Reserved — Future Unipad Variant (Slot 1)* |
| `0b000` (`0x0`) | `GND` (`0`) | `GND` (`0`) | `GND` (`0`) | *Reserved — Bench / Bring-up / Custom Prototype* |

### Mandatory Electrical Rules for Future Carrier PCBs

1. **3.3V-Only Domain (`GND` Straps Only)**: `GP26..GP28` (`ADC0..ADC2`) are the only RP2350 GPIOs with an internal reverse ESD diode to `IOVDD` (`3.3V`). Never connect `GP26..GP28` to 5V retro-computer lines and never hard-tie them directly to `+3V3` without series resistance.
2. **Zero-BOM Copper Solder Jumpers**: Use `Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm` for `OPEN` (`1`) bits and `Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm` for factory-strapped `GND` (`0`) bits (`$0.00` BOM and pick-and-place cost).
