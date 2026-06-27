// radio/sx1278_ook.cpp — see sx1278_ook.h.
// OOK register values are aligned to a known-good dump from this exact board
// (see comment in sx1278OokBegin). The radio config is therefore proven; the
// PulseTape capture/decode pipeline downstream is still unverified end-to-end.

#if defined(ARDUINO_ARCH_ESP32)

#include "sx1278_ook.h"
#include <Arduino.h>
#include <SPI.h>

// SX1276/78 register addresses (datasheet).
static const uint8_t regOpMode      = 0x01;
static const uint8_t regBitrateMsb  = 0x02;
static const uint8_t regBitrateLsb  = 0x03;
static const uint8_t regFrfMsb      = 0x06;
static const uint8_t regFrfMid      = 0x07;
static const uint8_t regFrfLsb      = 0x08;
static const uint8_t regLna          = 0x0C;
static const uint8_t regRxConfig    = 0x0D;
static const uint8_t regRxBw        = 0x12;
static const uint8_t regOokPeak     = 0x14;
static const uint8_t regOokFix      = 0x15;
static const uint8_t regOokAvg      = 0x16;
static const uint8_t regPreambleDetect  = 0x1F;  // bit7=PreambleDetectorOn
static const uint8_t regSyncConfig      = 0x27;  // bit4=SyncOn
static const uint8_t regPacketConfig1  = 0x30;  // bit7=PacketFormat, bit4-3=CRC
static const uint8_t regPacketConfig2  = 0x31;  // bit6=DataMode: 0=continuous, 1=packet
static const uint8_t regDioMapping1 = 0x40;
static const uint8_t regDioMapping2 = 0x41;
static const uint8_t regVersion      = 0x42;

// RegOpMode fields (FSK/OOK mode, i.e. LongRangeMode=0).
static const uint8_t opmodeOokLowfreq = 0x20 | 0x08;  // ModulationType=OOK | LowFreqModeOn
static const uint8_t modeSleep   = 0x00;
static const uint8_t modeStdby   = 0x01;
static const uint8_t modeRxCont = 0x05;

static uint8_t nssPin;

static void writeReg(uint8_t reg, uint8_t val) {
  digitalWrite(nssPin, LOW);
  SPI.transfer(reg | 0x80);  // MSB set = write
  SPI.transfer(val);
  digitalWrite(nssPin, HIGH);
}

static uint8_t readReg(uint8_t reg) {
  digitalWrite(nssPin, LOW);
  SPI.transfer(reg & 0x7F);  // MSB clear = read
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(nssPin, HIGH);
  return val;
}

