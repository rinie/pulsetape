// app/debug_sink.cpp — see debug_sink.h.

#include "debug_sink.h"
#include <Arduino.h>

// The data-bearing side(s) depend on modulation:
//   PSI_DATA_PS — both pulse and space carry bits,
//   PSI_DATA_P  — only the pulse (space is constant),
//   PSI_DATA_S  — only the space (pulse is constant).
static inline bool uses_pulse(const RawTelegram& t) { return t.data_type != PSI_DATA_S; }
static inline bool uses_space(const RawTelegram& t) { return t.data_type != PSI_DATA_P; }

// Type token, also used as the field-name suffix: ps / s / p, with the constant
// side's class index as a prefix when it isn't 0 (e.g. "1s" = pulse constant at
// class 1). So fields read xps/ips, xs/is, x1s/i1s, xp/ip, ...
static void print_type(const RawTelegram& t) {
  if (t.data_type == PSI_DATA_PS) { Serial.print("ps"); return; }
  if (t.const_class > 0) Serial.print(t.const_class);
  Serial.print(t.data_type == PSI_DATA_P ? "p" : "s");
}

// x<type>: data bits -> hex. A data class (0/1) packs one bit (MSB-first, 4/digit);
// a higher class (2+ = sync/preamble) flushes the partial nibble and prints as
// "<class>:" — so NewKAKU reads xs=2:<hex>.
static void emit_val(uint8_t v, uint8_t& acc, uint8_t& nbits) {
  if (v <= 1) {
    acc = (uint8_t)((acc << 1) | v);
    if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
  } else {
    if (nbits) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); acc = 0; nbits = 0; }
    Serial.print(v); Serial.print(':');
  }
}
static void print_xhex(const RawTelegram& t) {
  uint8_t acc = 0, nbits = 0;
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;
    if (uses_pulse(t)) emit_val(p, acc, nbits);
    if (uses_space(t)) emit_val(s, acc, nbits);
  }
  if (nbits) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); }
}

// i<type>: raw class-index string of the data-bearing side(s) only (the constant
// side is dropped — lossless, the type records its value). No ':' markers here.
static void print_iindex(const RawTelegram& t) {
  for (uint16_t i = 0; i < t.nibble_count; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;
    if (uses_pulse(t)) Serial.print(p, HEX);
    if (uses_space(t)) Serial.print(s, HEX);
  }
}

void debug_print_telegram(const RawTelegram& t) {
  Serial.print("RF;count=");
  Serial.print(t.count);
  Serial.print(";repeats=");
  Serial.print(t.repeat_count);

  // gap: silence to the previous repeat in microseconds (inter-telegram space),
  // same unit as micros[] below. Only present once a repeat has been seen — a
  // lone frame has nothing to measure.
  if (t.gap_us > 0) {
    Serial.print(";gap=");
    Serial.print(t.gap_us);
  }

  // state: pressed (confirmed at threshold) or released (window closed, true total).
  Serial.print(";state=");
  Serial.print(t.event == TELEGRAM_PRESSED ? "pressed"
             : t.event == TELEGRAM_RELEASED ? "released" : "?");

  // micros: pulse length per index value, ascending ranges (position = index).
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

  // x<type>: hexed index values (data bits, sync as "N:").
  Serial.print(";x"); print_type(t); Serial.print('=');
  print_xhex(t);

  // i<type>: raw index values of the data-bearing side(s).
  Serial.print(";i"); print_type(t); Serial.print('=');
  print_iindex(t);

  Serial.println();
}
