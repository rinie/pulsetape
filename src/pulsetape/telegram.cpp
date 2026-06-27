// pulsetape/telegram.cpp — see telegram.h.

#include "telegram.h"

bool telegramValid(const RawTelegram& t, const TelegramConfig& cfg) {
  if (t.count < cfg.minPulses) return false;

  // Degenerate-frame reject: real OOK has >= 2 well-populated timing classes (the
  // bit symbols). If one class accounts for >= maxClassPct of all elements there
  // is no bit structure — it's noise or a stuck carrier, not a telegram.
  if (cfg.maxClassPct > 0 && t.classCount > 0) {
    uint32_t total = 0, mx = 0;
    for (uint8_t i = 0; i < t.classCount; i++) {
      total += t.classHits[i];
      if (t.classHits[i] > mx) mx = t.classHits[i];
    }
    if (total > 0 && (mx * 100u / total) >= cfg.maxClassPct) return false;
  }

  uint16_t outOfRange = 0;
  uint16_t sameAsPrev = 0;
  uint16_t prev = 0;

  for (uint16_t i = 0; i < t.count; i++) {
    uint16_t p = t.pulses[i];

    if (p < cfg.pulseMinUs || p > cfg.pulseMaxUs) {
      outOfRange++;
    }

    // A long run of identical durations means the carrier is leaking through,
    // not real OOK data.
    if (p == prev) {
      sameAsPrev++;
    }
    else {
      sameAsPrev = 0;
    }
    if (sameAsPrev > 6) return false;

    prev = p;
  }

  // Tolerate up to ~10% out-of-range durations (weak reception).
  if (outOfRange > t.count / 10) return false;

  return true;
}

RawTelegram* RepeatDetector::find(const RawTelegram& t, const TelegramConfig& cfg,
                                  uint32_t nowUs) {
  for (uint8_t i = 0; i < REPEAT_RING_SIZE; i++) {
    RawTelegram* r = &ring[i];
    if (r->nibbleCount == 0) continue;
    if (r->released) continue;   // already closed out — a re-press starts fresh
    if ((nowUs - r->timestampUs) > cfg.repeatWindowUs) continue;
    if (PulseSpaceIndex::nibblesEqual(r->nibbles, r->nibbleCount,
                                      t.nibbles, t.nibbleCount)) {
      return r;
    }
  }
  return nullptr;
}

bool RepeatDetector::offer(RawTelegram& t, const TelegramConfig& cfg, uint32_t nowUs) {
  RawTelegram* existing = find(t, cfg, nowUs);

  if (existing != nullptr) {
    existing->repeatCount++;
    // Silence since the previous sighting of this fingerprint — the inter-repeat
    // gap. Capture it before overwriting the timestamp; keep the most recent one.
    existing->gapUs = nowUs - existing->timestampUs;
    existing->timestampUs = nowUs;
    t.repeatCount = existing->repeatCount;
    t.gapUs = existing->gapUs;
    // "Pressed" event (FORWARD_SECOND/_BOTH): fire once at the threshold.
    if (cfg.forwardMode != FORWARD_LAST &&
        !existing->forwarded && existing->repeatCount >= cfg.repeatMinCount) {
      existing->forwarded = 1;
      existing->event = TELEGRAM_PRESSED;
      t.event = TELEGRAM_PRESSED;
      return true;
    }
    return false;
  }

  // New fingerprint: store it in the ring.
  RawTelegram* slot = &ring[head % REPEAT_RING_SIZE];
  head++;
  *slot = t;
  slot->repeatCount = 1;
  slot->timestampUs = nowUs;
  slot->forwarded = 0;
  slot->released = 0;
  slot->gapUs = 0;
  t.repeatCount = 1;
  t.gapUs = 0;

  // Single-sighting fast path: only when repeatMinCount <= 1 and we emit a pressed event.
  if (cfg.forwardMode != FORWARD_LAST && cfg.repeatMinCount <= 1) {
    slot->forwarded = 1;
    slot->event = TELEGRAM_PRESSED;
    t.event = TELEGRAM_PRESSED;
    return true;
  }
  return false;
}

RawTelegram* RepeatDetector::takeExpired(const TelegramConfig& cfg, uint32_t nowUs) {
  for (uint8_t i = 0; i < REPEAT_RING_SIZE; i++) {
    RawTelegram* r = &ring[i];
    if (r->nibbleCount == 0 || r->released) continue;
    if ((nowUs - r->timestampUs) <= cfg.repeatWindowUs) continue;  // window still open

    if (cfg.forwardMode != FORWARD_SECOND && r->repeatCount >= cfg.repeatMinCount) {
      r->released = 1;                  // skipped by find() hereafter; freed on reuse
      r->event = TELEGRAM_RELEASED;     // repeatCount is the true total
      return r;
    }
    // Expired with no "released" event to emit (SECOND mode, or never confirmed): retire.
    r->nibbleCount = 0;
  }
  return nullptr;
}
