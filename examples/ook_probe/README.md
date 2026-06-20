# OOK hardware probe

A standalone diagnostic for the LilyGO T3 V1.6.1 (ESP32 + SX1278). It is **not**
part of the PulseTape pipeline — it deliberately makes no assumptions and just
shows raw truth, to bisect "radio/hardware vs PulseTape pipeline" problems.

It mirrors the proven SX1278 OOK register init from `src/radio/sx1278_ook.cpp`,
then runs in one of two modes (compile-time `PROBE_MODE`).

## Step 1 — find the data pin (PROBE_MODE_PINSCAN)

Default mode. Flash, open serial @ 115200, and **hold a 433 MHz OOK remote** during
each 2 s scan window. The probe prints edge counts for GPIO 26/32/33/34/35/39:

```
edge counts over 2000 ms:
  GPIO26 = 0
  GPIO32 = 0
  GPIO35 = 418      <- the data pin
  ...
```

The pin with a large count is where the SX1278 is putting demodulated data. This
settles the GPIO32-vs-GPIO35 question empirically.

## Step 2 — dump raw timings (PROBE_MODE_RMTDUMP)

Set `PROBE_RX_PIN` to the pin from step 1, change `PROBE_MODE` to
`PROBE_MODE_RMTDUMP`, reflash. Each remote press should print:

```
FRAME items=34 us: 10052,260,252,256,1280,192,1284,...
```

Compare to a known KAKU capture: short ~260 µs, long ~1280 µs. If these look
right, the radio + capture chain is fully good.

## Build

Arduino IDE: open `ook_probe.ino`, select the board, Upload. Or PlatformIO:
`cd examples/ook_probe && pio run -e esp32_lilygo1 -t upload && pio device monitor`.

## What each outcome means

| Observation | Conclusion |
|---|---|
| `SX1278 init: FAILED` | SPI wiring / `PIN_RST` wrong (RegVersion ≠ 0x12) |
| init OK, **no** pin shows edges | radio not demodulating — antenna / frequency / settings. Fall back to rtl_433_ESP @ `test` (`esp32_lilygo1`) to confirm the hardware |
| a pin shows edges, RMT dump empty | RMT config or wrong `PROBE_RX_PIN` |
| FRAME timings ≈ 260 / 1280 µs | hardware + radio + capture all good → any PulseTape miss is downstream (FrameAssembler / thresholds) |
