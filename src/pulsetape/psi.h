// pulsetape/psi.h
// PulseSpaceIndex: adaptive quantiser that turns a stream of (pulse, space)
// durations into a string of 4-bit indices ("nibbles"). Ported from
// rinie/NodoDueRkr pulsespaceindex.h (psNibbleIndex).
//
// Each distinct timing class becomes a bucket with an adaptive [min,max] window.
// A pulse/space pair is encoded as one byte: (pulseIndex << 4) | spaceIndex.
// The resulting nibble string IS the repeat fingerprint of a telegram: two
// repetitions of the same OOK frame produce the same string because the
// tolerance windows absorb ~5% timing drift. No CRC or hash needed.
//
// GENERIC LAYER: plain C++, no Arduino.h, no hardware headers, no board pins.

#ifndef PULSETAPE_PSI_H
#define PULSETAPE_PSI_H

#include <stdint.h>
#include <stddef.h>

// Max distinct timing buckets. Index 0x0F is reserved as the overflow class
// (value did not fit any bucket and no room to allocate a new one).
#define PSI_MICRO_ELEMENTS 15
#define PSI_OVERFLOW 0x0F

// Capacity of the nibble string (one byte per pulse/space pair).
#define PSI_MAX_NIBBLES 256

class PulseSpaceIndex {
 public:
  PulseSpaceIndex() { reset(); }

  // Clear all bucket state and the nibble string. Call once per telegram.
  void reset();

  // Feed one pulse/space pair (microseconds). Returns the packed nibble byte
  // (pulseIndex << 4) | spaceIndex and appends it to the nibble string.
  // Once the nibble string is full, further pairs are quantised but not stored.
  uint8_t addPair(uint16_t pulse_us, uint16_t space_us);

  // Re-rank timing classes by ascending duration (class 0 = shortest) and rewrite
  // the whole nibble string through that ranking. Call once after the last
  // addPair(), before reading the nibbles. This makes the fingerprint canonical
  // and independent of the order classes were discovered in — so two captures of
  // the same telegram match even if one started mid-frame or on a glitch. The
  // overflow class (PSI_OVERFLOW) is left unchanged.
  void normalize();

  // Number of nibble bytes accumulated (== number of pulse/space pairs stored).
  uint16_t nibbleCount() const { return nibble_count_; }

  // Pointer to the nibble string (length == nibbleCount()).
  const uint8_t* nibbles() const { return nibbles_; }

  // Number of distinct timing buckets discovered so far.
  uint8_t bucketCount() const { return bucket_count_; }

  // Inspect a bucket's adaptive window (for debug / protocol classification).
  uint16_t bucketMin(uint8_t i) const { return micro_min_[i]; }
  uint16_t bucketMax(uint8_t i) const { return micro_max_[i]; }

  // Compare two nibble strings for repeat detection. Returns true when equal.
  static bool nibblesEqual(const uint8_t* a, uint16_t a_len,
                           const uint8_t* b, uint16_t b_len);

 private:
  // Quantise a single duration to a bucket index, growing/extending buckets.
  uint8_t indexOf(uint16_t value);

  // Tolerance window (us) for snapping a value onto an existing bucket edge.
  static uint16_t toleranceFor(uint16_t value);

  uint16_t micro_min_[PSI_MICRO_ELEMENTS];
  uint16_t micro_max_[PSI_MICRO_ELEMENTS];
  uint8_t  bucket_count_;

  uint8_t  nibbles_[PSI_MAX_NIBBLES];
  uint16_t nibble_count_;
};

#endif // PULSETAPE_PSI_H
