// pulsetape/psi.cpp — see psi.h.

#include "psi.h"

void PulseSpaceIndex::reset() {
  numBuckets = 0;
  numNibbles = 0;
}

void PulseSpaceIndex::normalize() {
  if (numBuckets < 2) return;

  // order[k] = old index of the k-th shortest bucket (insertion sort; <=15 buckets).
  uint8_t order[PSI_MICRO_ELEMENTS];
  for (uint8_t i = 0; i < numBuckets; i++) order[i] = i;
  for (uint8_t i = 1; i < numBuckets; i++) {
    uint8_t key = order[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && microMin[order[j]] > microMin[key]) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }

  // remap[oldIndex] = newRank. Ranks are < numBuckets <= 15, so never collide
  // with PSI_OVERFLOW (0x0F).
  uint8_t remap[PSI_MICRO_ELEMENTS];
  for (uint8_t k = 0; k < numBuckets; k++) remap[order[k]] = k;

  // Rewrite the nibble string through the ranking (skip the overflow class).
  for (uint16_t i = 0; i < numNibbles; i++) {
    uint8_t b = nibbleBuf[i];
    uint8_t p = (b >> 4) & 0x0F;
    uint8_t s = b & 0x0F;
    if (p != PSI_OVERFLOW) p = remap[p];
    if (s != PSI_OVERFLOW) s = remap[s];
    nibbleBuf[i] = (uint8_t)((p << 4) | s);
  }

  // Reorder the bucket windows + hit counts to match, so index 0 is the shortest.
  uint16_t tmin[PSI_MICRO_ELEMENTS];
  uint16_t tmax[PSI_MICRO_ELEMENTS];
  uint16_t tcnt[PSI_MICRO_ELEMENTS];
  uint16_t tpul[PSI_MICRO_ELEMENTS];
  for (uint8_t k = 0; k < numBuckets; k++) {
    tmin[k] = microMin[order[k]];
    tmax[k] = microMax[order[k]];
    tcnt[k] = microCount[order[k]];
    tpul[k] = microPulse[order[k]];
  }
  for (uint8_t k = 0; k < numBuckets; k++) {
    microMin[k] = tmin[k];
    microMax[k] = tmax[k];
    microCount[k] = tcnt[k];
    microPulse[k] = tpul[k];
  }
}

uint8_t PulseSpaceIndex::detectDataType() const {
  // A side (pulse or space) "uses" a class if it placed >= sig elements in it.
  // If only the space side spreads across >=2 classes, the bit is in the gap
  // (PPM/PDM); if only the pulse side does, it's in the pulse width (PWM);
  // otherwise both vary (complementary PWM etc.) -> pack both.
  const uint16_t sig = 2;
  uint8_t pulseClasses = 0, spaceClasses = 0;
  for (uint8_t i = 0; i < numBuckets; i++) {
    uint16_t pulses = microPulse[i];
    uint16_t spaces = (microCount[i] >= pulses) ? (uint16_t)(microCount[i] - pulses) : 0;
    if (pulses >= sig) pulseClasses++;
    if (spaces >= sig) spaceClasses++;
  }
  if (pulseClasses <= 1 && spaceClasses >= 2) return PSI_DATA_S;
  if (spaceClasses <= 1 && pulseClasses >= 2) return PSI_DATA_P;
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
  for (uint8_t i = 0; i < numBuckets; i++) {
    if (microMin[i] <= value && value <= microMax[i]) {
      microCount[i]++;
      return i;
    }
  }

  // 2. Closest bucket within tolerance: extend that bucket's window to include
  //    value. Pick the nearest edge across all buckets.
  const uint16_t tol = toleranceFor(value);
  uint8_t  best = PSI_OVERFLOW;
  uint16_t bestDist = 0;
  for (uint8_t i = 0; i < numBuckets; i++) {
    uint16_t dist;
    if (value < microMin[i]) {
      dist = microMin[i] - value;
    }
    else {  // value > microMax[i] (the == cases were handled in step 1)
      dist = value - microMax[i];
    }
    if (dist <= tol && (best == PSI_OVERFLOW || dist < bestDist)) {
      best = i;
      bestDist = dist;
    }
  }
  if (best != PSI_OVERFLOW) {
    if (value < microMin[best]) microMin[best] = value;
    if (value > microMax[best]) microMax[best] = value;
    microCount[best]++;
    return best;
  }

  // 3. New bucket if there is room.
  if (numBuckets < PSI_MICRO_ELEMENTS) {
    uint8_t i = numBuckets++;
    microMin[i] = value;
    microMax[i] = value;
    microCount[i] = 1;
    microPulse[i] = 0;
    return i;
  }

  // 4. No room: overflow class.
  return PSI_OVERFLOW;
}

uint8_t PulseSpaceIndex::addPair(uint16_t pulseUs, uint16_t spaceUs) {
  uint8_t pulseIdx = indexOf(pulseUs);
  if (pulseIdx != PSI_OVERFLOW) microPulse[pulseIdx]++;
  uint8_t spaceIdx = indexOf(spaceUs);
  uint8_t nibble = (uint8_t)(((pulseIdx & 0x0F) << 4) | (spaceIdx & 0x0F));
  if (numNibbles < PSI_MAX_NIBBLES) {
    nibbleBuf[numNibbles++] = nibble;
  }
  return nibble;
}

bool PulseSpaceIndex::nibblesEqual(const uint8_t* a, uint16_t aLen,
                                  const uint8_t* b, uint16_t bLen) {
  if (aLen != bLen) return false;
  for (uint16_t i = 0; i < aLen; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}
