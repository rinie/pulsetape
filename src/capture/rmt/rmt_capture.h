// capture/rmt/rmt_capture.h
// ESP32 RMT-based capture backend — IDF v4 legacy API (arduino-esp32 2.x).
//
// HARDWARE LAYER: the only place that includes the ESP32 RMT driver. Implements
// the generic ICaptureBackend, so the generic core never knows RMT exists — the
// mirror of the RP2040 PIO backend. RMT does the heavy lifting in hardware:
// pulse-duration capture, end-of-frame idle detection (idle_threshold), and a
// minimum-pulse glitch filter (filter_ticks_thresh).
//
// The whole file is guarded so it compiles to nothing on non-ESP32 targets.

#ifndef PULSETAPE_RMT_CAPTURE_H
#define PULSETAPE_RMT_CAPTURE_H

#if defined(ARDUINO_ARCH_ESP32)

#include "driver/rmt.h"
#include "freertos/FreeRTOS.h"
#include "../../pulsetape/capture_iface.h"

class RmtCapture : public ICaptureBackend {
 public:
  bool begin(uint8_t data_pin) override;
  CaptureEvent next() override;

 private:
  static const rmt_channel_t kChannel = RMT_CHANNEL_0;

  RingbufHandle_t rb_        = nullptr;
  rmt_item32_t*   cur_items_ = nullptr;
  size_t          cur_count_ = 0;
  size_t          sym_idx_   = 0;
  uint8_t         half_      = 0;
};

#endif // ARDUINO_ARCH_ESP32
#endif // PULSETAPE_RMT_CAPTURE_H
