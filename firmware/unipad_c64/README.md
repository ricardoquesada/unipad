# Unipad C64 Firmware (`pico2_w` / RP2350)

Production firmware for **Unipad C64 (Rev. A)** powered by **Raspberry Pi Pico 2 W (`RP2350` + `CYW43439`)**, combining [Bluepad32](https://github.com/ricardoquesada/bluepad32) dual-mode Bluetooth (`Classic BR/EDR` + `BLE`) with deterministic **RP2350 Programmable I/O (`PIO`)** state machines for zero-jitter Commodore 64 SID analog timing.

## Key Capabilities

- **Bluetooth Gamepads, Mice, Keyboards & Wii Balance Board**:
  - Supports DualSense, DualShock 3/4 (including **Touchpad as a virtual Commodore 1351 mouse!**), Xbox Wireless (BLE), Nintendo Switch Pro / Joy-Con, Wii / Wii U, 8BitDo, Bluetooth Mice, and Bluetooth Keyboards.
- **Zero-Jitter RP2350 PIO Commodore 1351 Proportional Mouse (`c64_sid_pot`)**:
  - Hardware-synchronized to the C64 SID `512-Φ2` discharge edge on `GP16` (Port 1) and `GP17` (Port 2) with **`6.67 ns` (`150 MHz`)** single-cycle determinism and **0 CPU interrupts**.
  - Works with true Bluetooth Mice (defaulting to Port 1 for GEOS, C64 OS, *Lemmings*, *Maniac Mansion*, *SimCity*, *MSSIAH*) AND with Bluetooth Gamepads in **Mouse Emulation Mode** (`2` BT LED blinks).
- **Zero-Jitter RP2350 PIO Commodore 1311/1312 Analog Paddles (`c64_pot_mode paddle`)**:
  - Drives up to **4 simultaneous analog paddles** (`J1 POTX/POTY`, `J2 POTX/POTY`) for *Arkanoid*, *Krakout*, *Super Breakout*, *Pong*, *Night Driver*, and *Kaboom!*.
- **Full Unijoysticle 2 C64 Feature Parity & Beyond**:
  - **Normal Mode (`1` blink)**: 1 gamepad controls 1 C64 joystick port (`B` = Jump/Up, `A`/`L3` = Fire, `X`/`L2` = Button 2 `POTX`, `Y`/`R2` = Button 3 `POTY`, `R1` = Autofire).
  - **1351 Mouse Mode (`2` blinks)**: Left/Right analog sticks glide a 62.5 Hz Commodore 1351 cursor (`A` = Left Click / `FIRE`, `B` = Right Click / `UP`, `X` = Middle Click / `DOWN`).
  - **Twin-Stick / Enhanced Mode (`3` blinks)**: 1 dual-stick gamepad controls **both** C64 ports simultaneously (*Robotron 2084*, *Smash TV*, *Gridrunner*).
  - **4 Persistent C64 POT Modes** (stored in Flash TLV across power cycles):
    1. `3buttons` *(default)*: Cheetah Annihilator / C64GS 2nd (`POTX`) & 3rd (`POTY`) fire buttons.
    2. `5buttons`: `3buttons` + CrystalCT 4th (`Select` = `UP+DOWN`) & 5th (`Start` = `LEFT+RIGHT`) atomic button chords.
    3. `rumble`: Zero-interrupt PIO `c64_sid_sync_measure` detects SID `POTY` discharge pulses gated by CIA1 `$DC00` bits `6..7` and triggers dual-motor Bluetooth gamepad force-feedback!
    4. `paddle`: Cycle-exact PIO Commodore 1311/1312 paddles (`Left Stick X / L2` = Paddle X + `A/L1` Fire on `LEFT`; `Right Stick X / R2` = Paddle Y + `B/R1` Fire on `RIGHT`).
  - **Independent Per-Port POT Multiplexing**: Pair a Bluetooth Mouse on Port 1 (`PIO` 1351 mode) and a Bluetooth Gamepad on Port 2 (`3buttons` / `5buttons` / `rumble` / `paddle`) at the exact same time; pressing `SWAP` migrates each port's hardware PIO/SIO mode seamlessly on the fly.
  - **Automatic PAL (`985.248 kHz`) / NTSC (`1.022727 MHz`) SID Clock Calibration**: `c64_sid_sync_measure` measures the live C64 SID `256-Φ2` discharge low-pulse width (`~263.6 µs` PAL vs `~254.1 µs` NTSC) in hardware.

## Hardware-to-Firmware Pin Mapping (`RP2350` / Pico 2 W)

All pin assignments match [`hardware/unipad_c64/pico2w.kicad_sch`](../../hardware/unipad_c64/pico2w.kicad_sch) and [`include/unipad_c64_pins.h`](include/unipad_c64_pins.h):

| RP2350 GPIO | Schematic Net | Direction | Polarity / Electrical Notes |
| :--- | :--- | :--- | :--- |
| `GP0`, `GP1` | `GPIO_UART_TX`, `RX` | Out / In | `UART0` (`115200 8N1`) interactive CLI & logs on header `J4` |
| `GP2..GP6` | `GPIO_J1_UP..FIRE` | Output (5 contiguous) | **Active-HIGH** -> `74HCT05` (`U4`) open-drain LOW on C64 Port 1 |
| `GP7`, `GP8` | `GPIO_J1_POTX`, `POTY` | Output (SIO / `PIO0`) | **Active-HIGH** clamps `74HCT05` (`U6A`/`U6B`) LOW; `LOW` -> `10kΩ` `+5V` |
| `GP9..GP13` | `GPIO_J2_UP..FIRE` | Output (5 contiguous) | **Active-HIGH** -> `74HCT05` (`U5`) open-drain LOW on C64 Port 2 |
| `GP14`, `GP15` | `GPIO_J2_POTX`, `POTY` | Output (SIO / `PIO0`) | **Active-HIGH** clamps `74HCT05` (`U6F`/`U6E`) LOW; `LOW` -> `10kΩ` `+5V` |
| `GP16` | `GPIO_SYNC_J1` | Input (5V fail-safe FT) | `J1_POTY` via `10k/22k` divider (`2.62V` HIGH, `<=0.45V` LOW during SID discharge) |
| `GP17` | `GPIO_SYNC_J2` | Input (5V fail-safe FT) | `J2_POTY` via `10k/22k` divider (`2.62V` HIGH, `<=0.45V` LOW during SID discharge) |
| `GP18..GP20` | `LED_BT`, `J1`, `J2` | Output (3 contiguous) | **Active-HIGH** LEDs (`D4` Blue BT, `D5` Green Port 1, `D6` Red Port 2) |
| `GP21`, `GP22` | `SWITCH_MODE`, `SWAP` | Input | **Active-LOW** pushbuttons (`SW1` Mode / Hold-at-boot reset keys, `SW2` Swap) |
| `GP23..25, 29` | Internal CYW43439 gSPI | Reserved | Used internally on Pico 2 W by `pico_cyw43_arch` (`PIO2 SM0`) |
| `GP26..GP28` | Unconnected (`ADC0..2`) | **Do Not Connect** | Intentionally unused (RP2350 ADC pads are NOT 5V fail-safe) |

## Building & USB Serial Console (`115200` / USB CDC)

```bash
export PICO_SDK_PATH=~/src/rpi/pico-sdk
cmake -S . -B build
cmake --build build -j8
```

Connect over Micro-USB (or `J4` UART0 at `115200 8N1`) using `tio`, `picocom`, or `screen`:

```bash
tio /dev/tty.usbmodem*
```

Available interactive commands:

```text
help                                           Show command reference
status | version                               Show hardware, PIO, SID clock & controller status
swap_ports                                     Swap Control Port #1 and Control Port #2
gamepad_mode [normal|mouse|twinstick]          Get or set gamepad emulation mode
c64_pot_mode [3buttons|5buttons|rumble|paddle] Get or set persistent C64 POT mode
autofire_cps [1..62]                           Get or set persistent autofire CPS (7/13/29/62)
mouse_scale [0.1..5.0]                         Get or set persistent 1351 mouse sensitivity
c64_tv [auto|pal|ntsc]                         Get or set C64 SID Phi2 clock standard
bt_scan [on|off]                               Enable or disable Bluetooth discovery
list_devices                                   Dump connected Bluetooth HID devices
list_keys | del_keys                           List or erase stored Bluetooth pairing keys
properties                                     Dump all persistent Flash TLV properties
reboot                                         Reboot Unipad C64
```
