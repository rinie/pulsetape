// decode/manchester.h  — STUB / future work (CONTEXT.md TODO #6)
//
// Manchester / Oregon Scientific v2/v3 decoder operating on a RawTelegram. This
// is a generic decoder (no hardware deps) that would run on core 0 / the
// ESP32-S3 above the pulse-tape layer. See rf_telegram.md for a worked example
// and NodoDueRkr RkrManchesterAnalysis for the adaptive-bucket approach. Not
// implemented yet — interface sketch only.

#ifndef PULSETAPE_DECODE_MANCHESTER_H
#define PULSETAPE_DECODE_MANCHESTER_H

#include <stdint.h>
#include "../pulsetape/telegram.h"

// Decode `t` into Manchester bits. Returns true on a plausible decode.
// TODO: implement (preamble skip, half/full symbol classification, bit extract).
bool decodeManchester(const RawTelegram& t, uint8_t* bitsOut, uint16_t* bitsLen);

#endif // PULSETAPE_DECODE_MANCHESTER_H
