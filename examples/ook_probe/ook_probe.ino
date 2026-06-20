// examples/ook_probe/ook_probe.ino
//
// Standalone, INTERACTIVE hardware probe for the LilyGO T3 V1.6.1 (ESP32 + SX1278).
// NOT part of the PulseTape pipeline — no FrameAssembler, no generic core. It shows
// raw truth to bisect "radio/hardware vs PulseTape pipeline" problems.
//
// One build — drive it over the serial monitor (115200), no recompiling to switch
// pins or modes. Type '?' for the command menu. Two modes:
//   pin-scan  — which GPIO actually toggles when a 433 OOK remote fires (no
//               assumption; measures every candidate pin)
//   RMT dump  — raw pulse timings on the selected pin (compare to a KAKU capture:
//               ~260 us short / ~1280 us long)
//
// Self-contained: the SX1278 OOK init mirrors src/radio/sx1278_ook.cpp (values from
// a known-good on-device dump). MIT; no rtl_433_ESP / OOKwiz / RadioLib.

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

// Candidate data pins to scan (DIO0/1/2 area + the 34/35/39 input-only pins).
static const int kScanPins[] = {26, 32, 33, 34, 35, 39};
static const int kNumScan = sizeof(kScanPins) / sizeof(kScanPins[0]);

// ---- runtime state (changed live via serial) ----
enum Mode { MODE_PINSCAN, MODE_RMTDUMP };
static Mode g_mode  = MODE_PINSCAN;
static int  g_rxPin = 35;       // default guess; change with keys 1..6
static bool g_rmtOn = false;
static RingbufHandle_t rb = nullptr;

// ----------------- SX1278 SPI helpers + OOK init (mirrors src/radio/sx1278_ook.cpp)
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

// ----------------- config + live-register logging (verify working conditions) ---
static void printConfig() {
  Serial.println("--- config ---");
  Serial.print("SPI: SCK="); Serial.print(PIN_SCK);
  Serial.print(" MISO=");    Serial.print(PIN_MISO);
  Serial.print(" MOSI=");    Serial.print(PIN_MOSI);
  Serial.print(" NSS=");     Serial.print(PIN_NSS);
  Serial.print(" RST=");     Serial.println(PIN_RST);
  Serial.print("freq Hz = "); Serial.println(RF_FREQ_HZ);
  Serial.print("scan pins =");
  for (int i = 0; i < kNumScan; i++) { Serial.print(" GPIO"); Serial.print(kScanPins[i]); }
  Serial.println();
  Serial.println("--------------");
}

// Read back the live SX1278 registers so the log records the exact radio state
// that produced the result (compare against a known-good dump).
static void dumpRegs() {
  struct { const char* name; uint8_t addr; } regs[] = {
    {"Version  0x42", 0x42}, {"OpMode   0x01", 0x01}, {"Lna      0x0C", 0x0C},
    {"RxConfig 0x0D", 0x0D}, {"RxBw     0x12", 0x12}, {"OokPeak  0x14", 0x14},
    {"OokFix   0x15", 0x15}, {"OokAvg   0x16", 0x16}, {"DioMap1  0x40", 0x40},
  };
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  Serial.println("--- SX1278 live registers ---");
  for (auto& r : regs) {
    uint8_t v = rd(r.addr);
    Serial.print("  "); Serial.print(r.name); Serial.print(" = 0x");
    if (v < 0x10) Serial.print('0');
    Serial.println(v, HEX);
  }
  SPI.endTransaction();
  Serial.println("-----------------------------");
}

static void printHelp() {
  Serial.println();
  Serial.println("=== OOK probe — type a key in the serial monitor ===");
  Serial.println("  s      pin-scan mode (find the data pin; hold a 433 remote)");
  Serial.println("  d      RMT-dump mode (raw timings on the selected RX pin)");
  Serial.println("  1..6   select RX pin: 1=GPIO26 2=GPIO32 3=GPIO33 4=GPIO34 5=GPIO35 6=GPIO39");
  Serial.println("  g      print live SX1278 registers");
  Serial.println("  i      re-init the radio");
  Serial.println("  R      reboot the ESP32");
  Serial.println("  ?      this help");
  Serial.print("current: mode=");
  Serial.print(g_mode == MODE_PINSCAN ? "PINSCAN" : "RMTDUMP");
  Serial.print(", RX pin=GPIO"); Serial.println(g_rxPin);
  Serial.println("====================================================");
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
  Serial.print("edges over "); Serial.print(window_ms); Serial.print(" ms:");
  for (int i = 0; i < kNumScan; i++) {
    Serial.print("  GPIO"); Serial.print(kScanPins[i]);
    Serial.print("="); Serial.print(edges[i]);
  }
  Serial.println();
}

// ----------------- RMT raw dump --------------------------------------------------
static void rmtStop() {
  if (!g_rmtOn) return;
  rmt_rx_stop(RMT_CHANNEL_0);
  rmt_driver_uninstall(RMT_CHANNEL_0);
  rb = nullptr;
  g_rmtOn = false;
}
static void rmtStart(int pin) {
  rmtStop();
  rmt_config_t c = RMT_DEFAULT_CONFIG_RX((gpio_num_t)pin, RMT_CHANNEL_0);
  c.clk_div = 80;                          // 1 us / tick
  c.rx_config.filter_en = true;
  c.rx_config.filter_ticks_thresh = 50;    // ignore sub-pulse glitches
  c.rx_config.idle_threshold = 8000;       // 8 ms gap closes a frame
  rmt_config(&c);
  rmt_driver_install(RMT_CHANNEL_0, 4096, 0);
  rmt_get_ringbuf_handle(RMT_CHANNEL_0, &rb);
  rmt_rx_start(RMT_CHANNEL_0, true);
  g_rmtOn = true;
  Serial.print("RMT capturing GPIO"); Serial.println(pin);
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

// ----------------- serial command handling --------------------------------------
static void setMode(Mode m) {
  g_mode = m;
  if (m == MODE_RMTDUMP) {
    rmtStart(g_rxPin);
  } else {
    rmtStop();
    Serial.println("PINSCAN — hold a 433 MHz OOK remote during each window");
  }
}
static void selectPin(int idx) {            // idx 0..kNumScan-1
  g_rxPin = kScanPins[idx];
  Serial.print("RX pin -> GPIO"); Serial.println(g_rxPin);
  if (g_mode == MODE_RMTDUMP) rmtStart(g_rxPin);  // re-arm on the new pin
}
static void handleCmd(char c) {
  switch (c) {
    case 's': setMode(MODE_PINSCAN); break;
    case 'd': setMode(MODE_RMTDUMP); break;
    case 'g': dumpRegs(); break;
    case 'i': Serial.print("re-init: "); Serial.println(sx_init() ? "OK" : "FAIL"); dumpRegs(); break;
    case 'R': Serial.println("rebooting..."); delay(100); ESP.restart(); break;
    case '?': case 'h': printHelp(); break;
    default:
      if (c >= '1' && c <= ('0' + kNumScan)) selectPin(c - '1');
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n== PulseTape OOK hardware probe (interactive) ==");
  printConfig();
  Serial.print("SX1278 init: ");
  bool ok = sx_init();
  Serial.println(ok ? "OK" : "FAILED (check SPI wiring / RST pin)");
  if (ok) dumpRegs();
  printHelp();
  setMode(MODE_PINSCAN);   // start scanning by default
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    handleCmd(c);
  }
  if (g_mode == MODE_PINSCAN) {
    pinScan(1500);
  } else if (g_rmtOn) {
    rmtDrain();
  }
}
