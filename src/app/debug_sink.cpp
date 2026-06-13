// app/debug_sink.cpp — see debug_sink.h.

#include "debug_sink.h"
#include <Arduino.h>

void debug_print_telegram(const RawTelegram& t) {
  Serial.print("RF;count=");
  Serial.print(t.count);
  Serial.print(";repeats=");
  Serial.print(t.repeat_count);

  // Nibble fingerprint as hex (one byte = one pulse/space pair).
  Serial.print(";nibbles=");
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    if (t.nibbles[i] < 0x10) Serial.print('0');
    Serial.print(t.nibbles[i], HEX);
  }

  // Raw durations in microseconds.
  Serial.print(";pulses=");
  for (uint16_t i = 0; i < t.count; i++) {
    Serial.print(t.pulses[i]);
    if (i + 1 < t.count) Serial.print(',');
  }
  Serial.println();
}
