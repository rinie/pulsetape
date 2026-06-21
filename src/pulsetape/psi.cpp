// pulsetape/psi.cpp — see psi.h.

#include "psi.h"

void PulseSpaceIndex::reset() {
  bucket_count_ = 0;
  nibble_count_ = 0;
}

void PulseSpaceIndex::normalize() {
  if (bucket_count_ < 2) return;

  // order[k] = old index of the k-th shortest bucket (insertion sort; <=15 buckets).
  uint8_t order[PSI_MICRO_ELEMENTS];
  for (uint8_t i = 0; i < bucket_count_; i++) order[i] = i;
  for (uint8_t i = 1; i < bucket_count_; i++) {
    uint8_t key = order[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && micro_min_[order[j]] > micro_min_[key]) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }

  // remap[oldIndex] = newRank. Ranks are < bucket_count_ <= 15, so never collide
  // with PSI_OVERFLOW (0x0F).
  uint8_t remap[PSI_MICRO_ELEMENTS];
  for (uint8_t k = 0; k < bucket_count_; k++) remap[order[k]] = k;

  // Rewrite the nibble string through the ranking (skip the overflow class).
  for (uint16_t i = 0; i < nibble_count_; i++) {
    uint8_t b = nibbles_[i];
    uint8_t p = (b >> 4) & 0x0F;
    uint8_t s = b & 0x0F;
    if (p != PSI_OVERFLOW) p = remap[p];
    if (s != PSI_OVERFLOW) s = remap[s];
    nibbles_[i] = (uint8_t)((p << 4) | s);
  }

  // Reorder the bucket windows + hit counts to match, so index 0 is the shortest.
  uint16_t tmin[PSI_MICRO_ELEMENTS];
  uint16_t tmax[PSI_MICRO_ELEMENTS];
  uint16_t tcnt[PSI_MICRO_ELEMENTS];
  uint16_t tpul[PSI_MICRO_ELEMENTS];
  for (uint8_t k = 0; k < bucket_count_; k++) {
    tmin[k] = micro_min_[order[k]];
    tmax[k] = micro_max_[order[k]];
    tcnt[k] = micro_count_[order[k]];
    tpul[k] = micro_pulse_[order[k]];
  }
  for (uint8_t k = 0; k < bucket_count_; k++) {
    micro_min_[k] = tmin[k];
    micro_max_[k] = tmax[k];
    micro_count_[k] = tcnt[k];
    micro_pulse_[k] = tpul[k];
  }
}

uint8_t PulseSpaceIndex::detectDataType() const {
  // A side (pulse or space) "uses" a class if it placed >= sig elements in it.
  // If only the space side spreads across >=2 classes, the bit is in the gap
  // (PPM/PDM); if only the pulse side does, it's in the pulse width (PWM);
  // otherwise both vary (complementary PWM etc.) -> pack both.
  const uint16_t sig = 2;
  uint8_t pulse_classes = 0, space_classes = 0;
  for (uint8_t i = 0; i < bucket_count_; i++) {
    uint16_t pulses = micro_pulse_[i];
    uint16_t spaces = (micro_count_[i] >= pulses) ? (uint16_t)(micro_count_[i] - pulses) : 0;
    if (pulses >= sig) pulse_classes++;
    if (spaces >= sig) space_classes++;
  }
  if (pulse_classes <= 1 && space_classes >= 2) return PSI_DATA_S;
  if (space_classes <= 1 && pulse_classes >= 2) return PSI_DATA_P;
  return PSI_DATA_PS;
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
      micro_count_[i]++;
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
    micro_count_[best]++;
    return best;
  }

  // 3. New bucket if there is room.
  if (bucket_count_ < PSI_MICRO_ELEMENTS) {
    uint8_t i = bucket_count_++;
    micro_min_[i] = value;
    micro_max_[i] = value;
    micro_count_[i] = 1;
    micro_pulse_[i] = 0;
    return i;
  }

  // 4. No room: overflow class.
  return PSI_OVERFLOW;
}

uint8_t PulseSpaceIndex::addPair(uint16_t pulse_us, uint16_t space_us) {
  uint8_t pulse_idx = indexOf(pulse_us);
  if (pulse_idx != PSI_OVERFLOW) micro_pulse_[pulse_idx]++;
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
