// pulsetape/capture_iface.h
// The single contract between the generic pulse-tape core and the hardware.
//
// A capture backend (PIO today; interrupt / RMT / timer-capture tomorrow)
// produces a stream of CaptureEvents: edge durations in MICROSECONDS, plus
// frame-gap markers. The backend owns all silicon/board detail (it converts raw
// ticks to microseconds itself); it knows nothing about telegrams or nibbles.
//
// FrameAssembler is the generic consumer: it pairs durations into pulse/space,
// feeds the PulseSpaceIndex quantiser, applies the quality filter and repeat
// detector, and emits completed telegrams to a sink. It depends only on this
// header and the rest of the generic core — never on a backend implementation.
//
// GENERIC LAYER: plain C++, no Arduino.h, no hardware headers, no board pins.

#ifndef PULSETAPE_CAPTURE_IFACE_H
#define PULSETAPE_CAPTURE_IFACE_H

#include <stdint.h>
#include "psi.h"
#include "telegram.h"

// One event from a capture backend.
struct CaptureEvent {
  enum Type : uint8_t {
    DURATION,   // duration_us holds a measured HIGH or LOW edge length
    FRAME_GAP   // a long silence / overflow closed the current frame
  };
  Type     type;
  uint16_t duration_us;  // meaningful only when type == DURATION
};

// Abstract capture backend. Implementations live under src/capture/<backend>/.
class ICaptureBackend {
 public:
  virtual ~ICaptureBackend() {}

  // Bring up the capture hardware on `data_pin`. Returns false on failure.
  virtual bool begin(uint8_t data_pin) = 0;

  // Block until the next event is available and return it. Runs on the capture
  // core's loop. Implementations must convert raw ticks to microseconds and
  // emit FRAME_GAP on overflow/silence.
  virtual CaptureEvent next() = 0;
};

// Sink for completed, forward-worthy telegrams. The app supplies one (e.g. to
// push onto the inter-core queue). Must be quick and non-blocking.
typedef void (*TelegramSink)(const RawTelegram& telegram, void* ctx);

// Turns a CaptureEvent stream into validated, de-duplicated telegrams.
class FrameAssembler {
 public:
  // `frame_gap_us`: a DURATION longer than this on a space slot also closes the
  // frame (belt-and-suspenders alongside the backend's FRAME_GAP marker).
  FrameAssembler(const TelegramConfig& cfg, uint16_t frame_gap_us,
                 TelegramSink sink, void* sink_ctx)
      : cfg_(cfg), frame_gap_us_(frame_gap_us), sink_(sink), sink_ctx_(sink_ctx) {
    resetFrame();
  }

  // Feed one event. `now_ms` is the current time from the caller's clock; it is
  // used only for the repeat window (this layer never reads a clock itself).
  void onEvent(const CaptureEvent& ev, uint32_t now_ms);

 private:
  void resetFrame();
  void finalizeFrame(uint32_t now_ms);

  const TelegramConfig& cfg_;
  uint16_t              frame_gap_us_;
  TelegramSink          sink_;
  void*                 sink_ctx_;

  RawTelegram      current_;
  PulseSpaceIndex  psi_;
  RepeatDetector   repeats_;
  uint16_t         pending_pulse_us_;  // HIGH half waiting for its LOW partner
  bool             have_pending_;
};

#endif // PULSETAPE_CAPTURE_IFACE_H
