# 433 MHz OOK Pulse Timings — Reference

Companion to `rf_telegram.md`. Concrete pulse/gap timings for common 433.92 MHz
OOK families, taken from the source of established decoders (rtl_433, pilight),
plus the design rationale they imply for PulseTape: **why repeat-detection beats
checksums, and how it works around the AGC limits of cheap receivers.**

All values in microseconds (µs). Numbers are quoted from the projects cited at
the bottom; where two projects differ the spread is shown.

---

## Cross-project timing table

| Protocol (family) | Modulation | Base T | Short / Long | Sync / preamble | Inter-frame (reset) |
|---|---|---|---|---|---|
| **NewKAKU / Nexa / Proove** (PDM, ternary) | PPM | ~270 (pilight 315) | pulse 1T≈270; gaps 1T≈270 / 5T≈1300 | short high + **~10.5T gap ≈ 2650** | reset 2800; "packet gap 10 ms" |
| **Old KAKU / Elro / PT2262** (fixed-T) | PWM | 300–350 | 1T≈300 / 3T≈900 | long footer gap (~31T) | ~10 ms |
| **X10 RF** | PPM | 562.5 | pulse 562; gap 562 (bit 1) / 1687 (bit 0) | **9000 sync pulse + 4500 gap** (16× bit time) | reset 6000; ~40 ms between 5 repeats |
| **Oregon Scientific v2/v3** | Manchester (zero-bit) | half-bit **488** (1024 Hz) | half=488 / full=976 | run of 1-bits + sync nibble; v2 `0x5599`, v3 `0xfff5` | reset 2400 |
| **Fine Offset WH2** (weather) | PWM | ~544 | short **544** / long **1524**, fixed gap 1036 | preamble `0xFF` | reset 1200 |

rtl_433 field meaning: in PWM `short_width`/`long_width` are pulse widths; in PPM
they are gap widths. `reset_limit` is the silence that ends a package
(≈ inter-frame gap); `sync_width` is the sync pulse; `tolerance` ≈ 160–200 µs.

---

## The typical pattern

Three things recur across every family:

1. **Base time unit T ≈ 250–600 µs.** Switches cluster at **250–350**; PPM remotes
   (X10) ~560; Manchester half-bit (Oregon) ~**488**.
2. **Pulses and gaps are small integer multiples of T** — short = 1T,
   long = **3–5T** (≈900–1700 µs). That 1T-vs-(3–5T) contrast *is* the bit,
   whether the pulse varies (PWM: Fine Offset, old KAKU) or the gap varies
   (PPM/PDM: NewKAKU, X10).
3. **Sync + inter-frame gap** take one of two shapes:
   - **AGC / long-pulse sync** — X10: 9 ms high + 4.5 ms gap; Oregon: a run of
     1-bits (~12–16 half-bits ≈ 6–8 ms).
   - **short-high + long-gap sync** — switches: ~9–10.5T ≈ **2.5–2.8 ms**.
   - **Inter-frame silence** that marks "telegram over": **~2.4–10 ms**
     (`reset_limit` 1200–6000 µs), and telegrams **repeat 2–5×**.

---

## NewKAKU detail (T ≈ 260–315 µs)

```
start :  1T high, ~10.5T low          (~2650–2835 µs)
bit 0 :  1T high, 1T low, 1T high, 5T low     ≈ 8T   (pilight: 4T → ≈7T)
bit 1 :  1T high, 5T low, 1T high, 1T low     ≈ 8T
... 32 logical bits (26-bit addr, group, on/off, 4-bit unit) ...
stop  :  1T high, ~40T low             (long footer)
```

Landmarks: **~10.5T sync gap**, **~8T per logical bit**, **~40T footer**. (No
clean "20T" appears in rtl_433/pilight; the often-quoted 20T is likely a
per-implementation quantisation of T or a bit-pair window — reconcile against the
specific source if needed.)

---

## Why repeat-detection beats checksums (the design philosophy)

These protocols were designed to be decoded by **cheap, dumb receivers**, so they
build redundancy into the air, not into a CRC:

- **Many have weak or no checksum.** Old KAKU / PT2262 has none; several remotes
  carry only a parity bit. A CRC you don't have can't validate anything.
- **They all repeat.** Every family above sends the same frame **2–5×** within a
  few tens of ms. Two captures that *quantise to the identical timing-class
  string* is independent confirmation that is **stronger than a checksum on a
  single noisy capture**: a checksum proves a single frame is internally
  consistent; a repeat-match proves the receiver actually heard the same thing
  twice. Random noise does not repeat with the same structure.

This is exactly what PulseTape's nibble string does: `psNibbleIndex` reduces a
frame to its sequence of timing-class indices, and `RepeatDetector` forwards only
when ≥2 frames produce the **same string** inside the repeat window. The string
*is* the validation — no per-protocol CRC table required, and it works for
protocols PulseTape has never seen.

---

## How this fixes the AGC limits of cheap hardware

Cheap superregenerative / superheterodyne receivers (Aurel, SRX882S) run an
**automatic gain control** that is the main source of timing distortion:

- **Frame start is unreliable.** During the preamble/AGC-settle the gain is still
  ramping, so the first pulses arrive stretched, clipped, or missing. That is why
  protocols lead with a long AGC pulse or a run of 1-bits (X10's 9 ms pulse,
  Oregon's 1-bit run) — *throwaway* energy to settle the AGC before real data.
- **Silence becomes noise.** With no carrier, AGC winds gain to maximum and the
  data line fills with random edges — short, irregular pulses below 1T.

PulseTape absorbs both without per-protocol knowledge:

| AGC problem | Handled by |
|---|---|
| Stretched/clipped preamble | tolerant buckets (±150–300 µs) + treating the leading AGC pulse as just another timing class; repeat-match ignores a frame whose body still differs |
| Noise bursts in silence | `PULSE_MIN_US` floor (50 µs ≪ any real 250 µs T) and the out-of-range/`same-as-prev` carrier-leak filter in `telegram_valid()` |
| Where does a frame end? | `FRAME_GAP_US` (a LOW longer than this closes the frame) |
| Is this frame real? | it must **repeat identically** — noise won't |

### Frame-gap tuning note

`FRAME_GAP_US` must sit **above the longest intra-frame gap** but **below the
inter-frame silence**:

- Longest intra-frame gaps seen above: KAKU footer ~10 ms, KAKU sync ~2.8 ms,
  X10 sync 4.5 ms + 9 ms pulse.
- Inter-frame silence / `reset_limit`: ~2.4–10 ms.

These overlap, so no single value is perfect for every protocol. The current
**`FRAME_GAP_US = 8000`** is reasonable but tight against X10/KAKU; **6–10 ms,
tunable, is the practical band**, and repeat-detection cleans up the rare
mis-split. (`PULSE_MIN_US = 50` is safely below every real T.)

---

## Sources

- rtl_433 device decoders:
  [nexa.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/nexa.c),
  [proove.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/proove.c),
  [x10_rf.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/x10_rf.c),
  [oregon_scientific.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/oregon_scientific.c),
  [fineoffset.c](https://github.com/merbanan/rtl_433/blob/master/src/devices/fineoffset.c)
- pilight protocols:
  [arctech_switch.c](https://github.com/pilight/pilight/blob/master/libs/pilight/protocols/433.92/arctech_switch.c),
  [elro_800_switch.c](https://github.com/pilight/pilight/blob/master/libs/pilight/protocols/433.92/elro_800_switch.c)
- Related projects using the same bucket-quantisation philosophy: RFLink,
  Portisch (Sonoff RF Bridge), and `pulsespaceindex` (this author's own port).
