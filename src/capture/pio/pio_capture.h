// capture/pio/pio_capture.h
// PIO-based capture backend for RP2040 / RP2350.
//
// HARDWARE LAYER: this is the *only* place that includes hardware/pio.h. It
// implements the generic ICaptureBackend contract, so the generic core never
// knows PIO exists. To move to a different capture mechanism (interrupt, RMT,
// timer-capture) or different silicon, add a sibling backend implementing the
// same interface — nothing under src/pulsetape/ changes.

#ifndef PULSETAPE_PIO_CAPTURE_H
#define PULSETAPE_PIO_CAPTURE_H

#if defined(ARDUINO_ARCH_RP2040)

#include "hardware/pio.h"
#include "../../pulsetape/capture_iface.h"

class PioCapture : public ICaptureBackend {
 public:
  // `pio` selects the PIO block (default pio0). The program is loaded at a fixed
  // offset 0 so its absolute jmp targets are correct without a build toolchain;
  // therefore nothing else may occupy offset 0 on the same PIO block.
  explicit PioCapture(PIO pio = pio0) : pio_(pio), sm_(0), pio_clk_hz_(0) {}

  bool begin(uint8_t data_pin) override;
  CaptureEvent next() override;

 private:
  uint32_t ticksToUs(uint32_t count) const;

  PIO      pio_;
  uint     sm_;
  uint32_t pio_clk_hz_;  // effective PIO clock after clkdiv, cached in begin()
};

#endif // ARDUINO_ARCH_RP2040
#endif // PULSETAPE_PIO_CAPTURE_H
