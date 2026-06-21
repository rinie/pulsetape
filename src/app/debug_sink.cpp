// app/debug_sink.cpp — see debug_sink.h.

#include "debug_sink.h"
#include <Arduino.h>

// psix: data bits -> hex, pulsespaceindex style, packed per detected modulation:
//   PSI_DATA_PS — both pulse-bit and space-bit (2 bits/pair),
//   PSI_DATA_P  — pulse-bit only (PWM: gap is redundant),
//   PSI_DATA_S  — space-bit only (PPM/PDM: pulse is constant).
// Only pairs whose both indices are data classes (0/1) contribute; pairs touching
// a higher index (2+) are preamble/trailer and contribute no bits (no stripping).
// MSB-first, 4 bits per hex digit.
static void print_psix(const RawTelegram& t) {
  uint8_t acc = 0, nbits = 0;
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;
    if (p > 1 || s > 1) continue;   // preamble/trailer index: not part of the data hex
    if (t.data_type != PSI_DATA_S) {  // PS or P: pulse carries a bit
      acc = (uint8_t)((acc << 1) | (p ? 1 : 0)); if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
    }
    if (t.data_type != PSI_DATA_P) {  // PS or S: space carries a bit
      acc = (uint8_t)((acc << 1) | (s ? 1 : 0)); if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
    }
  }
  if (nbits > 0) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); }
}

void debug_print_telegram(const RawTelegram& t) {
  Serial.print("RF;count=");
  Serial.print(t.count);
  Serial.print(";repeats=");
  Serial.print(t.repeat_count);

  // micros: pulse length per index value, as ascending ranges (position = index).
  Serial.print(";micros=[");
  for (uint8_t i = 0; i < t.class_count; i++) {
    Serial.print(t.class_min[i]);
    if (t.class_max[i] != t.class_min[i]) { Serial.print('-'); Serial.print(t.class_max[i]); }
    if (i + 1 < t.class_count) Serial.print(',');
  }
  Serial.print(']');

  // counts: occurrences per index value (same order). Low count -> sync/spike.
  Serial.print(";counts=[");
  for (uint8_t i = 0; i < t.class_count; i++) {
    Serial.print(t.class_hits[i]);
    if (i + 1 < t.class_count) Serial.print(',');
  }
  Serial.print(']');

  // mod: detected modulation — which side of the pair carries the data bits.
  Serial.print(";mod=");
  Serial.print(t.data_type == PSI_DATA_P ? "p" : t.data_type == PSI_DATA_S ? "s" : "ps");

  // psix: data bits packed to hex (per mod).
  Serial.print(";psix=");
  print_psix(t);

  // psi: the raw pulse-space index string (one byte = one pulse/space class pair).
  Serial.print(";psi=");
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    if (t.nibbles[i] < 0x10) Serial.print('0');
    Serial.print(t.nibbles[i], HEX);
  }
  Serial.println();
}
