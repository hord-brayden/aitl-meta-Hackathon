// Infrared transmitter for the Musecorder port on the M5Stack StickS3.
//
// The StickS3 has an IR LED on GPIO46 (M5's K150 schematic). This drives it with
// the ESP32-S3 RMT peripheral at a 38 kHz carrier, so a Muse-installed applet can
// act as a universal remote: an `ir.nec` or `ir.raw` action fires a code. That is
// the "Muse, turn off the TV" trick — Muse writes the applet, a button runs it.
//
// POWER NOTE: on the StickS3 the IR LED is fed by the PMIC's switchable 5 V output
// (M5's board comment: "5 V out to Grove and IR"), which the display bring-up leaves
// OFF. Call sticks3_pmic_5v(true) (sticks3_lcd.h) before transmitting, or the LED
// stays dark. musecorder_ir_init() does NOT touch the PMIC so this module stays
// board-agnostic and independently testable.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the RMT TX channel on the given GPIO with a 38 kHz carrier.
// Safe to call more than once (later calls are no-ops). Returns false on failure.
bool musecorder_ir_init(int gpio);

// True once musecorder_ir_init() has succeeded.
bool musecorder_ir_ready(void);

// Send a standard NEC frame (address + command, each with its inverse, LSB first,
// 38 kHz). Blocks until the frame is on the wire. No-op/false if not ready.
bool musecorder_ir_nec(uint8_t address, uint8_t command);

// Send a raw burst: durations in microseconds, alternating mark (carrier on) then
// space (carrier off), starting with a mark. `carrier_hz` 0 keeps 38 kHz. Blocks
// until done. Useful for protocols other than NEC, captured elsewhere.
bool musecorder_ir_raw(const uint16_t *durations_us, size_t count);

// Bring up the RMT RX channel on the given GPIO (a demodulating IR receiver's
// output, e.g. GPIO42 on the StickS3). Safe to call more than once. Returns false
// on failure. This is how the device LEARNS: capture a real remote's burst, then
// replay it with musecorder_ir_raw().
bool musecorder_ir_rx_init(int gpio);

// True once musecorder_ir_rx_init() has succeeded.
bool musecorder_ir_rx_ready(void);

// Capture one IR burst. Blocks up to timeout_ms for a signal, then until the burst
// ends (a long idle gap). On success fills out[] with the durations in microseconds
// (mark, space, mark, ... starting with a mark), sets *count, and returns true.
// `cap` is out[]'s capacity. Returns false on timeout, error, or a too-short burst.
bool musecorder_ir_capture(uint16_t *out, size_t cap, size_t *count, int timeout_ms);

#ifdef __cplusplus
}
#endif
