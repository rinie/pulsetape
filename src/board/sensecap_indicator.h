// board/sensecap_indicator.h
// Seeed SenseCAP Indicator (D1/D1L/D1S/D1Pro) — RP2040 side.
//
// ALL board-specific facts live here: GPIO assignments, PIO clock divisor,
// and capture timing thresholds tuned to this hardware. Swapping to another
// board (or RP2350) means adding a sibling header and pointing board.h at it;
// nothing under src/pulsetape/ or src/capture/ should need to change.
//
// Pin map source: piowiring.md (Grove ports on the SenseCAP RP2040).
//   SRX882S DATA  -> GPIO27 (Grove ADC port)   [RX]
//   STX882  DATA  -> GPIO20 (Grove I2C port)   [TX]
// Timing source: rf_telegram.md.

#ifndef PULSETAPE_BOARD_SENSECAP_INDICATOR_H
#define PULSETAPE_BOARD_SENSECAP_INDICATOR_H

#define BOARD_NAME "SenseCAP Indicator (RP2040)"

// --- RF data pins ---
#define RF_DATA_PIN 27  // SRX882S pin 5 (DATA out) -> PIO input
#define STX_DATA_PIN 20 // STX882 pin 2 (DATA in)  <- PIO output (TX, future)

// --- PIO clock ---
// 125MHz system clock / 25 = 5MHz => 0.2us per tick.
// At div=25, 5 ticks = 1us (TICKS_PER_US below).
#define PIO_CLOCK_DIV 25
#define TICKS_PER_US 5

// --- Capture timing thresholds (microseconds) ---
#define PULSE_MIN_US 50     // shorter than this is noise
#define PULSE_MAX_US 32000  // longer than this is silence/gap
#define FRAME_GAP_US 8000   // silence that closes a frame (Oregon ~8ms, KAKU ~10ms)

// --- Telegram quality / repeat tuning ---
#define MIN_PULSES 8          // fewer pulses than this is noise, not a telegram
#define REPEAT_MIN_COUNT 2    // identical frames required before forwarding
#define REPEAT_WINDOW_MS 800  // frames within this window count as repeats
#define TAIL_TRIM_PAIRS 2     // trailing nibble pairs dropped from the fingerprint

#endif // PULSETAPE_BOARD_SENSECAP_INDICATOR_H
