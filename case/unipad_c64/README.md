# Unipad C64 — 3D Enclosure Specifications

Place 3D-printable enclosure CAD sources (`.step`, `.f3d`, `.scad`) and print-ready `.stl` / `.3mf` meshes for **Unipad C64 (Rev. A)** in this directory.

## Verified PCB Mechanical Envelope (`hardware/unipad_c64/unipad_c64.kicad_pcb`)

- **PCB Outer Dimensions (`Edge.Cuts`):** `62.50 mm` (width, `X = 82.25..144.75 mm`) × `60.00 mm` (depth, `Y = 90.00..150.00 mm`) × `1.60 mm` thickness (`3.0 mm` corner radius on rear corners).
- **M3 Mounting Holes (`3.2 mm` plated drill):**
  - `H1` (Rear-Left): `4.069 mm` from left PCB edge, `3.860 mm` from rear PCB edge (`X = 86.319, Y = 146.140`).
  - `H2` (Rear-Right): `4.075 mm` from right PCB edge, `3.860 mm` from rear PCB edge (`X = 140.675, Y = 146.140`).
  - Center-to-center `H1`–`H2` pitch: **`54.356 mm`**.
- **Connectors, Buttons & Indicators:**
  - **Front Edge (`Y = 90.00 mm`):** Dual right-angle female DB9 receptacles (`J2` Port #1, `J3` Port #2). Keep the enclosure front wall recessed behind the DB9 mating flanges so the adapter seats cleanly inside recessed Breadbin C64, C64C, and C128 port cutouts.
  - **Left Edge (`X = 82.25 mm`):** Right-angle tactile pushbuttons `SW1` (`RESET`), `SW3` (`MODE`), and `SW4` (`SWAP`), plus top-side status LEDs `D1` (`PWR`), `D4` (`BT`), `D5` (`PORT1`), and `D6` (`PORT2`).
  - **Right Edge (`X = 144.75 mm`):** Raspberry Pi Pico 2 W (`U1`) Micro-USB port for `.uf2` firmware updates and optional USB power.
  - **Top Lid `BOOTSEL` Access:** Include a cantilever flex plunger or `2.5 mm` pinhole above the Pico 2 W `BOOTSEL` button so users can enter USB `.uf2` bootloader mode without opening the case.
- **2.4 GHz RF Keep-Out:** The Pico 2 W onboard antenna sits over the internal FR4 cutout (`X = 91.50..102.50 mm`, `Y = 123.70..137.30 mm`). Use standard non-conductive **PLA / PETG / Nylon** — avoid carbon-fiber-filled (`-CF`) or metallic filaments.
