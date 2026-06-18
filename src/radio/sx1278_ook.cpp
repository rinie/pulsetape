// radio/sx1278_ook.cpp — see sx1278_ook.h.
// SCAFFOLD: datasheet-derived register values, not yet hardware-verified.

#if defined(ARDUINO_ARCH_ESP32)

#include "sx1278_ook.h"
#include <Arduino.h>
#include <SPI.h>

// SX1276/78 register addresses (datasheet).
static const uint8_t REG_OP_MODE      = 0x01;
static const uint8_t REG_BITRATE_MSB  = 0x02;
static const uint8_t REG_BITRATE_LSB  = 0x03;
static const uint8_t REG_FRF_MSB      = 0x06;
static const uint8_t REG_FRF_MID      = 0x07;
static const uint8_t REG_FRF_LSB      = 0x08;
static const uint8_t REG_LNA          = 0x0C;
static const uint8_t REG_RX_CONFIG    = 0x0D;
static const uint8_t REG_RX_BW        = 0x12;
static const uint8_t REG_OOK_PEAK     = 0x14;
static const uint8_t REG_OOK_FIX      = 0x15;
static const uint8_t REG_OOK_AVG      = 0x16;
static const uint8_t REG_DIO_MAPPING1 = 0x40;
static const uint8_t REG_DIO_MAPPING2 = 0x41;
static const uint8_t REG_VERSION      = 0x42;

// RegOpMode fields (FSK/OOK mode, i.e. LongRangeMode=0).
static const uint8_t OPMODE_OOK_LOWFREQ = 0x20 | 0x08;  // ModulationType=OOK | LowFreqModeOn
static const uint8_t MODE_SLEEP   = 0x00;
static const uint8_t MODE_STDBY   = 0x01;
static const uint8_t MODE_RX_CONT = 0x05;

static uint8_t s_nss;

static void writeReg(uint8_t reg, uint8_t val) {
  digitalWrite(s_nss, LOW);
  SPI.transfer(reg | 0x80);  // MSB set = write
  SPI.transfer(val);
  digitalWrite(s_nss, HIGH);
}

static uint8_t readReg(uint8_t reg) {
  digitalWrite(s_nss, LOW);
  SPI.transfer(reg & 0x7F);  // MSB clear = read
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(s_nss, HIGH);
  return val;
}

bool sx1278_ook_begin(uint8_t sck, uint8_t miso, uint8_t mosi,
                      uint8_t nss, uint8_t rst, uint32_t freq_hz) {
  s_nss = nss;
  pinMode(nss, OUTPUT);
  digitalWrite(nss, HIGH);
  pinMode(rst, OUTPUT);

  // Reset pulse (RST is active-low on the SX127x).
  digitalWrite(rst, LOW);
  delay(2);
  digitalWrite(rst, HIGH);
  delay(6);

  SPI.begin(sck, miso, mosi, nss);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  // Confirm the chip is talking (SX1276/78 RegVersion == 0x12).
  if (readReg(REG_VERSION) != 0x12) {
    SPI.endTransaction();
    return false;
  }

  // LongRangeMode bit is only writable in sleep.
  writeReg(REG_OP_MODE, MODE_SLEEP);
  delay(2);
  writeReg(REG_OP_MODE, OPMODE_OOK_LOWFREQ | MODE_STDBY);

  // Carrier frequency: Frf = freq * 2^19 / FXOSC, FXOSC = 32 MHz.
  uint32_t frf = (uint32_t)(((uint64_t)freq_hz << 19) / 32000000ULL);
  writeReg(REG_FRF_MSB, (uint8_t)(frf >> 16));
  writeReg(REG_FRF_MID, (uint8_t)(frf >> 8));
  writeReg(REG_FRF_LSB, (uint8_t)(frf));

  // The OOK front-end values below converge with what two established projects
  // settled on for 433 MHz OOK — rtl_433_ESP (https://github.com/NorthernMan54/
  // rtl_433_ESP, GPL, via RadioLib) and OOKwiz (https://github.com/ropg/OOKwiz,
  // LGPL-3.0). Used as inspiration for the *values* (datasheet facts) only; no
  // code from either project was copied.

  // Bitrate ~1.2 kbps: BitRate = FXOSC / rate. Even with bit-sync off this sets
  // the OOK peak-threshold timing reference; a slow rate suits slow OOK. rtl_433_
  // ESP uses 1.2 kbps. 32e6/1200 = 26667 = 0x682B.
  writeReg(REG_BITRATE_MSB, 0x68);
  writeReg(REG_BITRATE_LSB, 0x2B);

  // Max LNA gain + boost (RegLna).
  writeReg(REG_LNA, 0x23);

  // AGC auto on (RegRxConfig bit3). AGC settling is what makes the gaps noisy on
  // OOK — handled downstream by the glitch filter + repeat detection.
  writeReg(REG_RX_CONFIG, 0x08);

  // RegRxBw ~125 kHz: (RxBwMant=16 -> code 00) | (RxBwExp=2) -> 0x02. Wide on
  // purpose: rtl_433_ESP found narrower bandwidth dropped received signals, and
  // cheap transmitters drift. (~50 kHz lost packets; do not narrow without testing.)
  writeReg(REG_RX_BW, 0x02);

  // RegOokPeak: peak threshold (OokThreshType=01 -> 0x08), BitSyncOn=0 so DIO2
  // carries the RAW slicer output (required for arbitrary multi-rate OOK).
  // OokPeakTheshStep = 0.5 dB (bits 2:0 = 000).
  writeReg(REG_OOK_PEAK, 0x08);
  // RegOokFix: fixed floor under the peak detector — rejects low-level noise in
  // the gaps. Starting point ~90 (rtl_433_ESP default OOK_FIXED_THRESHOLD); tune
  // on hardware. (A future refinement: adjust this floor dynamically from RSSI /
  // noise, à la rtl_433_ESP's AUTOOOKFIX — reimplemented, not copied.)
  writeReg(REG_OOK_FIX, 0x5A);
  // RegOokAvg: OokPeakTheshDec = once per chip (bits 7:5 = 000); rest default.
  writeReg(REG_OOK_AVG, 0x02);

  // In continuous RX, DIO2 outputs DATA regardless of mapping; keep defaults.
  writeReg(REG_DIO_MAPPING1, 0x00);
  writeReg(REG_DIO_MAPPING2, 0x00);

  // Enter continuous receive — data now streams on DIO2.
  writeReg(REG_OP_MODE, OPMODE_OOK_LOWFREQ | MODE_RX_CONT);

  SPI.endTransaction();
  return true;
}

#endif // ARDUINO_ARCH_ESP32
