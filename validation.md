# End-to-end capture — validation log

Board: LilyGO TTGO T3 LoRa32 433 MHz V1.6.1 (ESP32-PICO-D4 + SX1278). Firmware:
`pulsetape.ino` main sketch — SX1278 OOK front-end →
**RMT capture → FrameAssembler → psNibbleIndex (normalize) → RepeatDetector** →
serial / OLED / LED. Captures below are real device output (2026-06-21).

## Radio config (boot readback — matches the known-good rtl_433_ESP dump)

```
SX1278 opmode=0x2D RxBw=0x1 OokPeak=0x8 OokFix=0xF PktCfg1=0x0 PktCfg2=0x0
```

`PktCfg2=0x0` is the one that mattered: continuous data mode, so DIO2 emits the
raw OOK bitstream (the `0x40` packet-mode default had muted it). DIO2 → GPIO32.

## Output line

```
RF;count=<edges>;repeats=<n>;micros=[<per-class us ranges>];counts=[<per-class hits>];mod=<p|s|ps>;psix=<data hex>;psi=<index string>
```

- **micros / counts** — per timing class (ascending by duration); a low count is a
  one-off sync or a noise spike, high counts are data classes.
- **mod** — detected modulation: `p` (pulse carries the bit), `s` (gap carries it,
  pulse constant), `ps` (both vary).
- **psix** — data bits → hex, packed per `mod`. **psi** — raw pulse/space index string.

## Captured protocols (real lines)

### New KAKU — PDM, 3 classes, `mod=s`
```
RF;count=131;repeats=2;micros=[192-320,1280-1344,2688];counts=[97,32,1];mod=s;psix=5956A966A65A9558;psi=0200010001010000010001000100010100010001000100000100010100000101000100010000010100000100010100010001000001000100010001000101...
```
Pulse is always short (all 97 pulses in class 0) → `mod=s`, so psix packs only the
gap bits → a compact 16-digit code. The lone `counts=…,1` is the ~2688 µs sync.

### Old KAKU / Sonoff — PWM, 2 classes, `mod=ps`
```
RF;count=49;repeats=2;micros=[320-384,1024-1088];counts=[24,24];mod=ps;psix=56555655566;psi=01010110010101010101011001010101010101100110
RF;count=49;repeats=2;micros=[320-448,1152-1280];counts=[24,24];mod=ps;psix=556565999A5;psi=01010101011001010110010110011001100110100101
```
Both pulse and gap vary (balanced `counts=[24,24]`) → `mod=ps`, two bits per pair.
Two distinct remotes → two stable codes.

## What this validates

1. **Adaptive bucketing** — `psNibbleIndex` finds 2 classes (KAKU/Sonoff) or 3
   (NewKAKU) on its own, no per-protocol config.
2. **Canonical fingerprint** — classes are normalized to ascending duration
   (0 = shortest), so the `psi`/`psix` are byte-identical across repeats despite
   raw-pulse jitter (the ±150 µs tolerance windows absorb it).
3. **Repeat-detection = validation** — each telegram forwards **once**, when a
   2nd identical fingerprint confirms it, no CRC. The core thesis
   (`protocol_timings.md`), proven on three protocols.
4. **counts** distinguish sync/spike (low) from data (high); **degenerate frames**
   (one class ≥ 90 %) are rejected as noise.
5. **mod detection** picks the modulation (NewKAKU→`s`, KAKU/Sonoff→`ps`) and
   `psix` packs the cleaner per-mode code.
6. **No length cap** — RMT now uses 4 memory blocks (512 edges); NewKAKU reports
   its true `count=131` (was truncated at 128).

## Done since the first capture
psNibbleIndex integration · duration normalization · one-event-per-telegram ·
tail-trim · micros/counts output · psix/psi naming · `mod` p/s/ps detection ·
degenerate-noise reject · RMT 128-edge cap lifted.

## Next steps

1. **Protocol decoders** (`src/decode/`): NewKAKU, old KAKU, EV1527/PT2262 →
   device id + command (NewKAKU's `s` bits are Manchester — decode to the 32-bit
   address/unit/on-off). Then Oregon/Manchester sensors.
2. **Output transport** — the T3's ESP32 is both capture and WiFi host, so it can
   go capture → decode → **MQTT / Home Assistant** directly (the original
   COBS/UART split was for the RP2040+ESP32 SenseCAP; keep that for that target).
3. **TX / replay** via the SX1276 (replaces the STX882).
4. **Long HVAC frames** — bump `PSI_MAX_NIBBLES`/`TELEGRAM_MAX_PULSES` + RMT blocks
   for 424-bit+ protocols (`long_packets.md`).
