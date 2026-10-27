# Unipad C64 Firmware (`pico2_w`)

Board-specific Bluepad32 platform and RP2350 PIO state-machine firmware for **Unipad C64 (Rev. A)**.

## Hardware-to-Firmware Pin Mapping (`RP2350` / Pico 2 W)

All pin assignments below match [`hardware/unipad_c64/pico2w.kicad_sch`](../../hardware/unipad_c64/pico2w.kicad_sch) and [`include/unipad_c64_pins.h`](include/unipad_c64_pins.h):

| RP2350 GPIO | Schematic Net | Direction | Polarity / Electrical Notes |
| :--- | :--- | :--- | :--- |
| `GP0` | `GPIO_UART_TX` | Output | `UART0 TX` to debug header `J4` pin 1 |
| `GP1` | `GPIO_UART_RX` | Input | `UART0 RX` from debug header `J4` pin 2 |
| `GP2..GP6` | `GPIO_J1_UP/DOWN/LEFT/RIGHT/FIRE` | Output (5 contiguous) | **Active-HIGH** drives `74HCT05` (`U4`) open-drain LOW on C64 Port 1 |
| `GP7, GP8` | `GPIO_J1_POTX`, `GPIO_J1_POTY` | Output (2 contiguous) | **Active-HIGH** clamps `74HCT05` (`U6A`/`U6B`) LOW; `LOW` releases to `10kΩ` pull-up |
| `GP9..GP13` | `GPIO_J2_UP/DOWN/LEFT/RIGHT/FIRE` | Output (5 contiguous) | **Active-HIGH** drives `74HCT05` (`U5`) open-drain LOW on C64 Port 2 |
| `GP14, GP15` | `GPIO_J2_POTX`, `GPIO_J2_POTY` | Output (2 contiguous) | **Active-HIGH** clamps `74HCT05` (`U6F`/`U6E`) LOW; `LOW` releases to `10kΩ` pull-up |
| `GP16` | `GPIO_SYNC_J1` | Input (5V fail-safe FT) | `J1_POTY` via `10k/22k` divider (`2.62V` HIGH, `<=0.45V` LOW during SID discharge) |
| `GP17` | `GPIO_SYNC_J2` | Input (5V fail-safe FT) | `J2_POTY` via `10k/22k` divider (`2.62V` HIGH, `<=0.45V` LOW during SID discharge) |
| `GP18..GP20` | `GPIO_LED_BT`, `LED_J1`, `LED_J2` | Output (3 contiguous) | **Active-HIGH** status LEDs (`3.3kΩ` series resistors) |
| `GP21, GP22` | `GPIO_SWITCH_MODE`, `SWITCH_SWAP` | Input | **Active-LOW** tactile pushbuttons (`10kΩ` external pull-ups to `+3V3`) |
| `GP23..25, 29` | Internal CYW43439 gSPI | Reserved | Used internally on Pico 2 W by `pico_cyw43_arch` (claims 1 PIO SM) |
| `GP26..GP28` | Unconnected (`ADC0..ADC2`) | **Do Not Connect** | Intentionally unused (RP2350 ADC pads have internal reverse diode to `IOVDD`) |

## Recommended RP2350 PIO Block Allocation (12 SMs Total)

- **`PIO0` (4 SMs):** Cycle-exact C64 SID `1351` Mouse & `1311/1312` Paddle timing (`SM0/SM1` = Port 1 `POTX/POTY` synced to `GP16`; `SM2/SM3` = Port 2 `POTX/POTY` synced to `GP17`).
- **`PIO1` (4 SMs):** `SM0` reserved by `cyw43_arch` 33 MHz wireless SPI; `SM1–SM3` available for optional DMA logic-analyzer capture.
- **`PIO2` (4 SMs):** `SM0/SM1` automatic PAL (`985.248 kHz`) / NTSC (`1.022727 MHz`) SID discharge pulse-width calibration & C64 Rumble detection; `SM2/SM3` atomic 5-bit parallel joystick port latching & hardware autofire.
