# RP2040 PIO RF Telegram Layer

Pure pulse-tape capture laag voor 433/868 MHz OOK/ASK receivers (Aurel, SRX882S e.a.).  
Geen protocol kennis — alleen ruwe telegrammen + repeat-detectie + signaal/ruis filter.  
Decoder logica (rtl_433 stijl, pilight, Manchester/Oregon) komt daarboven.

---

## Architectuur

```
RF DATA pin (Aurel/SRX882S)
        │
        ▼
┌───────────────────┐
│  PIO State Machine │  ← deterministisch, CPU-onafhankelijk
│  pulse_capture.pio │
│                   │
│  Meet HIGH/LOW    │
│  duur in ticks    │
│  DMA → ringbuffer │
└────────┬──────────┘
         │  IRQ bij frame-gap (>MINGAP)
         ▼
┌───────────────────┐
│  Core 1 handler   │  ← dedicated core, geen WiFi
│  telegram_layer.c │
│                   │
│  1. Sanity filter │  min/max pulsen, min telegramlengte
│  2. Normalisatie  │  ticks → µs (schaal factor)
│  3. Repeat detect │  hash + timing window
│  4. Push telegram │  naar Core 0 via queue
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│  Core 0           │  ← ESP32-S3 via UART, of eigen decoder
│  Protocol decode  │
│  (rtl_433/pilight)│
└───────────────────┘
```

---

## PIO State Machine: `pulse_capture.pio`

```pio
; pulse_capture.pio
; Meet HIGH en LOW pulsduur in systeemklok ticks.
; Output: abwisselend [high_ticks, low_ticks] naar TX FIFO.
; Bij stilte > SILENCE drempel: push SENTINEL waarde 0xFFFFFFFF.
;
; Clock divisor instellen vanuit C code:
;   Bij 125MHz systeemklok, div=125 → 1 tick = 1µs
;   Bij 125MHz, div=25  → 1 tick = 0.2µs (nauwkeuriger voor korte pulsen)
;
; SenseCAP RP2040: gebruik 125MHz / 25 = 0.2µs resolutie
; Oregon Scientific kortste puls: ~488µs → 2440 ticks @ 0.2µs

.program pulse_capture
.wrap_target

; --- Wacht op stijgende flank (begin HIGH puls) ---
wait_rise:
    wait 0 pin 0        ; wacht tot pin LAAG is (rust toestand)
    wait 1 pin 0        ; wacht op stijgende flank → start HIGH meting

; --- Meet HIGH duur ---
measure_high:
    mov x, ~null        ; x = 0xFFFFFFFF (teller aftellen)
high_loop:
    jmp pin high_still  ; als pin nog HIGH: door tellen
    jmp high_done       ; pin LAAG: HIGH puls klaar
high_still:
    jmp x-- high_loop   ; decrement teller
    ; overflow: puls te lang → silence/gap sentinel
    mov isr, ~null      ; push 0xFFFFFFFF als sentinel
    push noblock
    jmp wait_rise

high_done:
    mov isr, ~x         ; isr = gemeten HIGH ticks (geïnverteerd terug)
    push noblock        ; stuur HIGH duur naar FIFO

; --- Meet LOW duur ---
measure_low:
    mov x, ~null
low_loop:
    jmp pin low_done    ; pin HOOG: LOW klaar
    jmp x-- low_cont    ; nog steeds laag: tel door
    ; overflow = lang silence = einde telegram
    mov isr, ~null      ; sentinel
    push noblock
    jmp wait_rise
low_cont:
    jmp low_loop
low_done:
    mov isr, ~x
    push noblock        ; stuur LOW duur naar FIFO

.wrap                   ; terug naar wait_rise
```

---

## C layer: `telegram_layer.c`

### Constanten (aanpassen per setup)

```c
// telegram_layer.h

#define PIO_CLOCK_DIV       25          // 125MHz/25 = 0.2µs per tick
#define TICKS_PER_US        5           // bij div=25: 5 ticks = 1µs

// Pulsfilter grenzen (in µs)
#define PULSE_MIN_US        50          // korter = ruis (< 50µs)
#define PULSE_MAX_US        32000       // langer = silence/gap

// Frame gap: stilte die een telegram afsluit
#define FRAME_GAP_US        8000        // 8ms gap = einde telegram
                                        // Oregon: ~8ms inter-frame
                                        // KAKU: ~10ms
                                        // Gebruik laagste gemeenschappelijke deler

// Telegram kwaliteit
#define MIN_PULSES          8           // minder = ruis, geen telegram
#define MAX_PULSES          512         // ringbuffer grootte

// Repeat detectie
#define REPEAT_WINDOW_MS    800         // telegrammen binnen 800ms zijn kandidaat-repeats
#define REPEAT_MIN_COUNT    2           // minimaal 2x zelfde telegram = geldig

// Sentinel waarde van PIO bij overflow/silence
#define PIO_SENTINEL        0xFFFFFFFF
```

