# Regenerating `pulse_capture.pio.h`

`src/capture/pio/pulse_capture.pio.h` is the assembled form of
`src/capture/pio/pulse_capture.pio`. It was **hand-assembled** (no pioasm was
available at authoring time), so each instruction word is annotated with its
decode and the file must be treated as needing verification on hardware.

After editing `pulse_capture.pio`, regenerate the header with `pioasm` (ships
with the Pico SDK and the arduino-pico core):

```sh
pioasm -o c-sdk src/capture/pio/pulse_capture.pio src/capture/pio/pulse_capture.pio.h
```

Locating `pioasm`:

- **Pico SDK:** built under `$PICO_SDK_PATH/tools/pioasm` (or `pico-sdk/build`).
- **arduino-pico:** bundled under the installed core's
  `.../tools/pqt-pioasm/.../pioasm` (path varies by OS / core version).

## Notes that must stay true if you edit the program

- The driver (`pio_capture.cpp`) loads the program at **offset 0**
  (`pio_add_program_at_offset(..., 0)`) so the assembled absolute `jmp` targets
  are valid without re-running pioasm. If you change instruction order, the
  `jmp` targets in the header change too — regenerate.
- Both inner measure loops are **2 PIO cycles per count**. `CYCLES_PER_COUNT` in
  `pio_capture.cpp` must match, or the microsecond conversion will be off.
- `mov isr, ~null` produces the `0xFFFFFFFF` overflow sentinel the driver maps to
  a `FRAME_GAP` event.
