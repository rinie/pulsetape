# Long Packets, Modulation & Buffer Sizing

Companion to `protocol_timings.md`. Where that note covers short, common 433 MHz
OOK switches and sensors, this one covers the **long end of the spectrum**
(HVAC / heat-pump remotes), why the pulse-tape approach is **carrier-agnostic**
(OOK, FSK, *and* IR), and what packet length means for PulseTape's buffers.

---

## HVAC remotes are the long-packet outliers

Air-conditioner and heat-pump remotes are stateless-receiver designs: every
button press retransmits the **entire unit state** (temperature, mode, fan
speed, swing/louver, timers, clock, sometimes sensor readback), not a single
command. So the frame length grows with the feature set, not the action.

Longest known AC protocols (from IRremoteESP8266):

| Protocol | Bits | Bytes |
|---|---|---|
| **Hitachi AC424** | **424** | 53 |
| Hitachi AC344 | 344 | 43 |
| **Daikin312** | **312** | 39 |
| Hitachi AC296 | 296 | 37 |
| Hitachi AC264 | 264 | 33 |
| Daikin216 / Panasonic AC | 216 | 27 |
| Daikin200 / 176 / 160 | 160–200 | 20–25 |
| Mitsubishi Heavy 152 / Daikin152 | 152 | 19 |
| Daikin128 / Mitsubishi AC (144) | 128–144 | 16–18 |
| Coolix / simple split units | 24 | 3 |

The spread is ~18× (Coolix 24 bits → Hitachi 424 bits). Many Daikins also send
**multiple sections per press** (a short header frame + the long state frame)
separated by gaps — each section lands as its own telegram under a frame-gap
rule, which is fine: repeat-detection runs per section.

These are predominantly **IR** remotes; on 433 MHz RF the very-long AC protocols
are rarer, but the structural reason (whole-state payload, one-way, repeated) is
identical — and it is the buffer consequence, not the carrier, that matters here.

---

## Modulation is irrelevant to the tape layer

PulseTape captures **edge timings**, not a carrier. Everything upstream of the
data pin is the receiver's job, so the same pulse-tape + `psNibbleIndex` + repeat
layer works across carriers:

| Carrier | Front-end | What the data pin sees | Notes for PulseTape |
|---|---|---|---|
| **OOK/ASK 433 MHz** | superregen/superhet (SRX882S, Aurel) | sliced envelope, noisy in silence | the baseline; AGC noise handled by filters + repeats |
| **FSK 433/868 MHz** | CC1101 / RFM69 / SX127x | clean envelope, **squelched** in silence | less gap-noise; real **RSSI** available (the `rssi` field is pre-wired) |
| **IR (~38 kHz carrier)** | demodulating receiver (TSOP/Vishay) | **demodulated envelope** — a pulse train | carrier already stripped by the receiver; identical capture path |

Key IR detail: a *demodulating* IR receiver (TSOP38xxx) removes the 36–40 kHz
carrier and outputs only the on/off envelope — i.e. a HIGH/LOW pulse train
indistinguishable in shape from an OOK data line. So the PIO backend (it just
measures GPIO pulse widths) is unchanged; an IR front-end is simply another
board/pin variant — or, if it warranted different timing constants, another
`ICaptureBackend`. (A *non*-demodulating raw photodiode would expose the carrier
and is not the intended path.)

IR timing scales overlap RF nicely — NEC's base unit is 562.5 µs, the same
ballpark as X10 — so the existing tolerance buckets cover both. And IR is just as
**one-way**, so the same repeat / in-frame validation applies (see next section).

---

## IR protocol families: same shape, three wrinkles

IR consumer protocols reduce to the *same two encoding families* as 433 RF —
pulse-distance/width (→ `psNibbleIndex`, 2–3 timing classes) and Manchester
(→ `RkrManchesterAnalysis`, like Oregon). Timings differ but stay inside the
tolerance buckets.

