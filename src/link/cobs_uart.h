// link/cobs_uart.h  — STUB / future work (CONTEXT.md TODO #2)
//
// COBS-framed UART transport carrying RawTelegram from the RP2040 to the
// ESP32-S3. This slots in as the production replacement for the debug serial
// sink: the FrameAssembler's TelegramSink would encode + write here instead of
// (or in addition to) printing. Not implemented yet — interface sketch only.

#ifndef PULSETAPE_COBS_UART_H
#define PULSETAPE_COBS_UART_H

#include "../pulsetape/telegram.h"

// Encode `t` with COBS framing and write it to the UART link to the ESP32-S3.
// TODO: implement (COBS encode + UART write + 0x00 frame delimiter).
void cobs_uart_send(const RawTelegram& t);

#endif // PULSETAPE_COBS_UART_H
