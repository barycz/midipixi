# Preset format (`midipixi/1`)

A preset is a JSON object. The same bytes are used in preset files on the PC,
over the USB serial link and in the gear's flash. Missing fields take the
defaults below; unknown fields are ignored; unknown *names* (blend modes,
sources, destinations, …) are errors.

```json
{
  "format": "midipixi/1",
  "name": "Strobe",
  "author": "midipixi",
  "macros": [ { "name": "Intensity", "value": 0, "cc": 20 } ],
  "layers": [
    {
      "name": "Flash",
      "enabled": true,
      "trigger": "poly",
      "channel": 0,
      "noteRange": [0, 127],
      "voices": 4,
      "wrap": false,
      "blend": "add",
      "opacity": 1,
      "emitter": { "position": 0.5, "width": 1, "softness": 0, "hue": 0,
                   "saturation": 0, "brightness": 0, "hueSpread": 0 },
      "envs": [ { "attack": 0.001, "decay": 0.12, "sustain": 0, "release": 0.05 } ],
      "lfos": [ { "shape": "sine", "rate": 1, "sync": false, "beats": 1,
                  "retrigger": false, "phase": 0, "bipolar": true } ],
      "mods": [ { "source": "env1", "dest": "brightness", "amount": 1 } ]
    }
  ]
}
```

## Top level

| Field | Type | Default | Notes |
|---|---|---|---|
| `format` | string | `"midipixi/1"` | Presets with another major format are rejected. |
| `name`, `author` | string | `"Init"`, `""` | Max 31 bytes (UTF-8). |
| `macros` | array (≤ 8) | `Macro N`, value 0, CC 20+N-1 | `cc`: 0–127 or `null` (no CC). The CC value overrides the stored value at runtime, so DAW automation of that CC drives both the VST and the gear. |
| `layers` | array (≤ 8) | `[]` | Bottom layer first. |

## Layer

| Field | Default | Notes |
|---|---|---|
| `trigger` | `"poly"` | `poly`, `mono` (legato, last-note priority), `drone` (one always-on voice, notes ignored) |
| `channel` | `0` | 0 = omni, 1–16 |
| `noteRange` | `[0, 127]` | Key zone. Also defines the `note` source: low note = 0, high note = 1. |
| `voices` | `8` | 1–16 (poly) |
| `wrap` | `false` | Positions wrap around the strip ends (rings, loops). |
| `blend` | `"add"` | `add`, `screen`, `multiply`, `subtract`, `lighten`, `darken`, `difference`, `normal`, `hue`, `hueshift`, `mask` |
| `opacity` | `1` | 0–1, modulatable (`opacity`) |
| `emitter` | see below | Base values of the light shape |
| `envs` | 2 × ADSR | Times in seconds, sustain 0–1. Env 1 decides voice lifetime. |
| `lfos` | 3 × LFO | see below |
| `mods` | `[]` | ≤ 24 rows |

### Emitter

| Field | Default | Range / meaning |
|---|---|---|
| `position` | 0.5 | 0 = first LED, 1 = last LED. Not clamped (beams can leave the strip). |
| `width` | 0 | Fraction of the strip lit. 0 = one LED, 1 = whole strip (when centred). |
| `softness` | 0 | 0 = hard edges, 1 = soft fade |
| `hue` | 0 | Turns (0 = red, 1/3 = green, 2/3 = blue); wraps |
| `saturation` | 1 | 0–1 |
| `brightness` | 1 | 0–1 |
| `hueSpread` | 0 | Hue change across the full strip length (1 = full rainbow) |

### LFO

| Field | Default | Notes |
|---|---|---|
| `shape` | `"sine"` | `sine`, `triangle`, `sawup`, `sawdown`, `square`, `samplehold`, `smoothrandom` |
| `rate` | 1 | Hz (when `sync` is false) |
| `sync` | false | Tempo sync: one cycle lasts `beats` beats. Free-running synced LFOs lock to the song position while the transport plays. |
| `beats` | 1 | Cycle length in beats |
| `retrigger` | false | true: restart at `phase` on each note (per-voice). false: free running, shared by all voices. |
| `phase` | 0 | Start phase (turns) |
| `bipolar` | true | Output −1..1, or 0..1 when false |

### Modulation rows

```json
{ "source": "env1", "via": "velocity", "dest": "brightness", "amount": 0.8 }
```

Adds `source × via × amount` to `dest`. `via` is optional.

Sources: `note`, `velocity`, `aftertouch`, `gate`, `random`, `env1`, `env2`,
`lfo1`, `lfo2`, `lfo3`, `macro1`…`macro8`, `cc0`…`cc127`, `pitchbend`,
`pressure`, `beat`.

Destinations: `position`, `width`, `softness`, `hue`, `saturation`,
`brightness`, `huespread`, `opacity`, `lfo1.rate`…`lfo3.rate`,
`env1.attack`, `env1.decay`, `env1.sustain`, `env1.release` (and `env2.*`).

For `lfoN.rate` and `envN.attack|decay|release`, the amount is in units of
4 octaves (amount 0.25 at full source = double / half). `envN.sustain` is additive.

## Output configuration (`midipixi-config/1`)

Hardware settings live on the gear, not in presets:

```json
{ "format": "midipixi-config/1", "ledCount": 60, "colorOrder": "grb", "gamma": 2.2,
  "brightness": 1, "powerLimitMa": 2000, "maPerChannel": 20, "idleMaPerLed": 1,
  "dither": true, "reverse": false, "fps": 60, "dataPin": 2 }
```

`powerLimitMa` 0 disables the limiter. `dataPin` changes take effect after a reboot.
