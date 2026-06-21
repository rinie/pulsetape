// board/lilygo_t3_v161.h
// LilyGO TTGO T3 LoRa32 433 MHz V1.6.1  (ESP32-PICO-D4 + SX1276/SX1278)
//
// Verified pinout — confirmed on unit MAC e8:6b:ea:0a:0c:a0 (ESP32-PICO-D4 rev1.1)
// by register dump after successful KAKU reception (see "first kaku received.txt").
//
// SX1278 (onboard LoRa radio used as OOK front-end)
//   GPIO  5  — SX1278 SCK
//   GPIO 19  — SX1278 MISO
//   GPIO 27  — SX1278 MOSI
//   GPIO 18  — SX1278 NSS / CS
//   GPIO 23  — SX1278 RST  (active LOW; some V1.0 revisions use GPIO 14 — verify)
//   GPIO 26  — SX1278 DIO0 (RSSI threshold / IRQ)
//   GPIO 33  — SX1278 DIO1 (unused for OOK)
//   GPIO 32  — SX1278 DIO2 (DATA output in continuous OOK mode — capture pin)
//
// OLED (SSD1306, 128×64, I2C)
//   GPIO 21  — SDA  (pins_arduino.h for ttgo-lora32-v1 gives SDA=4 — WRONG for V1.6.1)
//   GPIO 22  — SCL  (pins_arduino.h gives SCL=15 — also WRONG)
//   I2C addr 0x3C, no RST pin (GPIO 16 is internal flash CS on PICO-D4 — never drive it)
//
// Misc
//   GPIO 25  — Onboard blue LED, active HIGH  (LED_BUILTIN in ttgo-lora32-v21new variant)
//   GPIO  4  — SD card CS
//   GPIO 15  — SD MOSI
//   GPIO  2  — SD MISO
//   GPIO 14  — SD SCK
//   GPIO 34  — Free, input-only, no pull  (J3 header)
//   GPIO 35  — Free, input-only, no pull  (J3 header)
//   GPIO 36  — Free, input-only            (VDET2 / SRX882S DATA option)
//   GPIO 39  — Free, input-only            (VDET1)
//
// OOK receive path (confirmed working):
//   SX1278 configured in OOK continuous mode via bare SPI (src/radio/sx1278_ook.cpp).
//   DIO2 (GPIO 32) outputs the threshold-sliced bitstream. A CHANGE interrupt on
//   GPIO 32 measures HIGH/LOW durations in µs — the same approach as rtl_433_ESP.

#ifndef PULSETAPE_BOARD_LILYGO_T3_V161_H
#define PULSETAPE_BOARD_LILYGO_T3_V161_H

// Pull in the selected board variant's pins_arduino.h so the LORA_* / LED_BUILTIN
// macros below resolve. Build with board **ttgo-lora32-v21new** (the V1.6.1 /
// V2.1.6 variant) — that variant is the single source of truth for the pin map,
// so building the right board automatically gives the right pins. This header is
// ESP32-only (included via board.h), so pulling Arduino.h here is fine and does
// not reach the generic core (src/pulsetape/ never includes board.h).
#include <Arduino.h>

#define BOARD_NAME "LilyGO TTGO T3 LoRa32 433MHz V1.6.1 (ESP32)"

// --- RF receive source selection ---
// PRIMARY (default): onboard SX1278 in OOK continuous mode; demodulated data on
//   DIO2 → GPIO32, captured by CHANGE interrupt (µs timing). No extra hardware.
// ALTERNATIVE: define PULSETAPE_RX_SOURCE_SRX882S to use an external SRX882S
//   superhet on GPIO36 instead and skip the SX1278 front-end.
//   (Better OOK sensitivity; requires the module.)
#if defined(PULSETAPE_RX_SOURCE_SRX882S)
  #define RF_DATA_PIN 36          // external SRX882S DATA
  #define USE_SX1278_FRONTEND 0
#else
  #define RF_DATA_PIN LORA_D2     // SX1278 DIO2 (GPIO32 on the v21new variant)
  #define USE_SX1278_FRONTEND 1
#endif

#define RF_FREQUENCY_HZ 433920000UL

// --- Capture timing thresholds (microseconds) ---
#define PULSE_MIN_US 50     // shorter = noise (RMT signal_range_min)
#define PULSE_MAX_US 32000  // 15-bit RMT duration at 1us resolution caps at 32767
#define FRAME_GAP_US 8000   // silence that closes a frame (RMT signal_range_max)

// --- Telegram quality / repeat tuning ---
#define MIN_PULSES 8
#define REPEAT_MIN_COUNT 2
#define REPEAT_WINDOW_MS 800
#define TAIL_TRIM_PAIRS 2   // drop trailing nibble pairs from the fingerprint
                            // (last bits wobble at the frame boundary); tune per captures
#define MAX_CLASS_PCT 90    // reject if one timing class is >= this % of elements
                            // (degenerate = noise/stuck carrier, not a telegram)

// --- Onboard SX1276/78 pins, sourced from the board variant's pins_arduino.h ---
// Single source of truth: the right board gives the right pins, and resolves the
// old GPIO32-vs-35 / RST-23-vs-14 ambiguities. Used by the OOK front-end
// (sx1278_ook.cpp); SX1276_DIO2 == RF_DATA_PIN above.
#define SX1276_SCK   LORA_SCK
#define SX1276_MISO  LORA_MISO
#define SX1276_MOSI  LORA_MOSI
#define SX1276_NSS   LORA_CS
#define SX1276_RST   LORA_RST
#define SX1276_DIO0  LORA_IRQ
#define SX1276_DIO1  LORA_D1
#define SX1276_DIO2  LORA_D2

// --- Board feedback capabilities (the app guards OLED/LED code on these) ---
// ONBOARD_LED: a GPIO the app pulses on each received telegram (active HIGH here).
// BOARD_HAS_OLED: this board has the SSD1306; the app shows telegrams on it.
// A different board can leave either undefined and the app still builds (serial only).
#ifdef LED_BUILTIN
#define ONBOARD_LED LED_BUILTIN   // blue LED (GPIO25), active HIGH
#else
#define ONBOARD_LED 25
#endif

#define BOARD_HAS_OLED 1

// --- Onboard SSD1306 OLED ---
// pins_arduino.h for ttgo-lora32-v1 has SDA=4/SCL=15 (V1.0 pinout) — WRONG
// for V1.6.1. Confirmed by runtime I2C scan: SDA=21, SCL=22, addr=0x3C.
// oled_display.cpp hard-codes all three values to avoid the macro re-definition
// battle with pins_arduino.h (included later via Wire.h, no #ifndef guards).
// GPIO16 on ESP32-PICO-D4 is SPICS0 — RST is hard-coded -1 in oled_display.cpp.

#endif // PULSETAPE_BOARD_LILYGO_T3_V161_H
