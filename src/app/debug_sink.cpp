// app/debug_sink.cpp — see debug_sink.h.

#include "debug_sink.h"
#include <Arduino.h>

// The data-bearing side(s) depend on modulation:
//   PSI_DATA_PS — both pulse and space carry bits,
//   PSI_DATA_P  — only the pulse (space is constant),
//   PSI_DATA_S  — only the space (pulse is constant).
static inline bool usesPulse(const RawTelegram& t) { return t.dataType != PSI_DATA_S; }
static inline bool usesSpace(const RawTelegram& t) { return t.dataType != PSI_DATA_P; }

// Type token, also used as the field-name suffix: ps / s / p, with the constant
// side's class index as a prefix when it isn't 0 (e.g. "1s" = pulse constant at
// class 1). So fields read xps/ips, xs/is, x1s/i1s, xp/ip, ...
static void printType(const RawTelegram& t) {
  if (t.dataType == PSI_DATA_PS) { Serial.print("ps"); return; }
  if (t.constClass > 0) Serial.print(t.constClass);
  Serial.print(t.dataType == PSI_DATA_P ? "p" : "s");
}

// x<type>: data bits -> hex. A data class (0/1) packs one bit (MSB-first, 4/digit);
// a higher class (2+ = sync/preamble) flushes the partial nibble and prints as
// "<class>:" — so NewKAKU reads xs=2:<hex>.
static void emitVal(uint8_t v, uint8_t& acc, uint8_t& nbits) {
  if (v <= 1) {
    acc = (uint8_t)((acc << 1) | v);
    if (++nbits == 4) { Serial.print(acc, HEX); acc = 0; nbits = 0; }
  }
  else {
    if (nbits) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); acc = 0; nbits = 0; }
    Serial.print(v); Serial.print(':');
  }
}
static void printXhex(const RawTelegram& t) {
  uint8_t acc = 0, nbits = 0;
  for (uint16_t i = 0; i < t.nibbleCount; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;
    if (usesPulse(t)) emitVal(p, acc, nbits);
    if (usesSpace(t)) emitVal(s, acc, nbits);
  }
  if (nbits) { acc = (uint8_t)(acc << (4 - nbits)); Serial.print(acc, HEX); }
}

// i<type>: raw class-index string of the data-bearing side(s) only (the constant
// side is dropped — lossless, the type records its value). No ':' markers here.
static void printIindex(const RawTelegram& t) {
  for (uint16_t i = 0; i < t.nibbleCount; i++) {
    uint8_t p = (t.nibbles[i] >> 4) & 0x0F;
    uint8_t s = t.nibbles[i] & 0x0F;
    if (usesPulse(t)) Serial.print(p, HEX);
    if (usesSpace(t)) Serial.print(s, HEX);
  }
}

void debugPrintTelegram(const RawTelegram& t) {
  Serial.print("RF;count=");
  Serial.print(t.count);
  Serial.print(";repeats=");
  Serial.print(t.repeatCount);

  // gap: silence to the previous repeat in microseconds (inter-telegram space),
  // same unit as micros[] below. Only present once a repeat has been seen — a
  // lone frame has nothing to measure.
  if (t.gapUs > 0) {
    Serial.print(";gap=");
    Serial.print(t.gapUs);
  }

  // state: pressed (confirmed at threshold) or released (window closed, true total).
  Serial.print(";state=");
  Serial.print(t.event == TELEGRAM_PRESSED ? "pressed"
             : t.event == TELEGRAM_RELEASED ? "released" : "?");

  // micros: pulse length per index value, ascending ranges (position = index).
  Serial.print(";micros=[");
  for (uint8_t i = 0; i < t.classCount; i++) {
    Serial.print(t.classMin[i]);
    if (t.classMax[i] != t.classMin[i]) { Serial.print('-'); Serial.print(t.classMax[i]); }
    if (i + 1 < t.classCount) Serial.print(',');
  }
  Serial.print(']');

  // counts: occurrences per index value (same order). Low count -> sync/spike.
  Serial.print(";counts=[");
  for (uint8_t i = 0; i < t.classCount; i++) {
    Serial.print(t.classHits[i]);
    if (i + 1 < t.classCount) Serial.print(',');
  }
  Serial.print(']');

  // x<type>: hexed index values (data bits, sync as "N:").
  Serial.print(";x"); printType(t); Serial.print('=');
  printXhex(t);

  // i<type>: raw index values of the data-bearing side(s).
  Serial.print(";i"); printType(t); Serial.print('=');
  printIindex(t);

  Serial.println();
}
