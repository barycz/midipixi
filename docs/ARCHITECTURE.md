# midipixi architecture

midipixi turns real-time MIDI into LED-strip light shows. The workflow mirrors
sound design on a synthesizer: instead of oscillators you have **emitters**
(shapes of light on the strip), shaped by **envelopes**, **LFOs**, **MIDI CCs**
and **macros** through a **modulation matrix**, stacked in **layers** that are
blended together like layered synth patches.

## Components

```
                 ┌──────────────── midipixi core (C++17, no OS deps) ─────────────────┐
                 │ preset model · JSON load/save · voices · ADSR/LFO · mod matrix ·    │
                 │ emitter renderer · layer blending · output stage · gear protocol    │
                 └───────────────▲───────────────────────────────▲─────────────────────┘
                                 │                               │
   DAW ──MIDI──▶ midipixi VST (JUCE)                  midipixi gear (Pico 2 + pico-sdk)
                 LED-bar simulator, editor,  ──CDC──▶  USB MIDI in → core → PIO/DMA → WS2812
                 preset files, upload        serial    presets in flash, Program Change
   DAW ──MIDI (+clock)────────────────────────────────▶ (same MIDI, routed by the DAW)
```

| Folder | What | Builds with |
|---|---|---|
| `core/` | The engine, preset format and gear protocol. Pure C++17: no exceptions, no RTTI, no heap use in the render path, no OS calls. | MSVC / GCC / arm-none-eabi-gcc |
| `host/` | Desktop-only helpers: serial port, gear discovery (USB VID/PID), gear client. | MSVC (Windows first) |
| `tools/mpx-cli/` | Command line tool: validate presets, render a MIDI file to a "waterfall" PNG, upload presets to the gear. | desktop |
| `gear/` | Firmware for Raspberry Pi Pico 2 (RP2350). Pico 1 (RP2040) builds too. | pico-sdk 2.x |
| `vst/` | JUCE 8 plugin (VST3) + standalone app: simulator and preset editor. | JUCE 8, CMake |
| `presets/` | Factory presets (JSON). | — |

**On stage the gear runs the engine itself.** The DAW routes the MIDI track
(and MIDI clock) straight to the gear's USB MIDI port. The VST runs the very
same core code on the same MIDI, so the simulator matches the hardware. The gear
keeps working if the plugin window closes or the DAW's audio stutters.

## Key decisions

| Topic | Decision | Why |
|---|---|---|
| Plugin framework | JUCE 8 (AGPLv3) | VST3 + standalone from one codebase; project is open source under AGPLv3. |
| First target | Windows, VST3 | Other OSes and formats (AU, CLAP) later. |
| Plugin type | Instrument with a silent stereo output | Several DAWs (e.g. Ableton) do not host VST MIDI effects. |
| MCU | RP2350 (Pico 2), float math | Hardware FPU, so core uses plain `float` everywhere. RP2040 (Pico 1) build uses the SDK's ROM float routines; to be measured. |
| LED output | WS2812 via PIO + DMA | Zero CPU time for the bitstream. |
| Gear USB | Composite USB MIDI + CDC serial (TinyUSB) | Windows MIDI ports are often exclusive to one app (the DAW); a separate serial channel lets the editor talk to the gear while the DAW owns the MIDI port. |
| Preset format | JSON, same bytes on PC, over the wire and in flash | Human readable, diffable, versioned (`"format": "midipixi/1"`). |
| JSON on MCU | Own small non-allocating parser/writer in core | Parsed only at load time into a fixed-size struct; the render loop never touches JSON. Number parsing/formatting is locale independent (a DAW may set a locale with decimal commas). |
| Gear protocol | Newline-delimited JSON over CDC serial | Debuggable with any serial terminal; USB already guarantees integrity. See [PROTOCOL.md](PROTOCOL.md). |
| Host tools in the build | None besides compiler + Python | The PIO program is assembled at runtime with SDK encoders and the UF2 is produced by `scripts/bin2uf2.py`, so locked-down Windows machines (Smart App Control) can build the firmware. |
| Strip-independent presets | Positions and widths normalized to 0..1 | A preset looks the same on 60 or 600 LEDs. LED count, color order, gamma, power budget live in the separate gear config. |
| 2D panels | Not in v1 | 1D strips only. |
| USB VID/PID | `1209:0001` (pid.codes test PID) for development | Must be replaced before public release: request a free PID from Raspberry Pi (github.com/raspberrypi/usb-pid) or pid.codes. Defined in one place: `core/include/midipixi/usb_ids.h`. |

## Effect model (the synth analogy)

