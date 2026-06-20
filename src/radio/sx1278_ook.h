// radio/sx1278_ook.h
// Front-end configurator for the onboard SX1276/78 (LilyGO T3): puts the radio
// into OOK *continuous* receive so the demodulated bitstream appears on DIO2,
// which a capture backend (RMT) then reads from a GPIO.
//
// This is a RADIO FRONT-END, separate from the capture backend: the backend just
// measures edges on a pin and neither knows nor cares whether the source is this
// SX1278 or an external SRX882S. Configuring the radio is a one-shot SPI step at
// startup.
//
// Bare Arduino SPI, no RadioLib — register values are derived from the Semtech
// SX1276/77/78/79 datasheet (facts), so PulseTape stays dependency-free and MIT.
// Which OOK knobs matter (peak threshold, wide RxBw, low bitrate, fixed floor,
// bit-sync off) and the chosen values were informed by two established projects
// as INSPIRATION ONLY — no code copied from either:
//   - rtl_433_ESP (https://github.com/NorthernMan54/rtl_433_ESP, GPL, via RadioLib)
//   - OOKwiz      (https://github.com/ropg/OOKwiz, LGPL-3.0)
// RadioLib (MIT) is a drop-in alternative if you prefer a maintained driver or
// want the TX path later.
//
// SCAFFOLD: not yet compiled/tested on hardware; verify register values and
// reception on the bench. ESP32-only; compiles to nothing elsewhere.

#ifndef PULSETAPE_SX1278_OOK_H
#define PULSETAPE_SX1278_OOK_H

#if defined(ARDUINO_ARCH_ESP32)

#include <stdint.h>

// Configure the SX1278 for OOK continuous RX with data on DIO2. Returns false if
// the chip doesn't answer (RegVersion mismatch). Call once in setup() before the
// RMT backend begins capturing DIO2's GPIO.
bool sx1278_ook_begin(uint8_t sck, uint8_t miso, uint8_t mosi,
                      uint8_t nss, uint8_t rst, uint32_t freq_hz);

// Read current RSSI (FSK/OOK mode, RegRssiValue 0x11). Returns -dBm (positive).
// Call only after sx1278_ook_begin() and while in RX mode.
uint8_t sx1278_rssi();

#endif // ARDUINO_ARCH_ESP32
#endif // PULSETAPE_SX1278_OOK_H
