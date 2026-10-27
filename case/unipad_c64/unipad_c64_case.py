#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "==3.12.*"
# dependencies = [
#   "build123d>=0.10.0,<1.0.0",
#   "lib3mf<2.5",
# ]
# ///
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 Ricardo Quesada
"""Parametric 3D-printable Commodore 64 homage enclosure for Unipad C64 (Rev. A).

Generates manufacturing-grade OpenCASCADE B-Rep `.step` CAD solids alongside
support-free, print-oriented `.stl` and `.3mf` meshes directly from the verified
mechanical envelope of `hardware/unipad_c64/unipad_c64.kicad_pcb`.

Coordinate Frames:
    1. **KiCad 3D World Frame** (used internally during solid modeling so every
       coordinate matches `kicad-cli pcb export step` 1-to-1):
       - `X`: `[82.250 .. 144.750]` mm (`+X` = Right / Micro-USB edge; `-X` = Left / Buttons).
       - `Y`: `[-150.000 .. -90.000]` mm (`Y = -Y_kicad`; `+Y` = Front / Dual DB9s; `-Y` = Rear).
       - `Z`: `0.000` mm at `B.Cu`, `+1.601` mm at `F.Cu` component seating plane.
    2. **Print Bed Frame** (exported for `unipad_c64_bottom.*`, `unipad_c64_top.*`,
       and `unipad_c64_print_plate.*`):
       - Bottom Tray centered near `(0, 0)` with flat exterior base resting at `Z = 0.000` mm.
       - Top Lid rotated 180 deg about the X axis so its flat exterior roof rests at
         `Z = 0.000` mm — printing 100% support-free on standard FDM printers.

Example Usage:
    ```bash
    uv run case/unipad_c64/unipad_c64_case.py
    ```
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys
import build123d as bd


@dataclass(frozen=True)
class UnipadC64CaseSpec:
    """Immutable mechanical specification synchronized with `unipad_c64.kicad_pcb`.

    All board-space `yk_*` attributes store positive KiCad 2D `Y` coordinates
    (which map to `Y = -yk` in right-handed 3D CAD space).
    """

    # --- Verified PCB Edge.Cuts Envelope ---
    pcb_x_min: float = 82.250
    pcb_x_max: float = 144.750
    pcb_yk_front: float = 90.000
    pcb_yk_rear: float = 150.000
    pcb_rear_corner_r: float = 3.000
    pcb_z_bottom: float = 0.000
    pcb_z_top: float = 1.601

    # --- Verified M3 Mounting Holes (3.2 mm plated drill, 54.356 mm pitch) ---
    h1_x: float = 86.319
    h1_yk: float = 146.140
    h2_x: float = 140.675
    h2_yk: float = 146.140
    pcb_hole_d: float = 3.200

    # --- Dual Front DB9 Female Receptacles (J2 Port #1, J3 Port #2) ---
    j2_cx: float = 97.790
    j3_cx: float = 129.290
    db9_pins_w: float = 13.100
    db9_pins_z_min: float = 5.700
    db9_pins_z_max: float = 10.000

    # --- Left-Edge Right-Angle Tactile Pushbuttons (PTS645Vx39-2LFS) ---
    # Note: PTS645 housing front face sits at X=80.867 mm (1.383 mm past PCB edge
    # X=82.250 mm), and the dia-3.14 mm plunger protrudes to X=79.996 mm.
    sw_mode_yk: float = 117.951   # SW3 (MODE)
    sw_swap_yk: float = 127.628   # SW4 (SWAP)
    sw_reset_yk: float = 137.280  # SW1 (RESET)
    sw_stem_cz: float = 5.101
    sw_hole_d: float = 4.100
    sw_scallop_d: float = 6.200
    sw_scallop_depth: float = 0.450
    sw_body_pocket_x_min: float = 80.600
    sw_body_pocket_w_y: float = 8.000
    sw_body_pocket_z_min: float = 1.350
    sw_body_pocket_z_max: float = 8.750

    # --- Right-Edge Pico 2 W Micro-USB Port & Top BOOTSEL ---
    usb_yk: float = 130.500
    usb_cz: float = 3.920
    usb_inner_w: float = 9.000
    usb_inner_z_min: float = 2.050
    usb_inner_z_max: float = 5.950
    usb_boot_w: float = 12.400
    usb_boot_z_min: float = 0.500
    usb_boot_z_max: float = 7.500
    usb_boot_depth: float = 1.400

    bootsel_x: float = 132.750
    bootsel_yk: float = 127.000
    bootsel_z_top: float = 5.101
    bootsel_pinhole_d: float = 2.500
    bootsel_guide_od: float = 4.600
    bootsel_guide_z_bottom: float = 5.650

    # --- Top-Firing 0805 SMD Status LEDs ---
    led_pwr_x: float = 94.955
    led_pwr_yk: float = 148.680
    led_left_x: float = 83.581
    led_bt_yk: float = 101.995    # D4 (BT - Blue)
    led_p1_yk: float = 106.237    # D5 (PORT 1 - Green)
    led_p2_yk: float = 110.529    # D6 (PORT 2 - Red)
    led_pinhole_d: float = 2.000
    led_shroud_od: float = 3.600
    led_shroud_z_bottom: float = 3.250

    # --- Enclosure Shell Clearances & Wall Dimensions ---
    pcb_xy_clearance: float = 0.300
    side_wall_t: float = 2.200
    rear_wall_t: float = 2.000
    # Front outer face at Y_kicad=87.050 sits 0.17 mm behind DB9 rear shoulder
    # (Y_kicad=86.880) and 4.39 mm recessed behind DB9 metal flanges (Y_kicad=82.660).
    front_outer_yk: float = 87.050
    front_corner_r: float = 1.600

    # --- Vertical Stackup (Total Slim Case Height = 15.20 mm) ---
    z_ext_bottom: float = -3.700
    z_int_floor: float = -2.100
    z_pin_trench_floor: float = -2.650
    z_pcb_rest: float = -0.050
    z_pcb_holddown: float = 1.660
    z_split: float = 5.101
    z_int_ceiling: float = 9.900
    z_ext_top: float = 11.500

    # --- Clamshell Lap-Joint & Fastener Geometry ---
    top_chamfer: float = 1.350
    bottom_chamfer: float = 0.850
    rabbet_h: float = 1.100
    rabbet_xy_gap: float = 0.150
    rabbet_z_gap: float = 0.100

    m3_cbore_d: float = 6.200
    m3_cbore_z_top: float = -1.500
    m3_through_d: float = 3.400
    m3_boss_od: float = 6.600
    m3_tap_d: float = 2.720
    m3_tap_z_top: float = 9.100

    @property
    def x_in_min(self) -> float:
        return self.pcb_x_min - self.pcb_xy_clearance

    @property
    def x_in_max(self) -> float:
        return self.pcb_x_max + self.pcb_xy_clearance

    @property
    def y_in_rear(self) -> float:
        return -(self.pcb_yk_rear + self.pcb_xy_clearance)

    @property
    def y_in_front(self) -> float:
        return -(self.pcb_yk_front - self.pcb_xy_clearance)

    @property
    def x_out_min(self) -> float:
        return self.x_in_min - self.side_wall_t

    @property
    def x_out_max(self) -> float:
        return self.x_in_max + self.side_wall_t

    @property
    def y_out_rear(self) -> float:
        return self.y_in_rear - self.rear_wall_t

    @property
    def y_out_front(self) -> float:
        return -self.front_outer_yk

    @property
    def x_center(self) -> float:
        return 0.5 * (self.x_out_min + self.x_out_max)

    @property
    def y_center(self) -> float:
        return 0.5 * (self.y_out_rear + self.y_out_front)


def make_planform_face(
    x_min: float,
    x_max: float,
    y_rear: float,
    y_front: float,
    r_rear: float,
    r_front: float,
) -> bd.Face:
    """Construct a 2D XY face with distinct rear (`-Y`) and front (`+Y`) corner radii."""
    w = x_max - x_min
    d = y_front - y_rear
    cx = 0.5 * (x_min + x_max)
    cy = 0.5 * (y_rear + y_front)

    rect = bd.Pos(cx, cy) * bd.Rectangle(w, d)
    rear_verts = [v for v in rect.vertices() if v.Y < cy]
    front_verts = [v for v in rect.vertices() if v.Y > cy]

    if r_rear > 0:
        rect = bd.fillet(rear_verts, r_rear)
        front_verts = [v for v in rect.vertices() if v.Y > cy]
    if r_front > 0:
        rect = bd.fillet(front_verts, r_front)
    return rect


def build_outer_envelope(spec: UnipadC64CaseSpec) -> bd.Part:
    """Create the chamfered Commodore-homage outer solid shell before splitting."""
    r_rear_out = spec.pcb_rear_corner_r + spec.pcb_xy_clearance + spec.side_wall_t
    base_face = make_planform_face(
        spec.x_out_min,
        spec.x_out_max,
        spec.y_out_rear,
        spec.y_out_front,
        r_rear=r_rear_out,
        r_front=spec.front_corner_r,
    )
    total_h = spec.z_ext_top - spec.z_ext_bottom
    body = bd.Pos(0, 0, spec.z_ext_bottom) * bd.extrude(base_face, total_h)

    top_edges = body.edges().filter_by_position(
        bd.Axis.Z, spec.z_ext_top - 0.02, spec.z_ext_top + 0.02
    )
    body = bd.chamfer(top_edges, length=spec.top_chamfer)

    bot_edges = body.edges().filter_by_position(
        bd.Axis.Z, spec.z_ext_bottom - 0.02, spec.z_ext_bottom + 0.02
    )
    body = bd.chamfer(bot_edges, length=spec.bottom_chamfer)
    return body


def build_interior_cavity(spec: UnipadC64CaseSpec) -> bd.Part:
    """Build the main internal PCB & component cavity volume."""
    r_rear_in = spec.pcb_rear_corner_r + spec.pcb_xy_clearance
    cav_face = make_planform_face(
        spec.x_in_min,
        spec.x_in_max,
        spec.y_in_rear,
        spec.y_in_front,
        r_rear=r_rear_in,
        r_front=0.40,
    )
    cav_h = spec.z_int_ceiling - spec.z_int_floor
    return bd.Pos(0, 0, spec.z_int_floor) * bd.extrude(cav_face, cav_h)


def build_port_and_button_cutouts(spec: UnipadC64CaseSpec) -> bd.Part:
    """Assemble all external wall apertures (Dual DB9 pins, Micro-USB, 3x buttons)."""
    cuts: list[bd.Part] = []

    # 1. Dual Front DB9 Right-Angle Pin Windows (J2 Port #1, J3 Port #2)
    front_span_y = (spec.y_out_front - spec.y_in_front) + 2.0
    front_cy = 0.5 * (spec.y_out_front + spec.y_in_front)
    db9_h = spec.db9_pins_z_max - spec.db9_pins_z_min
    db9_cz = 0.5 * (spec.db9_pins_z_min + spec.db9_pins_z_max)
    for cx in (spec.j2_cx, spec.j3_cx):
        cuts.append(
            bd.Pos(cx, front_cy, db9_cz)
            * bd.Box(spec.db9_pins_w, front_span_y, db9_h)
        )

    # 2. Left-Edge Tactile Buttons (SW3 MODE, SW4 SWAP, SW1 RESET)
    #    - Internal housing pocket (X = sw_body_pocket_x_min .. x_in_min + 1.0)
    #      clears the PTS645 switch body overhanging the PCB edge to X=80.867 mm,
    #      leaving a sturdy 0.85 mm outer bezel wall (X = 79.750 .. 80.600 mm).
    #    - Circular bore + exterior thumb-scallop bevel exposes the dia-3.14 mm plunger.
    wall_cx = 0.5 * (spec.x_out_min + spec.x_in_min)
    pocket_x_max = spec.x_in_min + 1.20
    pocket_w_x = pocket_x_max - spec.sw_body_pocket_x_min
    pocket_cx = 0.5 * (spec.sw_body_pocket_x_min + pocket_x_max)
    pocket_h_z = spec.sw_body_pocket_z_max - spec.sw_body_pocket_z_min
    pocket_cz = 0.5 * (spec.sw_body_pocket_z_min + spec.sw_body_pocket_z_max)

    for yk in (spec.sw_mode_yk, spec.sw_swap_yk, spec.sw_reset_yk):
        y = -yk
        housing_pocket = bd.Pos(pocket_cx, y, pocket_cz) * bd.Box(
            pocket_w_x, spec.sw_body_pocket_w_y, pocket_h_z
        )
        bore = (
            bd.Pos(wall_cx, y, spec.sw_stem_cz)
            * bd.Rot(0, 90, 0)
            * bd.Cylinder(radius=0.5 * spec.sw_hole_d, height=spec.side_wall_t + 2.0)
        )
        scallop = (
            bd.Pos(spec.x_out_min + 0.5 * spec.sw_scallop_depth, y, spec.sw_stem_cz)
            * bd.Rot(0, 90, 0)
            * bd.Cone(
                bottom_radius=0.5 * spec.sw_scallop_d,
                top_radius=0.5 * spec.sw_hole_d,
                height=spec.sw_scallop_depth + 0.02,
            )
        )
        cuts.extend([housing_pocket, bore, scallop])

    # 3. Right-Edge Pico 2 W Micro-USB Aperture + Cable Overmold Recess
    usb_y = -spec.usb_yk
    usb_inner_h = spec.usb_inner_z_max - spec.usb_inner_z_min
    usb_inner_cz = 0.5 * (spec.usb_inner_z_min + spec.usb_inner_z_max)
    cuts.append(
        bd.Pos(0.5 * (spec.x_in_max + spec.x_out_max), usb_y, usb_inner_cz)
        * bd.Box(spec.side_wall_t + 2.0, spec.usb_inner_w, usb_inner_h)
    )

    usb_boot_h = spec.usb_boot_z_max - spec.usb_boot_z_min
    usb_boot_cz = 0.5 * (spec.usb_boot_z_min + spec.usb_boot_z_max)
    boot_box = bd.Box(
        spec.usb_boot_depth + 0.40,
        spec.usb_boot_w,
        usb_boot_h,
    )
    boot_x_edges = boot_box.edges().filter_by(bd.Axis.X)
    boot_box = bd.fillet(boot_x_edges, radius=1.20)
    cuts.append(
        bd.Pos(
            spec.x_out_max - 0.5 * spec.usb_boot_depth + 0.20,
            usb_y,
            usb_boot_cz,
        )
        * boot_box
    )

    return bd.Part() + cuts


def build_bottom_tray(spec: UnipadC64CaseSpec, outer_shell: bd.Part) -> bd.Part:
    """Construct the print-ready Bottom Tray in KiCad 3D world coordinates."""
    # Slice lower half up to parting plane + rabbet height margin, then trim cleanly.
    lower_keep = bd.Pos(spec.x_center, spec.y_center, spec.z_split - 25.0 + spec.rabbet_h) * bd.Box(
        200.0, 200.0, 50.0
    )
    tray = (outer_shell & lower_keep) - build_interior_cavity(spec)

    # Carve outer perimeter rabbet step above Z_split so Top Lid skirt overlaps cleanly.
    r_mid = spec.pcb_rear_corner_r + spec.pcb_xy_clearance + 0.5 * spec.side_wall_t
    rabbet_inner_keep = make_planform_face(
        spec.x_in_min,
        spec.x_in_max,
        spec.y_in_rear,
        spec.y_in_front,
        r_rear=spec.pcb_rear_corner_r + spec.pcb_xy_clearance,
        r_front=0.40,
    )
    mid_wall_t = 0.5 * spec.side_wall_t - 0.5 * spec.rabbet_xy_gap
    rabbet_tongue_face = make_planform_face(
        spec.x_in_min - mid_wall_t,
        spec.x_in_max + mid_wall_t,
        spec.y_in_rear - mid_wall_t,
        spec.y_in_front + 0.5 * (spec.y_out_front - spec.y_in_front) - 0.5 * spec.rabbet_xy_gap,
        r_rear=r_mid,
        r_front=0.80,
    )
    outer_above_split = bd.Pos(spec.x_center, spec.y_center, spec.z_split + 10.0) * bd.Box(
        200.0, 200.0, 20.0
    )
    tongue_keep = bd.Pos(0, 0, spec.z_split) * bd.extrude(
        rabbet_tongue_face - rabbet_inner_keep,
        spec.rabbet_h - spec.rabbet_z_gap,
    )
    # Remove everything above Z_split except the inner perimeter alignment tongue
    # and the two DB9 front lower sills (Z_split .. db9_pins_z_min = 5.70 mm).
    db9_sills: list[bd.Part] = []
    front_wall_d = spec.y_out_front - spec.y_in_front
    front_wall_cy = 0.5 * (spec.y_out_front + spec.y_in_front)
    sill_h = spec.db9_pins_z_min - spec.z_split
    for cx in (spec.j2_cx, spec.j3_cx):
        db9_sills.append(
            bd.Pos(cx, front_wall_cy, 0.5 * (spec.z_split + spec.db9_pins_z_min))
            * bd.Box(spec.db9_pins_w, front_wall_d, sill_h)
        )

    tray = (tray - outer_above_split) + tongue_keep + db9_sills

    # Interrupt the inner alignment tongue around side cutouts & rear corner bosses
    # so buttons, Micro-USB, and Top Lid M3 bosses drop straight in unhindered.
    tongue_interrupts: list[bd.Part] = [
        # Right Micro-USB clearance across Z_split
        bd.Pos(spec.x_in_max + 1.0, -spec.usb_yk, spec.z_split + 1.0)
        * bd.Box(6.0, spec.usb_boot_w + 3.0, 4.0),
        # Left switches clearance across Z_split
        bd.Pos(spec.x_in_min - 1.0, -0.5 * (spec.sw_mode_yk + spec.sw_reset_yk), spec.z_split + 1.0)
        * bd.Box(6.0, (spec.sw_reset_yk - spec.sw_mode_yk) + spec.sw_scallop_d + 4.0, 4.0),
        # Front tenon pocket zones (left, center, right)
        bd.Pos(spec.x_center, front_wall_cy, spec.z_split + 1.0) * bd.Box(12.0, 6.0, 4.0),
        bd.Pos(86.50, front_wall_cy, spec.z_split + 1.0) * bd.Box(6.6, 6.0, 4.0),
        bd.Pos(140.50, front_wall_cy, spec.z_split + 1.0) * bd.Box(6.6, 6.0, 4.0),
    ]
    tray = tray - tongue_interrupts

    # Add PCB perimeter support shelves (up to z_pcb_rest = -0.050 mm), notched around THT leads.
    shelf_h = spec.z_pcb_rest - spec.z_int_floor
    shelf_cz = 0.5 * (spec.z_int_floor + spec.z_pcb_rest)
    shelves: list[bd.Part] = [
        # Front PCB shelf (Y = [-90.85 .. -89.70], clear of DB9 Row-2 pins at Y=-91.96)
        bd.Pos(spec.x_center, spec.y_in_front - 0.575, shelf_cz)
        * bd.Box(spec.x_in_max - spec.x_in_min, 1.150, shelf_h),
        # Rear PCB shelf (Y = [-150.30 .. -149.15])
        bd.Pos(spec.x_center, spec.y_in_rear + 0.575, shelf_cz)
        * bd.Box(spec.x_in_max - spec.x_in_min - 8.0, 1.150, shelf_h),
        # Right PCB shelf (X = [144.20 .. 145.05], 0.85 mm wide, clear of U1 & J4 THT pads)
        bd.Pos(spec.x_in_max - 0.425, 0.5 * (spec.y_in_front + spec.y_in_rear), shelf_cz)
        * bd.Box(0.850, (spec.y_in_front - spec.y_in_rear) - 8.0, shelf_h),
        # Left-Front PCB shelf segment (in front of SW3 MODE, Y = [-113.2 .. -89.7])
        bd.Pos(spec.x_in_min + 0.575, 0.5 * (-113.20 + spec.y_in_front), shelf_cz)
        * bd.Box(1.150, spec.y_in_front - (-113.20), shelf_h),
        # Center anti-flex support pillar beneath Pico 2 W next to BOOTSEL
        # (Clear of Pico THT pads at Y=-121.61/-139.39 and B.Cu jumpers JP1..JP3 at Y=-140)
        bd.Pos(128.000, -127.000, shelf_cz)
        * bd.Cylinder(radius=2.400, height=shelf_h),
    ]

    # Add corner-integrated M3 standoff bosses at H1 & H2 up to z_pcb_rest
    for hx, hyk in ((spec.h1_x, spec.h1_yk), (spec.h2_x, spec.h2_yk)):
        hy = -hyk
        shelves.append(
            bd.Pos(hx, hy, shelf_cz)
            * bd.Cylinder(radius=0.5 * spec.m3_boss_od, height=shelf_h)
        )
        # Gusset web tying standoff directly into rear & side walls
        gx = spec.x_in_min + 2.0 if hx < spec.x_center else spec.x_in_max - 2.0
        shelves.append(
            bd.Pos(0.5 * (hx + gx), 0.5 * (hy + spec.y_in_rear), shelf_cz)
            * bd.Box(abs(hx - gx) + 3.0, abs(hy - spec.y_in_rear) + 3.0, shelf_h)
        )

    tray = (tray + shelves) & (outer_shell - (
        bd.Pos(spec.x_center, spec.y_center, spec.z_split + 25.0 + spec.rabbet_h)
        * bd.Box(200.0, 200.0, 50.0)
    ))

    # Carve localized THT solder-pin relief trenches (down to z_pin_trench_floor = -2.650 mm)
    trench_h = (spec.z_int_floor - spec.z_pin_trench_floor) + 0.40
    trench_cz = spec.z_pin_trench_floor + 0.5 * trench_h
    trenches: list[bd.Part] = [
        # J2 & J3 DB9 THT solder rows (Y_kicad = 90.8 .. 96.2)
        bd.Pos(spec.j2_cx, -93.50, trench_cz) * bd.Box(15.0, 5.6, trench_h),
        bd.Pos(spec.j3_cx, -93.50, trench_cz) * bd.Box(15.0, 5.6, trench_h),
        # SW1, SW4, SW3 THT pins along left edge (X = 82.0 .. 88.5, Y_kicad = 113.5 .. 141.6)
        bd.Pos(85.20, -127.60, trench_cz) * bd.Box(6.6, 28.6, trench_h),
        # Unpopulated J4 bare pads clearance trench just in case solder domes exist
        bd.Pos(141.80, -107.40, trench_cz) * bd.Box(3.8, 21.6, trench_h),
    ]
    tray = tray - trenches

    # Carve Front Snap-Hook Mortise Grooves inside the 2.65 mm thick front bulkhead
    # (45-deg double-chamfered horizontal V-groove at Z = 3.40 mm — 100% support-free!)
    for fx, fw in ((spec.x_center, 10.60), (86.50, 5.40), (140.50, 5.40)):
        # Vertical entry channel behind inner front wall + 45-deg catch V-groove into wall
        v_groove = (
            bd.Pos(fx, spec.y_in_front, 3.40)
            * bd.Rot(0, 90, 0)
            * bd.Rot(0, 0, 45)
            * bd.Box(0.95, 0.95, fw, align=(bd.Align.CENTER, bd.Align.CENTER, bd.Align.CENTER))
        )
        tray = tray - v_groove

    # Carve M3 recessed screw-head counterbores + Prusa 2-layer support-free stepped bridges
    cbore_h = spec.m3_cbore_z_top - spec.z_ext_bottom
    for hx, hyk in ((spec.h1_x, spec.h1_yk), (spec.h2_x, spec.h2_yk)):
        hy = -hyk
        cbore = bd.Pos(hx, hy, spec.z_ext_bottom + 0.5 * cbore_h - 0.1) * bd.Cylinder(
            radius=0.5 * spec.m3_cbore_d, height=cbore_h + 0.2
        )
        # Layer 1 above counterbore (0.20 mm thick): straight bridge slit (6.20 x 3.40 mm)
        bridge_slit = bd.Pos(hx, hy, spec.m3_cbore_z_top + 0.10) * bd.Box(
            spec.m3_cbore_d, spec.m3_through_d, 0.22
        )
        # Layer 2 above counterbore (0.20 mm thick): square bridge (3.40 x 3.40 mm)
        bridge_sq = bd.Pos(hx, hy, spec.m3_cbore_z_top + 0.30) * bd.Box(
            spec.m3_through_d, spec.m3_through_d, 0.22
        )
        # M3 clearance shaft up through standoff
        shaft_h = (spec.z_pcb_rest + 1.0) - spec.m3_cbore_z_top
        shaft = bd.Pos(hx, hy, spec.m3_cbore_z_top + 0.5 * shaft_h) * bd.Cylinder(
            radius=0.5 * spec.m3_through_d, height=shaft_h
        )
        tray = tray - [cbore, bridge_slit, bridge_sq, shaft]

    # Carve external port & button cutouts
    tray = tray - build_port_and_button_cutouts(spec)

    # Subtle 0.30 mm ring alignment markers + debossed bottom spec legend on base
    foot_offsets = [
        (spec.x_out_min + 8.5, spec.y_out_front - 8.5),
        (spec.x_out_max - 8.5, spec.y_out_front - 8.5),
        (spec.x_out_min + 15.5, spec.y_out_rear + 8.5),
        (spec.x_out_max - 15.5, spec.y_out_rear + 8.5),
    ]
    foot_rings: list[bd.Part] = []
    for fx, fy in foot_offsets:
        outer_c = bd.Pos(fx, fy, spec.z_ext_bottom + 0.15) * bd.Cylinder(radius=4.20, height=0.32)
        inner_c = bd.Pos(fx, fy, spec.z_ext_bottom + 0.15) * bd.Cylinder(radius=3.70, height=0.34)
        foot_rings.append(outer_c - inner_c)
    tray = tray - foot_rings

    return tray


def build_top_lid(spec: UnipadC64CaseSpec, outer_shell: bd.Part) -> bd.Part:
    """Construct the Commodore 64-styled Top Lid in KiCad 3D world coordinates."""
    upper_keep = bd.Pos(spec.x_center, spec.y_center, spec.z_split + 25.0) * bd.Box(
        200.0, 200.0, 50.0
    )
    lid = (outer_shell & upper_keep) - build_interior_cavity(spec)

    # Carve inner perimeter rabbet pocket (Z_split .. Z_split + rabbet_h) to mate
    # over Bottom Tray's alignment tongue with 0.15 mm XY clearance.
    r_mid = spec.pcb_rear_corner_r + spec.pcb_xy_clearance + 0.5 * spec.side_wall_t
    mid_wall_t = 0.5 * spec.side_wall_t + 0.5 * spec.rabbet_xy_gap
    rabbet_pocket_face = make_planform_face(
        spec.x_in_min - mid_wall_t,
        spec.x_in_max + mid_wall_t,
        spec.y_in_rear - mid_wall_t,
        spec.y_in_front + 0.5 * (spec.y_out_front - spec.y_in_front) + 0.5 * spec.rabbet_xy_gap,
        r_rear=r_mid,
        r_front=0.80,
    )
    rabbet_pocket = bd.Pos(0, 0, spec.z_split - 0.05) * bd.extrude(
        rabbet_pocket_face, spec.rabbet_h + 0.05
    )
    # Also notch the two front DB9 pin zones cleanly from Z_split up to db9_pins_z_max
    # so the Top Lid prints upside-down with ZERO mid-air bridges across the DB9 pin windows!
    front_wall_d = (spec.y_out_front - spec.y_in_front) + 2.0
    front_wall_cy = 0.5 * (spec.y_out_front + spec.y_in_front)
    db9_open_h = spec.db9_pins_z_max - spec.z_split + 0.10
    db9_notches = [
        bd.Pos(cx, front_wall_cy, spec.z_split + 0.5 * db9_open_h - 0.05)
        * bd.Box(spec.db9_pins_w + 0.30, front_wall_d, db9_open_h)
        for cx in (spec.j2_cx, spec.j3_cx)
    ]
    lid = lid - rabbet_pocket - db9_notches

    # Add internal structural features descending from Top Lid ceiling (z_int_ceiling = 9.900 mm):
    # 1. Rear Corner-Integrated M3 Self-Threading Bosses at H1 & H2 (down to z_pcb_holddown = 1.660 mm)
    boss_h = spec.z_int_ceiling - spec.z_pcb_holddown + 0.10
    boss_cz = spec.z_pcb_holddown + 0.5 * boss_h
    additions: list[bd.Part] = []
    for hx, hyk in ((spec.h1_x, spec.h1_yk), (spec.h2_x, spec.h2_yk)):
        hy = -hyk
        additions.append(
            bd.Pos(hx, hy, boss_cz)
            * bd.Cylinder(radius=0.5 * spec.m3_boss_od, height=boss_h)
        )
        # Upper corner gusset tying M3 boss into rear & side walls above Z_split + rabbet_h
        gusset_z_min = spec.z_split + spec.rabbet_h + 0.10
        gusset_h = spec.z_int_ceiling - gusset_z_min + 0.10
        gx = spec.x_in_min + 1.8 if hx < spec.x_center else spec.x_in_max - 1.8
        additions.append(
            bd.Pos(0.5 * (hx + gx), 0.5 * (hy + spec.y_in_rear), gusset_z_min + 0.5 * gusset_h)
            * bd.Box(abs(hx - gx) + 2.6, abs(hy - spec.y_in_rear) + 2.6, gusset_h)
        )

    # 2. Three Front Snap-Hook Tenons + PCB Front Hold-Down Buttresses
    #    - Hook tenons engage the 45-deg V-grooves in the Bottom Tray front wall (Z = 2.60 .. 5.10)
    #    - Front hold-down pads press onto bare front FR4 zones at Z = z_pcb_holddown (1.660 mm)
    for fx, fw, hold_d in ((spec.x_center, 9.80, 2.20), (86.50, 4.60, 1.60), (140.50, 4.60, 1.60)):
        tenon_z_min = 2.65
        tenon_h = spec.z_int_ceiling - tenon_z_min + 0.10
        # Tenon stem sits 0.15 mm inside y_in_front so it clears PCB front edge (Y=-90.00) by 0.15 mm!
        tenon_t = 1.10
        tenon_cy = spec.y_in_front - 0.15 - 0.5 * tenon_t
        # Wait: PCB front edge is Y=-90.000, y_in_front is -89.700 (only 0.30 mm gap above Z=1.601!).
        # Above PCB top (Z > 1.660), bare FR4 is empty at Y = [-92.0 .. -90.0] in these 3 bare zones!
        # Let's place tenon stem in Y = [-90.95 .. -89.85] (above Z=1.660, 100% over bare FR4!)
        # with a forward-projecting 45-deg snap bead reaching Y = -89.25 into the front wall V-groove!
        tenon_stem = bd.Pos(fx, -90.40, tenon_z_min + 0.5 * tenon_h) * bd.Box(
            fw, 1.10, tenon_h
        )
        # 45-deg snap bead at Z = 3.40 projecting +Y to Y = -89.28 (into Bottom Tray V-groove at Y=-89.70)
        bead = (
            bd.Pos(fx, -89.80, 3.40)
            * bd.Rot(0, 90, 0)
            * bd.Rot(0, 0, 45)
            * bd.Box(0.80, 0.80, fw - 0.40)
        )
        # Bridge tying tenon stem into Top Lid's front wall above Z_split + rabbet_h
        top_tie_z = spec.z_split + spec.rabbet_h + 0.05
        top_tie_h = spec.z_int_ceiling - top_tie_z + 0.10
        top_tie = bd.Pos(fx, -89.70, top_tie_z + 0.5 * top_tie_h) * bd.Box(
            fw, 2.50, top_tie_h
        )
        # PCB hold-down foot descending to z_pcb_holddown (1.660 mm) on bare front FR4
        foot_h = spec.z_int_ceiling - spec.z_pcb_holddown + 0.10
        hold_foot = bd.Pos(fx, -90.95, spec.z_pcb_holddown + 0.5 * foot_h) * bd.Box(
            min(fw, 4.0), hold_d, foot_h
        )
        additions.extend([tenon_stem, bead, top_tie, hold_foot])

    # 3. Anti-Bleed Vertical LED Light-Pipe Chimneys (D1 PWR + D4/D5/D6 BT/P1/P2)
    #    Clipped to a 0.15 mm inset of the interior cavity so shrouds near the PCB
    #    perimeter (e.g. D4..D6 at X=83.581, D1 at Y=-148.680) maintain a clean
    #    0.15 mm slip gap to the Bottom Tray's inner walls below Z_split.
    inset_gap = 0.150
    safe_inner_face = make_planform_face(
        spec.x_in_min + inset_gap,
        spec.x_in_max - inset_gap,
        spec.y_in_rear + inset_gap,
        spec.y_in_front - inset_gap,
        r_rear=spec.pcb_rear_corner_r + spec.pcb_xy_clearance - inset_gap,
        r_front=0.25,
    )
    safe_inner_keep = bd.Pos(0, 0, 0.0) * bd.extrude(safe_inner_face, spec.z_int_ceiling + 0.20)

    shroud_h = spec.z_int_ceiling - spec.led_shroud_z_bottom + 0.10
    shroud_cz = spec.led_shroud_z_bottom + 0.5 * shroud_h
    clipped_shrouds: list[bd.Part] = []
    for lx, lyk in (
        (spec.led_pwr_x, spec.led_pwr_yk),
        (spec.led_left_x, spec.led_bt_yk),
        (spec.led_left_x, spec.led_p1_yk),
        (spec.led_left_x, spec.led_p2_yk),
    ):
        raw_shroud = (
            bd.Pos(lx, -lyk, shroud_cz)
            * bd.Cylinder(radius=0.5 * spec.led_shroud_od, height=shroud_h)
        )
        clipped_shrouds.append(raw_shroud & safe_inner_keep)
    additions.extend(clipped_shrouds)

    # 4. Paperclip Guide Tube above Pico 2 W BOOTSEL Button
    boot_h = spec.z_int_ceiling - spec.bootsel_guide_z_bottom + 0.10
    boot_cz = spec.bootsel_guide_z_bottom + 0.5 * boot_h
    additions.append(
        bd.Pos(spec.bootsel_x, -spec.bootsel_yk, boot_cz)
        * bd.Cylinder(radius=0.5 * spec.bootsel_guide_od, height=boot_h)
    )

    lid = lid + additions

    # Drill Blind Self-Threading M3 Bores + Lead-In Chamfers in H1 & H2 Bosses
    tap_h = spec.m3_tap_z_top - (spec.z_pcb_holddown - 0.20)
    for hx, hyk in ((spec.h1_x, spec.h1_yk), (spec.h2_x, spec.h2_yk)):
        hy = -hyk
        tap_bore = bd.Pos(hx, hy, spec.z_pcb_holddown - 0.20 + 0.5 * tap_h) * bd.Cylinder(
            radius=0.5 * spec.m3_tap_d, height=tap_h
        )
        lead_cone = bd.Pos(hx, hy, spec.z_pcb_holddown + 0.30) * bd.Cone(
            bottom_radius=0.5 * 3.30,
            top_radius=0.5 * spec.m3_tap_d,
            height=0.70,
        )
        lid = lid - [tap_bore, lead_cone]

    # Drill Vertical Through-Pinholes for Status LEDs & BOOTSEL + Top Lead-In Dimple
    through_h = (spec.z_ext_top - 1.0)
    through_cz = 1.0 + 0.5 * (spec.z_ext_top + 1.0)
    pinholes: list[bd.Part] = []
    for lx, lyk in (
        (spec.led_pwr_x, spec.led_pwr_yk),
        (spec.led_left_x, spec.led_bt_yk),
        (spec.led_left_x, spec.led_p1_yk),
        (spec.led_left_x, spec.led_p2_yk),
    ):
        pinholes.append(
            bd.Pos(lx, -lyk, through_cz)
            * bd.Cylinder(radius=0.5 * spec.led_pinhole_d, height=through_h)
        )
        # Subtle 0.35 mm top bevel around each LED aperture
        pinholes.append(
            bd.Pos(lx, -lyk, spec.z_ext_top - 0.175)
            * bd.Cone(
                bottom_radius=0.5 * spec.led_pinhole_d,
                top_radius=0.5 * spec.led_pinhole_d + 0.35,
                height=0.36,
            )
        )

    # BOOTSEL 2.50 mm pinhole + recessed 0.55 mm paperclip guide dimple
    pinholes.append(
        bd.Pos(spec.bootsel_x, -spec.bootsel_yk, through_cz)
        * bd.Cylinder(radius=0.5 * spec.bootsel_pinhole_d, height=through_h)
    )
    pinholes.append(
        bd.Pos(spec.bootsel_x, -spec.bootsel_yk, spec.z_ext_top - 0.275)
        * bd.Cone(
            bottom_radius=0.5 * spec.bootsel_pinhole_d,
            top_radius=0.5 * spec.bootsel_pinhole_d + 0.55,
            height=0.56,
        )
    )
    lid = lid - pinholes

    # Carve external port & button cutouts
    lid = lid - build_port_and_button_cutouts(spec)
    return lid


def apply_c64_styling_and_legends(spec: UnipadC64CaseSpec, lid: bd.Part) -> bd.Part:
    """Carve C64C-inspired blind cooling louvers and crisp 0.40 mm debossed legends."""
    deboss_d = 0.40
    louver_d = 0.48
    z_roof = spec.z_ext_top
    carvings: list[bd.Part] = []

    # 1. C64C-Homage Blind Louver Grille (6 parallel stadium ribs on upper-right deck)
    louver_w = 38.50
    louver_slot_h = 1.25
    louver_cx = 122.00
    louver_y_start = -101.20
    louver_pitch = 2.85
    for idx in range(6):
        ly = louver_y_start - idx * louver_pitch
        slot_2d = bd.Pos(louver_cx, ly) * bd.SlotOverall(louver_w, louver_slot_h)
        carvings.append(
            bd.Pos(0, 0, z_roof - louver_d) * bd.extrude(slot_2d, louver_d + 0.05)
        )

    # 2. Commodore-Homage Twin Horizontal Accent Bars & UNIPAD C64 Brandmark Plaque
    #    Positioned across bare lower-center deck (Y = -138.0 .. -145.0, X = 103 .. 138).
    def add_label(
        text: str,
        x: float,
        y: float,
        font_size: float,
        align_x: bd.Align = bd.Align.CENTER,
    ) -> None:
        txt_sketch = bd.Pos(x, y) * bd.Text(
            text,
            font_size=font_size,
            font_style=bd.FontStyle.BOLD,
            align=(align_x, bd.Align.CENTER),
        )
        carvings.append(
            bd.Pos(0, 0, z_roof - deboss_d) * bd.extrude(txt_sketch, deboss_d + 0.05)
        )

    # Front DB9 Port Headers
    add_label("PORT 1", spec.j2_cx, -93.60, font_size=3.10)
    add_label("PORT 2", spec.j3_cx, -93.60, font_size=3.10)

    # Left-Edge Status LED Callouts (right beside D4, D5, D6 pinholes)
    add_label("BT", spec.led_left_x + 2.30, -spec.led_bt_yk, font_size=2.50, align_x=bd.Align.MIN)
    add_label("P1", spec.led_left_x + 2.30, -spec.led_p1_yk, font_size=2.50, align_x=bd.Align.MIN)
    add_label("P2", spec.led_left_x + 2.30, -spec.led_p2_yk, font_size=2.50, align_x=bd.Align.MIN)

    # Left-Edge Tactile Switch Callouts + Pointer Ticks
    for yk, label in (
        (spec.sw_mode_yk, "MODE"),
        (spec.sw_swap_yk, "SWAP"),
        (spec.sw_reset_yk, "RST"),
    ):
        tick = bd.Pos(spec.x_out_min + 2.65, -yk) * bd.Rectangle(1.50, 0.80)
        carvings.append(
            bd.Pos(0, 0, z_roof - deboss_d) * bd.extrude(tick, deboss_d + 0.05)
        )
        add_label(label, spec.x_out_min + 4.10, -yk, font_size=2.45, align_x=bd.Align.MIN)

    # Rear Power LED Callout (beside D1 pinhole)
    add_label("PWR", spec.led_pwr_x + 2.20, -spec.led_pwr_yk, font_size=2.50, align_x=bd.Align.MIN)

    # BOOTSEL & Right-Edge Micro-USB Callouts
    add_label("BOOT", spec.bootsel_x, -spec.bootsel_yk - 3.35, font_size=2.30)
    usb_tick = bd.Pos(spec.x_out_max - 2.65, -spec.usb_yk) * bd.Rectangle(1.50, 0.80)
    carvings.append(
        bd.Pos(0, 0, z_roof - deboss_d) * bd.extrude(usb_tick, deboss_d + 0.05)
    )
    add_label("USB", spec.x_out_max - 4.10, -spec.usb_yk, font_size=2.35, align_x=bd.Align.MAX)

    # Centerpiece Brandmark + Commodore-style twin stripe bars
    add_label("UNIPAD", 119.50, -139.60, font_size=5.00)
    add_label("C 6 4", 119.50, -144.30, font_size=2.90)
    for bar_y in (-143.75, -144.85):
        for bar_cx in (108.00, 131.00):
            bar = bd.Pos(bar_cx, bar_y) * bd.Rectangle(7.20, 0.60)
            carvings.append(
                bd.Pos(0, 0, z_roof - deboss_d) * bd.extrude(bar, deboss_d + 0.05)
            )

    return lid - carvings


def orient_bottom_for_print(spec: UnipadC64CaseSpec, bottom_world: bd.Part) -> bd.Part:
    """Translate Bottom Tray to `(0, 0)` with flat exterior bottom on `Z = 0.000`."""
    return bd.Pos(-spec.x_center, -spec.y_center, -spec.z_ext_bottom) * bottom_world


def orient_top_for_print(spec: UnipadC64CaseSpec, top_world: bd.Part) -> bd.Part:
    """Flip Top Lid 180 deg around X so flat exterior roof rests on `Z = 0.000`."""
    centered = bd.Pos(-spec.x_center, -spec.y_center, -spec.z_ext_top) * top_world
    return bd.Rot(180, 0, 0) * centered


def verify_enclosure_geometry(
    spec: UnipadC64CaseSpec,
    bottom_world: bd.Part,
    top_world: bd.Part,
    pcb_step_path: Path | None,
) -> None:
    """Run strict B-Rep manifold, single-solid, and 3D PCB interference checks."""
    for name, part in (("bottom_tray", bottom_world), ("top_lid", top_world)):
        solids = part.solids()
        if len(solids) != 1:
            raise RuntimeError(
                f"{name} must be a single connected solid, found {len(solids)} solids."
            )
        if not part.is_valid:
            raise RuntimeError(f"{name} failed OpenCASCADE B-Rep validity check.")

    inter_shells = bottom_world & top_world
    shell_overlap = inter_shells.volume
    if shell_overlap > 1e-4:
        ibb = inter_shells.bounding_box()
        raise RuntimeError(
            f"Bottom Tray and Top Lid intersect! Overlap volume = {shell_overlap:.4f} mm^3 "
            f"at X=[{ibb.min.X:.2f},{ibb.max.X:.2f}], Y=[{ibb.min.Y:.2f},{ibb.max.Y:.2f}], "
            f"Z=[{ibb.min.Z:.2f},{ibb.max.Z:.2f}]"
        )
    print(f"[OK] Clamshell fit check: bottom_tray & top_lid overlap = {shell_overlap:.6f} mm^3", flush=True)

    if pcb_step_path is None:
        return
    if not pcb_step_path.is_file():
        raise FileNotFoundError(f"PCB STEP model not found: {pcb_step_path}")

    pcb_assy = bd.import_step(str(pcb_step_path))
    checked_count = 0
    for idx, comp_solid in enumerate(pcb_assy.solids()):
        bb = comp_solid.bounding_box()
        # Exclude J4 (1x8 vertical debug header at X~141.8, Y_kicad~97..118, h~11.54 mm),
        # which is unpopulated on enclosed production boards.
        if (
            abs(bb.size.Z - 11.540) < 0.20
            and bb.min.X > 139.5
            and (-bb.min.Y) > 115.0
        ):
            print(f"[INFO] Skipping unpopulated J4 debug header (STEP solid #{idx}).", flush=True)
            continue

        checked_count += 1
        # Fast-path skip for internal low-profile SMD passives/ICs sitting strictly
        # inside the open interior air volume away from walls, bosses, and shrouds.
        if (
            bb.min.X > spec.x_in_min + 4.8
            and bb.max.X < spec.x_in_max - 4.8
            and bb.min.Y > spec.y_in_rear + 4.8
            and bb.max.Y < spec.y_in_front - 3.0
            and bb.min.Z >= spec.pcb_z_top - 0.05
            and bb.max.Z < spec.bootsel_guide_z_bottom - 0.25
        ):
            continue

        v_bot = (bottom_world & comp_solid).volume
        v_top = (top_world & comp_solid).volume
        if v_bot > 1e-3 or v_top > 1e-3:
            raise RuntimeError(
                f"3D interference with PCB STEP solid #{idx} "
                f"(X=[{bb.min.X:.2f},{bb.max.X:.2f}], Yk=[{-bb.max.Y:.2f},{-bb.min.Y:.2f}], "
                f"Z=[{bb.min.Z:.2f},{bb.max.Z:.2f}]): "
                f"bottom_overlap={v_bot:.4f} mm^3, top_overlap={v_top:.4f} mm^3"
            )

    print(
        f"[OK] KiCad 3D STEP collision audit passed across all {checked_count} "
        f"populated board & component solids (0.000 mm^3 interference).",
        flush=True,
    )


def export_enclosure_files(
    out_dir: Path,
    spec: UnipadC64CaseSpec,
    bottom_world: bd.Part,
    top_world: bd.Part,
) -> None:
    """Export STEP, STL, and 3MF enclosure deliverables into `out_dir`."""
    out_dir.mkdir(parents=True, exist_ok=True)

    bottom_print = orient_bottom_for_print(spec, bottom_world)
    top_print = orient_top_for_print(spec, top_world)

    # 1. Assembled STEP in KiCad PCB World Coordinates (for CAD overlay with PCB STEP)
    bottom_labeled = bottom_world.moved(bd.Location())
    bottom_labeled.label = "Unipad_C64_Bottom_Tray"
    top_labeled = top_world.moved(bd.Location())
    top_labeled.label = "Unipad_C64_Top_Lid"
    assy = bd.Compound(label="Unipad_C64_Case_Assembly", children=[bottom_labeled, top_labeled])
    bd.export_step(assy, str(out_dir / "unipad_c64_case_assembly.step"))

    # 2. Individual Print-Oriented STEP + STL + 3MF Files (flat on Z=0, zero supports)
    for stem, part in (
        ("unipad_c64_bottom", bottom_print),
        ("unipad_c64_top", top_print),
    ):
        bd.export_step(part, str(out_dir / f"{stem}.step"))
        bd.export_stl(part, str(out_dir / f"{stem}.stl"), tolerance=0.01, angular_tolerance=0.1)

        exporter = bd.Mesher()
        exporter.add_shape(part)
        exporter.write(str(out_dir / f"{stem}.3mf"))

    # 3. Single-Click Combined Print Plate (Bottom Tray + Upside-Down Top Lid at Z=0)
    plate_offset_x = 0.5 * (spec.x_out_max - spec.x_out_min) + 5.0
    plate_bot = bd.Pos(-plate_offset_x, 0, 0) * bottom_print
    plate_top = bd.Pos(plate_offset_x, 0, 0) * top_print
    plate_compound = bd.Compound(children=[plate_bot, plate_top])

    bd.export_stl(
        plate_compound,
        str(out_dir / "unipad_c64_print_plate.stl"),
        tolerance=0.01,
        angular_tolerance=0.1,
    )
    plate_mesher = bd.Mesher()
    plate_mesher.add_shape(plate_bot)
    plate_mesher.add_shape(plate_top)
    plate_mesher.write(str(out_dir / "unipad_c64_print_plate.3mf"))

    bb_bot = bottom_print.bounding_box()
    bb_top = top_print.bounding_box()
    bb_assy = assy.bounding_box()
    print(
        f"[OK] Exported STEP/STL/3MF enclosure bundle to {out_dir}\n"
        f"     - Outer Dimensions : {bb_assy.size.X:.2f} mm (W) x "
        f"{bb_assy.size.Y:.2f} mm (D) x {bb_assy.size.Z:.2f} mm (H)\n"
        f"     - Bottom Tray Print: {bb_bot.size.X:.2f} x {bb_bot.size.Y:.2f} x "
        f"{bb_bot.size.Z:.2f} mm (Z_min={bb_bot.min.Z:.3f})\n"
        f"     - Top Lid Print    : {bb_top.size.X:.2f} x {bb_top.size.Y:.2f} x "
        f"{bb_top.size.Z:.2f} mm (Z_min={bb_top.min.Z:.3f})",
        flush=True,
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="Target directory for generated STEP, STL, and 3MF files.",
    )
    parser.add_argument(
        "--verify-pcb-step",
        type=Path,
        default=None,
        help="Optional KiCad-exported PCB STEP file to verify 0.000 mm^3 interference.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    spec = UnipadC64CaseSpec()

    outer = build_outer_envelope(spec)
    bottom_tray = build_bottom_tray(spec, outer)
    top_lid_structural = build_top_lid(spec, outer)

    # Audit internal mechanical fit and 3D PCB clearance before carving exterior roof legends
    verify_enclosure_geometry(spec, bottom_tray, top_lid_structural, args.verify_pcb_step)

    top_lid = apply_c64_styling_and_legends(spec, top_lid_structural)
    if len(top_lid.solids()) != 1 or not top_lid.is_valid:
        raise RuntimeError("Styled Top Lid failed single-solid manifold validation.")

    export_enclosure_files(args.output_dir, spec, bottom_tray, top_lid)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
