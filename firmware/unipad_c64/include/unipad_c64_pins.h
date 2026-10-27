// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Hardware-verified RP2350 (Raspberry Pi Pico 2 W) GPIO mapping for Unipad C64 (Rev. A).
// Single source of truth synchronized with:
//   - hardware/unipad_c64/pico2w.kicad_sch
//   - hardware/unipad_c64/c64_ports.kicad_sch

#ifndef UNIPAD_C64_PINS_H
#define UNIPAD_C64_PINS_H

#include "unipad_board_id.h"

// ---------------------------------------------------------------------------
// Debug UART0 (routed to 1x8 Debug / Logic Analyzer Header J4)
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_UART_TX          0u   // GP0 -> J4 Pin 1 (UART0 TX)
#define UNIPAD_PIN_UART_RX          1u   // GP1 -> J4 Pin 2 (UART0 RX)

// ---------------------------------------------------------------------------
// C64 Control Port #1 (J2) — Digital Joystick Lines (Contiguous 5-bit PIO bus)
// Polarity: Driving GPIO HIGH (1) turns ON the 74HCT05 (U4) open-drain inverter,
// pulling the corresponding C64 CIA1 DB9 pin LOW (active).
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_J1_BASE          2u   // Contiguous 5-pin base for PIO `out pins, 5`
#define UNIPAD_PIN_J1_UP            2u   // GP2 -> U4A -> J2 Pin 1
#define UNIPAD_PIN_J1_DOWN          3u   // GP3 -> U4B -> J2 Pin 2
#define UNIPAD_PIN_J1_LEFT          4u   // GP4 -> U4C -> J2 Pin 3
#define UNIPAD_PIN_J1_RIGHT         5u   // GP5 -> U4D -> J2 Pin 4
#define UNIPAD_PIN_J1_FIRE          6u   // GP6 -> U4E -> J2 Pin 6
#define UNIPAD_J1_PIN_COUNT         5u

// ---------------------------------------------------------------------------
// C64 Control Port #1 (J2) — Analog POT / 2nd & 3rd Button / 1351 / Paddle Lines
// Polarity:
//   - GPIO HIGH (1) -> 74HCT05 (U6A/U6B) clamps POT line LOW (~0.1V), preventing
//     the C64 SID 470pF integration capacitor from charging (or asserting button).
//   - GPIO LOW  (0) -> 74HCT05 Hi-Z; onboard 10k pull-up (R13/R14) charges the
//     SID capacitor past the ~2.4V SID threshold to latch $D419 / $D41A.
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_J1_POT_BASE      7u
#define UNIPAD_PIN_J1_POTX          7u   // GP7 -> U6A -> J2 Pin 9 (POTX)
#define UNIPAD_PIN_J1_POTY          8u   // GP8 -> U6B -> J2 Pin 5 (POTY)

// ---------------------------------------------------------------------------
// C64 Control Port #2 (J3) — Digital Joystick Lines (Contiguous 5-bit PIO bus)
// Polarity: Active-HIGH at RP2350 GPIO -> Active-LOW open-drain at 74HCT05 (U5).
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_J2_BASE          9u   // Contiguous 5-pin base for PIO `out pins, 5`
#define UNIPAD_PIN_J2_UP            9u   // GP9  -> U5A -> J3 Pin 1
#define UNIPAD_PIN_J2_DOWN          10u  // GP10 -> U5B -> J3 Pin 2
#define UNIPAD_PIN_J2_LEFT          11u  // GP11 -> U5C -> J3 Pin 3
#define UNIPAD_PIN_J2_RIGHT         12u  // GP12 -> U5D -> J3 Pin 4
#define UNIPAD_PIN_J2_FIRE          13u  // GP13 -> U5E -> J3 Pin 6
#define UNIPAD_J2_PIN_COUNT         5u

// ---------------------------------------------------------------------------
// C64 Control Port #2 (J3) — Analog POT / 2nd & 3rd Button / 1351 / Paddle Lines
// Polarity: Active-HIGH clamp via 74HCT05 (U6F/U6E); LOW releases to 10k (R16/R17).
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_J2_POT_BASE      14u
#define UNIPAD_PIN_J2_POTX          14u  // GP14 -> U6F -> J3 Pin 9 (POTX)
#define UNIPAD_PIN_J2_POTY          15u  // GP15 -> U6E -> J3 Pin 5 (POTY)

// ---------------------------------------------------------------------------
// C64 SID Discharge Synchronization & Rumble Sense Inputs (5V Fail-Safe FT Pads)
// Electrical: 10k series (R19/R22) + 22k pull-down (R20/R23) divides C64 POTY
// to 2.62V steady-state HIGH and <= 0.45V LOW during the 256-Phi2 SID discharge.
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_SYNC_J1          16u  // GP16 <- R19/R20 divider from J1_POTY
#define UNIPAD_PIN_SYNC_J2          17u  // GP17 <- R22/R23 divider from J2_POTY

// ---------------------------------------------------------------------------
// Status LEDs (Active-HIGH via 3.3k series resistors R7, R8, R9)
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_LED_BT           18u  // GP18 -> D4 (Blue: Bluetooth status)
#define UNIPAD_PIN_LED_PORT1        19u  // GP19 -> D5 (Green: Control Port #1)
#define UNIPAD_PIN_LED_PORT2        20u  // GP20 -> D6 (Red: Control Port #2)

// ---------------------------------------------------------------------------
// User Tactile Pushbuttons (Active-LOW, 10k external pull-ups R10, R11 to +3V3)
// Note: SW1 (RESET) is wired directly to the Pico 2 W hardware RUN pin (Pin 30).
// ---------------------------------------------------------------------------
#define UNIPAD_PIN_SWITCH_MODE      21u  // GP21 <- SW3 (MODE)
#define UNIPAD_PIN_SWITCH_SWAP      22u  // GP22 <- SW4 (SWAP)

// ---------------------------------------------------------------------------
// Hardware Board Identification Bootstrap Pins (`BOARD_ID[2:0]`, 3.3V-Only Domain)
// Defined in `unipad_board_id.h`:
//   - UNIPAD_PIN_BOARD_ID0 (GP26 / Pin 31) <- JP1 (OPEN on Unipad C64 -> 1)
//   - UNIPAD_PIN_BOARD_ID1 (GP27 / Pin 32) <- JP2 (OPEN on Unipad C64 -> 1)
//   - UNIPAD_PIN_BOARD_ID2 (GP28 / Pin 34) <- JP3 (OPEN on Unipad C64 -> 1)
// Sampled once at boot via internal pull-ups (`0b111` = `UNIPAD_BOARD_C64`) and
// immediately isolated (`PUE=0, PDE=0, IE=0`) for `0.0 uA` static draw.
// Note: GP23, GP24, GP25, and GP29 are reserved internally on Pico 2 W for the
// Infineon CYW43439 wireless gSPI interface.
// ---------------------------------------------------------------------------

#endif  // UNIPAD_C64_PINS_H
