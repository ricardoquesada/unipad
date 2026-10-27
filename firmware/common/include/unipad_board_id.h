// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Canonical 3-bit hardware board-identification contract (`BOARD_ID[2:0]`)
// shared across the entire Unipad RP2350 (Raspberry Pi Pico 2 W) product family.

#ifndef UNIPAD_BOARD_ID_H
#define UNIPAD_BOARD_ID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Reserved RP2350 (Pico 2 W) Bootstrap Strapping Pins
// ---------------------------------------------------------------------------
//
// Electrical Contract & Safety Invariants (MANDATORY across all Unipad PCBs):
//
//   1. 3.3V-ONLY DOMAIN:
//      GP26, GP27, and GP28 are RP2350 ADC-capable pads (`ADC0..ADC2`), which
//      incorporate an internal reverse ESD diode to `IOVDD` (3.3V) (Pico 2 W
//      Datasheet Section 3.2 Note, p. 13). They MUST NEVER connect to 5V
//      vintage computer DB9 signals.
//
//   2. STRAPPING POLARITY (`OPEN = 1`, `GND = 0`):
//      - Bit = `1` (`OPEN`): Leave solder jumper open (`NC`). Sampled HIGH
//        (3.3V) during boot via the RP2350 internal pull-up (~50..80 kOhm).
//      - Bit = `0` (`GND`):  Bridge solder jumper directly to `GND` in copper.
//        Sampled LOW (0.0V) during boot.
//      - NEVER hard-tie GP26..GP28 directly to `+3V3` without a series resistor,
//        preventing pad damage if user code ever configures a pin output LOW.
//
//   3. RP2350-E9 ERRATUM IMMUNITY & ZERO STATIC CURRENT (`0.0 uA`):
//      RP2350 Erratum `RP2350-E9` affects weak internal pull-downs (`PDE=1`)
//      latching around ~2.1V. Using internal pull-ups (`PUE=1`) with hard `GND`
//      straps completely avoids `RP2350-E9`. Immediately after latching the
//      3-bit code during boot (~28 us), `unipad_board_id_detect()` disables all
//      internal pull resistors (`PUE=0, PDE=0`) and digital input buffers
//      (`IE=0`), guaranteeing `0.0 uA` continuous DC draw on grounded straps.
// ---------------------------------------------------------------------------

/** @brief Bit 0 (LSB, weight 1) bootstrap pin: Pico 2 W Pin 31 (`GP26` / `ADC0` -> `JP1`). */
#define UNIPAD_PIN_BOARD_ID0        26u

/** @brief Bit 1 (weight 2) bootstrap pin: Pico 2 W Pin 32 (`GP27` / `ADC1` -> `JP2`). */
#define UNIPAD_PIN_BOARD_ID1        27u

/** @brief Bit 2 (MSB, weight 4) bootstrap pin: Pico 2 W Pin 34 (`GP28` / `ADC2` -> `JP3`). */
#define UNIPAD_PIN_BOARD_ID2        28u

/** @brief Number of dedicated binary bootstrap pins in `BOARD_ID[2:0]`. */
#define UNIPAD_BOARD_ID_PIN_COUNT   3u

/** @brief Total number of binary strap combinations (`2^3 = 8`). */
#define UNIPAD_BOARD_ID_MAX_COUNT   8u

/**
 * @brief Canonical 3-bit hardware board identifiers (`{GP28, GP27, GP26}`).
 *
 * Truth table (`1` = Jumper Open / `NC`, `0` = Jumper Bridged to `GND`):
 *
 *   ID[2:0] | GP28 (JP3) | GP27 (JP2) | GP26 (JP1) | Target Hardware Variant
 *   --------+------------+------------+------------+------------------------------
 *    0b111  |   OPEN (1) |   OPEN (1) |   OPEN (1) | Unipad C64 (Commodore 64/128)
 *    0b110  |   OPEN (1) |   OPEN (1) |    GND (0) | Unipad A500 (Commodore Amiga)
 *    0b101  |   OPEN (1) |    GND (0) |   OPEN (1) | Unipad 800XL (Atari 8-bit/2600)
 *    0b100  |   OPEN (1) |    GND (0) |    GND (0) | Unipad Atari ST (Atari ST/Falcon)
 *    0b011  |    GND (0) |   OPEN (1) |   OPEN (1) | Unipad MSX (MSX / MSX2)
 *    0b010  |    GND (0) |   OPEN (1) |    GND (0) | Unipad Single Port (1x DB9)
 *    0b001  |    GND (0) |    GND (0) |   OPEN (1) | Reserved (Future Unipad SKU)
 *    0b000  |    GND (0) |    GND (0) |    GND (0) | Reserved / Extended ADC Escape
 *
 * Note: Assigning `0b111` (all jumpers open) to `UNIPAD_BOARD_C64` ensures 100%
 * zero-modification backward compatibility with all `Unipad C64 (Rev. A)` PCBs.
 */
