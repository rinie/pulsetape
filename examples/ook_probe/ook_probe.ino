// examples/ook_probe/ook_probe.ino
//
// Standalone HARDWARE PROBE for the LilyGO T3 V1.6.1 (ESP32 + SX1278). It is NOT
// part of the PulseTape pipeline — no FrameAssembler, no generic core. It exists
// to show raw truth and settle two questions empirically:
//
//   PROBE_MODE_PINSCAN  — which GPIO actually toggles when a 433 OOK remote fires
//                         (makes NO assumption; it measures every candidate pin)
//   PROBE_MODE_RMTDUMP  — raw RMT pulse timings on PROBE_RX_PIN, so you can compare
//                         to a known KAKU capture (~260 us short / ~1280 us long)
//
// Self-contained: the SX1278 OOK init below mirrors the register values in
// src/radio/sx1278_ook.cpp (themselves taken from a known-good on-device dump).
// MIT (our own code); no rtl_433_ESP / OOKwiz / RadioLib involved.
//
// Usage:
//   1. Flash with PROBE_MODE = PROBE_MODE_PINSCAN. Open serial @ 115200, hold a
//      433 remote. The GPIO with a large edge count IS the data pin.
//   2. Set PROBE_RX_PIN to that pin, PROBE_MODE = PROBE_MODE_RMTDUMP, reflash.
//      Each remote press should print a FRAME line of raw durations.
//
// Interpreting results:
//   - "SX1278 init: FAILED"            -> SPI wiring / pins (RegVersion != 0x12).
//   - init OK but NO pin shows edges    -> radio not demodulating: antenna,
//                                          frequency, or settings — go validate
//                                          with rtl_433_ESP @ test (esp32_lilygo1).
//   - a pin shows edges but RMT dump is empty -> RMT config / wrong PROBE_RX_PIN.
//   - FRAME timings look like ~260/1280 -> hardware + radio + capture all good;
//                                          any PulseTape issue is downstream.

#include <Arduino.h>
#include <SPI.h>
#include "driver/rmt.h"

// ---- T3 V1.6.1 SX1278 SPI/control pins ----
#define PIN_SCK   5
#define PIN_MISO  19
#define PIN_MOSI  27
#define PIN_NSS   18
#define PIN_RST   23          // some revisions 14 — verify if init fails
#define RF_FREQ_HZ 433920000UL

// ---- probe configuration ----
#define PROBE_MODE_PINSCAN 0
#define PROBE_MODE_RMTDUMP 1
#define PROBE_MODE PROBE_MODE_PINSCAN   // <-- switch to PROBE_MODE_RMTDUMP after the scan
#define PROBE_RX_PIN 35                 // <-- set to the pin the scan reports as active

// Candidate data pins to scan (DIO0/1/2 area + the 34/35/39 input-only pins).
static const int kScanPins[] = {26, 32, 33, 34, 35, 39};
static const int kNumScan = sizeof(kScanPins) / sizeof(kScanPins[0]);

