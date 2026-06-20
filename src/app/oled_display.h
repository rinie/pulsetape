// app/oled_display.h
// SSD1306 OLED status display for the LilyGO T3 (ESP32 only).
// Compiles to nothing on non-ESP32 targets.

#ifndef PULSETAPE_OLED_DISPLAY_H
#define PULSETAPE_OLED_DISPLAY_H

#if defined(ARDUINO_ARCH_ESP32)

#include "../pulsetape/telegram.h"

// Call once in setup(). sx1278_ok is the return value of sx1278_ook_begin(),
// or false when using an external receiver (no SX1278 frontend).
void oled_begin(bool sx1278_ok);

// Call from the telegram sink to refresh the display with the latest telegram.
void oled_show_telegram(const RawTelegram& t);

// Call from the interrupt-capture path when a raw frame is detected.
void oled_show_frame(uint16_t pulse_count, uint32_t frame_num, int8_t rssi_dbm);

#endif // ARDUINO_ARCH_ESP32
#endif // PULSETAPE_OLED_DISPLAY_H
