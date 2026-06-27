// pulsetape.ino
// Thin entry point. All real logic lives under src/ in three layers:
//   src/pulsetape/  generic pulse-tape core (no hardware deps)
//   src/capture/    capture backend(s) implementing the generic interface
//   src/board/      board-specific pins/clock/thresholds
//
// Per-target wiring (selected by the Arduino core's architecture macro):
//   RP2040 (SenseCAP): PIO capture on core 1 -> queue -> debug print on core 0
//   ESP32  (LilyGO T3): RMT capture -> FrameAssembler -> debug + OLED + LED
//
// Board feedback (OLED, LED) is conditional on capability macros from the board
// header (BOARD_HAS_OLED, ONBOARD_LED), so a different board with neither still
// builds and runs (serial only).

#include <Arduino.h>

#include "src/board/board.h"
#include "src/pulsetape/telegram.h"
#include "src/pulsetape/capture_iface.h"
#include "src/app/debug_sink.h"

// Filled from the board layer; the one place board constants meet the generic
// config struct. Shared by both targets.
static TelegramConfig cfg = {
    /* pulseMinUs     */ PULSE_MIN_US,
    /* pulseMaxUs     */ PULSE_MAX_US,
    /* minPulses      */ MIN_PULSES,
    /* repeatMinCount */ REPEAT_MIN_COUNT,
    /* repeatWindowUs */ REPEAT_WINDOW_US,
    /* tailTrimPairs  */ TAIL_TRIM_PAIRS,
    /* maxClassPct    */ MAX_CLASS_PCT,
    /* forwardMode    */ FORWARD_MODE,
};

// ===================================================================== ESP32
#if defined(ARDUINO_ARCH_ESP32)

#include "src/capture/rmt/rmt_capture.h"
#if USE_SX1278_FRONTEND
#include "src/radio/sx1278_ook.h"
#endif
#if defined(BOARD_HAS_OLED)
#include "src/app/oled_display.h"
#endif

// RMT captures the DIO2 edge train in hardware; FrameAssembler runs the generic
// core (psNibbleIndex + telegram filter + repeat detection). RMT + its ISR do the
// timing, so a single loop() on one core is enough.
static RmtCapture capture;

#ifdef ONBOARD_LED
static const uint32_t LED_BLINK_MS = 40;
static uint32_t ledOffAt = 0;   // millis() deadline to switch the LED back off
#endif

// Sink: invoked for each telegram that passed repeat validation. Surface it on
// every output the board offers — serial always, OLED + LED when the board has them.
static void telegramSink(const RawTelegram& t, void*) {
  debugPrintTelegram(t);
#if defined(BOARD_HAS_OLED)
  oledShowTelegram(t);
#endif
#ifdef ONBOARD_LED
  digitalWrite(ONBOARD_LED, HIGH);
  ledOffAt = millis() + LED_BLINK_MS;
#endif
}

static FrameAssembler assembler(cfg, FRAME_GAP_US, telegramSink, nullptr);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== PulseTape (main sketch) ===");
  Serial.println("build " __DATE__ " " __TIME__ " | board " BOARD_NAME);

#if USE_SX1278_FRONTEND
  bool sxOk = sx1278OokBegin(SX1276_SCK, SX1276_MISO, SX1276_MOSI,
                             SX1276_NSS, SX1276_RST, RF_FREQUENCY_HZ);
  Serial.print("SX1278 init: ");
  Serial.println(sxOk ? "OK" : "FAILED (check SPI wiring)");
#else
  bool sxOk = false;
#endif

#if defined(BOARD_HAS_OLED)
  oledBegin(sxOk);
#else
  (void)sxOk;
#endif

#ifdef ONBOARD_LED
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LOW);
#endif

  capture.begin(RF_DATA_PIN);
  Serial.print("RMT capture on GPIO"); Serial.print(RF_DATA_PIN);
  Serial.println(" -> FrameAssembler (psNibbleIndex + repeat detection)");
}

void loop() {
  CaptureEvent ev = capture.next();   // bounded-blocking read of the RMT ring buffer
  assembler.onEvent(ev, micros());    // generic core works in microseconds throughout

#ifdef ONBOARD_LED
  if (ledOffAt != 0 && (int32_t)(millis() - ledOffAt) >= 0) {
    digitalWrite(ONBOARD_LED, LOW);
    ledOffAt = 0;
  }
#endif
}

// ==================================================================== RP2040
#elif defined(ARDUINO_ARCH_RP2040)

#include "pico/util/queue.h"
#include "src/capture/pio/pio_capture.h"

#define QUEUE_DEPTH 4

static queue_t       queue;
static volatile bool queueReady = false;
static PioCapture    capture;  // defaults to pio0

// Sink: hand a forward-worthy telegram to core 0 via the queue. Non-blocking.
static void queueSink(const RawTelegram& t, void* ctx) {
  queue_t* q = static_cast<queue_t*>(ctx);
  queue_try_add(q, &t);
}
static FrameAssembler assembler(cfg, FRAME_GAP_US, queueSink, &queue);

// ---- Core 0 ----
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== PulseTape (main sketch) ===");
  Serial.println("build " __DATE__ " " __TIME__ " | board " BOARD_NAME);
  queue_init(&queue, sizeof(RawTelegram), QUEUE_DEPTH);
  queueReady = true;
}

void loop() {
  RawTelegram t;
  queue_remove_blocking(&queue, &t);
  debugPrintTelegram(t);
}

// ---- Core 1 ----
void setup1() {
  while (!queueReady) tight_loop_contents();  // wait for the queue
  capture.begin(RF_DATA_PIN);
}

void loop1() {
  CaptureEvent ev = capture.next();   // blocks on the PIO FIFO
  assembler.onEvent(ev, micros());    // generic core works in microseconds throughout
}

#else
#error "Unsupported architecture: expected ARDUINO_ARCH_ESP32 or ARDUINO_ARCH_RP2040."
#endif
