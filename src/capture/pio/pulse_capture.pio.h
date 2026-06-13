// -------------------------------------------------- //
// pulse_capture.pio.h
//
// HAND-ASSEMBLED from pulse_capture.pio (no pioasm toolchain was available when
// this was written). The intended source of truth is pioasm output — please
// REGENERATE and verify this file on hardware (see tools/build_pio.md). Each
// instruction word below is annotated with its decode so the encoding is
// checkable by eye.
// -------------------------------------------------- //

#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

#define pulse_capture_wrap_target 0
#define pulse_capture_wrap 14

static const uint16_t pulse_capture_program_instructions[] = {
            // .wrap_target
    0x2020, //  0: wait   0 pin, 0
    0x20a0, //  1: wait   1 pin, 0
    0xa02b, //  2: mov    x, ~null
    0x00c5, //  3: jmp    pin, 5          (high_loop -> high_cont)
    0x0007, //  4: jmp    7               (-> high_done)
    0x0043, //  5: jmp    x--, 3          (high_cont -> high_loop)
    0x000f, //  6: jmp    15              (-> send_gap)
    0xa0c9, //  7: mov    isr, ~x         (high_done)
    0x8000, //  8: push   noblock
    0xa02b, //  9: mov    x, ~null
    0x00cd, // 10: jmp    pin, 13         (low_loop -> low_done)
    0x004a, // 11: jmp    x--, 10         (-> low_loop)
    0x000f, // 12: jmp    15              (-> send_gap)
    0xa0c9, // 13: mov    isr, ~x         (low_done)
    0x8000, // 14: push   noblock
            // .wrap
    0xa0cb, // 15: mov    isr, ~null      (send_gap)
    0x8000, // 16: push   noblock
    0x0000, // 17: jmp    0               (-> start)
};

#if !PICO_NO_HARDWARE
static const struct pio_program pulse_capture_program = {
    .instructions = pulse_capture_program_instructions,
    .length = 18,
    .origin = -1,
};

static inline pio_sm_config pulse_capture_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + pulse_capture_wrap_target, offset + pulse_capture_wrap);
    return c;
}
#endif
