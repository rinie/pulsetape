// app/oled_display.cpp — see oled_display.h.

#if defined(ARDUINO_ARCH_ESP32)

#include "oled_display.h"
#include "../board/board.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_W  128
#define SCREEN_H   64

// RST=-1: GPIO16 on ESP32-PICO-D4 is SPICS0 (internal flash CS); never drive it.
// pins_arduino.h for this board defines OLED_RST=16 unconditionally and wins the
// macro battle, so we hard-code -1 here instead of using the macro.
static Adafruit_SSD1306 disp(SCREEN_W, SCREEN_H, &Wire, -1);
static uint32_t count = 0;
static bool ok = false;

static void redrawHeader() {
    disp.setTextSize(1);
    disp.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    disp.setCursor(0, 0);
    disp.print("PulseTape");
    disp.print("  #");
    disp.println(count);

    disp.setCursor(0, 8);
    disp.print("SX1278:");
    disp.println(ok ? "OK " : "FAIL");
}

// Confirmed by runtime I2C scan on LilyGO TTGO T3 LoRa32 V1.6.1.
// pins_arduino.h for ttgo-lora32-v1 gives SDA=4/SCL=15 (V1.0 pinout) — wrong.
// Hard-coded to avoid the macro re-definition from pins_arduino.h (included
// later via Wire.h with no #ifndef guard). Named (not SDA/SCL) to dodge that clash.
static const uint8_t oledSda  = 21;
static const uint8_t oledScl  = 22;
static const uint8_t oledAddr = 0x3C;

void oledBegin(bool sx1278Ok) {
    ok = sx1278Ok;
    Wire.begin(oledSda, oledScl);
    // Blank the display immediately so stale content doesn't linger during init.
    Wire.beginTransmission(oledAddr);
    Wire.write(0x00);  // command mode
    Wire.write(0xAE);  // display off
    Wire.endTransmission();
    if (!disp.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
        Serial.println("OLED: SSD1306 begin failed");
        return;
    }
    disp.clearDisplay();
    redrawHeader();
    disp.setCursor(0, 16);
    disp.println("waiting for RF...");
    disp.display();
}

void oledShowFrame(uint16_t pulseCount, uint32_t frameNum, int8_t rssiDbm) {
    count = frameNum;
    disp.clearDisplay();
    redrawHeader();
    disp.setCursor(0, 16);
    disp.print("pulses: "); disp.println(pulseCount);
    disp.setCursor(0, 24);
    disp.print("rssi:   "); disp.print(rssiDbm); disp.println(" dBm");
    disp.display();
}

void oledShowTelegram(const RawTelegram& t) {
    count++;
    disp.clearDisplay();
    redrawHeader();

    disp.setCursor(0, 16);
    disp.print("rpt:");
    disp.print(t.repeatCount);
    disp.print("  p:");
    disp.println(t.count);

    // Nibble hex string — 2 chars per nibble byte, 21 chars per OLED row = 10/row.
    disp.setCursor(0, 24);
    uint16_t n = t.nibbleCount < 30 ? t.nibbleCount : 30;
    for (uint16_t i = 0; i < n; i++) {
        if (i == 10) { disp.setCursor(0, 32); }
        if (i == 20) { disp.setCursor(0, 40); }
        if (t.nibbles[i] < 0x10) disp.print('0');
        disp.print(t.nibbles[i], HEX);
    }

    disp.display();
}

#endif // ARDUINO_ARCH_ESP32
