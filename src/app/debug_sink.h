// app/debug_sink.h
// Core-0 consumer: prints a captured telegram as an RFLink-style debug line over
// USB serial. App layer — Arduino-aware on purpose.

#ifndef PULSETAPE_DEBUG_SINK_H
#define PULSETAPE_DEBUG_SINK_H

#include "../pulsetape/telegram.h"

// Print one telegram:
//   RF;count=N;repeats=R;nibbles=<hex>;pulses=p1,p2,...
void debug_print_telegram(const RawTelegram& t);

#endif // PULSETAPE_DEBUG_SINK_H
