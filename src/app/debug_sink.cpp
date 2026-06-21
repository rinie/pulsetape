// app/debug_sink.cpp — see debug_sink.h.

#include "debug_sink.h"
#include <Arduino.h>

// psix: hex of the data bits, pulsespaceindex style. Each (pulse,space) pair whose
// BOTH indices are data classes (0 or 1) contributes two bits — pulse-bit then
// space-bit — packed MSB-first, 4 bits per hex digit. Pairs touching a higher
// index (2+) are assumed preamble/trailer and contribute no bits (no stripping —
// they stay visible in the psi string and the micros table). This is the 'ps'
// (pulse+space) mode; pure PWM ('p') / PPM ('s') would pack one side only.
static void print_psix(const RawTelegram& t) {
  uint8_t acc = 0, nbits = 0;
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;
    if (p > 1 || s > 1) continue;   // preamble/trailer index: not part of the data hex
    acc = (uint8_t)((acc << 1) | (p ? 1 : 0)); if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
    acc = (uint8_t)((acc << 1) | (s ? 1 : 0)); if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
  }
  if (nbits > 0) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); }
}

void debug_print_telegram(const RawTelegram& t) {
  Serial.print("RF;count=");
  Serial.print(t.count);
  Serial.print(";repeats=");
  Serial.print(t.repeat_count);

  // micros: the pulse length per index value, as ranges (ascending by duration).
  // e.g. micros=0:192-256,1:1280-1344,2:2688
  Serial.print(";micros=");
  for (uint8_t i = 0; i < t.class_count; i++) {
    Serial.print(i); Serial.print(':');
    Serial.print(t.class_min[i]);
    if (t.class_max[i] != t.class_min[i]) { Serial.print('-'); Serial.print(t.class_max[i]); }
    if (i + 1 < t.class_count) Serial.print(',');
  }

  // psix: data bits packed to hex (0/1 indices only).
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
