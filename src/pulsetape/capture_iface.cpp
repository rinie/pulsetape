// pulsetape/capture_iface.cpp — FrameAssembler. See capture_iface.h.

#include "capture_iface.h"
#include <string.h>

void FrameAssembler::resetFrame() {
  current_.count = 0;
  current_.nibble_count = 0;
  current_.repeat_count = 0;
  current_.forwarded = 0;
  current_.rssi = -1;  // SRX882S has no RSSI
  psi_.reset();
  have_pending_ = false;
  pending_pulse_us_ = 0;
}

void FrameAssembler::finalizeFrame(uint32_t now_ms) {
  if (current_.count >= cfg_.min_pulses) {
    // Rank timing classes by duration (0 = shortest) so the fingerprint is
    // canonical, then snapshot it. Both this frame and the ring entries are
    // normalized, so repeat-matching compares like with like.
    psi_.normalize();
    // Trim unreliable trailing nibbles (the last bit(s) wobble at the transmission
    // tail / frame-gap boundary) from the repeat fingerprint. Raw pulses[] are kept
    // intact for decoders — only the matching fingerprint is shortened.
    uint16_t nc = psi_.nibbleCount();
    nc = (nc > cfg_.tail_trim_pairs) ? (uint16_t)(nc - cfg_.tail_trim_pairs) : 0;
    current_.nibble_count = nc;
    memcpy(current_.nibbles, psi_.nibbles(), nc);

    // Snapshot the timing-class table (ascending by duration after normalize):
    // the pulse length for each index value, used instead of the full pulse list.
    current_.class_count = psi_.bucketCount();
    for (uint8_t i = 0; i < current_.class_count; i++) {
      current_.class_min[i] = psi_.bucketMin(i);
      current_.class_max[i] = psi_.bucketMax(i);
    }
    current_.timestamp_ms = now_ms;

    if (telegram_valid(current_, cfg_)) {
      if (repeats_.offer(current_, cfg_, now_ms)) {
        if (sink_) sink_(current_, sink_ctx_);
      }
    }
  }
  resetFrame();
}

void FrameAssembler::onEvent(const CaptureEvent& ev, uint32_t now_ms) {
  if (ev.type == CaptureEvent::FRAME_GAP) {
    finalizeFrame(now_ms);
    return;
  }

  const uint16_t us = ev.duration_us;

  // A long LOW (space) also marks the end of a frame.
  const bool on_space_slot = (current_.count & 1) != 0;
  if (on_space_slot && us > frame_gap_us_) {
    finalizeFrame(now_ms);
    return;
  }

  // Append the raw duration.
  if (current_.count < TELEGRAM_MAX_PULSES) {
    current_.pulses[current_.count++] = us;
  } else {
    // Buffer overrun: too long to be a real telegram, drop it.
    resetFrame();
    return;
  }

  // Pair HIGH (pulse) with the following LOW (space) and quantise.
  if (!have_pending_) {
    pending_pulse_us_ = us;
    have_pending_ = true;
  } else {
    psi_.addPair(pending_pulse_us_, us);
    have_pending_ = false;
  }
}
