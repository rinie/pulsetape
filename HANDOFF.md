# HANDOFF — current state & how to pick this up

Onboarding note for a fresh session or a different machine (e.g. the laptop with
the Arduino toolchain). Read this first, then `CONTEXT.md`. As of 2026-06-18.

## What this is

A pure pulse-tape capture firmware for 433 MHz OOK/ASK (and IR, FSK — same edge
train). The RF front end produces a pulse train on a GPIO; a capture backend
times the edges; a hardware-free generic core quantises each pulse/space pair to
a nibble string (`psNibbleIndex`), filters noise, and validates by
**repeat-detection** (two identical nibble strings, no CRC needed). See
`README.md` for the layering and `CONTEXT.md` for the original design.

## Layering (the invariant that must hold)

```
app (.ino) → board (pins) → capture backend → generic core
```
- `src/pulsetape/` is **hardware-free** (no `Arduino.h`/`hardware/`/`driver/`/pins).
  Verify with: grep those tokens under `src/pulsetape/` — only comments should match.
- Swapping board / capture mechanism / radio must not touch `src/pulsetape/`.

## Targets

| Board | MCU | Backend | RF source | Toolchain |
|---|---|---|---|---|
| SenseCAP Indicator | RP2040 | PIO (`src/capture/pio/`) | SRX882S → GPIO27 | arduino-pico (earlephilhower) |
| LilyGO TTGO T3 LoRa32 433 V1.6.1 | ESP32 | RMT (`src/capture/rmt/`) | onboard SX1278 OOK → DIO2/GPIO32 (primary); SRX882S → GPIO36 via `-DPULSETAPE_RX_SOURCE_SRX882S` | **arduino-esp32 3.x (IDF v5)** |

The `.ino` selects backend + threading by `ARDUINO_ARCH_*`; `board.h` defaults the
board to match the architecture.

## ⚠️ Status: NOTHING is compiled or hardware-tested yet

No toolchain was available on the authoring machine. Treat all firmware as a
scaffold. Specifically unverified:
- **`src/capture/rmt/rmt_capture.cpp`** — assumes the ESP-IDF v5 RMT RX driver
  (`driver/rmt_rx.h`), i.e. **arduino-esp32 3.x**. Will not compile on 2.x.
- **`src/radio/sx1278_ook.cpp`** — bare-SPI SX1278 OOK init; register values are
  datasheet-derived (RxBw ~125 kHz, ~1.2 kbps, peak threshold + fixed floor).
  Converge with rtl_433_ESP / OOKwiz but are untuned for your unit.
- **`src/capture/pio/pulse_capture.pio.h`** — hand-assembled (no pioasm available);
  regenerate per `tools/build_pio.md`.
- Pin map for the T3 V1.6.1 varies by revision (esp. SX1276 RST 14 vs 23, OLED I2C)
  — verify against your board. DIO2→GPIO32 is solid.

## Toolchain (ESP32 / the T3)

- **Arduino IDE 2.x**: install **esp32 by Espressif 3.x** (Boards Manager). Board
  *TTGO LoRa32-OLED* or *ESP32 Dev Module* (pins come from `board.h`). Upload over
  USB @ 115200. Windows: install the **CH9102** USB driver if no COM port appears.
- **PlatformIO**: the `lilygo_t3_v161` env uses `platform = espressif32`, but stock
  espressif32 pulls arduino-esp32 **2.x** → RMT backend won't build. Use the
  **pioarduino** platform fork to get 3.x. (TODO: pin this in `platformio.ini`.)

## First bring-up checks (in order)

1. **Build** for the T3 with esp32 core 3.x. Expect first-compile errors (scaffold)
   — fix and note them.
2. **SPI wiring**: `sx1278_ook_begin()` returns false unless `RegVersion == 0x12`.
   Getting `true` proves the SX1278 SPI link.
3. **Capture**: trigger a known 433 remote (e.g. KAKU). Serial @ 115200 should show
   `RF;count=…;repeats=…;nibbles=<hex>;pulses=…`.
4. **Validation**: the **nibbles string is stable across repeats** and `repeats>=2`
   before a line is emitted — that stability is the validation.
5. If reception is weak/noisy: tune `RegRxBw`, the OOK fixed floor (`RegOokFix`,
   ~0x5A), and `FRAME_GAP_US` against real captures.

## Deferred decisions (don't silently change; tune on hardware)

- `FRAME_GAP_US = 8000` — OOKwiz uses ~2000 µs; tune after capturing real frames.
- Buffer sizes (`PSI_MAX_NIBBLES 256` / `TELEGRAM_MAX_PULSES 512`) — too small for
  long HVAC frames (Hitachi 424-bit ≈ 850 edges); bump to 512/1024 if needed
  (`long_packets.md`).

## Where things are documented

- `CONTEXT.md` — original architecture/intent · `rf_telegram.md` — PIO + C layer
- `protocol_timings.md` — OOK timings + repeat-vs-checksum philosophy
- `long_packets.md` — HVAC long packets, modulation/IR generality, buffer sizing
- `receivers.md` — receiver comparison, SX1278+DIO2 decision, OOKwiz/rtl_433_ESP
  lessons, **licensing line** (PulseTape MIT; OOKwiz LGPL-3.0 & rtl_433_ESP GPL are
  reference-only — facts/ideas reused, **no code copied**)
- `bbb_ninja_port.md` — BeagleBone Black port sketch
- `tools/build_pio.md` — regenerating the PIO header

## Suggested next steps

1. Get it compiling on the Arduino laptop (esp32 core 3.x); fix scaffold errors.
2. Bench bring-up checks above; tune RxBw / OOK floor / frame-gap.
3. Optional, needs no hardware: a host (`g++`/CMake) test feeding synthetic pulse
   trains through `PulseSpaceIndex` + `FrameAssembler` + `RepeatDetector`.
4. Then: COBS UART link, ESP32 decode/MQTT, SX1276 TX (`src/link/`, `src/decode/`,
   `src/capture/tx_pio/` stubs exist).

Note: the per-project auto-memory lives in `~/.claude/...` on the original machine,
not in this repo — its substance is mirrored in the docs above.
