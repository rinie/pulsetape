// board/lilygo_t3_v161.h
// LilyGO TTGO T3 LoRa32 433MHz V1.6.1 (ESP32, SX1276).
//
// RX uses an EXTERNAL analog superheterodyne receiver (e.g. SRX882S) on a free
// input-only GPIO, captured via the ESP32 RMT backend. The onboard SX1276 is a
// poor OOK receiver (see receivers.md) and is reserved here for TX / future FSK.
//
// All values verified against community pinouts for the V1.6.1; note the board's
// pin mapping is inconsistent across revisions (esp. SX1276 RST and OLED I2C),
// so confirm against your unit.

#ifndef PULSETAPE_BOARD_LILYGO_T3_V161_H
#define PULSETAPE_BOARD_LILYGO_T3_V161_H

#define BOARD_NAME "LilyGO TTGO T3 LoRa32 433MHz V1.6.1 (ESP32)"

// --- RF receive source selection ---
// PRIMARY (default): onboard SX1278 in OOK continuous mode; demodulated data on
//   DIO2 -> GPIO32, captured by RMT. No extra hardware. See receivers.md.
// ALTERNATIVE: define PULSETAPE_RX_SOURCE_SRX882S to instead use an external
//   SRX882S superhet on GPIO36 (input-only, free on the V1.6.1 header; DATA is
//   3.3V -> wire direct, no divider; tie SRX882S pin 4 CS -> VCC; 17cm antenna)
//   and skip the SX1278 front-end. (Better OOK sensitivity; needs the module.)
#if defined(PULSETAPE_RX_SOURCE_SRX882S)
  #define RF_DATA_PIN 36          // external SRX882S DATA
  #define USE_SX1278_FRONTEND 0
#else
  #define RF_DATA_PIN 32          // SX1276 DIO2 (== SX1276_DIO2)
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

// --- Onboard SX1276 (reserved for TX / future FSK; NOT used for OOK RX) ---
#define SX1276_SCK   5
#define SX1276_MISO  19
#define SX1276_MOSI  27
#define SX1276_NSS   18
#define SX1276_RST   23   // some revisions 14 — verify
#define SX1276_DIO0  26
#define SX1276_DIO1  33
#define SX1276_DIO2  32

// --- Onboard SSD1306 OLED (reference; not used by capture) ---
// pins_arduino.h for ttgo-lora32-v1 defines OLED_SDA=4 / OLED_SCL=15;
// guard so our values only apply when the variant header is absent.
#ifndef OLED_SDA
#define OLED_SDA 21       // some revisions 4 — verify
#endif
#ifndef OLED_SCL
#define OLED_SCL 22       // some revisions 15 — verify
#endif
#define OLED_RST 16

#endif // PULSETAPE_BOARD_LILYGO_T3_V161_H
