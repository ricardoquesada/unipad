# Unipad C64 — 3D-Printable Enclosure (Rev. A)

Commodore 64 Breadbin / C64C homage two-piece clamshell enclosure for **Unipad C64 (Rev. A)**, modeled parametrically with [`build123d`](https://github.com/gumyr/build123d) (OpenCASCADE B-Rep kernel) and verified for **`0.000 mm³` 3D boolean interference** against the full KiCad 10 STEP assembly (`67.50 × 65.25 × 15.20 mm` outer envelope).

## Generated CAD & Print Files

| File | Format | Orientation & Purpose |
| :--- | :--- | :--- |
| [`unipad_c64_print_plate.3mf`](unipad_c64_print_plate.3mf) / [`.stl`](unipad_c64_print_plate.stl) | 3MF / STL | **One-click print plate** — Bottom Tray + upside-down Top Lid side-by-side flat on `Z = 0` (**zero supports**). |
| [`unipad_c64_bottom.3mf`](unipad_c64_bottom.3mf) / [`.stl`](unipad_c64_bottom.stl) / [`.step`](unipad_c64_bottom.step) | 3MF / STL / STEP | **Bottom Tray** — flat exterior base on `Z = 0`, recessed M3 counterbores with 2-layer support-free stepped bridges, THT solder-pin relief trenches, and PCB support shelves. |
| [`unipad_c64_top.3mf`](unipad_c64_top.3mf) / [`.stl`](unipad_c64_top.stl) / [`.step`](unipad_c64_top.step) | 3MF / STL / STEP | **Top Lid** — pre-flipped 180° with flat exterior roof on `Z = 0` (**zero supports**), C64C blind cooling louvers, `0.40 mm` debossed legends, anti-bleed LED light-pipe chimneys, `BOOTSEL` paperclip guide tube, front snap-hook tenons, and self-threading M3 bosses. |
| [`unipad_c64_case_assembly.step`](unipad_c64_case_assembly.step) | STEP (AP214) | **Assembled B-Rep CAD model** in exact KiCad PCB world coordinates (`hardware/unipad_c64/unipad_c64.kicad_pcb`) for mechanical CAD inspection. |
| [`unipad_c64_case.py`](unipad_c64_case.py) | Python (`uv`) | Self-contained parametric `build123d` source & automated KiCad 3D STEP collision verifier. |

## Recommended FDM Print Settings

- **Material:** Non-conductive **PLA**, **PETG**, or **ASA** *(classic C64C warm beige / light grey or breadbin taupe recommended; **never** use carbon-fiber `-CF` or metallic filaments near the 2.4 GHz Pico 2 W antenna)*.
- **Nozzle & Layer Height:** `0.40 mm` nozzle, **`0.20 mm` layer height** (debossed legends are `0.40 mm` = 2 exact layers; M3 counterbore stepped bridges are `2 × 0.20 mm` layers).
- **Supports:** **None (`OFF`)** — every overhang, aperture, snap bead, and counterbore is engineered to print 100% support-free.
- **Perimeters & Infill:** `3–4` wall loops, `4–5` top/bottom solid shells, `20–25%` gyroid or grid infill.

## Hardware & Assembly

1. **Required Fasteners:** `2 × M3 × 8 mm` or `M3 × 10 mm` socket-head, button-head, or pan-head screws *(self-threads directly into the `Ø2.72 mm` corner-gusseted Top Lid bosses — no brass heat-set inserts required)*. Optional: `4 × Ø8 mm` stick-on silicone feet aligned to the bottom ring guides.
2. **Note on `J4` Debug Header:** The slim `15.20 mm` enclosure assumes the factory `1×8` vertical logic-analyzer header `J4` is left unpopulated on cased production boards.
3. **Assembly Steps:**
   - Drop the assembled Unipad C64 PCB straight down into the **Bottom Tray** (`SW1`/`SW4`/`SW3` button stems and the Pico 2 W Micro-USB port drop cleanly into their split-line cradles).
   - Tilt the **Top Lid** ~10° forward, hook its three front tenons into the matching 45° V-grooves inside the Bottom Tray's front bulkhead, and pivot the rear of the lid down flush over `H1` and `H2`.
   - Drive the two `M3 × 8 mm` (or `M3 × 10 mm`) screws upward through the recessed bottom counterbores at `H1` and `H2` until snug.

## Regenerating & Auditing Against KiCad 3D STEP

[`unipad_c64_case.py`](unipad_c64_case.py) uses PEP 723 inline dependency metadata so [`uv`](https://docs.astral.sh/uv/) runs it in an isolated cache without polluting the repository with a `.venv`:

```bash
# Regenerate all STEP, STL, and 3MF files in case/unipad_c64/
uv run case/unipad_c64/unipad_c64_case.py

# Export current PCB 3D STEP via KiCad 10 CLI and verify 0.000 mm^3 3D interference
kicad-cli pcb export step --subst-models -o /tmp/unipad_c64_pcb_ref.step hardware/unipad_c64/unipad_c64.kicad_pcb
uv run case/unipad_c64/unipad_c64_case.py --verify-pcb-step /tmp/unipad_c64_pcb_ref.step
rm -f /tmp/unipad_c64_pcb_ref.step
```

## Verified PCB Mechanical Envelope (`hardware/unipad_c64/unipad_c64.kicad_pcb`)

- **PCB Outer Dimensions (`Edge.Cuts`):** `62.50 mm` (width, `X = 82.25..144.75 mm`) × `60.00 mm` (depth, `Y = 90.00..150.00 mm`) × `1.60 mm` thickness (`3.0 mm` corner radius on rear corners).
- **M3 Mounting Holes (`3.2 mm` plated drill):**
  - `H1` (Rear-Left): `4.069 mm` from left PCB edge, `3.860 mm` from rear PCB edge (`X = 86.319, Y = 146.140`).
  - `H2` (Rear-Right): `4.075 mm` from right PCB edge, `3.860 mm` from rear PCB edge (`X = 140.675, Y = 146.140`).
  - Center-to-center `H1`–`H2` pitch: **`54.356 mm`**.
- **Connectors, Buttons & Indicators:**
  - **Front Edge (`Y = 90.00 mm`):** Dual right-angle female DB9 receptacles (`J2` Port #1 at `X = 97.790`, `J3` Port #2 at `X = 129.290`). Keep the enclosure front wall recessed behind the DB9 mating flanges (`Y = 82.66 mm`; enclosure front face sits at `Y = 87.05 mm`) so the adapter seats cleanly inside recessed Breadbin C64, C64C, and C128 port cutouts.
  - **Left Edge (`X = 82.25 mm`):** Right-angle tactile pushbuttons `SW1` (`RESET`, stem `Y = 137.280`), `SW3` (`MODE`, stem `Y = 117.951`), and `SW4` (`SWAP`, stem `Y = 127.628`), plus top-side status LEDs `D1` (`PWR` at `X = 94.955, Y = 148.680`), `D4` (`BT` at `X = 83.581, Y = 101.995`), `D5` (`PORT1` at `X = 83.581, Y = 106.237`), and `D6` (`PORT2` at `X = 83.581, Y = 110.529`).
  - **Right Edge (`X = 144.75 mm`):** Raspberry Pi Pico 2 W (`U1`) Micro-USB port (`Y = 130.500`) for `.uf2` firmware updates and optional USB power.
  - **Top Lid `BOOTSEL` Access:** Recessed `2.5 mm` vertical guide pinhole above the Pico 2 W `BOOTSEL` button (`X = 132.750, Y = 127.000`) so users can enter USB `.uf2` bootloader mode without opening the case.
- **2.4 GHz RF Keep-Out:** The Pico 2 W onboard antenna sits over the internal FR4 cutout (`X = 91.50..102.50 mm`, `Y = 123.70..137.30 mm`). Use standard non-conductive **PLA / PETG / Nylon** — avoid carbon-fiber-filled (`-CF`) or metallic filaments.
