# First end-to-end capture — validation log

Date: 2026-06-20. Board: LilyGO TTGO T3 LoRa32 433 MHz V1.6.1 (ESP32-PICO-D4 +
SX1278). Firmware: `pulsetape.ino` main sketch — SX1278 OOK front-end →
**RMT capture → FrameAssembler → psNibbleIndex → RepeatDetector** → serial/OLED/LED.

This records the first successful reception and what it proves.

## Radio config (boot readback — all values match the known-good dump)

```
SX1278 opmode=0x2D RxBw=0x1 OokPeak=0x8 OokFix=0xF PktCfg1=0x0 PktCfg2=0x0
```

`PktCfg2=0x0` is the one that mattered: continuous data mode, so DIO2 emits the
raw OOK bitstream (the `0x40` packet-mode default had muted it). DIO2 → GPIO32.

## Captured protocols

Three different transmitters, each captured and fingerprinted. The **nibble byte**
is `(pulseClassIndex << 4) | spaceClassIndex`; the string is the repeat fingerprint.

### Old KAKU (PWM, 2 timing classes)
- `count=49`, classes: short ~320–384 µs, long ~1088–1152 µs.
- nibbles: `010101100101010101010110010101010101011001100101`
  (bytes are `01` = short-high/long-low, `10` = long-high/short-low — classic PWM bit).

### New KAKU (PDM, 3 timing classes)
- `count=128`, classes: short ~192–256 µs, long ~1280–1344 µs, sync gap ~2688 µs.
- nibbles: `01000200020200000200020002000202000200020002000002000202000002020002000200000202000002000202000200020000020002000200020002020000`
  (third class is the sync gap — exactly why NewKAKU needs the extra bucket).

### Sonoff 433 (EV1527/PT2262-style PWM, 2 timing classes)
- `count=49`, classes: short ~384 µs, long ~1216–1280 µs.
- nibbles: `010101010110010101100101100110011001101001011001`

## What this validates

1. **Adaptive bucketing works.** `psNibbleIndex` discovered 2 classes for KAKU/Sonoff
   and 3 for NewKAKU on its own — no per-protocol config.
2. **The nibble string is a stable fingerprint.** Every repeat of a given press
   produced a **byte-for-byte identical** nibble string, even though the raw
   `pulses` jittered between repeats (e.g. `1344`↔`1345`, `256`↔`255`,
   `320`↔`384`). The ±150 µs tolerance windows absorbed the jitter.
3. **Repeat-detection is the validation.** `repeats` climbed 2→3→4→5→6 across a
   held press and reset on the next — i.e. "same fingerprint seen N times" with no
   CRC, confirmed live on three protocols. This is the core thesis (see
   `protocol_timings.md`): for one-way cheap-receiver RF, **repetition is the
   error check.**

The capture/quantise/repeat core is therefore done and proven on real signals.

---

## Next steps

Roadmap from here, roughly in priority order.

1. **One event per telegram (forwarding semantics).** Today the sink fires on
   *every* repeat (`repeats=2,3,4,5,6…`). Before any downstream consumer, change
   `RepeatDetector`/sink to forward **once** when `repeat_count` first reaches
   `REPEAT_MIN_COUNT`, then suppress until the repeat window expires. Small, and a
   prerequisite for clean decode/publish.

2. **Protocol decoders** above the nibble layer (`src/decode/`): old KAKU,
   NewKAKU, EV1527/PT2262 → device id + command; then Manchester/Oregon. These
   consume the validated `RawTelegram` (nibbles + pulses), so they run on clean,
   already-de-duplicated input.

3. **Output transport / architecture fork.** The original design (`CONTEXT.md`)
   split RP2040 capture ↔ ESP32 WiFi over COBS/UART. **On the T3 the ESP32 is both
   the capture host and the WiFi host**, so that split collapses: ESP32 can do
   capture → decode → **MQTT / Home Assistant** in one chip. Decide: keep the
   COBS/UART path (for the SenseCAP/RP2040 target) and *also* add a direct
   ESP32→MQTT path for the T3.

4. **TX (replay/control).** Use the SX1276 to transmit OOK (it replaces the STX882
   from the original plan); reuse the captured pulse arrays for replay.

5. **Robustness/tuning.** Bump `PSI_MAX_NIBBLES`/`TELEGRAM_MAX_PULSES` for long
   HVAC frames (see `long_packets.md`), tune `FRAME_GAP_US` against captures,
   trim the trailing unpaired pulse (odd `count`).

6. **Display polish.** Richer OLED view (decoded device/command once decoders land).
