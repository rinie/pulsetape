// capture/rmt/rmt_capture.cpp — see rmt_capture.h.
// IDF v4 legacy RMT RX driver (arduino-esp32 2.x, ESP-IDF v4).

#if defined(ARDUINO_ARCH_ESP32)

#include "rmt_capture.h"
#include "../../board/board.h"

// APB clock is 80 MHz; divider 80 => 1 tick = 1 us, matching IDF v5 version.
// Max 15-bit duration at 1 us/tick is 32767 us (matches PULSE_MAX_US = 32000).
static const uint8_t RMT_CLK_DIV = 80;

bool RmtCapture::begin(uint8_t data_pin) {
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_RX((gpio_num_t)data_pin, kChannel);
    cfg.clk_div = RMT_CLK_DIV;
    cfg.rx_config.filter_en          = true;
    cfg.rx_config.filter_ticks_thresh = (uint8_t)PULSE_MIN_US;    // 50 ticks @ 1MHz = 50 us
    cfg.rx_config.idle_threshold      = (uint16_t)FRAME_GAP_US;   // 8000 ticks @ 1MHz = 8 ms

    if (rmt_config(&cfg) != ESP_OK) return false;
    // Ring buffer of 4096 bytes — enough for the longest OOK frame.
    if (rmt_driver_install(kChannel, 4096, 0) != ESP_OK) return false;
    rmt_get_ringbuf_handle(kChannel, &rb_);
    rmt_rx_start(kChannel, /*rx_idx_rst=*/true);
    return true;
}

CaptureEvent RmtCapture::next() {
    CaptureEvent ev;

    // Block for a completed frame if we have none in flight.
    // IDF v4 RMT driver pushes one frame per ring-buffer item after the idle timeout.
    // Use a bounded wait so loop() returns periodically and the arduino-esp32 task
    // watchdog (fed between loop() calls) doesn't fire on a quiet channel.
    if (!cur_items_) {
        size_t rx_size = 0;
        cur_items_ = (rmt_item32_t*)xRingbufferReceive(rb_, &rx_size, pdMS_TO_TICKS(500));
        if (!cur_items_) {
            ev.type = CaptureEvent::FRAME_GAP;
            ev.duration_us = 0;
            return ev;
        }
        cur_count_ = rx_size / sizeof(rmt_item32_t);
        sym_idx_   = 0;
        half_      = 0;
    }

    // Drain the current frame one duration at a time.
    if (sym_idx_ < cur_count_) {
        uint16_t dur = (half_ == 0) ? cur_items_[sym_idx_].duration0
                                    : cur_items_[sym_idx_].duration1;
        if (half_ == 0) {
            half_ = 1;
        } else {
            half_ = 0;
            sym_idx_++;
        }

        if (dur == 0) {
            // IDF v4 RMT marks end-of-frame with a zero-duration item.
            vRingbufferReturnItem(rb_, cur_items_);
            cur_items_ = nullptr;
            ev.type = CaptureEvent::FRAME_GAP;
            ev.duration_us = 0;
            return ev;
        }

        ev.type = CaptureEvent::DURATION;
        ev.duration_us = dur;
        return ev;
    }

    // Exhausted all items without an explicit zero terminator — still end of frame.
    vRingbufferReturnItem(rb_, cur_items_);
    cur_items_ = nullptr;
    ev.type = CaptureEvent::FRAME_GAP;
    ev.duration_us = 0;
    return ev;
}

#endif // ARDUINO_ARCH_ESP32
