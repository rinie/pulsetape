// app/debug_sink.cpp — see debug_sink.h.

#include "debug_sink.h"
#include <Arduino.h>

// Render the normalized nibble string to hex, in the style of pulsespaceindex's
// psix(): the two shortest classes (0,1) are DATA — each (pulse,space) pair emits
// two bits (pulse-bit then space-bit), packed MSB-first, 4 bits per hex digit. Any
// pair touching a higher class (2+ = sync/long) is a header/trailer marker, printed
// as its two class digits and set off from the data with a ':'.
static void print_hex(const RawTelegram& t) {
  uint8_t acc = 0, nbits = 0;
  bool in_data = false, any = false;

  for (uint16_t i = 0; i < t.nibble_count; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;

    if (p <= 1 && s <= 1) {                 // data pair
      if (!in_data && any) Serial.print(':');
      in_data = true; any = true;
      acc = (uint8_t)((acc << 1) | (p ? 1 : 0)); if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
      acc = (uint8_t)((acc << 1) | (s ? 1 : 0)); if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
    } else {                                // header/trailer (sync/long) pair
      if (nbits > 0) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); acc = 0; nbits = 0; }
      if (in_data) Serial.print(':');
      in_data = false; any = true;
      Serial.print(p); Serial.print(s);
    }
  }
  if (nbits > 0) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); }
}

void debug_print_telegram(const RawTelegram& t) {
  Serial.print("RF;count=");
  Serial.print(t.count);
  Serial.print(";repeats=");
  Serial.print(t.repeat_count);

  // psix-style hex of the fingerprint.
  Serial.print(";hex=");
  print_hex(t);

  // Raw nibble fingerprint (one byte = one pulse/space class pair).
  Serial.print(";nibbles=");
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    if (t.nibbles[i] < 0x10) Serial.print('0');
    Serial.print(t.nibbles[i], HEX);
  }

  // Pulse length per index value (the timing-class table), not the full telegram.
  // e.g. classes=0:192-256,1:1280-1344,2:2688
  Serial.print(";classes=");
  for (uint8_t i = 0; i < t.class_count; i++) {
    Serial.print(i); Serial.print(':');
    Serial.print(t.class_min[i]);
    if (t.class_max[i] != t.class_min[i]) { Serial.print('-'); Serial.print(t.class_max[i]); }
    if (i + 1 < t.class_count) Serial.print(',');
  }
  Serial.println();
}
