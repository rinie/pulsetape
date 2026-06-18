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
| **Analog superhet (SRX882S)** | **good** | **trivial — 1 input pin** | no | no | ★ chosen for RX |
| Analog superregen (RXB6 etc.) | poor (wide, noisy) | 1 pin + 5V level shift | no | no | avoid |
| CC1101 | mediocre | hard — SPI + GDO pins, pin-starved on T3 | yes | yes | only if FSK needed |
| SX127x (OOK direct) | mediocre / noisy | medium — radio init, DIO2 routing | yes | yes | reserve for TX/FSK |

---

## Decisions

1. **OOK receive → external analog superheterodyne (SRX882S).** Best
   sensitivity-per-effort, one input pin, 3.3 V native (no level shifting), no
   SPI, no register config. On the LilyGO T3 it wires to **GPIO36** (input-only,
   free) and is captured by **ESP32 RMT**.
2. **Transmit → onboard SX1276.** TX is not sensitivity-critical, so the SX1276
   is a fine OOK transmitter and replaces the separate STX882 from the original
   plan. (Not implemented yet.)
3. **CC1101 only if FSK reception becomes a goal.** It adds FSK + RSSI, accepting
   that it needs SPI plus GDO pins and is awkward on the pin-starved T3 (the usual
   GDO wiring even collides with the SX1276's MOSI on GPIO27).
4. **The front end is deliberately not load-bearing.** Every option here is
   mediocre versus an SDR; what makes a capture trustworthy is the
   **repeat-detection + length gate**, not the radio. Because receivers sit behind
   `ICaptureBackend`, swapping SRX882S → CC1101 → SX1276 costs a board header and
   (for CC1101) an SPI init — the generic core never changes.

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

## Sources

- OpenMQTTGateway — CC1101 vs SX127x OOK sensitivity:
  https://community.openmqttgateway.com/t/difference-in-receiver-sensibility-cc1101-sx127x/2784 ,
  https://community.openmqttgateway.com/t/testing-cc1101-vs-sx1278-reception-on-esp32-rtl-433-fsk/3225
- rtl_433_ESP (CC1101/SX127x port) — https://github.com/NorthernMan54/rtl_433_ESP
- ESPHome SX127x (OOK→DIO2→RMT path) — https://esphome.io/components/sx127x/