### Data structuren

```c
// Ruwe puls in µs (HIGH dan LOW afwisselend, index 0 = eerste HIGH)
typedef struct {
    uint16_t pulses[MAX_PULSES];  // µs waarden
    uint16_t count;               // aantal pulsen (HIGH+LOW paren * 2)
    uint32_t timestamp_ms;        // millis() bij ontvangst
    int8_t   rssi;                // optioneel: -dBm van CC1101/SX127x
                                  // -1 als niet beschikbaar (Aurel/SRX882S)
    uint8_t  repeat_count;        // hoeveel keer dit telegram ontvangen
    uint32_t hash;                // snelle repeat-hash
} RawTelegram;

// Interne capture buffer (Core 1)
static uint32_t  cap_buf[MAX_PULSES];   // PIO FIFO output (ticks)
static uint16_t  cap_idx = 0;
static bool      in_frame = false;

// Repeat ring (laatste N telegrammen)
#define REPEAT_RING_SIZE  8
static RawTelegram repeat_ring[REPEAT_RING_SIZE];
static uint8_t     repeat_ring_head = 0;

// Core 0 ↔ Core 1 queue
#define QUEUE_DEPTH  4
static queue_t telegram_queue;  // pico SDK queue_t
```

### Hash functie voor repeat detectie

```c
// Tolerante hash: quantiseert pulsen naar 50µs buckets
// zodat lichte timing-variatie tussen herhalingen niet mismatcht.
// Oregon Scientific: repeats kunnen ~5% timing-variatie hebben.
static uint32_t telegram_hash(const RawTelegram *t) {
    uint32_t h = 0x811c9dc5;  // FNV-1a basis
    for (uint16_t i = 0; i < t->count; i++) {
        // quantiseer naar 50µs buckets (ruis-tolerant)
        uint8_t bucket = (uint8_t)(t->pulses[i] / 50);
        h ^= bucket;
        h *= 0x01000193;
    }
    return h;
}
```

### Signaal/ruis filter

```c
// Geeft true als telegram de kwaliteitsdrempel haalt.
// Controleert:
//   1. Minimaal aantal pulsen
//   2. Geen pulsen buiten geldig bereik (fout in ontvanger of 50Hz brom)
//   3. Minimale "variatie" (aaneengesloten gelijke pulsen = draaggolf lek)
static bool telegram_valid(const RawTelegram *t) {
    if (t->count < MIN_PULSES) return false;

    uint16_t out_of_range = 0;
    uint16_t same_as_prev = 0;
    uint16_t prev = 0;

    for (uint16_t i = 0; i < t->count; i++) {
        uint16_t p = t->pulses[i];

        if (p < PULSE_MIN_US || p > PULSE_MAX_US) {
            out_of_range++;
        }

        // Te veel aaneengesloten identieke pulsen = draaggolf doorslag
        if (p == prev) {
            same_as_prev++;
        } else {
            same_as_prev = 0;
        }
        if (same_as_prev > 6) return false;  // >6 identiek = geen OOK data

        prev = p;
    }

    // Maximaal 10% buiten bereik tolereren (slechte ontvangst)
    if (out_of_range > t->count / 10) return false;

    return true;
}
```

### Repeat detectie

```c
// Zoek het telegram in de repeat ring.
// Geeft pointer naar bestaande entry als gevonden, anders NULL.
// "Zelfde" = zelfde hash EN count EN binnen REPEAT_WINDOW_MS.
static RawTelegram *find_repeat(const RawTelegram *t) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (uint8_t i = 0; i < REPEAT_RING_SIZE; i++) {
        RawTelegram *r = &repeat_ring[i];
        if (r->count == 0) continue;
        if (r->hash != t->hash) continue;
        if (r->count != t->count) continue;
        if ((now - r->timestamp_ms) > REPEAT_WINDOW_MS) continue;
        return r;
    }
    return NULL;
}

// Voeg telegram toe aan repeat ring of verhoog teller.
// Geeft true als telegram voldoende herhaald is voor doorsturen.
static bool update_repeat_ring(RawTelegram *t) {
    RawTelegram *existing = find_repeat(t);

    if (existing != NULL) {
        existing->repeat_count++;
        existing->timestamp_ms = to_ms_since_boot(get_absolute_time());
        // Kopieer naar t zodat caller de repeat_count ziet
        t->repeat_count = existing->repeat_count;
        return (existing->repeat_count >= REPEAT_MIN_COUNT);
    }

    // Nieuw telegram: opslaan in ring
    RawTelegram *slot = &repeat_ring[repeat_ring_head % REPEAT_RING_SIZE];
    repeat_ring_head++;
    *slot = *t;
    slot->repeat_count = 1;
    t->repeat_count = 1;

    // Eenmalig telegram ook doorsturen? Keuze:
    //   false = wacht op bevestiging (minder ruis, trager)
    //   true  = ook singles doorsturen (sneller, meer ruis)
    // Hier: alleen bij REPEAT_MIN_COUNT <= 1 singles doorsturen
    return (REPEAT_MIN_COUNT <= 1);
}
```

