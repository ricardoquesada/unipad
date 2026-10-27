# Unipad™ C64 (Rev. A)

**Unipad C64** is a low-power (`< 45 mA`), CIA/SID-protected wireless Bluetooth adapter for the Commodore 64 and Commodore 128, supporting modern Bluetooth **Gamepads**, **Commodore 1351 Analog Mice**, **Commodore 1311/1312 Paddles**, and **Rumble**.

## Schematic

* [Unipad C64 Schematic (PDF)](../../docs/schematics/schematic_unipad_c64.pdf)

## Layout & Fabrication

* Schematic and 4-layer PCB layout files are in **KiCad 10** format in this folder (`F.Cu`, `In1.Cu` GND, `In2.Cu` Power, `B.Cu`).
* Shared custom footprints resolve from [`../libs/Unipad.pretty/`](../libs/Unipad.pretty/).
* Turnkey JLCPCB 4-layer Gerbers, Excellon drill files, BOM, and Pick-and-Place CPL are located in [`gerber_jlcpcb_rev_a/`](gerber_jlcpcb_rev_a/).
* **Hardware Board ID (`BOARD_ID[2:0] = 0b111`)**: Three zero-BOM open copper solder jumpers **`JP1` (`ID0` / `GP26`)**, **`JP2` (`ID1` / `GP27`)**, and **`JP3` (`ID2` / `GP28`)** are placed on `B.Cu` beneath `U1` Pins 31–34 (excluded from BOM and CPL; left `OPEN` by default for `Unipad C64`).

## Bill of Materials (BOM)

| Item | Qty | Reference(s) | Value | LibPart | Footprint | LCSC |
| ---- | --- | ------------ | ----- | ------- | --------- | ---- |
| 1 | 7 | C1, C4, C8, C10, C11, C12, C13 | .1uF | Device:C_Small | Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder | C49678 |
| 2 | 3 | C5, C6, C9 | 10uF | Device:C_Small | Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder | C15850 |
| 3 | 1 | C7 | 47uF | Device:C_Small | Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder | C124129 |
| 4 | 1 | D1 | PWR | Device:LED | LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder | C2297 |
| 5 | 1 | D4 | BLUE | Device:LED | LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder | C84259 |
| 6 | 1 | D5 | GREEN | Device:LED | LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder | C2297 |
| 7 | 1 | D6 | RED | Device:LED | LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder | C84256 |
| 8 | 1 | F1 | 250mA_PTC | Device:Polyfuse_Small | Fuse:Fuse_0805_2012Metric | C20608679 |
| 9 | 1 | FB1 | 600R@100MHz | Device:FerriteBead_Small | Inductor_SMD:L_0805_2012Metric | C1017 |
| 10 | 2 | H1, H2 | MountingHole_Pad | Mechanical:MountingHole_Pad | MountingHole:MountingHole_3.2mm_M3_Pad_Via | ~ |
| 11 | 1 | J2 | PORT #1 | Connector:DB9_Female | Connector_Dsub:DSUB-9_Socket_Horizontal_P2.77x2.54mm_EdgePinOffset9.40mm | ~ |
| 12 | 1 | J3 | PORT #2 | Connector:DB9_Female | Connector_Dsub:DSUB-9_Socket_Horizontal_P2.77x2.54mm_EdgePinOffset9.40mm | ~ |
| 13 | 1 | J4 | DEBUG_LA_HDR | Connector_Generic:Conn_01x08 | Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical | ~ |
| 14 | 1 | Q1 | DMG2305UX | Transistor_FET:TP0610T | Package_TO_SOT_SMD:SOT-23 | C144153 |
| 15 | 9 | R1, R10, R11, R13, R14, R16, R17, R19, R22 | 10K | Device:R | Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder | C17414 |
| 16 | 4 | R2, R7, R8, R9 | 3K3 | Device:R | Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder | C26010 |
| 17 | 1 | R3 | 100K | Device:R | Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder | C149504 |
| 18 | 2 | R20, R23 | 22K | Device:R | Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder | C17560 |
| 19 | 1 | SW1 | RESET | Switch:SW_Push | Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx39-2LFS | ~ |
| 20 | 1 | SW3 | MODE | Switch:SW_Push | Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx39-2LFS | ~ |
| 21 | 1 | SW4 | SWAP | Switch:SW_Push | Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx39-2LFS | ~ |
| 22 | 1 | U1 | Raspberry_Pi_Pico_2_W | Pico_1 | Unipad:RPi_Pico_2_W_SMD_TH | ~ |
| 23 | 3 | U4, U5, U6 | 74HCT05 | 74xx:74LS05 | Package_SO:SOIC-14_3.9x8.7mm_P1.27mm | C49188197 |

## License

Licensed under [CC BY-NC-SA 4.0](../../LICENSE).
