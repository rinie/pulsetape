// app/oled_display.cpp — see oled_display.h.

#if defined(ARDUINO_ARCH_ESP32)

#include "oled_display.h"
#include "../board/board.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_W  128
#define SCREEN_H   64
#define OLED_ADDR 0x3C

// RST=-1: GPIO16 on ESP32-PICO-D4 is SPICS0 (internal flash CS); never drive it.
// pins_arduino.h for this board defines OLED_RST=16 unconditionally and wins the
// macro battle, so we hard-code -1 here instead of using the macro.
static Adafruit_SSD1306 s_disp(SCREEN_W, SCREEN_H, &Wire, -1);
static uint32_t s_count = 0;
static bool s_ok = false;

static void redraw_header() {
    s_disp.setTextSize(1);
    s_disp.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    s_disp.setCursor(0, 0);
    s_disp.print("PulseTape");
    s_disp.print("  #");
    s_disp.println(s_count);

    s_disp.setCursor(0, 8);
    s_disp.print("SX1278:");
    s_disp.println(s_ok ? "OK " : "FAIL");
}

// Confirmed by runtime I2C scan on LilyGO TTGO T3 LoRa32 V1.6.1.
// pins_arduino.h for ttgo-lora32-v1 gives SDA=4/SCL=15 (V1.0 pinout) — wrong.
// Hard-coded to avoid the macro re-definition from pins_arduino.h (included
// later via Wire.h with no #ifndef guard).
static const uint8_t kSDA  = 21;
static const uint8_t kSCL  = 22;
static const uint8_t kADDR = 0x3C;

void oled_begin(bool sx1278_ok) {
    s_ok = sx1278_ok;
    Wire.begin(kSDA, kSCL);
    if (!s_disp.begin(SSD1306_SWITCHCAPVCC, kADDR)) {
        Serial.println("OLED: SSD1306 begin failed");
        return;
    }
    s_disp.clearDisplay();
    redraw_header();
    s_disp.setCursor(0, 16);
    s_disp.println("waiting for RF...");
    s_disp.display();
}

void oled_show_telegram(const RawTelegram& t) {
    s_count++;
    s_disp.clearDisplay();
    redraw_header();

    s_disp.setCursor(0, 16);
    s_disp.print("rpt:");
    s_disp.print(t.repeat_count);
    s_disp.print("  p:");
    s_disp.println(t.count);

    // Nibble hex string — 2 chars per nibble byte, 21 chars per OLED row = 10/row.
    s_disp.setCursor(0, 24);
    uint16_t n = t.nibble_count < 30 ? t.nibble_count : 30;
    for (uint16_t i = 0; i < n; i++) {
        if (i == 10) { s_disp.setCursor(0, 32); }
        if (i == 20) { s_disp.setCursor(0, 40); }
        if (t.nibbles[i] < 0x10) s_disp.print('0');
        s_disp.print(t.nibbles[i], HEX);
    }

    s_disp.display();
}

#endif // ARDUINO_ARCH_ESP32