| Synth | midipixi |
|---|---|
| Oscillator | **Emitter**: a light shape with position, width, softness, hue, saturation, brightness, hue spread |
| Voice | One emitter instance, allocated on note-on, with its own envelopes and LFO phases |
| Patch layer | **Layer**: trigger mode, key zone, emitter, 2 ADSRs, 3 LFOs, mod matrix, blend mode, opacity |
| Amp envelope | **Env 1** decides voice lifetime (voice ends when Env 1 finishes its release) |
| Macro knobs | 8 **macros**, each driven by a MIDI CC (default CC 20–27) so DAW automation reaches both VST and gear |

### Trigger modes

- `poly` — one voice per note (up to `voices`, oldest released voice is stolen first).
- `mono` — one voice, last-note priority, legato note changes.
- `drone` — one voice that is always on; notes are ignored (ambience layers).

Each layer has a MIDI channel filter (0 = omni) and a key zone (`noteRange`).
Notes outside the zone are ignored by that layer, so one preset can split the
keyboard: kick on C1 strobes, melody above draws notes.

### Modulation

Every matrix row is `{source, via?, dest, amount}`. A row adds
`source × via × amount` to its destination. Final value = `base + Σ rows`,
then clamped (hue wraps; position is unclamped so beams can leave the strip,
or wraps if the layer has `wrap` on).

Modulation is evaluated **per voice, per frame**. Only emitter rasterization
runs per pixel. Modulator parameters (LFO rate, envelope times) can be
modulation targets; they use the previous frame's value (one-frame latency,
standard practice).

Sources:

| Source | Range | Scope |
|---|---|---|
| `note` | 0..1 across the layer's note range | voice |
| `velocity` | 0..1 | voice |
| `aftertouch` | 0..1 (poly pressure) | voice |
| `gate` | 1 while key held, else 0 | voice |
| `random` | 0..1, fixed per note | voice |
| `env1`, `env2` | 0..1 | voice |
| `lfo1`..`lfo3` | -1..1 (bipolar) or 0..1 | voice (free-running LFOs share phase across voices) |
| `macro1`..`macro8` | 0..1 | global |
| `cc0`..`cc127` | 0..1, smoothed | global |
| `pitchbend` | -1..1, smoothed | global |
| `pressure` | 0..1 channel pressure, smoothed | global |
| `beat` | 0..1 ramp each beat (host transport or MIDI clock) | global |

Destinations: `position`, `width`, `softness`, `hue`, `saturation`,
`brightness`, `huespread`, `opacity` (layer level), `lfoN.rate` (amount in
units of 4 octaves), `envN.attack|decay|release` (amount in units of 4
octaves), `envN.sustain` (additive).

Layer-level destinations (`opacity`) are evaluated with the voice sources of
the most recently triggered active voice.

### Emitter shape

`width` is the fraction of the strip that is lit (0 = a single LED,
1 = the whole strip when centred). `softness` turns hard edges into smooth
fades. Edges are anti-aliased, so slowly moving beams glide between LEDs
instead of jumping.

### Layer blending

Layers are rendered into their own buffer, then composited bottom to top onto
the output with the layer's blend mode and opacity:
`add` (default), `screen`, `multiply`, `subtract`, `lighten`, `darken`,
`difference`, `normal`, `hue` (colorize the layers below with this layer's hue),
`hueshift` (rotate the hue of the layers below by this layer's hue),
`mask` (this layer's brightness masks the layers below).

### Output stage

Gamma curve (default 2.2), master brightness, power limiter (estimates current
from pixel values and scales the frame down to a mA budget), temporal
dithering (smooth slow fades at low brightness), color order, reverse.

A WS2812 draws ~60 mA at full white: 300 LEDs ≈ 18 A. **Set the power limit to
what your supply can deliver.**

### Timing

- **VST:** the engine runs on the audio thread and advances by sample count, so
  event timing is exact and tempo-synced LFOs follow the host transport. It
  renders a frame every 1/60 s of audio time and hands it to the UI through a
  lock-protected double buffer (try-lock on the audio thread, never blocks).
- **Gear:** core 0 runs USB, MIDI and the protocol; core 1 runs the engine at a
  fixed frame rate. MIDI events are timestamped on arrival and applied at their
  time. Tempo-synced LFOs follow incoming **MIDI clock** — send clock from the
  DAW to the gear.

## Roadmap

1. Skeleton: CMake, presets, CI.
2. Core engine + `mpx-cli` waterfall renderer + tests.
3. Gear firmware: composite USB, PIO/DMA output, flash presets, Program Change, serial protocol.
4. VST v1: simulator, editor panels, preset files, upload to gear.
5. VST v2: drag & drop modulation, matrix view, modulation rings, live edit to gear.
6. Polish: multiple outputs, more emitter types, standalone polish, Pico 1 measurements, other OSes.
