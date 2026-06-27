// pulsetape/telegram.h
// Raw pulse-tape telegram, quality filter, and repeat detector.
//
// GENERIC LAYER: plain C++, no Arduino.h, no hardware headers, no board pins.
// All timing/threshold values arrive via TelegramConfig (the app fills it from
// the board layer) and the current time arrives as a `nowUs` parameter, so this
// file never calls micros() or reads a GPIO. That is what lets it survive a
// board or capture-mechanism swap unchanged.
//
// Every duration in this layer is MICROSECONDS — the same unit as the pulse/space
// durations, so nothing here mixes ms and us. The caller supplies a micros() clock.

#ifndef PULSETAPE_TELEGRAM_H
#define PULSETAPE_TELEGRAM_H

#include <stdint.h>
#include "psi.h"

// Capacity of the raw pulse buffer (HIGH/LOW durations, alternating).
// One pulse/space pair == 2 pulses == 1 nibble, so this is 2x PSI_MAX_NIBBLES.
#define TELEGRAM_MAX_PULSES (PSI_MAX_NIBBLES * 2)

// Number of recent telegrams kept for repeat matching.
#define REPEAT_RING_SIZE 8

// When to forward a confirmed telegram downstream:
#define FORWARD_LAST   0   // once at window close, with the true repeat total (default)
#define FORWARD_SECOND 1   // once the instant repeatMinCount is reached (low latency)
#define FORWARD_BOTH   2   // both: a "pressed" event at the threshold + "released" at close

// Event reported with a forwarded telegram (RawTelegram.event):
#define TELEGRAM_PRESSED  1   // confirmed (threshold reached) — start of a press
#define TELEGRAM_RELEASED 2   // window closed — end of press, repeatCount = true total

// A captured telegram: raw durations plus its nibble fingerprint.
struct RawTelegram {
  uint16_t pulses[TELEGRAM_MAX_PULSES];  // microseconds, HIGH then LOW alternating
  uint16_t count;                        // number of pulses stored
  uint32_t timestampUs;                  // capture time (caller-supplied micros clock)
  int8_t   rssi;                         // -dBm if available, else -1
  uint8_t  repeatCount;                  // times this fingerprint was seen
  uint8_t  forwarded;                    // 1 once the "pressed" event has fired
  uint8_t  released;                     // 1 once the "released" event has fired
  uint8_t  event;                        // TELEGRAM_PRESSED / TELEGRAM_RELEASED
  uint32_t gapUs;                        // silence to the previous repeat (us); 0 if first sighting

  uint8_t  nibbles[PSI_MAX_NIBBLES];     // repeat fingerprint (from PulseSpaceIndex)
  uint16_t nibbleCount;

  // Timing-class table: the per-class duration window (us), ascending by duration
  // after normalization (class 0 = shortest). This is the "pulse length per index
  // value" — enough to interpret the nibble string without the full pulse list.
  uint8_t  classCount;
  uint16_t classMin[PSI_MICRO_ELEMENTS];
  uint16_t classMax[PSI_MICRO_ELEMENTS];
  uint16_t classHits[PSI_MICRO_ELEMENTS];   // occurrences per class (spike/sync vs data)
  uint8_t  dataType;                        // PSI_DATA_PS / _P / _S (which side carries bits)
  uint8_t  constClass;                      // for _S/_P: class index of the constant side
                                            // (0 -> type "s"/"p"; 1 -> "1s"/"1p")
};

// Tuning thresholds. The app populates this from the board layer.
struct TelegramConfig {
  uint16_t pulseMinUs;        // shorter -> counts as out-of-range
  uint16_t pulseMaxUs;        // longer  -> counts as out-of-range
  uint16_t minPulses;         // fewer   -> rejected as noise
  uint8_t  repeatMinCount;    // identical frames required before forwarding
  uint32_t repeatWindowUs;    // frames within this window are repeat candidates
  uint8_t  tailTrimPairs;     // trailing nibble pairs dropped from the fingerprint
                              // (transmission-end/boundary jitter); raw pulses kept
  uint8_t  maxClassPct;       // reject the frame if one timing class is >= this %
                              // of all elements (degenerate = noise/carrier, not data)
  uint8_t  forwardMode;       // FORWARD_LAST / _SECOND / _BOTH
};

// Quality filter. Rejects telegrams that are too short, have too many
// out-of-range durations, or show a long run of identical durations (carrier
// leak rather than OOK data). Returns true when the telegram looks valid.
bool telegramValid(const RawTelegram& t, const TelegramConfig& cfg);

// Repeat detector backed by a small ring of recent telegrams. Repeat identity
// is a string-compare on the nibble fingerprint (no hash) within the time window.
class RepeatDetector {
 public:
  RepeatDetector() : head(0) {
    for (uint8_t i = 0; i < REPEAT_RING_SIZE; i++) {
      ring[i].nibbleCount = 0;
      ring[i].forwarded = 0;
      ring[i].released = 0;
      ring[i].gapUs = 0;
    }
  }

  // Offer a freshly captured telegram. Updates its repeatCount in the ring.
  // Returns true (with t.event = TELEGRAM_PRESSED) only in FORWARD_SECOND/_BOTH
  // mode, the first time repeatCount reaches cfg.repeatMinCount — the immediate
  // "pressed" event. In FORWARD_LAST mode it always returns false (the telegram is
  // forwarded later, by takeExpired).
  bool offer(RawTelegram& t, const TelegramConfig& cfg, uint32_t nowUs);

  // Drain the next telegram whose repeat window has closed and that should fire a
  // "released" event (FORWARD_LAST/_BOTH, repeatCount >= min). Sets its
  // event = TELEGRAM_RELEASED and repeatCount = the true total. Returns nullptr
  // when none remain. Call repeatedly (e.g. each frame/idle tick) with the current
  // time; the caller forwards each returned telegram to the sink.
  RawTelegram* takeExpired(const TelegramConfig& cfg, uint32_t nowUs);

 private:
  RawTelegram* find(const RawTelegram& t, const TelegramConfig& cfg, uint32_t nowUs);

  RawTelegram ring[REPEAT_RING_SIZE];
  uint8_t     head;
};

#endif // PULSETAPE_TELEGRAM_H
