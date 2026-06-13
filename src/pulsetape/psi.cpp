// pulsetape/psi.cpp — see psi.h.

#include "psi.h"

void PulseSpaceIndex::reset() {
  bucket_count_ = 0;
  nibble_count_ = 0;
}

// Tolerance windows from NodoDueRkr: wider as durations grow, because absolute
// jitter scales roughly with the period. A value within `tolerance` of an
// existing bucket edge is folded into that bucket rather than spawning a new one.
uint16_t PulseSpaceIndex::toleranceFor(uint16_t value) {
  if (value < 1000) return 150;
  if (value < 2000) return 200;
  if (value < 3000) return 300;
  if (value < 4000) return 400;
  if (value < 5000) return 600;
  return 2000;
}

uint8_t PulseSpaceIndex::indexOf(uint16_t value) {
  // 1. Exact range match: value already inside a bucket window.
  for (uint8_t i = 0; i < bucket_count_; i++) {
    if (micro_min_[i] <= value && value <= micro_max_[i]) {
      return i;
    }
  }

  // 2. Closest bucket within tolerance: extend that bucket's window to include
  //    value. Pick the nearest edge across all buckets.
  const uint16_t tol = toleranceFor(value);
  uint8_t  best = PSI_OVERFLOW;
  uint16_t best_dist = 0;
  for (uint8_t i = 0; i < bucket_count_; i++) {
    uint16_t dist;
    if (value < micro_min_[i]) {
      dist = micro_min_[i] - value;
    } else {  // value > micro_max_[i] (the == cases were handled in step 1)
      dist = value - micro_max_[i];
    }
    if (dist <= tol && (best == PSI_OVERFLOW || dist < best_dist)) {
      best = i;
      best_dist = dist;
    }
  }
  if (best != PSI_OVERFLOW) {
    if (value < micro_min_[best]) micro_min_[best] = value;
    if (value > micro_max_[best]) micro_max_[best] = value;
    return best;
  }

  // 3. New bucket if there is room.
  if (bucket_count_ < PSI_MICRO_ELEMENTS) {
    uint8_t i = bucket_count_++;
    micro_min_[i] = value;
    micro_max_[i] = value;
    return i;
  }

  // 4. No room: overflow class.
  return PSI_OVERFLOW;
}

uint8_t PulseSpaceIndex::addPair(uint16_t pulse_us, uint16_t space_us) {
  uint8_t pulse_idx = indexOf(pulse_us);
  uint8_t space_idx = indexOf(space_us);
  uint8_t nibble = (uint8_t)(((pulse_idx & 0x0F) << 4) | (space_idx & 0x0F));
  if (nibble_count_ < PSI_MAX_NIBBLES) {
    nibbles_[nibble_count_++] = nibble;
  }
  return nibble;
}

bool PulseSpaceIndex::nibblesEqual(const uint8_t* a, uint16_t a_len,
                                  const uint8_t* b, uint16_t b_len) {
  if (a_len != b_len) return false;
  for (uint16_t i = 0; i < a_len; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}