// ----------------- SX1278 minimal OOK init (mirrors src/radio/sx1278_ook.cpp) ----
static void wr(uint8_t reg, uint8_t val) {
  digitalWrite(PIN_NSS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(val);
  digitalWrite(PIN_NSS, HIGH);
}
static uint8_t rd(uint8_t reg) {
  digitalWrite(PIN_NSS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(PIN_NSS, HIGH);
  return v;
}

static bool sx_init() {
  pinMode(PIN_NSS, OUTPUT); digitalWrite(PIN_NSS, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);  delay(2);
  digitalWrite(PIN_RST, HIGH); delay(6);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  if (rd(0x42) != 0x12) { SPI.endTransaction(); return false; }   // RegVersion

  wr(0x01, rd(0x01) & 0xF8); delay(10);   // sleep, keep LongRangeMode
  wr(0x01, 0x00);            delay(5);    // sleep + FSK/OOK
  wr(0x01, 0x28 | 0x01);     delay(5);    // OOK + LowFreq, standby (0x29)

  uint32_t frf = (uint32_t)(((uint64_t)RF_FREQ_HZ << 19) / 32000000ULL);
  wr(0x06, frf >> 16); wr(0x07, frf >> 8); wr(0x08, frf);

  wr(0x02, 0x68); wr(0x03, 0x2B);   // bitrate ~1.2 kbps
  wr(0x0C, 0x20);                   // RegLna: max gain, HF boost off
  wr(0x0D, 0x08);                   // RegRxConfig: AGC auto on
  wr(0x12, 0x01);                   // RegRxBw ~250 kHz
  wr(0x14, 0x08);                   // RegOokPeak: peak, bit-sync off
  wr(0x15, 0x0F);                   // RegOokFix: low fixed floor
  wr(0x16, 0x12);                   // RegOokAvg: default
  wr(0x40, 0x00); wr(0x41, 0x00);   // DIO mapping (DIO2 = Data in continuous)

  wr(0x01, 0x28 | 0x05); delay(5);  // RX continuous (0x2D)

  uint8_t opmode = rd(0x01);
  SPI.endTransaction();
  Serial.print("opmode after init = 0x"); Serial.println(opmode, HEX);
  return true;
}

// ----------------- pin scan (no assumption about the data pin) -------------------
static void pinScan(uint32_t window_ms) {
  for (int i = 0; i < kNumScan; i++) pinMode(kScanPins[i], INPUT);
  int  last[kNumScan];
  long edges[kNumScan];
  for (int i = 0; i < kNumScan; i++) { last[i] = digitalRead(kScanPins[i]); edges[i] = 0; }

  uint32_t t0 = millis();
  while (millis() - t0 < window_ms) {
    for (int i = 0; i < kNumScan; i++) {
      int v = digitalRead(kScanPins[i]);
      if (v != last[i]) { edges[i]++; last[i] = v; }
    }
  }
  Serial.print("edge counts over "); Serial.print(window_ms); Serial.println(" ms:");
  for (int i = 0; i < kNumScan; i++) {
    Serial.print("  GPIO"); Serial.print(kScanPins[i]);
    Serial.print(" = ");    Serial.println(edges[i]);
  }
}

// ----------------- RMT raw dump --------------------------------------------------
static RingbufHandle_t rb = nullptr;
static void rmtStart(int pin) {
  rmt_config_t c = RMT_DEFAULT_CONFIG_RX((gpio_num_t)pin, RMT_CHANNEL_0);
  c.clk_div = 80;                          // 1 us / tick
  c.rx_config.filter_en = true;
  c.rx_config.filter_ticks_thresh = 50;    // ignore sub-pulse glitches
  c.rx_config.idle_threshold = 8000;       // 8 ms gap closes a frame
  rmt_config(&c);
  rmt_driver_install(RMT_CHANNEL_0, 4096, 0);
  rmt_get_ringbuf_handle(RMT_CHANNEL_0, &rb);
  rmt_rx_start(RMT_CHANNEL_0, true);
}
static void rmtDrain() {
  size_t n = 0;
  rmt_item32_t* it = (rmt_item32_t*)xRingbufferReceive(rb, &n, pdMS_TO_TICKS(500));
  if (!it) return;
  size_t count = n / sizeof(rmt_item32_t);
  Serial.print("FRAME items="); Serial.print(count); Serial.print(" us: ");
  for (size_t i = 0; i < count; i++) {
    Serial.print(it[i].duration0); Serial.print(',');
    Serial.print(it[i].duration1);
    if (i + 1 < count) Serial.print(',');
  }
  Serial.println();
  vRingbufferReturnItem(rb, it);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n== PulseTape OOK hardware probe ==");
  Serial.print("SX1278 init: ");
  Serial.println(sx_init() ? "OK" : "FAILED (check SPI wiring / RST pin)");
#if PROBE_MODE == PROBE_MODE_RMTDUMP
  rmtStart(PROBE_RX_PIN);
  Serial.print("RMT dump mode on GPIO"); Serial.println(PROBE_RX_PIN);
#else
  Serial.println("PIN SCAN mode — hold a 433 MHz OOK remote during each scan window");
#endif
}

void loop() {
#if PROBE_MODE == PROBE_MODE_RMTDUMP
  rmtDrain();
#else
  pinScan(2000);
  delay(200);
#endif
}
