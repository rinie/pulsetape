// pulsetape/telegram.cpp — see telegram.h.

#include "telegram.h"

bool telegram_valid(const RawTelegram& t, const TelegramConfig& cfg) {
  if (t.count < cfg.min_pulses) return false;

  // Degenerate-frame reject: real OOK has >= 2 well-populated timing classes (the
  // bit symbols). If one class accounts for >= max_class_pct of all elements there
  // is no bit structure — it's noise or a stuck carrier, not a telegram.
  if (cfg.max_class_pct > 0 && t.class_count > 0) {
    uint32_t total = 0, mx = 0;
    for (uint8_t i = 0; i < t.class_count; i++) {
      total += t.class_hits[i];
      if (t.class_hits[i] > mx) mx = t.class_hits[i];
    }
    if (total > 0 && (mx * 100u / total) >= cfg.max_class_pct) return false;
  }

  uint16_t out_of_range = 0;
  uint16_t same_as_prev = 0;
  uint16_t prev = 0;

  for (uint16_t i = 0; i < t.count; i++) {
    uint16_t p = t.pulses[i];

    if (p < cfg.pulse_min_us || p > cfg.pulse_max_us) {
      out_of_range++;
    }

    // A long run of identical durations means the carrier is leaking through,
    // not real OOK data.
    if (p == prev) {
      same_as_prev++;
    } else {
      same_as_prev = 0;
    }
    if (same_as_prev > 6) return false;

    prev = p;
  }

  // Tolerate up to ~10% out-of-range durations (weak reception).
  if (out_of_range > t.count / 10) return false;

  return true;
}

RawTelegram* RepeatDetector::find(const RawTelegram& t, const TelegramConfig& cfg,
                                  uint32_t now_ms) {
  for (uint8_t i = 0; i < REPEAT_RING_SIZE; i++) {
    RawTelegram* r = &ring_[i];
    if (r->nibble_count == 0) continue;
    if ((now_ms - r->timestamp_ms) > cfg.repeat_window_ms) continue;
    if (PulseSpaceIndex::nibblesEqual(r->nibbles, r->nibble_count,
                                      t.nibbles, t.nibble_count)) {
      return r;
    }
  }
  return nullptr;
}

bool RepeatDetector::offer(RawTelegram& t, const TelegramConfig& cfg, uint32_t now_ms) {
  RawTelegram* existing = find(t, cfg, now_ms);

  if (existing != nullptr) {
    existing->repeat_count++;
    existing->timestamp_ms = now_ms;
    t.repeat_count = existing->repeat_count;
    // Forward exactly once: the first time we reach the threshold and haven't
    // forwarded this telegram yet. Later repeats in the window stay silent.
    if (!existing->forwarded && existing->repeat_count >= cfg.repeat_min_count) {
      existing->forwarded = 1;
      return true;
    }
    return false;
  }

  // New fingerprint: store it in the ring.
  RawTelegram* slot = &ring_[head_ % REPEAT_RING_SIZE];
  head_++;
  *slot = t;
  slot->repeat_count = 1;
  slot->timestamp_ms = now_ms;
  t.repeat_count = 1;

  // Forward immediately only if a single sighting already meets the threshold.
  bool forward_now = (cfg.repeat_min_count <= 1);
  slot->forwarded = forward_now ? 1 : 0;
  return forward_now;
}
