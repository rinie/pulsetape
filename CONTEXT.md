# RF Bridge — SenseCAP Indicator
## Context voor Claude Code

---

## Hardware

**Platform:** Seeed Studio SenseCAP Indicator D1 (of D1L/D1S/D1Pro)
- ESP32-S3 @ 240MHz — WiFi, BLE, touchscreen (4"), display via LVGL
- RP2040 @ 133MHz — sensors, Grove, SD, buzzer, UART naar ESP32-S3
- Communicatie tussen MCUs: UART met COBS packet framing
- Grove I2C: GPIO20 (SDA), GPIO21 (SCL)
- Grove ADC/digitaal: GPIO26, GPIO27

**RF modules (3.3V, direct op RP2040 GPIO):**
- SRX882S superheterodyne ontvanger → Grove ADC poort
  - pin 2 GND, pin 3 VCC (3.3V), pin 4 CS (3.3V!), pin 5 DATA → GPIO27, pin 6 GND
- STX882 zender → Grove I2C poort (als digitale GPIO)
  - pin 1 ANT, pin 2 DATA → GPIO20, pin 3 VCC (3.3V), pin 4 GND
- Antennes: 17cm rechte draad (λ/4 @ 433MHz), aparte antenne per module
- Geen spanningsdeler nodig (beide modules zijn 3.3V natief)

**Frequentie:** 433.92MHz OOK/ASK only. 868MHz is YAGNI.

---

## Architectuur

```
SRX882S DATA pin (GPIO27)
        │
        ▼
┌─────────────────────┐
│ PIO state machine   │  Core 1, deterministisch, CPU-onafhankelijk
│ pulse_capture.pio   │  Meet HIGH/LOW pulsduur in ticks → µs
│ 125MHz/25 = 0.2µs   │  Sentinel 0xFFFFFFFF bij overflow (= frame gap)
└────────┬────────────┘
         │
         ▼
┌─────────────────────┐
│ Frame assembler     │  Core 1
│ telegram_layer.c    │  
│                     │  1. ticks → µs conversie
│                     │  2. psNibbleIndex() quantisatie (zie repos)
│                     │  3. Repeat-detectie op nibble-string
│                     │  4. Sanity filter (min pulsen, ruis)
│                     │  5. Push naar Core 0 queue
└────────┬────────────┘
         │  RawTelegram struct
         ▼
┌─────────────────────┐
│ Core 0              │  Stuurt via UART (COBS) naar ESP32-S3
│ Protocol decode     │  RkrManchesterAnalysis, RFControl-stijl, etc.
└─────────────────────┘
         │
         ▼ UART/COBS
┌─────────────────────┐
│ ESP32-S3            │  WiFi → Domoticz / Home Assistant
│ Display             │  Touchscreen UI via LVGL
└─────────────────────┘
```

**TX pad:** GPIO20 → STX882 DATA. Aparte PIO state machine voor pulse replay.
Geen RX/TX schakellogica nodig (aparte modules).

---

## Sleutel-repos

```bash
git clone --depth 1 https://github.com/rinie/NodoDueRkr.git
git clone --depth 1 https://github.com/rinie/pulsespaceindex.git
```

**NodoDueRkr** (`pulsespaceindex.h`) — de kern:
- `psNibbleIndex(pulse, space)` — quantiseert een pulse/space paar naar een 4-bit
  nibble-index door adaptieve min/max clusters bij te houden per telegram.
  Tolerantie-ramen: 150µs (<1ms), 200µs (<2ms), 300µs (<3ms) etc.
  Resultaat: nibble-string als `"3142314231423142"` — dit IS de repeat-fingerprint.
- `RkrManchesterAnalysis()` — Manchester decoder bovenop de OokTimeRange structuur,
  werkt met de min/max tabel die psNibbleIndex opbouwt. Dekt Oregon Scientific v2/v3.
- `OokProperties` struct — houdt `lastHash`, `nRepeats`, `lastTotalTime` bij.
- `processBitRkr()` — streaming interface: roept psNibbleIndex aan per pulse/space paar.

**pulsespaceindex.js** — Node.js port van hetzelfde algoritme:
- `PulseSpaceIndex` class met `countPulseSpace()` en `detectPS01Values()`
- `detectPS01Values()` vindt de dominante "0" en "1" tijdklassen protocol-onafhankelijk
- `ps01fFrameHT` detecteert header/trailer timing (= frame gap klasse)
- Nuttig als referentie voor de C implementatie op RP2040

**Filosofie (uit pulsespaceindex.h commentaar):**
> Protocollen zijn ontworpen om goedkope ontvangers te overwinnen: AGC preamble,
> herhalingen, simpele encoding. Gebruik die eigenschappen: de nibble-string van
> herhaling 2 en 3 is identiek aan herhaling 1 → dat IS de validatie, niet een CRC.

---

## Repeat-detectie

De nibble-string is de repeat-sleutel — geen aparte hash nodig:

```c
// Twee herhalingen van hetzelfde OOK-telegram geven
// vrijwel identieke nibble-strings omdat de adaptieve
// ramen ±5% drift absorberen.
// String-compare op psiNibbles[] is voldoende.
// Bij frameCount >= 2 met identieke string: telegram geldig.
```

Instellingen:
- `REPEAT_MIN_COUNT 2` — minimaal 2 identieke frames voor doorsturen
- `REPEAT_WINDOW_US 800000` — window (800 ms) waarbinnen frames als herhaling tellen
- `FRAME_GAP_US 8000` — stilte die een frame afsluit (Oregon: ~8ms, KAKU: ~10ms)

---

## 433MHz protocollen — dekking

| Familie | Encoding | Dekking |
|---|---|---|
| KAKU / DIO / HomeEasy | PWM/PDM vaste T | psNibbleIndex → 2 tijdklassen |
| NewKAKU / Chacon / Nexa | PDM vaste T | psNibbleIndex → 3 tijdklassen |
| Oregon Scientific v2/v3 | Manchester | RkrManchesterAnalysis |
| Fine Offset / AcuRite | PWM variabel | psNibbleIndex adaptief |
| LaCrosse / Hideki | PWM variabel | psNibbleIndex adaptief |
| Somfy RTS | 433.42MHz variant | aparte resonator nodig in hardware |
| X10 RF | PWM vaste T | psNibbleIndex |

RFControl (pimatic) en Portisch gebruiken dezelfde bucket-quantisatie filosofie —
compatibel met de PSI aanpak.

---

## Bestaande code referentie — rf_telegram.md

De volledige PIO assembler + C laag is uitgewerkt in `rf_telegram.md`
(gegenereerd in de design-sessie). Bevat:
- `pulse_capture.pio` — complete PIO state machine
- `telegram_layer.c` / `telegram_layer.h` — frame assembler, filter, repeat ring
- `rf_capture_pio_init()` — PIO initialisatie
- `core1_rf_capture()` — Core 1 main loop
- `core0 process_telegrams()` — consumer + debug output
- Manchester decoder voorbeeld bovenop RawTelegram

Integreer `psNibbleIndex` in de frame assembler als vervanging van de simpele
FNV-50µs-bucket hash: roep `psNibbleIndex(pulse_us, space_us)` aan per paar,
sla de nibble-string op in `RawTelegram`, gebruik string-compare voor repeat-detectie.

---

## Bouwomgeving

- Arduino IDE met RP2040 board package (earlephilhower/arduino-pico)
- Of Pico SDK (C/C++) voor directe PIO controle
- RP2040 flashen: pinhole-knop indrukken onderin SenseCAP + USB → RPI-BOOT drive
- ESP32-S3 flashen: Espressif IDF of Arduino ESP32 package

## Stijlconventies (js code, niet van toepassing op C/Arduino)
- `.js` extensies, nooit `.mjs`/`.cjs`
- Semicolons, single quotes, exports onderaan
- ESLint airbnb-extended (ESLint 9 flat config)

---

## Wat nog niet gedaan is

- [ ] psNibbleIndex integreren in Core 1 frame assembler (vervangt FNV hash)
- [ ] UART/COBS protocol definiëren tussen RP2040 en ESP32-S3
- [ ] ESP32-S3 kant: UART ontvanger + Domoticz/MQTT bridge
- [ ] TX: pulse replay PIO state machine voor STX882
- [ ] LVGL display UI op ESP32-S3 voor ontvangen RF frames
- [ ] Oregon Scientific v2/v3 decoder testen met echte sensor
