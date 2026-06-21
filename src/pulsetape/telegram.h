// pulsetape/telegram.h
// Raw pulse-tape telegram, quality filter, and repeat detector.
//
// GENERIC LAYER: plain C++, no Arduino.h, no hardware headers, no board pins.
// All timing/threshold values arrive via TelegramConfig (the app fills it from
// the board layer) and the current time arrives as a `now_ms` parameter, so this
// file never calls millis() or reads a GPIO. That is what lets it survive a
// board or capture-mechanism swap unchanged.

#ifndef PULSETAPE_TELEGRAM_H
#define PULSETAPE_TELEGRAM_H

#include <stdint.h>
#include "psi.h"

// Capacity of the raw pulse buffer (HIGH/LOW durations, alternating).
// One pulse/space pair == 2 pulses == 1 nibble, so this is 2x PSI_MAX_NIBBLES.
#define TELEGRAM_MAX_PULSES (PSI_MAX_NIBBLES * 2)

// Number of recent telegrams kept for repeat matching.
#define REPEAT_RING_SIZE 8

// A captured telegram: raw durations plus its nibble fingerprint.
struct RawTelegram {
  uint16_t pulses[TELEGRAM_MAX_PULSES];  // microseconds, HIGH then LOW alternating
  uint16_t count;                        // number of pulses stored
  uint32_t timestamp_ms;                 // capture time (caller-supplied clock)
  int8_t   rssi;                         // -dBm if available, else -1
  uint8_t  repeat_count;                 // times this fingerprint was seen
  uint8_t  forwarded;                    // 1 once emitted (suppresses further repeats)

  uint8_t  nibbles[PSI_MAX_NIBBLES];     // repeat fingerprint (from PulseSpaceIndex)
  uint16_t nibble_count;

  // Timing-class table: the per-class duration window (us), ascending by duration
  // after normalization (class 0 = shortest). This is the "pulse length per index
  // value" — enough to interpret the nibble string without the full pulse list.
  uint8_t  class_count;
  uint16_t class_min[PSI_MICRO_ELEMENTS];
  uint16_t class_max[PSI_MICRO_ELEMENTS];
  uint16_t class_hits[PSI_MICRO_ELEMENTS];  // occurrences per class (spike/sync vs data)
  uint8_t  data_type;                       // PSI_DATA_PS / _P / _S (which side carries bits)
  uint8_t  const_class;                     // for _S/_P: class index of the constant side
                                            // (0 -> type "s"/"p"; 1 -> "1s"/"1p")
};

// Tuning thresholds. The app populates this from the board layer.
struct TelegramConfig {
  uint16_t pulse_min_us;      // shorter -> counts as out-of-range
  uint16_t pulse_max_us;      // longer  -> counts as out-of-range
  uint16_t min_pulses;        // fewer   -> rejected as noise
  uint8_t  repeat_min_count;  // identical frames required before forwarding
  uint16_t repeat_window_ms;  // frames within this window are repeat candidates
  uint8_t  tail_trim_pairs;   // trailing nibble pairs dropped from the fingerprint
                              // (transmission-end/boundary jitter); raw pulses kept
  uint8_t  max_class_pct;     // reject the frame if one timing class is >= this %
                              // of all elements (degenerate = noise/carrier, not data)
};

// Quality filter. Rejects telegrams that are too short, have too many
// out-of-range durations, or show a long run of identical durations (carrier
// leak rather than OOK data). Returns true when the telegram looks valid.
bool telegram_valid(const RawTelegram& t, const TelegramConfig& cfg);

// Repeat detector backed by a small ring of recent telegrams. Repeat identity
// is a string-compare on the nibble fingerprint (no hash) within the time window.
class RepeatDetector {
 public:
  RepeatDetector() : head_(0) {
    for (uint8_t i = 0; i < REPEAT_RING_SIZE; i++) {
      ring_[i].nibble_count = 0;
      ring_[i].forwarded = 0;
    }
  }

  // Offer a freshly captured telegram. Updates repeat_count (on t and in the
  // ring) and returns true EXACTLY ONCE per telegram — when its repeat_count
  // first reaches cfg.repeat_min_count. Further repeats inside the window return
  // false (already forwarded); a fresh occurrence after the window forwards again.
  bool offer(RawTelegram& t, const TelegramConfig& cfg, uint32_t now_ms);

 private:
  RawTelegram* find(const RawTelegram& t, const TelegramConfig& cfg, uint32_t now_ms);

  RawTelegram ring_[REPEAT_RING_SIZE];
  uint8_t     head_;
};

#endif // PULSETAPE_TELEGRAM_H
