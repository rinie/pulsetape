// app/oled_display.h
// SSD1306 OLED status display for the LilyGO T3 (ESP32 only).
// Compiles to nothing on non-ESP32 targets.

#ifndef PULSETAPE_OLED_DISPLAY_H
#define PULSETAPE_OLED_DISPLAY_H

#if defined(ARDUINO_ARCH_ESP32)

#include "../pulsetape/telegram.h"

// Call once in setup(). sx1278Ok is the return value of sx1278OokBegin(),
// or false when using an external receiver (no SX1278 frontend).
void oledBegin(bool sx1278Ok);

// Call from the telegram sink to refresh the display with the latest telegram.
void oledShowTelegram(const RawTelegram& t);

// Call from the interrupt-capture path when a raw frame is detected.
void oledShowFrame(uint16_t pulseCount, uint32_t frameNum, int8_t rssiDbm);

#endif // ARDUINO_ARCH_ESP32
#endif // PULSETAPE_OLED_DISPLAY_H
