# OOK hardware probe

A standalone diagnostic for the LilyGO T3 V1.6.1 (ESP32 + SX1278). It is **not**
part of the PulseTape pipeline — it deliberately makes no assumptions and just
shows raw truth, to bisect "radio/hardware vs PulseTape pipeline" problems.

It mirrors the proven SX1278 OOK register init from `src/radio/sx1278_ook.cpp`.
**Flash once, then drive it over the serial monitor** — no recompiling to switch
pins or modes. Results also render on the **onboard OLED** (mode + RX pin, the
per-pin edge counts in scan mode, frame # / item count / first timings in dump
mode), so you can read the probe without a serial monitor. (Needs Adafruit
SSD1306 + GFX; set `USE_OLED 0` in the sketch for a bare serial-only build.)

Type `?` for the menu:

```
  s      pin-scan mode (find the data pin; hold a 433 remote)
  d      RMT-dump mode (raw timings on the selected RX pin)
  1..6   select RX pin: 1=GPIO26 2=GPIO32 3=GPIO33 4=GPIO34 5=GPIO35 6=GPIO39
  g      print live SX1278 registers
  i      re-init the radio
  R      reboot the ESP32
  ?      this help
```

At boot it logs the **exact working conditions** — SPI/RST pins, frequency, mode,
and a read-back of the live SX1278 registers (Version, OpMode, Lna, RxConfig,
RxBw, OokPeak, OokFix, OokAvg, DioMap1) — so any capture in the log is paired with
the precise config that produced it (compare against a known-good dump):

```
--- config ---
SPI: SCK=5 MISO=19 MOSI=27 NSS=18 RST=23
freq Hz = 433920000
mode = PINSCAN, scan pins = GPIO26 GPIO32 GPIO33 GPIO34 GPIO35 GPIO39
--------------
SX1278 init: OK
--- SX1278 live registers ---
  Version  0x42 = 0x12
  OpMode   0x01 = 0x2D
  RxBw     0x12 = 0x01
  OokPeak  0x14 = 0x08
  OokFix   0x15 = 0x0F
  ...
-----------------------------
```

## Step 1 — find the data pin (`s`, the default mode)

Open serial @ 115200 and **hold a 433 MHz OOK remote**. Each ~1.5 s window prints
edge counts across GPIO 26/32/33/34/35/39:

```
edges over 1500 ms:  GPIO26=0  GPIO32=0  GPIO33=0  GPIO34=0  GPIO35=418  GPIO39=0
```

The pin with a large count is where the SX1278 is putting demodulated data — this
settles the GPIO32-vs-GPIO35 question empirically.

## Step 2 — dump raw timings (`d`)

Press the number key for the winning pin (e.g. `5` for GPIO35), then `d`. Each
remote press now prints:

```
FRAME items=34 us: 10052,260,252,256,1280,192,1284,...
```

Compare to a known KAKU capture: short ~260 µs, long ~1280 µs. If these look
right, the radio + capture chain is fully good. (`g` re-prints the live registers
any time; `R` reboots.)

## Build

Arduino IDE: open `ook_probe.ino`, select a **TTGO LoRa32** board (it must define
the `LORA_*` pin macros — a generic "ESP32 Dev Module" won't compile), Upload, open Serial Monitor
@ 115200 (set line ending to "No line ending" or any — single keys work either
way). Or PlatformIO:
`cd examples/ook_probe && pio run -e esp32_lilygo1 -t upload && pio device monitor`.

## What each outcome means

| Observation | Conclusion |
|---|---|
| `SX1278 init: FAILED` | SPI wiring / `PIN_RST` wrong (RegVersion ≠ 0x12) |
| init OK, **no** pin shows edges | radio not demodulating — antenna / frequency / settings. Fall back to rtl_433_ESP @ `test` (`esp32_lilygo1`) to confirm the hardware |
| a pin shows edges, RMT dump empty | RMT config or wrong `PROBE_RX_PIN` |
| FRAME timings ≈ 260 / 1280 µs | hardware + radio + capture all good → any PulseTape miss is downstream (FrameAssembler / thresholds) |
