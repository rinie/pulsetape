// capture/rmt/rmt_capture.cpp — see rmt_capture.h.
// SCAFFOLD: targets ESP-IDF v5 RMT RX (arduino-esp32 3.x); not yet hardware-tested.

#if defined(ARDUINO_ARCH_ESP32)

#include "rmt_capture.h"

// 1 MHz resolution => 1 RMT tick = 1 us, so symbol durations are already in us.
// Max 15-bit duration at this resolution is 32767 us (matches PULSE_MAX_US).
static const uint32_t RMT_RESOLUTION_HZ = 1000000;

// Hardware symbol block per channel (ESP32 = 64). The large rx_buf_ is filled
// from this by the driver.
static const size_t RMT_MEM_BLOCK_SYMBOLS = 64;

bool IRAM_ATTR RmtCapture::onRecvDone(rmt_channel_handle_t,
                                      const rmt_rx_done_event_data_t* edata, void* ctx) {
  RmtCapture* self = static_cast<RmtCapture*>(ctx);
  // edata->received_symbols points at self->rx_buf_ (the buffer we passed to
  // rmt_receive). Hand the symbol count to next(); it drains rx_buf_ before
  // re-arming, so the buffer is not overwritten underneath us.
  BaseType_t hp_task_woken = pdFALSE;
  size_t n = edata->num_symbols;
  xQueueSendFromISR(self->done_queue_, &n, &hp_task_woken);
  return hp_task_woken == pdTRUE;
}

void RmtCapture::arm() {
  rmt_receive(channel_, rx_buf_, sizeof(rx_buf_), &recv_cfg_);
}

bool RmtCapture::begin(uint8_t data_pin) {
  done_queue_ = xQueueCreate(4, sizeof(size_t));
  if (!done_queue_) return false;

  rmt_rx_channel_config_t ch_cfg = {};
  ch_cfg.gpio_num = (gpio_num_t)data_pin;
  ch_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  ch_cfg.resolution_hz = RMT_RESOLUTION_HZ;
  ch_cfg.mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS;
  if (rmt_new_rx_channel(&ch_cfg, &channel_) != ESP_OK) return false;

  rmt_rx_event_callbacks_t cbs = {};
  cbs.on_recv_done = &RmtCapture::onRecvDone;
  if (rmt_rx_register_event_callbacks(channel_, &cbs, this) != ESP_OK) return false;

  // Glitch filter and frame-end idle threshold — done in hardware by RMT.
  recv_cfg_.signal_range_min_ns = (uint32_t)PULSE_MIN_US * 1000;
  recv_cfg_.signal_range_max_ns = (uint32_t)FRAME_GAP_US * 1000;

  if (rmt_enable(channel_) != ESP_OK) return false;
  return true;
}

CaptureEvent RmtCapture::next() {
  CaptureEvent ev;

  // Block for a completed frame if we have none in flight.
  if (!have_frame_) {
    arm();  // start receiving into rx_buf_
    size_t n = 0;
    xQueueReceive(done_queue_, &n, portMAX_DELAY);  // ISR signals when idle closes the frame
    cur_symbols_ = n;
    sym_idx_ = 0;
    half_ = 0;
    have_frame_ = true;
  }

  // Drain the current frame one duration at a time.
  if (sym_idx_ < cur_symbols_) {
    uint16_t dur = (half_ == 0) ? rx_buf_[sym_idx_].duration0
                                : rx_buf_[sym_idx_].duration1;
    // Advance the (symbol, half) cursor.
    if (half_ == 0) {
      half_ = 1;
    } else {
      half_ = 0;
      sym_idx_++;
    }

    if (dur == 0) {
      // RMT marks the end of a frame with a zero duration.
      have_frame_ = false;
      ev.type = CaptureEvent::FRAME_GAP;
      ev.duration_us = 0;
      return ev;
    }

    ev.type = CaptureEvent::DURATION;
    ev.duration_us = dur;
    return ev;
  }

  // Ran out of symbols without an explicit zero terminator -> frame end.
  have_frame_ = false;
  ev.type = CaptureEvent::FRAME_GAP;
  ev.duration_us = 0;
  return ev;
}

#endif // ARDUINO_ARCH_ESP32
