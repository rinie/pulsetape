// capture/rmt/rmt_capture.cpp — see rmt_capture.h.
// IDF v4 legacy RMT RX driver (arduino-esp32 2.x, ESP-IDF v4).

#if defined(ARDUINO_ARCH_ESP32)

#include "rmt_capture.h"
#include "../../board/board.h"

// APB clock is 80 MHz; divider 80 => 1 tick = 1 us, matching IDF v5 version.
// Max 15-bit duration at 1 us/tick is 32767 us (matches PULSE_MAX_US = 32000).
static const uint8_t rmtClkDiv = 80;

bool RmtCapture::begin(uint8_t dataPin) {
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_RX((gpio_num_t)dataPin, channel);
    cfg.clk_div = rmtClkDiv;
    // Each RMT memory block holds 64 symbols = 128 edges; the default 1 block caps
    // a frame at 128 edges (NewKAKU ~132 was being truncated!). Use 4 blocks ->
    // 256 symbols = 512 edges, matching TELEGRAM_MAX_PULSES. (Channel 0 borrows
    // blocks 0-3; we use no other RMT channels. Long HVAC frames would need more
    // blocks + larger PSI buffers — see long_packets.md.)
    cfg.mem_block_num = 4;
    cfg.rx_config.filter_en          = true;
    cfg.rx_config.filter_ticks_thresh = (uint8_t)PULSE_MIN_US;    // 50 ticks @ 1MHz = 50 us
    cfg.rx_config.idle_threshold      = (uint16_t)FRAME_GAP_US;   // 8000 ticks @ 1MHz = 8 ms

    if (rmt_config(&cfg) != ESP_OK) return false;
    // Ring buffer of 4096 bytes — enough for the longest OOK frame.
    if (rmt_driver_install(channel, 4096, 0) != ESP_OK) return false;
    rmt_get_ringbuf_handle(channel, &rb);
    rmt_rx_start(channel, /*rx_idx_rst=*/true);
    return true;
}

CaptureEvent RmtCapture::next() {
    CaptureEvent ev;

    // Block for a completed frame if we have none in flight.
    // IDF v4 RMT driver pushes one frame per ring-buffer item after the idle timeout.
    // Use a bounded wait so loop() returns periodically and the arduino-esp32 task
    // watchdog (fed between loop() calls) doesn't fire on a quiet channel.
    if (!curItems) {
        size_t rxSize = 0;
        curItems = (rmt_item32_t*)xRingbufferReceive(rb, &rxSize, pdMS_TO_TICKS(500));
        if (!curItems) {
            ev.type = CaptureEvent::frameGap;
            ev.durationUs = 0;
            return ev;
        }
        curCount = rxSize / sizeof(rmt_item32_t);
        symIdx   = 0;
        half     = 0;
    }

    // Drain the current frame one duration at a time.
    if (symIdx < curCount) {
        uint16_t dur = (half == 0) ? curItems[symIdx].duration0
                                   : curItems[symIdx].duration1;
        if (half == 0) {
            half = 1;
        }
        else {
            half = 0;
            symIdx++;
        }

        if (dur == 0) {
            // IDF v4 RMT marks end-of-frame with a zero-duration item.
            vRingbufferReturnItem(rb, curItems);
            curItems = nullptr;
            ev.type = CaptureEvent::frameGap;
            ev.durationUs = 0;
            return ev;
        }

        ev.type = CaptureEvent::duration;
        ev.durationUs = dur;
        return ev;
    }

    // Exhausted all items without an explicit zero terminator — still end of frame.
    vRingbufferReturnItem(rb, curItems);
    curItems = nullptr;
    ev.type = CaptureEvent::frameGap;
    ev.durationUs = 0;
    return ev;
}

#endif // ARDUINO_ARCH_ESP32
