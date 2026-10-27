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
