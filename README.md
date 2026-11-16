# midipixi

**A MIDI-driven LED strip visual effects engine — design light like you design sound.**

midipixi lets musicians and lighting artists drive LED-strip installations with
real-time MIDI from their DAW. It's built for live performers (live loopers,
electronic sets) playing smaller venues lit by LED strips.

If you can build a synth patch, you can build a midipixi preset: emitters
instead of oscillators, ADSRs and LFOs, MIDI CCs and macros wired through a
modulation matrix, layers blended like stacked sounds.

| Component | What it is |
|---|---|
| **midipixi gear** | A Raspberry Pi Pico 2 that plugs into USB, shows up as a MIDI device and drives WS2812 (NeoPixel) strips. No UI; presets are uploaded over USB serial and selected with MIDI Program Change. |
| **midipixi VST** | A VST3 "instrument" that shows an LED-strip simulator instead of making sound. It's the preset editor for the gear, and also runs as a standalone app. |
| **midipixi core** | The shared C++ engine both of them run, so the simulator matches the hardware. |

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design,
[docs/PRESET_FORMAT.md](docs/PRESET_FORMAT.md) for the preset JSON and
[docs/PROTOCOL.md](docs/PROTOCOL.md) for the gear's USB serial protocol.

## Building (Windows)

Requirements: Visual Studio 2022 with C++ tools, CMake ≥ 3.22, Ninja, git.
For the gear firmware: [Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(arm-none-eabi) and [pico-sdk 2.x](https://github.com/raspberrypi/pico-sdk)
with the `lib/tinyusb` submodule. Put them in `.tools/arm-gnu-toolchain` and
`.tools/pico-sdk`, or set `PICO_SDK_PATH` and put the toolchain on `PATH`.

`scripts\dev.cmd` runs any command inside a Visual Studio developer environment:

```bat
scripts\dev.cmd cmake --preset desktop
scripts\dev.cmd cmake --build --preset desktop
scripts\dev.cmd ctest --preset desktop
```

| Preset | Builds |
|---|---|
| `desktop` | core, tests, `mpx-cli`, VST3 + standalone (downloads JUCE on first configure) |
| `core-only` | core, tests, `mpx-cli` |
| `pico2` | gear firmware for Pico 2 → `build/pico2/gear/midipixi_gear.uf2` |
| `pico` | gear firmware for Pico 1 (RP2040) |

## License

AGPL-3.0-or-later. See [LICENSE](LICENSE).