### Core 1: PIO uitlezen + frame assembler

```c
// Draait op Core 1. Leest PIO FIFO, assembleert frames,
// filtert, detecteert repeats, pusht naar queue voor Core 0.
void core1_rf_capture() {
    // PIO init (zie pio_init hieronder)
    PIO pio = pio0;
    uint sm = 0;
    rf_capture_pio_init(pio, sm, RF_DATA_PIN);

    RawTelegram current = {0};
    uint32_t last_tick_us = 0;

    while (true) {
        // Blokkeer tot PIO data beschikbaar is
        uint32_t raw = pio_sm_get_blocking(pio, sm);

        if (raw == PIO_SENTINEL) {
            // Frame gap gedetecteerd door PIO overflow
            if (current.count >= MIN_PULSES) {
                current.hash = telegram_hash(&current);
                current.timestamp_ms = to_ms_since_boot(get_absolute_time());
                current.rssi = -1;  // Aurel: geen RSSI

                if (telegram_valid(&current)) {
                    if (update_repeat_ring(&current)) {
                        // Kopieer naar queue (blocking zou Core 1 niet moeten blokkeren)
                        queue_try_add(&telegram_queue, &current);
                    }
                }
            }
            // Reset voor nieuw frame
            memset(&current, 0, sizeof(current));
            continue;
        }

        // Converteer ticks → µs
        uint16_t us = (uint16_t)(raw / TICKS_PER_US);

        // Clamp frame-gap in µs range: PIO sentinel vangt overflow,
        // maar lange LOW puls > FRAME_GAP_US is ook einde frame
        if (us > FRAME_GAP_US && (current.count % 2 == 1)) {
            // LOW puls te lang = frame gap (PIO overflow misschien net gemist)
            // Sluit frame af zoals sentinel
            if (current.count >= MIN_PULSES) {
                current.hash = telegram_hash(&current);
                current.timestamp_ms = to_ms_since_boot(get_absolute_time());
                current.rssi = -1;

                if (telegram_valid(&current)) {
                    if (update_repeat_ring(&current)) {
                        queue_try_add(&telegram_queue, &current);
                    }
                }
            }
            memset(&current, 0, sizeof(current));
            continue;
        }

        // Puls toevoegen aan huidig frame
        if (current.count < MAX_PULSES) {
            current.pulses[current.count++] = us;
        } else {
            // Buffer vol: telegram te lang, waarschijnlijk ruis
            memset(&current, 0, sizeof(current));
        }
    }
}
```

### PIO initialisatie

```c
void rf_capture_pio_init(PIO pio, uint sm, uint pin) {
    uint offset = pio_add_program(pio, &pulse_capture_program);

    pio_sm_config c = pulse_capture_program_get_default_config(offset);

    // Pin als input
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);
    pio_gpio_init(pio, pin);

    // Clock divisor: 125MHz / 25 = 5MHz → 0.2µs per tick
    sm_config_set_clkdiv(&c, PIO_CLOCK_DIV);

    // FIFO: RX only, 32 bit
    sm_config_set_in_shift(&c, false, true, 32);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
```

### Core 0: telegram consumer

```c
// Core 0: ontvang telegrammen van Core 1 en geef door aan decoder.
// Hier koppel je rtl_433 decoders, pilight, of eigen Manchester decoder aan.
void process_telegrams() {
    queue_init(&telegram_queue, sizeof(RawTelegram), QUEUE_DEPTH);
    multicore_launch_core1(core1_rf_capture);

    RawTelegram t;
    while (true) {
        queue_remove_blocking(&telegram_queue, &t);

        // Debug output (RFLink QRFDEBUG stijl, hex µs waarden)
        // Format: count;p1,p2,p3,...  (µs, decimaal)
        printf("RF;count=%d;repeats=%d;hash=%08lX;pulses=",
               t.count, t.repeat_count, t.hash);
        for (uint16_t i = 0; i < t.count; i++) {
            printf("%d", t.pulses[i]);
            if (i < t.count - 1) printf(",");
        }
        printf("\n");

        // TODO: koppel hier decoder aan:
        // decode_manchester_oregon(&t);
        // decode_rtl433_style(&t);
        // decode_kaku(&t);
    }
}
```

