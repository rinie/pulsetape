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
**one-way**: NEC sends repeat frames, AC remotes resend the full state, so
repeat-detection applies there too.

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
