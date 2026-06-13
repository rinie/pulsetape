// capture/tx_pio/tx_pio.h  — STUB / future work (CONTEXT.md TODO #4)
//
// TX pulse-replay backend: a second PIO state machine that drives STX_DATA_PIN
// (STX882 DATA) by clocking out a stored pulse/space sequence. Mirror of the RX
// PIO backend; isolated here so a different TX mechanism can replace it. Not
// implemented yet — interface sketch only.

#ifndef PULSETAPE_TX_PIO_H
#define PULSETAPE_TX_PIO_H

#include <stdint.h>

class TxPio {
 public:
  // Bring up the TX state machine on `data_pin`.
  // TODO: implement (load tx .pio program, configure SM as output).
  bool begin(uint8_t data_pin);

  // Replay a pulse/space sequence (microseconds), HIGH then LOW alternating.
  // TODO: implement (feed durations to the SM TX FIFO).
  void replay(const uint16_t* pulses, uint16_t count);
};

#endif // PULSETAPE_TX_PIO_H
