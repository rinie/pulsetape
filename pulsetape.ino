// pulsetape.ino
// Thin entry point. All real logic lives under src/ in three layers:
//   src/pulsetape/  generic pulse-tape core (no hardware deps)
//   src/capture/    capture backend(s) implementing the generic interface
//   src/board/      board-specific pins/clock/thresholds
//
// Wiring on the RP2040 (arduino-pico, dual core):
//   Core 1 (setup1/loop1): PIO capture -> frame assembly -> telegram queue
//   Core 0 (setup/loop):   drain the queue -> print debug line over USB serial

#include <Arduino.h>
#include "pico/util/queue.h"

#include "src/board/board.h"
#include "src/pulsetape/telegram.h"
#include "src/pulsetape/capture_iface.h"
#include "src/capture/pio/pio_capture.h"
#include "src/app/debug_sink.h"

#define QUEUE_DEPTH 4

static queue_t        g_queue;
static volatile bool  g_queue_ready = false;

static PioCapture     g_capture;  // defaults to pio0

// Filled from the board layer; this is the only place board constants meet the
// generic config struct.
static TelegramConfig g_cfg = {
    /* pulse_min_us     */ PULSE_MIN_US,
    /* pulse_max_us     */ PULSE_MAX_US,
    /* min_pulses       */ MIN_PULSES,
    /* repeat_min_count */ REPEAT_MIN_COUNT,
    /* repeat_window_ms */ REPEAT_WINDOW_MS,
};

// Sink: hand a forward-worthy telegram to core 0 via the queue. Non-blocking;
// drops the telegram if the queue is momentarily full.
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
