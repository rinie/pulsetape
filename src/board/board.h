// board/board.h
// Single point of board selection. Define one BOARD_* macro (via the build
// system or here) and this header pulls in the matching board config, which
// provides RF_DATA_PIN and the capture timing/quality thresholds.
//
// If nothing is selected, default by architecture: ESP32 -> LilyGO T3, RP2040 ->
// SenseCAP Indicator. To port to a new board: add board/<name>.h exporting the
// same names and add a branch below. No other layer references a concrete board.

#ifndef PULSETAPE_BOARD_BOARD_H
#define PULSETAPE_BOARD_BOARD_H

#if !defined(BOARD_SENSECAP_INDICATOR) && !defined(BOARD_LILYGO_T3_V161)
  #if defined(ARDUINO_ARCH_ESP32)
    #define BOARD_LILYGO_T3_V161
  #else
    #define BOARD_SENSECAP_INDICATOR
  #endif
#endif

#if defined(BOARD_LILYGO_T3_V161)
#include "lilygo_t3_v161.h"
#elif defined(BOARD_SENSECAP_INDICATOR)
#include "sensecap_indicator.h"
#else
#error "No board selected. Define a BOARD_* macro (see board/board.h)."
#endif

#endif // PULSETAPE_BOARD_BOARD_H
