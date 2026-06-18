// capture/pio/pio_capture.cpp — see pio_capture.h.

#if defined(ARDUINO_ARCH_RP2040)

#include "pio_capture.h"
#include "pulse_capture.pio.h"
#include "../../board/board.h"

#include "hardware/clocks.h"

// Sentinel emitted by the PIO program on counter underflow.
static const uint32_t PIO_SENTINEL = 0xFFFFFFFFu;

// PIO cycles consumed per counter decrement in the inner measure loops. Both the
// HIGH and LOW loops in pulse_capture.pio are exactly 2 instructions/cycle long.
static const uint32_t CYCLES_PER_COUNT = 2;

// Load the program at this fixed offset so the assembled absolute jmp targets
// (which assume origin 0) are valid without running pioasm at build time.
static const uint PROGRAM_OFFSET = 0;

bool PioCapture::begin(uint8_t data_pin) {
  // Load the capture program at a known offset.
  pio_add_program_at_offset(pio_, &pulse_capture_program, PROGRAM_OFFSET);

  int sm = pio_claim_unused_sm(pio_, false);
  if (sm < 0) return false;
  sm_ = (uint)sm;

  // Route the GPIO to PIO and configure it as an input the SM can read.
  pio_gpio_init(pio_, data_pin);
  pio_sm_set_consecutive_pindirs(pio_, sm_, data_pin, 1, false);

  pio_sm_config c = pulse_capture_program_get_default_config(PROGRAM_OFFSET);
  sm_config_set_in_pins(&c, data_pin);   // `wait ... pin` source
  sm_config_set_jmp_pin(&c, data_pin);   // `jmp pin` test source
  sm_config_set_clkdiv_int_frac(&c, PIO_CLOCK_DIV, 0);
  // Manual push via `push noblock`; no autopush, full 32-bit ISR.
  sm_config_set_in_shift(&c, false, false, 32);

  pio_sm_init(pio_, sm_, PROGRAM_OFFSET, &c);

  // Cache the effective PIO clock for tick->us conversion.
  pio_clk_hz_ = clock_get_hz(clk_sys) / PIO_CLOCK_DIV;

  pio_sm_set_enabled(pio_, sm_, true);
  return true;
}

uint32_t PioCapture::ticksToUs(uint32_t count) const {
  if (pio_clk_hz_ == 0) return 0;
  // microseconds = count * cycles_per_count / pio_clk_hz * 1e6
  return (uint32_t)((uint64_t)count * CYCLES_PER_COUNT * 1000000ull / pio_clk_hz_);
}

CaptureEvent PioCapture::next() {
  CaptureEvent ev;
  uint32_t raw = pio_sm_get_blocking(pio_, sm_);

  if (raw == PIO_SENTINEL) {
    ev.type = CaptureEvent::FRAME_GAP;
    ev.duration_us = 0;
    return ev;
  }

  uint32_t us = ticksToUs(raw);
  if (us > FRAME_GAP_US) {
    // A silence this long ends the frame; report it as a gap rather than a
    // (potentially uint16-overflowing) duration.
    ev.type = CaptureEvent::FRAME_GAP;
    ev.duration_us = 0;
    return ev;
  }

  ev.type = CaptureEvent::DURATION;
  ev.duration_us = (uint16_t)us;
  return ev;
}

#endif // ARDUINO_ARCH_RP2040
