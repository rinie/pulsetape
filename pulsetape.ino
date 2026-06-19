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
#include "src/app/oled_display.h"
#if USE_SX1278_FRONTEND
#include "src/radio/sx1278_ook.h"
#endif

static RmtCapture g_capture;

// RMT + its ISR do capture in hardware, so a single task is enough: the sink
// prints to Serial and refreshes the OLED.
static void print_sink(const RawTelegram& t, void*) {
  debug_print_telegram(t);
  oled_show_telegram(t);
}
static FrameAssembler g_assembler(g_cfg, FRAME_GAP_US, print_sink, nullptr);

void setup() {
  Serial.begin(115200);
  delay(500);  // let the host open the port before first print
#if USE_SX1278_FRONTEND
  bool sx_ok = sx1278_ook_begin(SX1276_SCK, SX1276_MISO, SX1276_MOSI, SX1276_NSS, SX1276_RST,
                                 RF_FREQUENCY_HZ);
  Serial.print("SX1278 init: ");
  Serial.println(sx_ok ? "OK" : "FAILED (check SPI wiring)");
#else
  bool sx_ok = false;
#endif
  oled_begin(sx_ok);
  setup_gpio_scan();
  g_capture.begin(RF_DATA_PIN);
  Serial.println("RMT capture started, waiting for RF...");
}

// GPIO scan: sample DIO candidate pins and print which ones are ever HIGH.
// This bypasses RMT entirely to check if DIO2 is physically on GPIO32.
// Candidate pins from V1.x schematics: DIO0=26, DIO1=33, DIO2=32, DIO3=35.
static const uint8_t kDioPins[] = {26, 32, 33, 34, 35, 39};
static uint32_t g_pin_high[6] = {};
static uint32_t g_pulse_count = 0;
static uint32_t g_last_report_ms = 0;

void setup_gpio_scan() {
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(kDioPins[i], INPUT);
  }
}

void loop() {
  // Sample all candidate DIO pins.
  for (uint8_t i = 0; i < 6; i++) {
    if (digitalRead(kDioPins[i])) g_pin_high[i]++;
  }

  CaptureEvent ev = g_capture.next();
  if (ev.type == CaptureEvent::DURATION) g_pulse_count++;
  g_assembler.onEvent(ev, millis());

  uint32_t now = millis();
  if (now - g_last_report_ms >= 5000) {
    Serial.print("rmt_pulses="); Serial.print(g_pulse_count);
    Serial.print(" gpio:");
    for (uint8_t i = 0; i < 6; i++) {
      Serial.print(kDioPins[i]); Serial.print('='); Serial.print(g_pin_high[i]);
      if (i < 5) Serial.print(',');
    }
    Serial.println();
    g_pulse_count = 0;
    memset(g_pin_high, 0, sizeof(g_pin_high));
    g_last_report_ms = now;
  }
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