typedef enum {
    UNIPAD_BOARD_RESERVED_0  = 0x0u, /**< 0b000: Reserved / future ADC divider escape */
    UNIPAD_BOARD_RESERVED_1  = 0x1u, /**< 0b001: Reserved for future retro platform */
    UNIPAD_BOARD_SINGLE_PORT = 0x2u, /**< 0b010: Unipad Single-Port DB9 dongle */
    UNIPAD_BOARD_MSX         = 0x3u, /**< 0b011: Unipad MSX / MSX2 (Pin 8 strobe) */
    UNIPAD_BOARD_ATARIST     = 0x4u, /**< 0b100: Unipad Atari ST / Falcon */
    UNIPAD_BOARD_800XL       = 0x5u, /**< 0b101: Unipad Atari 800XL / 2600 / 7800 */
    UNIPAD_BOARD_A500        = 0x6u, /**< 0b110: Unipad Amiga 500 / 1200 / 2000 */
    UNIPAD_BOARD_C64         = 0x7u, /**< 0b111: Unipad C64 / C128 (JP1..JP3 open) */
} unipad_board_id_t;

/**
 * @brief Immutable metadata descriptor for a Unipad hardware variant.
 */
typedef struct {
    unipad_board_id_t id;     /**< 3-bit hardware strap code (`0x0..0x7`) */
    const char* slug;         /**< Short lowercase target slug (e.g. `"unipad_c64"`) */
    const char* model_name;   /**< Human-readable product name (e.g. `"Unipad C64"`) */
    const char* target_hosts; /**< Supported vintage computer family description */
    uint8_t port_count;       /**< Number of physical DB9 control ports (`1` or `2`, `0` if reserved) */
    bool is_allocated;        /**< `true` if assigned to a defined Unipad board specification */
} unipad_board_info_t;

/**
 * @brief Samples `GP26..GP28` (`BOARD_ID[2:0]`) once at boot, caches the result,
 *        and immediately de-energizes/isolates all three pads (`0.0 uA` static draw).
 *
 * Safe to invoke as the very first statement in `main()` prior to configuring any
 * carrier output GPIOs so wrong-firmware safety interlocks can keep DB9 drivers
 * in Hi-Z on a hardware mismatch. Subsequent calls return the cached boot sample
 * without re-enabling pad pull-ups.
 *
 * @return Detected `unipad_board_id_t` (`0x0..0x7`).
 *
 * @code
 * // Example boot-time usage inside `main()`:
 * const unipad_board_id_t board = unipad_board_id_detect();
 * if (!unipad_board_id_matches(UNIPAD_BOARD_C64)) {
 *     // Refuse to drive C64 GPIO outputs on a non-C64 PCB!
 * }
 * @endcode
 */
unipad_board_id_t unipad_board_id_detect(void);

/**
 * @brief Returns the cached `unipad_board_id_t` latched at boot (lazily invoking
 *        `unipad_board_id_detect()` if not yet sampled).
 *
 * @return Detected `unipad_board_id_t` (`0x0..0x7`).
 */
unipad_board_id_t unipad_board_id_get(void);

/**
 * @brief Returns the raw 3-bit strap mask `(GP28 << 2) | (GP27 << 1) | GP26`
 *        sampled during boot (`0x00..0x07`).
 *
 * @return Raw 3-bit unsigned integer (`0..7`).
 */
uint8_t unipad_board_id_get_raw_mask(void);

/**
 * @brief Looks up the immutable metadata descriptor for any `unipad_board_id_t`.
 *
 * Guaranteed non-NULL: out-of-range values clamp safely to a fallback descriptor.
 *
 * @param id Board identifier (`0x0..0x7`).
 * @return Pointer to static read-only `unipad_board_info_t`.
 */
const unipad_board_info_t* unipad_board_id_get_info(unipad_board_id_t id);

/**
 * @brief Convenience helper returning the human-readable model name for `id`
 *        (e.g., `"Unipad C64"` for `UNIPAD_BOARD_C64`).
 *
 * @param id Board identifier (`0x0..0x7`).
 * @return Null-terminated static string.
 */
const char* unipad_board_id_name(unipad_board_id_t id);

/**
 * @brief Verifies whether the physical PCB matches `expected_id`.
 *
 * @param expected_id Expected board identifier for the running firmware target.
 * @return `true` if `unipad_board_id_get() == expected_id`, `false` otherwise.
 */
bool unipad_board_id_matches(unipad_board_id_t expected_id);

/**
 * @brief Formats a diagnostic strap summary string into `out_buf`, e.g.:
 *        `"0b111 (GP28=1, GP27=1, GP26=1)"`.
 *
 * @param raw_mask 3-bit strap value (`0..7`).
 * @param out_buf  Destination character buffer (recommended `>= 32` bytes).
 * @param buf_size Size of `out_buf` in bytes.
 * @return `out_buf` pointer (or `""` if `out_buf == NULL` or `buf_size == 0`).
 */
const char* unipad_board_id_format_straps(uint8_t raw_mask, char* out_buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif  // UNIPAD_BOARD_ID_H