| Protocol | Carrier | Encoding | Base unit | Bit timing | Bits | Leader/AGC | Repeat |
|---|---|---|---|---|---|---|---|
| **NEC** | 38 kHz | pulse-**distance** | 560 µs burst | 0 = 1.125 ms, 1 = 2.25 ms | 32 | **9 ms mark + 4.5 ms space** | special **repeat-code** every 110 ms (not a full resend) |
| **RC5** | 36 kHz | **Manchester** | half-bit 889 µs (bit 1.778 ms) | constant period | 14 | none (2 start bits) | full frame every 114 ms, **toggle bit** |
| **RC6** | 36 kHz | **Manchester** | **t = 444 µs** | 1t bits, **trailer = 2t** | 16+ | **6t mark (2.666 ms) + 2t space** | full frame, toggle = 2t |
| **Sony SIRC** | 40 kHz | pulse-**width** | 600 µs | 0 = 600 µs, 1 = 1200 µs mark | 12/15/20 | 2.4 ms mark + 600 µs | full frame, min 3×, ~45 ms |

What carries over unchanged:

- **Few timing classes, integer multiples of one unit** — the adaptive buckets
  discover them exactly as for RF.
- **The long AGC leader** (NEC 9 ms, RC6 6t, Sony 2.4 ms) is the same
  gain-settling preamble as RF (X10's 9 ms pulse, Oregon's 1-bit run).
- **`FRAME_GAP_US = 8000` still fits**: the largest intra-frame gap is NEC's
  4.5 ms leader space (< 8 ms); inter-frame intervals are 45–114 ms (≫ 8 ms).

Three IR-specific wrinkles, all handled above the tape layer:

1. **RC6 has variable bit width** — the trailer/toggle bit is `2t`, double a
   normal `1t`. `psNibbleIndex` just files it as one more timing-class bucket,
   but a *pure* Manchester decoder needs a special case for it. (RC5 is
   constant-period and has no such bit.)
2. **NEC breaks the "2 identical frames" rule** — holding a key sends *one* full
   frame, then short **repeat-codes**, not full resends. So full-frame
   repeat-matching won't see two identical bodies from a single press. **But**
   NEC sends address + ~address + command + ~command — each value carries its
   own complement, an **in-frame integrity check** (the IR equivalent of a
   checksum) that is self-validating without repeats.
3. **RC5 / RC6 / Sony do resend the full frame** (toggle bit held constant while
   pressed), so repeat-detection works naturally; the toggle bit is how they
   distinguish "held" from "re-pressed".

Net: the tape layer captures all of them identically; only the **validation
strategy forks** — repeat-detection for RF / RC5 / RC6 / Sony, in-frame
complement-check for NEC-style — and that fork lives in the decoder layer, above
the tape, where it belongs.

---

## What packet length means for the buffers

For PWM/PPM, one data bit ≈ one pulse/space pair ≈ one nibble byte. Current
constants (`psi.h` / `telegram.h`):

- `PSI_MAX_NIBBLES = 256` → ~256 bit-pairs
- `TELEGRAM_MAX_PULSES = 512` edges

That comfortably holds Daikin216 / Panasonic (216 bits), but **Daikin312 and
Hitachi AC344/424 overflow it**: a 424-bit frame is ~424 pairs ≈ **850+ edges**,
past the 512-edge buffer. Today such a frame hits the
`count >= TELEGRAM_MAX_PULSES → drop` path in `FrameAssembler::onEvent` and is
silently discarded.

To cover the worst case, raise `PSI_MAX_NIBBLES` to ~**512**
(→ `TELEGRAM_MAX_PULSES = 1024`):

| | now | for long HVAC |
|---|---|---|
| `PSI_MAX_NIBBLES` | 256 | 512 |
| `TELEGRAM_MAX_PULSES` | 512 | 1024 |
| `sizeof(RawTelegram)` | ~1.3 KB | ~3 KB |
| queue (depth 4) | ~5 KB | ~12 KB |
| repeat ring (8) | ~10 KB | ~24 KB |

Comfortable on the RP2040's 264 KB. `PSI_MICRO_ELEMENTS = 15` needs no change —
even long AC protocols use only a handful of timing classes.

**Not changed yet** — buffer sizing is deferred to practical validation along
with `FRAME_GAP_US`. This note records the rationale so the bump is a known,
costed change when real long frames are on the bench.

---

## Sources

- IRremoteESP8266 supported protocols (AC state lengths) —
  https://github.com/crankyoldgit/IRremoteESP8266/blob/master/SupportedProtocols.md
- rtl_433 — https://github.com/merbanan/rtl_433
- IR protocol timings (SB-Projects):
  [NEC](https://www.sbprojects.net/knowledge/ir/nec.php),
  [RC5](https://www.sbprojects.net/knowledge/ir/rc5.php),
  [RC6](https://www.sbprojects.net/knowledge/ir/rc6.php)
