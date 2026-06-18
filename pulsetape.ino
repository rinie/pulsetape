// pulsetape.ino
// Thin entry point. All real logic lives under src/ in three layers:
//   src/pulsetape/  generic pulse-tape core (no hardware deps)
//   src/capture/    capture backend(s) implementing the generic interface
//   src/board/      board-specific pins/clock/thresholds
//
// Per-target wiring (selected by the Arduino core's architecture macro):
//   RP2040 (SenseCAP): PIO capture on core 1 -> queue -> debug print on core 0
//   ESP32  (LilyGO T3): RMT capture -> assemble -> debug print, single loop()

#include <Arduino.h>

#include "src/board/board.h"
#include "src/pulsetape/telegram.h"
#include "src/pulsetape/capture_iface.h"
#include "src/app/debug_sink.h"

// Filled from the board layer; the one place board constants meet the generic
// config struct. Shared by both targets.
static TelegramConfig g_cfg = {
    /* pulse_min_us     */ PULSE_MIN_US,
    /* pulse_max_us     */ PULSE_MAX_US,
    /* min_pulses       */ MIN_PULSES,
    /* repeat_min_count */ REPEAT_MIN_COUNT,
    /* repeat_window_ms */ REPEAT_WINDOW_MS,
};

// ===================================================================== ESP32
#if defined(ARDUINO_ARCH_ESP32)

#include "src/capture/rmt/rmt_capture.h"
#if USE_SX1278_FRONTEND
#include "src/radio/sx1278_ook.h"
#endif

static RmtCapture g_capture;

// RMT + its ISR do capture in hardware, so a single task is enough: the sink
// prints directly.
static void print_sink(const RawTelegram& t, void*) { debug_print_telegram(t); }
static FrameAssembler g_assembler(g_cfg, FRAME_GAP_US, print_sink, nullptr);

void setup() {
  Serial.begin(115200);
#if USE_SX1278_FRONTEND
  // Put the onboard SX1278 into OOK continuous RX so DIO2 carries the data the
  // RMT backend captures on RF_DATA_PIN (GPIO32).
  sx1278_ook_begin(SX1276_SCK, SX1276_MISO, SX1276_MOSI, SX1276_NSS, SX1276_RST,
                   RF_FREQUENCY_HZ);
#endif
  g_capture.begin(RF_DATA_PIN);
}

void loop() {
  CaptureEvent ev = g_capture.next();   // blocks on the RMT done-queue
  g_assembler.onEvent(ev, millis());
}

// ==================================================================== RP2040
#elif defined(ARDUINO_ARCH_RP2040)

#include "pico/util/queue.h"
#include "src/capture/pio/pio_capture.h"

#define QUEUE_DEPTH 4

static queue_t       g_queue;
static volatile bool g_queue_ready = false;
static PioCapture    g_capture;  // defaults to pio0

// Sink: hand a forward-worthy telegram to core 0 via the queue. Non-blocking.
static void queue_sink(const RawTelegram& t, void* ctx) {
  queue_t* q = static_cast<queue_t*>(ctx);
  queue_try_add(q, &t);
}
static FrameAssembler g_assembler(g_cfg, FRAME_GAP_US, queue_sink, &g_queue);

// ---- Core 0 ----
void setup() {
  Serial.begin(115200);
  queue_init(&g_queue, sizeof(RawTelegram), QUEUE_DEPTH);
  g_queue_ready = true;
}

void loop() {
  RawTelegram t;
  queue_remove_blocking(&g_queue, &t);
  debug_print_telegram(t);
}

// ---- Core 1 ----
void setup1() {
  while (!g_queue_ready) tight_loop_contents();  // wait for the queue
  g_capture.begin(RF_DATA_PIN);
}

void loop1() {
  CaptureEvent ev = g_capture.next();   // blocks on the PIO FIFO
  g_assembler.onEvent(ev, millis());
}

#else
#error "Unsupported architecture: expected ARDUINO_ARCH_ESP32 or ARDUINO_ARCH_RP2040."
#endif
