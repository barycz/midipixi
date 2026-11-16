# Gear configuration protocol

The gear enumerates as a composite USB device:

| Interface | Purpose |
|---|---|
| USB MIDI ("midipixi gear") | Performance data from the DAW: notes, CCs, pitch bend, pressure, clock, Program Change |
| CDC serial ("midipixi config") | Configuration: presets and LED settings |

USB ID: `1209:0001` during development (pid.codes test PID, see
[ARCHITECTURE.md](ARCHITECTURE.md)). The host finds the gear's serial port by
this VID/PID.

## MIDI implementation

| Message | Effect |
|---|---|
| Note on/off | Triggers voices in layers whose channel/key zone match |
| Control change | `ccN` modulation sources; macro CCs (default 20–27) set macros; 64 sustain, 120 all sound off, 121 reset controllers, 123 all notes off |
| Pitch bend, channel pressure, poly pressure | `pitchbend`, `pressure`, `aftertouch` sources |
| Program change *n* (any channel) | Runs the preset in slot *n* (not persisted; the boot preset stays) |
| Clock, start, stop, continue, song position | Tempo and beat position for synced LFOs and the `beat` source |

## Serial protocol

Newline-delimited JSON: the host sends one request object per line, and the
gear answers with exactly one response object per line. Every response has
`"ok": true|false`, and failures add `"error": "message"`. If a request
carries a numeric `"id"`, the response echoes it.

Any serial terminal works for debugging (baud rate is irrelevant):

```
→ {"cmd":"hello"}
← {"ok":true,"device":"midipixi gear","version":"0.1.0","protocol":1,"board":"pico2","slots":32,"active":0,"maxPixels":1024,"maxLayers":8}
```

| Request | Response fields | Notes |
|---|---|---|
| `{"cmd":"hello"}` | `device`, `version`, `protocol`, `board`, `slots`, `active`, `maxPixels`, `maxLayers` | |
| `{"cmd":"list"}` | `active`, `slots: [{"slot":0,"name":"Strobe"}, …]` | Only occupied slots |
| `{"cmd":"get","slot":n}` | `slot`, `preset` | |
| `{"cmd":"put","slot":n,"preset":{…},"select":false}` | `slot`, `bytes` | Validates, stores compact JSON. Takes up to ~0.5 s (flash). |
| `{"cmd":"delete","slot":n}` | | |
| `{"cmd":"select","slot":n}` | | Runs the slot and makes it the boot preset |
| `{"cmd":"live","preset":{…}}` | | Runs a preset without storing it; playing notes continue. Used for live editing. `active` becomes -1. |
| `{"cmd":"config"}` | `config` | |
| `{"cmd":"config","set":{…}}` | `config` | Partial updates allowed; stored in flash. `dataPin` applies after a reboot. |
| `{"cmd":"stats"}` | `frames`, `fps`, `renderUs`, `renderAvgUs`, `renderMaxUs`, `frameBudgetUs`, `lateFrames`, `voices`, `currentMa`, `limiter`, `bpm`, `midiEvents`, `droppedMidi`, `uptimeMs`, `sysClockHz` | `renderMaxUs` resets on each read |
| `{"cmd":"bootsel"}` | | Reboots into the USB bootloader for firmware updates |

Limits: request lines up to 32 KB (20 KB on Pico 1), stored presets up to
~16 KB of compact JSON, 32 slots.

## Flash layout

The last 516 KB of flash hold 32 slots of 16 KB plus one 4 KB settings sector.
Each record has a magic number, a length and a CRC32. On first boot, the
factory presets from `presets/` are written to slots 0–5.
