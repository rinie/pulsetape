# PulseTape

Pure pulse-tape capture firmware for 433 MHz OOK/ASK receivers on the RP2040,
built for the Seeed SenseCAP Indicator but structured to move to other boards
(RP2350, etc.) and other capture mechanisms with minimal churn.

The RP2040 captures raw RF pulse trains with a PIO state machine, quantises each
pulse/space pair into a nibble string with `psNibbleIndex` (the repeat
fingerprint), filters noise, detects repeats, and emits validated telegrams.
Protocol decoding (Manchester/Oregon, KAKU, …) and the ESP32-S3 WiFi/display
bridge live above this layer. See `CONTEXT.md`, `piowiring.md`, `rf_telegram.md`
for the full design.

## Layering (why the directories look like this)

Strict dependency direction — the generic core depends on nothing below it:

| Layer | Path | Knows about |
|---|---|---|
| Generic core | `src/pulsetape/` | nothing hardware — plain C++, no `Arduino.h`, no PIO, no pins |
| Capture backend | `src/capture/pio/` (RP2040), `src/capture/rmt/` (ESP32) | implements `capture_iface.h` |
| Board | `src/board/` | per-board pins, clock, thresholds |
| App | `pulsetape.ino`, `src/app/` | wires the above together, prints debug |

- **Swap the board** → add `src/board/<name>.h` and point `board.h` at it.
- **Swap the capture mechanism** (RMT/interrupt/timer instead of PIO) → add a
  sibling backend implementing `ICaptureBackend`. `src/pulsetape/` does not change.

### Supported targets

| Board | MCU | Capture backend | RF receiver |
|---|---|---|---|
| SenseCAP Indicator | RP2040 | PIO (`src/capture/pio/`) | SRX882S → GPIO27 |
| LilyGO TTGO T3 LoRa32 433 V1.6.1 | ESP32 | RMT (`src/capture/rmt/`) | external SRX882S → GPIO36 (onboard SX1276 reserved for TX — see `receivers.md`) |

The `.ino` selects backend + threading by architecture macro; `board.h` defaults
the board to match (ESP32 → LilyGO, RP2040 → SenseCAP) or honours an explicit
`-DBOARD_*` flag.

Future work slots (stub headers): `src/link/cobs_uart.h`,
`src/decode/manchester.h`, `src/capture/tx_pio/tx_pio.h`.

## Build & flash

### Arduino IDE
1. Install the core for your target: **arduino-pico** (earlephilhower) for the
   SenseCAP/RP2040, or **arduino-esp32 3.x** for the LilyGO T3/ESP32.
2. Open `pulsetape.ino` and select the matching board.
3. Flash:
   - RP2040: bootloader mode (hold BOOT pinhole while connecting USB → `RPI-BOOT`).
   - ESP32: just Upload over USB.
4. Open Serial Monitor at 115200 baud.

Arduino IDE compiles the sketch plus `src/` recursively; backends for the other
architecture compile to nothing (guarded by `ARDUINO_ARCH_*`), so all layers are
picked up automatically.

### PlatformIO (optional)
```sh
pio run                # build
pio run -t upload      # flash (BOOT pinhole first)
pio device monitor     # serial @ 115200
```
See `platformio.ini`.

## Output

Each forwarded telegram prints as:

```
RF;count=<n>;repeats=<r>;nibbles=<hex>;pulses=<us,us,...>
```

The **nibbles** field is the repeat fingerprint: repeats of the same telegram
produce an identical string, and `repeats>=REPEAT_MIN_COUNT` is the validation
(no CRC needed).

## Editing the PIO program

`src/capture/pio/pulse_capture.pio.h` is hand-assembled and should be
regenerated with `pioasm` after any change to `pulse_capture.pio` — see
`tools/build_pio.md`.
