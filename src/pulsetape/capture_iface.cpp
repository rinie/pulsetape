// pulsetape/capture_iface.cpp — FrameAssembler. See capture_iface.h.

#include "capture_iface.h"
#include <string.h>

void FrameAssembler::resetFrame() {
  current.count = 0;
  current.nibbleCount = 0;
  current.repeatCount = 0;
  current.forwarded = 0;
  current.released = 0;
  current.event = 0;
  current.rssi = -1;  // SRX882S has no RSSI
  psi.reset();
  havePending = false;
  pendingPulseUs = 0;
}

void FrameAssembler::finalizeFrame(uint32_t nowUs) {
  // A frame ends on a HIGH whose trailing LOW is the inter-frame gap, so an odd
  // number of durations leaves one unpaired pulse. Drop it from the reported count
  // so count == the classified pair-elements (sum of the per-class counts); it
  // carries no pair and never reached psNibbleIndex anyway.
  if (havePending && current.count > 0) current.count--;

  if (current.count >= cfg.minPulses) {
    // Rank timing classes by duration (0 = shortest) so the fingerprint is
    // canonical, then snapshot it. Both this frame and the ring entries are
    // normalized, so repeat-matching compares like with like.
    psi.normalize();
    // Trim unreliable trailing nibbles (the last bit(s) wobble at the transmission
    // tail / frame-gap boundary) from the repeat fingerprint. Raw pulses[] are kept
    // intact for decoders — only the matching fingerprint is shortened.
    uint16_t nc = psi.nibbleCount();
    nc = (nc > cfg.tailTrimPairs) ? (uint16_t)(nc - cfg.tailTrimPairs) : 0;
    current.nibbleCount = nc;
    memcpy(current.nibbles, psi.nibbles(), nc);

    // Snapshot the timing-class table (ascending by duration after normalize):
    // the pulse length for each index value, used instead of the full pulse list.
    current.classCount = psi.bucketCount();
    for (uint8_t i = 0; i < current.classCount; i++) {
      current.classMin[i] = psi.bucketMin(i);
      current.classMax[i] = psi.bucketMax(i);
      current.classHits[i] = psi.bucketHits(i);
    }
    current.dataType = psi.detectDataType();
    // For S/P, record which class the constant (dropped) side sits in, so the
    // output can denote a constant-1 column ("1s"/"1p") vs constant-0 ("s"/"p").
    current.constClass = 0;
    if (current.dataType == PSI_DATA_S) {           // pulse is constant
      for (uint8_t i = 0; i < current.classCount; i++)
        if (psi.bucketPulseHits(i) > 0) { current.constClass = i; break; }
    }
    else if (current.dataType == PSI_DATA_P) {      // space is constant
      for (uint8_t i = 0; i < current.classCount; i++)
        if (psi.bucketHits(i) - psi.bucketPulseHits(i) > 0) { current.constClass = i; break; }
    }
    current.timestampUs = nowUs;

    if (telegramValid(current, cfg)) {
      if (repeats.offer(current, cfg, nowUs)) {
        if (sink) sink(current, sinkCtx);
      }
    }
  }
  resetFrame();
}

void FrameAssembler::onEvent(const CaptureEvent& ev, uint32_t nowUs) {
  if (ev.type == CaptureEvent::FRAME_GAP) {
    finalizeFrame(nowUs);
    // Frame gaps also arrive on idle ticks, so this drives the window-close
    // "released" events (FORWARD_LAST/_BOTH) with their true repeat totals.
    RawTelegram* r;
    while ((r = repeats.takeExpired(cfg, nowUs)) != nullptr) {
      if (sink) sink(*r, sinkCtx);
    }
    return;
  }

  const uint16_t us = ev.durationUs;

  // A long LOW (space) also marks the end of a frame.
  const bool onSpaceSlot = (current.count & 1) != 0;
  if (onSpaceSlot && us > frameGapUs) {
    finalizeFrame(nowUs);
    return;
  }

  // Append the raw duration.
  if (current.count < TELEGRAM_MAX_PULSES) {
    current.pulses[current.count++] = us;
  }
  else {
    // Buffer overrun: too long to be a real telegram, drop it.
    resetFrame();
    return;
  }

  // Pair HIGH (pulse) with the following LOW (space) and quantise.
  if (!havePending) {
    pendingPulseUs = us;
    havePending = true;
  }
  else {
    psi.addPair(pendingPulseUs, us);
    havePending = false;
  }
}
