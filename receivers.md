# 433 MHz Receivers — Comparison & Decisions

Why PulseTape uses an external analog superheterodyne receiver for OOK RX rather
than the transceiver chip on a LoRa board, and how the front end stays swappable.
Companion to `protocol_timings.md` / `long_packets.md`.

---

## The counterintuitive finding

The intuition "a LoRa transceiver (SX127x) or a CC1101 must beat a $2 analog
module" is **wrong for OOK**. Community testing (OpenMQTTGateway / rtl_433_ESP)
found:

- In ASK/OOK mode, **both CC1101 and SX127x have "VERY bad" sensitivity compared
  to old analog superheterodyne receivers.**
- It is not even a clear CC1101 win: side-by-side, an **SX1278 board received
  *more* devices than ESP32 + CC1101** (3 vs 2 sensors).
- All of these chips get **~half the range of an RTL-SDR**.

So "rtl_433-on-LoRa favours CC1101 over SX127x" is real but narrow: CC1101 is
preferred for its **mature OOK + FSK library support, configurability, and RSSI**
— not for superior OOK sensitivity.

### Why
A digital transceiver reconstructs the OOK envelope through a programmable IF
chain + threshold slicer tuned mainly for *its* FSK/packet role. An analog
superhet's entire job is amplitude demodulation, so its AGC/data-slicer extracts
weak OOK better. This is why RFLink, the classic CUL, and most "just works" 433
OOK rigs use analog superhets.

### Note on the SX127x "squelch" myth
In FSK **packet** mode the chip qualifies the signal (preamble + sync detection)
and the data line is clean in silence. In the **OOK continuous / direct** mode
required for generic multi-protocol sniffing, that baseband is bypassed: DIO2 is
the raw slicer output, the AGC ramps gain in silence, and you get noise in the
gaps — the same behaviour as a cheap module. The SX127x is a *better radio*, not
a *cleaner OOK data line*.

---

## Comparison (for OOK reception)

| Receiver | OOK sensitivity | Effort on the T3 | RSSI | FSK | Verdict |
|---|---|---|---|---|---|
| **RTL-SDR** | best | (separate SDR host) | yes | yes | bench analysis, not embedded |
| Analog superhet (SRX882S) | good | trivial — 1 input pin | no | no | ★ optional 2nd source / sensitivity upgrade |
| Analog superregen (RXB6 etc.) | poor (wide, noisy) | 1 pin + 5V level shift | no | no | avoid |
| CC1101 | mediocre | hard — SPI + GDO pins, pin-starved on T3 | yes | yes | only if FSK needed |
| **SX127x (OOK direct)** | mediocre, workable with right settings | medium — radio init, but DIO2 already on GPIO32 | yes | yes (TX too) | ★ primary on the T3 (no extra hardware) |

Note the SX127x sensitivity caveat is real but **settings-bound**: OOKwiz (below) runs
SX1278 OOK well by exposing peak-threshold / RxBw / bitrate. The earlier "SX127x is
bad" reports used a different library's settings.

---

## Decisions

1. **OOK receive (primary on the T3) → onboard SX1278 + DIO2.** Start with what's
   on the board: no extra hardware, DIO2 is already routed to **GPIO32**, and
   OOKwiz proves SX1278 OOK works with the right settings. A bare-SPI front-end
   (`src/radio/sx1278_ook.*`) puts the radio in OOK continuous mode; **ESP32 RMT**
   captures DIO2 on GPIO32.
2. **Sensitivity upgrade / second source → external SRX882S on GPIO36.** A good
   analog superhet still edges the SX127x at the margins. Enable it with
   `-DPULSETAPE_RX_SOURCE_SRX882S` (skips the SX1278 front-end), or run it on a
   *second* RMT channel alongside the SX1278 for receiver diversity, cross-confirmed
   by repeat-matching.
3. **Transmit → onboard SX1276.** TX is not sensitivity-critical, so the SX1276 is
   a fine OOK transmitter and replaces the separate STX882. (Not implemented yet.)