---

## Manchester / Oregon Scientific aanknoping

Oregon Scientific gebruikt **OOK_PULSE_MANCHESTER_ZEROBIT**:
- Symbolenlengte ~1220µs (2 × 610µs halfgolven)
- Bit-1: HIGH-LOW, Bit-0: LOW-HIGH
- Preamble: aaneengesloten 1-bits (~16 halfgolven)
- Sync nibble: `0xA` (Manchester: `10011001`)

De PIO laag levert ruwe µs pulsen. De Manchester decoder werkt dan zo:

```c
// Minimale Manchester decoder bovenop RawTelegram
// Past bij Oregon v2/v3 en vergelijkbare sensoren.
bool decode_manchester(const RawTelegram *t, uint8_t *bits_out, uint16_t *bits_len) {
    // Oregon half-symbool nominaal 488µs (v2) of 610µs (v3)
    // Tolerantie: ±30%
    const uint16_t HALF_NOM = 488;   // µs
    const uint16_t HALF_MIN = 300;
    const uint16_t HALF_MAX = 650;
    const uint16_t FULL_MIN = 600;
    const uint16_t FULL_MAX = 1400;

    uint16_t bit_count = 0;
    uint16_t i = 0;

    // Sla preamble over: zoek sync (lang LOW na reeks korte)
    while (i < t->count) {
        if (t->pulses[i] >= FULL_MIN && t->pulses[i] <= FULL_MAX) break;
        i++;
    }

    // Decodeer Manchester bits
    while (i < t->count && bit_count < 256) {
        uint16_t p = t->pulses[i];

        if (p >= HALF_MIN && p <= HALF_MAX) {
            // Halve puls: kijk naar volgende
            if (i + 1 < t->count && t->pulses[i+1] >= HALF_MIN && t->pulses[i+1] <= HALF_MAX) {
                // Twee halven = één bit
                // Eerste helft HIGH = 1, LOW = 0
                bits_out[bit_count++] = (i % 2 == 0) ? 1 : 0;
                i += 2;
            } else {
                i++;  // enkelvoudige halve puls, skip
            }
        } else if (p >= FULL_MIN && p <= FULL_MAX) {
            // Volle puls = bit zonder overgang (zelfde richting)
            bits_out[bit_count++] = (i % 2 == 0) ? 0 : 1;
            i++;
        } else {
            break;  // buiten bereik: einde decodeerbaar gedeelte
        }
    }

    *bits_len = bit_count;
    return (bit_count >= 32);  // minimaal 4 bytes voor zinvol Oregon telegram
}
```

---

## Vergelijking met RFLink/Arduino aanpak

| Aspect | RFLink (Arduino Mega) | Deze PIO laag (RP2040) |
|---|---|---|
| Pulse capture | `attachInterrupt` + `micros()` | PIO state machine, CPU-onafhankelijk |
| Jitter | ~1-4µs door millis() ISR | <0.2µs (deterministisch) |
| Frame gap detectie | Loop telt LOW duur | PIO overflow → sentinel |
| Repeat detectie | `RawSignal.Repeats` vlag per plugin | Generiek hash-gebaseerd, vóór decoder |
| Manchester | Per-plugin, gemengd met decode | Aparte decoder bovenop ruwe tape |
| Protocol kennis | In firmware ingebakken | Geen — pure tape laag |
| RSSI | Nee (Aurel heeft geen RSSI) | Veld aanwezig, -1 als niet beschikbaar |
| Gelijktijdig meerdere SM | Nee | Ja: 2e PIO SM voor 868MHz parallel |

---

## Notities voor SenseCAP Indicator

- **RP2040** doet de PIO capture (Core 1) + repeat/filter (Core 0)
- **ESP32-S3** ontvangt telegrammen via de ingebouwde UART bridge
- Telegramformaat over UART: `RF;count=N;repeats=R;hash=H;pulses=p1,p2,...\n`
- Aurel RTX-MID-5V DATA pin → RP2040 GPIO (via 5V→3.3V spanningsdeler!)
- Voor TX: ESP32-S3 stuurt pulsenreeks terug over UART → RP2040 → Aurel ENABLE+DATA
