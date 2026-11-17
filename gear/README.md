# midipixi gear

Firmware for a Raspberry Pi Pico 2 (RP2350). Pico 1 (RP2040) also builds; see
"Pico 1" below.

## Wiring

```
Pico 2                    WS2812 strip
GP2  (pin 4) ──[330 Ω]──▶ DIN
GND  (pin 3) ───────────▶ GND ◀── PSU −
                           5V ◀── PSU + (5 V, sized for the strip)
USB ─────────▶ computer
```

- **Power the strip from its own 5 V supply**, not from the Pico's USB.
  Connect the grounds. A 1000 µF capacitor across the strip's power input is
  recommended.
- The Pico outputs 3.3 V data. Many WS2812 strips accept it; if yours
  flickers, add a level shifter (e.g. 74AHCT125) powered from 5 V.
- Set a power limit that matches your supply:
  `mpx-cli config powerLimitMa=3000`.

The data pin can be changed with `mpx-cli config dataPin=N`, followed by a
power cycle.

## Building and flashing

```bat
scripts\dev.cmd cmake --preset pico2
scripts\dev.cmd cmake --build --preset pico2
```

Hold BOOTSEL while plugging the Pico in (or run `mpx-cli bootsel` on a gear
that's already running), then copy `build/pico2/gear/midipixi_gear.uf2` to the
`RP2350` drive.

The build needs no host tools besides the compiler and Python. The PIO
program is assembled at runtime, and the UF2 is made by `scripts/bin2uf2.py`.

## First steps

```bat
mpx-cli info                         :: device, LED config, render statistics
mpx-cli config ledCount=60           :: your strip length
mpx-cli list                         :: factory presets in slots 0-6
mpx-cli upload 6 mypreset.json --select
```

In the DAW, route a MIDI track to the **midipixi gear** MIDI port. Send MIDI
clock to it for tempo-synced effects. Program Change *n* switches to slot *n*.

## Architecture

- Core 0: TinyUSB (MIDI + CDC), the configuration protocol, flash storage,
  activity LED.
- Core 1: engine + output stage at a fixed frame rate (default 60 fps).
  MIDI events are timestamped on core 0 and applied at their time on core 1.
- PIO0 + one DMA channel stream the frame to the strip. With dithering on, the
  frame is resent with new dither steps until the next frame is due (about
  250 refreshes/s at 120 LEDs), which keeps dim fades flicker-free.
- Flash writes pause core 1 briefly (`flash_safe_execute`). Program Change
  never writes flash.

## Pico 1 (RP2040)

```bat
scripts\dev.cmd cmake --preset pico
scripts\dev.cmd cmake --build --preset pico
```

The RP2040 has no FPU. The engine's `float` math goes through the SDK's
optimized ROM float routines. `mpx-cli info` prints `renderAvgUs`,
`renderMaxUs` and `frameBudgetUs`, so you can measure how much headroom a
preset leaves.
