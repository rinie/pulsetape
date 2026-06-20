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

#include "src/app/oled_display.h"
#if USE_SX1278_FRONTEND
#include "src/radio/sx1278_ook.h"
#endif

// Interrupt-based OOK pulse capture — mirrors the approach used by rtl_433_ESP:
//   CHANGE interrupt on DIO2 (GPIO32), measure HIGH/LOW durations in µs.
//   HIGH period = carrier present (pulse), LOW period = carrier absent (gap).
//   Frame detection: silence > FRAME_GAP_US after at least MIN_PULSES pairs.

#define PULSE_BUF_SIZE 256

static volatile uint16_t g_pulse[PULSE_BUF_SIZE];   // HIGH durations (µs)
static volatile uint16_t g_gap[PULSE_BUF_SIZE];     // LOW durations (µs)
static volatile int16_t  g_nrpulses   = 0;
static volatile unsigned long g_lastChange = 0;

static void IRAM_ATTR onDataEdge() {
  unsigned long now = micros();
  unsigned long dur = now - g_lastChange;

  if (dur < PULSE_MIN_US) return;   // discard sub-50 µs glitches
  g_lastChange = now;

  // After the edge, digitalRead gives the new level.
  // LOW  now → a HIGH period (pulse) just ended.
  // HIGH now → a LOW period  (gap)   just ended.
  // This is the same convention as rtl_433_ESP::interruptHandler().
  bool level = (bool)digitalRead(RF_DATA_PIN);
  int16_t n = g_nrpulses;

  if (!level) {                      // pulse ended
    if (n < PULSE_BUF_SIZE)
      g_pulse[n] = (uint16_t)(dur < 65535u ? dur : 65535u);
  } else {                           // gap ended — only count if a pulse preceded it
    if (n < PULSE_BUF_SIZE && g_pulse[n] > 0) {
      g_gap[n]   = (uint16_t)(dur < 65535u ? dur : 65535u);
      g_nrpulses = n + 1;
    }
  }
}

static uint32_t g_frame_count = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

#if USE_SX1278_FRONTEND
  bool sx_ok = sx1278_ook_begin(SX1276_SCK, SX1276_MISO, SX1276_MOSI,
                                 SX1276_NSS, SX1276_RST, RF_FREQUENCY_HZ);
  Serial.print("SX1278 init: ");
  Serial.println(sx_ok ? "OK" : "FAILED (check SPI wiring)");
#else
  bool sx_ok = false;
#endif

  oled_begin(sx_ok);

  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LOW);

  g_lastChange = micros();
  pinMode(RF_DATA_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RF_DATA_PIN), onDataEdge, CHANGE);

  Serial.print("Listening on GPIO"); Serial.print(RF_DATA_PIN);
  Serial.println(" (DIO2 / SX1278 OOK bitstream)");
}

void loop() {
  int16_t n = g_nrpulses;                     // one volatile read — safe snapshot
  unsigned long last = g_lastChange;

  if (n >= MIN_PULSES && (micros() - last) > FRAME_GAP_US) {
    // Frame complete: snapshot under critical section, then reset
    static uint16_t snapPulse[PULSE_BUF_SIZE];
    static uint16_t snapGap[PULSE_BUF_SIZE];
    noInterrupts();
    int16_t snapN = g_nrpulses;
    for (int16_t i = 0; i < snapN; i++) {
      snapPulse[i] = g_pulse[i];
      snapGap[i]   = g_gap[i];
    }
    g_nrpulses = 0;
    interrupts();

    g_frame_count++;
    digitalWrite(ONBOARD_LED, HIGH);

#if USE_SX1278_FRONTEND
    int8_t rssi = -(int8_t)sx1278_rssi();
#else
    int8_t rssi = 0;
#endif

    oled_show_frame((uint16_t)snapN, g_frame_count, rssi);

    Serial.print("FRAME #"); Serial.print(g_frame_count);
    Serial.print(" pulses="); Serial.print(snapN);
    Serial.print(" rssi="); Serial.print(rssi); Serial.println("dBm");
    for (int16_t i = 0; i < snapN; i++) {
      Serial.print(snapPulse[i]);
      Serial.print(',');
      Serial.print(snapGap[i]);
      if (i + 1 < snapN) Serial.print(' ');
    }
    Serial.println();

    delay(30);
    digitalWrite(ONBOARD_LED, LOW);
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