bool sx1278OokBegin(uint8_t sck, uint8_t miso, uint8_t mosi,
                    uint8_t nss, uint8_t rst, uint32_t freqHz) {
  nssPin = nss;
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
  if (readReg(regVersion) != 0x12) {
    SPI.endTransaction();
    return false;
  }

  // LongRangeMode can only change in sleep. If the chip booted in LoRa mode,
  // a single write of 0x00 may be ignored. Two-step: sleep in current mode
  // first, then clear LongRangeMode, then set OOK standby.
  writeReg(regOpMode, readReg(regOpMode) & 0xF8);  // sleep, keep LongRangeMode
  delay(10);
  writeReg(regOpMode, 0x00);                          // sleep + FSK/OOK
  delay(5);
  writeReg(regOpMode, opmodeOokLowfreq | modeStdby);
  delay(5);
  Serial.print("SX1278 opmode_after_stdby=0x"); Serial.println(readReg(regOpMode), HEX);

  // Carrier frequency: Frf = freq * 2^19 / FXOSC, FXOSC = 32 MHz.
  uint32_t frf = (uint32_t)(((uint64_t)freqHz << 19) / 32000000ULL);
  writeReg(regFrfMsb, (uint8_t)(frf >> 16));
  writeReg(regFrfMid, (uint8_t)(frf >> 8));
  writeReg(regFrfLsb, (uint8_t)(frf));

  // ---------------------------------------------------------------------------
  // FULL OOK continuous-RX register map. Every value matches the known-good dump
  // read from THIS board after rtl_433_ESP (RadioLib) received KAKU
  // ("first kaku received.txt"). Values are facts (Semtech SX1276/77/78/79
  // datasheet + that dump); no code copied from rtl_433_ESP (GPL) / OOKwiz (LGPL).
  //
  //  Reg               Addr  Value   Meaning
  //  RegOpMode         0x01  0x2D    LongRangeMode=0 (FSK/OOK), OOK, LowFreqOn, RXCONT
  //  RegBitrate        0x02  0x682B  ~1.2 kbps — OOK threshold timing reference
  //  RegFrf            0x06  (calc)  carrier = freq * 2^19 / 32 MHz (433.92 MHz)
  //  RegLna            0x0C  0x20    max gain; HF boost off (433 uses the LF port)
  //  RegRxConfig       0x0D  0x08    AgcAutoOn
  //  RegRxBw           0x12  0x01    RxBwMant=16, Exp=1 -> ~250 kHz (narrower lost signal)
  //  RegOokPeak        0x14  0x08    OokThreshType=01 (peak), BitSyncOn=0 (raw bitstream)
  //  RegOokFix         0x15  0x0F    fixed floor under the peak detector
  //  RegOokAvg         0x16  0x12    peak-threshold decrement ~once/chip (datasheet default)
  //  RegPreambleDetect 0x1F  0x00    preamble detector OFF
  //  RegSyncConfig     0x27  0x00    sync-word detection OFF
  //  RegPacketConfig1  0x30  0x00    fixed length, no whitening, no CRC, no addr filter
  //  RegPacketConfig2  0x31  0x00    DataMode bit6=0 => CONTINUOUS (0x40=packet MUTES DIO2!)
  //  RegDioMapping1    0x40  0x00    DIO2 = DATA in continuous mode
  //  RegDioMapping2    0x41  0x00    defaults
  //
  // The bug that cost a day: RegPacketConfig2 must be 0x00 (continuous). Its reset
  // default 0x40 is PACKET mode, which gates DIO2 — radio configured but mute.
  // ---------------------------------------------------------------------------

  // Bitrate ~1.2 kbps: BitRate = FXOSC / rate. Even with bit-sync off this sets
  // the OOK peak-threshold timing reference; a slow rate suits slow OOK. rtl_433_
  // ESP uses 1.2 kbps. 32e6/1200 = 26667 = 0x682B. (Not in the dump; kept.)
  writeReg(regBitrateMsb, 0x68);
  writeReg(regBitrateLsb, 0x2B);

  // RegLna: max gain, HF boost off (0x20) — matches the working dump. 433 MHz
  // uses the LF port, so the HF boost bits are moot here.
  writeReg(regLna, 0x20);

  // AGC auto on (RegRxConfig bit3). AGC settling is what makes the gaps noisy on
  // OOK — handled downstream by the glitch filter + repeat detection.
  writeReg(regRxConfig, 0x08);

  // RegRxBw ~250 kHz: (RxBwMant=16 -> code 00) | (RxBwExp=1) -> 0x01. The working
  // dump uses this; wide suits drifty cheap transmitters. Narrow only with testing
  // (rtl_433_ESP found narrower bandwidth dropped signals).
  writeReg(regRxBw, 0x01);

  // RegOokPeak 0x08: OokThreshType[4:3]=01 (PEAK detector), BitSyncOn(bit5)=0 so
  // DIO2 carries the RAW slicer output (required for arbitrary multi-rate OOK),
  // OokPeakThreshStep[2:0]=000. Proven value from the dump; the peak detector uses
  // RegOokFix (below) as its floor. (A spurious 0x20 = BitSyncOn on + fixed broke it.)
  writeReg(regOokPeak, 0x08);
  // RegOokFix: floor threshold = 0x0F (15) per the working dump.
  writeReg(regOokFix, 0x0F);
  // RegOokAvg: datasheet default 0x12; matches dump.
  writeReg(regOokAvg, 0x12);

  // Disable packet handler: preamble detect off, sync word off, no CRC,
  // no address filtering. In direct/continuous mode the packet engine must
  // be fully bypassed or it gates the DATA output. RadioLib's beginFSK()
  // sets these via disableSyncWordFiltering(), disablePreambleDetect(), etc.
  writeReg(regPreambleDetect,  0x00);  // preamble detector off
  writeReg(regSyncConfig,      0x00);  // sync word off
  writeReg(regPacketConfig1,  0x00);  // variable len off, no whitening, no CRC, no address filter
  // Continuous data mode: RegPacketConfig2 bit6 DataMode = 0 (0=continuous,
  // 1=packet; reset default is 0x40=packet). Must be CLEARED, or the packet
  // engine gates DIO2 and no raw OOK bitstream comes out. The known-good
  // rtl_433_ESP dump shows RegPacketConfig2 = 0x00. (A previous 0x40 here left
  // the chip in packet mode → zero edges on every pin.)
  writeReg(regPacketConfig2, 0x00);

  // DIO2 = DATA in continuous mode (mapping 00 in RegDioMapping1 bits[3:2]).
  writeReg(regDioMapping1, 0x00);
  writeReg(regDioMapping2, 0x00);

  // Enter continuous receive — data now streams on DIO2.
  writeReg(regOpMode, opmodeOokLowfreq | modeRxCont);
  delay(5);
  // Read back the critical registers so the boot log verifies the live state
  // (PktCfg2 especially — 0x00 = continuous; 0x40 would mean the chip is mute).
  Serial.print("SX1278 opmode=0x"); Serial.print(readReg(regOpMode), HEX);
  Serial.print(" RxBw=0x"); Serial.print(readReg(regRxBw), HEX);
  Serial.print(" OokPeak=0x"); Serial.print(readReg(regOokPeak), HEX);
  Serial.print(" OokFix=0x"); Serial.print(readReg(regOokFix), HEX);
  Serial.print(" PktCfg1=0x"); Serial.print(readReg(regPacketConfig1), HEX);
  Serial.print(" PktCfg2=0x"); Serial.println(readReg(regPacketConfig2), HEX);

  SPI.endTransaction();
  return true;
}

uint8_t sx1278Rssi() {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  uint8_t v = readReg(0x11);  // RegRssiValue (FSK/OOK mode)
  SPI.endTransaction();
  return v >> 1;  // LSB = 0.5 dB, return integer dBm
}

#endif // ARDUINO_ARCH_ESP32
