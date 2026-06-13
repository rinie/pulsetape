# Port Note — PulseTape on BeagleBone Black + NinjaBlocks

## Status

Design note only. Not built or tested. The target hardware is the original
**NinjaBlocks** kit (BeagleBone Black + Ninja shield). This note maps the
existing PulseTape layering onto that platform so the port can be scoped before
any code is written. See `CONTEXT.md` / `rf_telegram.md` for the RP2040 design
this derives from.

---

## What carries over unchanged

The entire **generic core** (`src/pulsetape/`) is plain C++ with no hardware
includes, so it ports verbatim:

- `PulseSpaceIndex` (`psNibbleIndex` quantiser + nibble string)
- `RawTelegram`, `telegram_valid()` filter
- `RepeatDetector` (nibble-string compare, no hash)
- the `ICaptureBackend` contract + `FrameAssembler`

This is the payoff of the layering: the algorithm that took the most thought is
exactly the part that moves without edits. On the BBB it compiles as a native
Linux binary (or runs as the existing `pulsespaceindex.js` Node port — see
below).

## What has to be replaced

| Layer | RP2040 / SenseCAP | BeagleBone Black + Ninja |
|---|---|---|
| Capture backend | PIO state machine (`src/capture/pio/`) | **new backend** — PRU, `gpiod`, or shield MCU (see below) |
| Board | `src/board/sensecap_indicator.h` | new `src/board/beaglebone_ninja.h` (P8/P9 GPIO) |
| Build | Arduino arduino-pico / PlatformIO | **native Linux CMake/g++** (or cross-compile) |
| Inter-core transport | SDK `queue_t` between cores | PRU shared memory / RPMsg, or a userspace ring buffer |
| Output | USB-serial debug line | stdout / MQTT / socket (BBB is the network host) |

The board + capture backend are the only parts touching hardware; everything
above the `ICaptureBackend` line is untouched.

---

## The NinjaBlocks topology matters

The original NinjaBlock paired a Linux board with an **Arduino-class shield**
("Ninja shield") that owned the 433 MHz RX/TX and sensors and talked to the host
over **serial**. Confirm which of these the physical board is, because it
decides where the capture backend lives:

**(A) Shield MCU does capture, BBB decodes** — closest to the PulseTape
RP2040+ESP32 split. The shield's MCU plays the RP2040 role; the BBB plays the
ESP32-S3/Linux role.

```
433 RX --> Ninja shield MCU (ATmega)        BeagleBone Black (Linux)
           - pulse capture                  - generic core (psi/telegram)
           - frame assembly       serial    - decoders / MQTT / display
           - psNibbleIndex        ------->   - or just run pulsespaceindex.js
```
- Capture backend on the MCU: **timer input-capture / pin-change interrupts**
  (AVR has no PIO). The serial line carries `RawTelegram` (reuse the planned
  COBS framing from `src/link/cobs_uart.h`).
- The BBB side becomes a thin serial reader feeding the generic core — or it
  just consumes already-assembled telegrams from the shield.

**(B) RF DATA wired straight to a BBB GPIO** — no capture MCU; the BBB does
everything.

```
433 RX DATA --> BBB GPIO --> PRU or gpiod --> generic core (same process)
```

---

## Capture backend options on the BBB (option B)

Ordered best-fidelity → least-effort:

1. **PRU-ICSS** — the BBB's two 200 MHz real-time cores are the genuine analog
   to RP2040 PIO: deterministic, ~5 ns/instruction, isolated from the Linux
   scheduler. Write a small PRU firmware that measures pulse/space durations
   into shared memory; a Linux reader (RPMsg or `/dev/mem` mmap) drains it into
   `FrameAssembler`. Best timing, most setup (PRU toolchain, device-tree
   overlay, pinmux). The 433Utils BBB work uses the PRU for the same reason.
2. **`gpiod` edge events** — the kernel chardev delivers per-edge **nanosecond
   timestamps** to userspace. No PRU code, but exposed to scheduler jitter. The
   tolerance windows in `psNibbleIndex` absorb a lot of jitter, so PWM/PPM
   families (KAKU, X10, DIO) are usually fine; tight Manchester (Oregon) is
   marginal. Good first cut to validate the port before investing in PRU.
3. **sysfs / legacy IRQ polling** — avoid; worst jitter, deprecated.

Each option is a single class implementing `ICaptureBackend::begin()` + `next()`
emitting `CaptureEvent{DURATION|FRAME_GAP}` in microseconds. The same C-side
frame-gap rule applies (a LOW > `FRAME_GAP_US` closes the frame).

---

## Build environment

- BBB runs Debian; build with **CMake + g++** natively on the board, or
  cross-compile from a host (arm-linux-gnueabihf). No Arduino layer.
- A `native`/Linux target can live beside the Arduino one without disturbing it:
  the generic core already has no Arduino dependency, so a small `CMakeLists.txt`
  compiling `src/pulsetape/*.cpp` + the chosen backend is all that's needed.
- PRU firmware builds separately with TI's `clpru` / `pru-gcc`.

## Bonus: the JS route

`CONTEXT.md` references `pulsespaceindex.js`, a Node port of the same algorithm.
Since the BBB is a full Linux host (and NinjaBlocks' own stack was Node-based),
the decode side can run that JS directly — fed pulse timings from PRU or
`gpiod` — with no C++ build at all. Reasonable when the BBB is the decode host
rather than a constrained MCU.

---

## Open questions / TODO

- [ ] Confirm shield topology: capture-MCU-over-serial (A) vs direct-GPIO (B).
- [ ] If (B): pick PRU vs `gpiod` for the first cut (suggest `gpiod` to validate,
      PRU for production).
- [ ] Identify the Ninja shield's 433 MHz RX DATA pin → BBB P8/P9 GPIO; write
      `src/board/beaglebone_ninja.h`.
- [ ] Add a Linux `CMakeLists.txt` target for `src/pulsetape/` + backend.
- [ ] Decide output transport (stdout / MQTT / socket) for the Linux host.
- [ ] Reuse `src/link/cobs_uart.h` framing if topology (A).

## References

- BeagleboneBlack-433Utils — https://github.com/bjuriewicz/BeagleboneBlack-433Utils
- RF wireless kernel module for Pi/BeagleBone (Hackaday) —
  https://hackaday.com/2013/06/27/rf-wireless-kernel-module-for-raspberry-pi-beaglebone-and-others/
