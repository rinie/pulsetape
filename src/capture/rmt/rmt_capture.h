// capture/rmt/rmt_capture.h
// ESP32 RMT-based capture backend.
//
// HARDWARE LAYER: the only place that includes the ESP32 RMT driver. Implements
// the generic ICaptureBackend, so the generic core never knows RMT exists — the
// mirror of the RP2040 PIO backend. RMT does the heavy lifting in hardware:
// pulse-duration capture, end-of-frame idle detection (signal_range_max_ns), and
// a minimum-pulse glitch filter (signal_range_min_ns).
//
// Targets the ESP-IDF v5 RMT RX driver (arduino-esp32 3.x). The whole file is
// guarded so it compiles to nothing on non-ESP32 targets.
//
// SCAFFOLD: not yet compiled/tested on hardware.

#ifndef PULSETAPE_RMT_CAPTURE_H
#define PULSETAPE_RMT_CAPTURE_H

#if defined(ARDUINO_ARCH_ESP32)

#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "../../pulsetape/capture_iface.h"

// Receive buffer capacity in RMT symbols (one symbol = two edges). Sized for
// long frames; the driver ping-pongs the 64-symbol hardware block into here, so
// large frames work on plain ESP32 without DMA (just more interrupts).
#define RMT_RX_BUF_SYMBOLS 512

class RmtCapture : public ICaptureBackend {
 public:
  bool begin(uint8_t data_pin) override;
  CaptureEvent next() override;

 private:
  static bool IRAM_ATTR onRecvDone(rmt_channel_handle_t ch,
                                   const rmt_rx_done_event_data_t* edata, void* ctx);
  void arm();  // (re)start an RMT receive into rx_buf_

  rmt_channel_handle_t channel_ = nullptr;
  rmt_receive_config_t recv_cfg_ = {};
  QueueHandle_t        done_queue_ = nullptr;  // ISR -> next(): symbol count of a finished frame

  rmt_symbol_word_t rx_buf_[RMT_RX_BUF_SYMBOLS];
  size_t   cur_symbols_ = 0;  // symbols in the frame currently being drained
  size_t   sym_idx_ = 0;      // which symbol
  uint8_t  half_ = 0;         // 0 = duration0/level0, 1 = duration1/level1
  bool     have_frame_ = false;
};

#endif // ARDUINO_ARCH_ESP32
#endif // PULSETAPE_RMT_CAPTURE_H
