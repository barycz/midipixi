#pragma once
// The preset data model. Plain fixed-size structs: no pointers, no heap, so a
// Preset can be copied between threads/cores with memcpy semantics.

#include <cstddef>
#include <cstdint>

#include "midipixi/limits.h"

namespace mpx {

enum class TriggerMode : uint8_t { Poly, Mono, Legato, Drone, Count };
// Glide time: fixed for any interval, or per octave (wider jumps take longer).
enum class GlideMode : uint8_t { Time, Octave, Count };

enum class BlendMode : uint8_t {
    Add,
    Screen,
    Multiply,
    Subtract,
    Lighten,
    Darken,
    Difference,
    Normal,
    Hue,
    HueShift,
    Mask,
    Count
};

enum class LfoShape : uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Count };

enum class SourceType : uint8_t {
    None,
    Note,
    Velocity,
    Aftertouch,
    Gate,
    Random,
    Env1,
    Env2,
    Lfo1,
    Lfo2,
    Lfo3,
    Macro,      // index = macro number 0..7
    CC,         // index = controller number 0..127
    PitchBend,
    Pressure,
    Beat,
    Count
};

enum class Dest : uint8_t {
    Position,
    Width,
    Softness,
    Hue,
    Saturation,
    Brightness,
    HueSpread,
    Opacity,
    Lfo1Rate,
    Lfo2Rate,
    Lfo3Rate,
    Env1Attack,
    Env1Decay,
    Env1Sustain,
    Env1Release,
    Env2Attack,
    Env2Decay,
    Env2Sustain,
    Env2Release,
    Count
};

inline constexpr int kNumDests = static_cast<int>(Dest::Count);

struct ModSource {
    SourceType type = SourceType::None;
    uint8_t index = 0;

    bool isNone() const { return type == SourceType::None; }
    bool operator==(const ModSource& o) const { return type == o.type && index == o.index; }
    bool operator!=(const ModSource& o) const { return !(*this == o); }
};

struct ModRow {
    ModSource source;
    ModSource via;  // optional scaler (None = 1)
    Dest dest = Dest::Brightness;
    float amount = 0.0f;
};

struct EnvParams {
    float attack = 0.005f;   // seconds
    float decay = 0.3f;      // seconds
    float sustain = 0.0f;    // 0..1
    float release = 0.2f;    // seconds
};

struct LfoParams {
    LfoShape shape = LfoShape::Sine;
    float rate = 1.0f;       // Hz (when not synced)
    bool sync = false;       // tempo sync: one cycle = `beats` beats
    float beats = 1.0f;
    bool retrigger = false;  // restart phase on each note (else free running, shared by all voices)
    bool oneShot = false;    // run a single cycle from `phase` to the end, then hold (per voice, implies retrigger)
    float phase = 0.0f;      // start phase, turns
    bool bipolar = true;     // -1..1 or 0..1
};

// True when the LFO runs per voice (restarted by each note) rather than layer-wide.
inline bool isPerVoice(const LfoParams& p) { return p.retrigger || p.oneShot; }

struct EmitterParams {
    float position = 0.5f;    // 0..1 along the strip
    float width = 0.0f;       // fraction of the strip lit (0 = one LED, 1 = whole strip)
    float softness = 0.0f;    // 0 = hard edges, 1 = soft fade
    float hue = 0.0f;         // turns
    float saturation = 1.0f;
    float brightness = 1.0f;
    float hueSpread = 0.0f;   // hue change (turns) across the full strip length
};

struct Layer {
    char name[kNameLen] = "Layer";
    bool enabled = true;
    TriggerMode trigger = TriggerMode::Poly;
    uint8_t channel = 0;  // 0 = omni, 1..16
    uint8_t noteLow = 0;
    uint8_t noteHigh = 127;
    uint8_t voices = 8;   // 1..kMaxVoices (poly mode)
    bool wrap = false;    // positions wrap around the strip ends
    bool stack = false;   // poly: repeating a sounding note starts another voice instead of retriggering it
    float glide = 0.0f;   // legato: seconds to slide the note source to an overlapping note (0 = jump)
    GlideMode glideMode = GlideMode::Time;
    BlendMode blend = BlendMode::Add;
    float opacity = 1.0f;
    EmitterParams emitter;
    EnvParams env[kNumEnvs];
    LfoParams lfo[kNumLfos];
    uint8_t modCount = 0;
    ModRow mods[kMaxModRows];

    // Returns the new row index, or -1 when the matrix is full.
    int addMod(ModSource src, Dest dest, float amount, ModSource via = {});
    void removeMod(int index);
    int findMod(ModSource src, Dest dest) const;
};

struct Macro {
    char name[16] = "";
    float value = 0.0f;   // default value (0..1)
    uint8_t cc = 255;     // controlling MIDI CC, 255 = none
};

struct Preset {
    char name[kNameLen] = "Init";
    char author[kNameLen] = "";
    uint8_t layerCount = 0;
    Layer layers[kMaxLayers];
    Macro macros[kNumMacros];

    Preset();
    // Returns the new layer index or -1 when full.
    int addLayer(const Layer& l);
    void removeLayer(int index);
    void moveLayer(int from, int to);
};

// A layer wired like a basic "note visualisation" patch: each note lights the
// LED at its position, Env 1 controls brightness. Equivalent of a synth's init patch.
Layer makeInitLayer();
Preset makeInitPreset();

void copyName(char* dst, size_t cap, const char* src);

// --- string names (used by JSON and UIs) -----------------------------------

const char* toString(TriggerMode m);
const char* toString(GlideMode m);
const char* toString(BlendMode m);
const char* toString(LfoShape s);
const char* toString(Dest d);
// Writes e.g. "env1", "cc74", "macro3". Returns buf.
const char* toString(ModSource s, char* buf, size_t cap);
const char* sourceTypeName(SourceType t);  // "cc", "macro", "env1", ...

bool parseTriggerMode(const char* s, TriggerMode& out);
bool parseGlideMode(const char* s, GlideMode& out);
bool parseBlendMode(const char* s, BlendMode& out);
bool parseLfoShape(const char* s, LfoShape& out);
bool parseDest(const char* s, Dest& out);
bool parseModSource(const char* s, ModSource& out);

// Human readable labels for UIs.
const char* displayName(Dest d);
const char* displayName(BlendMode m);

// Does this source carry a per-voice value (as opposed to a global value)?
bool isVoiceSource(SourceType t);
// Is the source bipolar (-1..1) for the given layer settings?
bool isBipolarSource(const Layer& layer, ModSource s);

} // namespace mpx
