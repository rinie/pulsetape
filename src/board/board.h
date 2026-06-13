// board/board.h
// Single point of board selection. Define exactly one BOARD_* macro (via the
// build system or here) and this header pulls in the matching board config,
// which provides RF_DATA_PIN, STX_DATA_PIN, PIO_CLOCK_DIV, TICKS_PER_US and the
// capture timing thresholds.
//
// To port to a new board: add board/<name>.h exporting the same names, then add
// a branch below. No other layer references a concrete board header.

#ifndef PULSETAPE_BOARD_BOARD_H
#define PULSETAPE_BOARD_BOARD_H

// Default to the SenseCAP Indicator when no board is selected by the build.
#if !defined(BOARD_SENSECAP_INDICATOR)
#define BOARD_SENSECAP_INDICATOR
#endif

#if defined(BOARD_SENSECAP_INDICATOR)
#include "sensecap_indicator.h"
#else
#error "No board selected. Define a BOARD_* macro (see board/board.h)."
#endif

#endif // PULSETAPE_BOARD_BOARD_H