4. **CC1101 only if FSK reception becomes a goal.** Adds FSK + RSSI, but needs SPI
   plus GDO pins and is awkward on the pin-starved T3 (the usual GDO wiring even
   collides with the SX1276's MOSI on GPIO27).
5. **The front end is deliberately not load-bearing.** Every option here is mediocre
   versus an SDR; what makes a capture trustworthy is the **repeat-detection + length
   gate**, not the radio. Because receivers sit behind `ICaptureBackend`, swapping
   SX1278 → SRX882S → CC1101 costs a board header / front-end init — the generic core
   never changes.

---

## What RMT contributes (ESP32)

The ESP32 RMT peripheral implements in hardware what the software assembler would
otherwise do, regardless of which receiver feeds it:

- 15-bit duration + level symbols → `CaptureEvent{DURATION}`
- `signal_range_max_ns` → frame-end idle = `FRAME_GAP_US`
- `signal_range_min_ns` → minimum-pulse glitch filter = `PULSE_MIN_US`

So a noisier analog front end is partly cleaned up by RMT's glitch filter before
it even reaches the generic core.

---

## What we learned from OOKwiz (reference only — see licensing)

[OOKwiz](https://github.com/ropg/OOKwiz) (ropg) is a mature ESP32 OOK
receive/transmit framework. It independently arrived at almost the same design
as PulseTape, which is strong validation:

- **RawTimings → Pulsetrain → Meaning** mirrors our raw pulses → nibble string →
  decoder. Its `bin_width` default is **150 µs** — the same tolerance our
  `psNibbleIndex` uses.
- **Radio = SPI config only; data flows on a GPIO pin** — exactly our
  front-end + `ICaptureBackend` split, and the basis for the SX1278+DIO2 path.
- **Repeat-dedup with no checksums** — only differing packets are surfaced;
  identical repeats bump a counter. Same philosophy as `RepeatDetector`.
- It treats **SX1278 as a first-class OOK radio** via exposed knobs
  (`threshold_type` peak/fixed/avg, `threshold_level`, `bandwidth`, `bitrate`) —
  confirming SX127x OOK is settings-bound, not hopeless.

Concept-level ideas worth adopting (reimplemented in our own code): a lower
frame-gap (OOKwiz's new-packet gap default is ~2000 µs vs our 8000 — a data point
for tuning `FRAME_GAP_US`), a discard threshold for the AGC-settle preamble, a
running noise-score, and de-noising (merge sub-minimum transitions).

## Licensing line (why OOKwiz is reference-only)

PulseTape is **MIT**; OOKwiz is **LGPL-3.0** — incompatible for code reuse. We
therefore use OOKwiz as **inspiration, not source**: ideas, architecture, and
parameter values (facts) are free to reimplement, but no OOKwiz code is copied
and the library is not linked (LGPL-3.0 static-linking obligations would
encumber an MIT firmware). General understanding, not legal advice.

## Radio init: bare SPI vs RadioLib

The SX1278 OOK-continuous-RX setup is ~a dozen SPI register writes (mode, Frf,
RxBw, OOK peak-threshold + fixed floor, bit-sync off, DIO mapping, RX-continuous).
PulseTape does this **bare-SPI** in `src/radio/sx1278_ook.cpp`, with register
values from the Semtech datasheet (facts) — keeping the project dependency-free
and MIT. [RadioLib](https://github.com/jgromes/RadioLib) (MIT) is a drop-in
alternative if you prefer a maintained driver or want the SX1276 TX path; it is
license-clean to depend on, unlike OOKwiz.

The starting values converge with what rtl_433_ESP and OOKwiz settled on for OOK
(used as inspiration; no code copied):

| Setting | rtl_433_ESP | OOKwiz | PulseTape `sx1278_ook` |
|---|---|---|---|
| RX bandwidth | ~125 kHz (narrower lost signals) | wide | **~125 kHz** (`RegRxBw 0x02`) |
| OOK threshold | peak + fixed floor ~90, dec 1/1-chip, step 0.5 dB | peak | **peak + floor 0x5A, dec 1/1-chip, step 0.5 dB** |
| Bitrate | 1.2 kbps | exposed | **~1.2 kbps** (`0x682B`) |
| Bit-sync | — | raw | **off** (raw slicer on DIO2) |
| Radio driver | RadioLib (MIT) | bare registers | bare registers |
| Capture | edge ISR | edge ISR | **RMT** |

Both real projects capture with a GPIO edge ISR; PulseTape uses RMT instead
(hardware duration + frame-gap + glitch filter). A future refinement is a dynamic
RSSI/noise-driven floor like rtl_433_ESP's `AUTOOOKFIX` — the *idea*, reimplemented.

---

## Sources

- OpenMQTTGateway — CC1101 vs SX127x OOK sensitivity:
  https://community.openmqttgateway.com/t/difference-in-receiver-sensibility-cc1101-sx127x/2784 ,
  https://community.openmqttgateway.com/t/testing-cc1101-vs-sx1278-reception-on-esp32-rtl-433-fsk/3225
- rtl_433_ESP (CC1101/SX127x port) — https://github.com/NorthernMan54/rtl_433_ESP
- ESPHome SX127x (OOK→DIO2→RMT path) — https://esphome.io/components/sx127x/
- OOKwiz (architecture reference, LGPL-3.0 — not copied) — https://github.com/ropg/OOKwiz
- RadioLib (MIT — license-clean SX127x driver alternative) — https://github.com/jgromes/RadioLib
- Semtech SX1276/77/78/79 datasheet (register values) — https://cdn-shop.adafruit.com/product-files/3179/sx1276_77_78_79.pdf
