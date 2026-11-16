#pragma once
// The midipixi engine: MIDI in, pixels out.
//
// Usage (both VST and gear):
//   engine.setPreset(p);  engine.setPixelCount(60);
//   loop: engine.advance(dtToNextEvent); engine.handleMidi(...);   // events at their time
//         engine.advance(dtToFrame); engine.render(); use engine.pixels()
//
// No heap allocation, no exceptions, no OS calls.

#include <cstdint>

#include "midipixi/color.h"
#include "midipixi/limits.h"
#include "midipixi/preset.h"

namespace mpx {

struct EnvState {
    enum Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0.0f;
    float releaseFrom = 0.0f;
};

struct LfoState {
    float phase = 0.0f;  // turns, 0..1
    float value = 0.0f;  // last output
    float randA = 0.0f;  // previous random target (-1..1)
    float randB = 0.0f;  // current random target (-1..1)
};

struct Voice {
    bool active = false;
    bool gate = false;       // key held (or held by sustain pedal)
    bool sustained = false;  // key released while the sustain pedal is down
    uint8_t note = 0;
    uint8_t channel = 0;
    float velocity = 0.0f;
    float aftertouch = 0.0f;
    float random = 0.0f;
    uint32_t serial = 0;  // allocation order, larger = newer
    EnvState env[kNumEnvs];
    LfoState lfo[kNumLfos];       // used by retriggered LFOs
    float lfoRate[kNumLfos] = {}; // effective rates from the last render (Hz)
    EnvParams envEff[kNumEnvs];   // effective envelope params from the last render
};

class Engine {
public:
    Engine();

    // keepVoices = true: live edit (playing voices continue with the new
    // parameters, layers whose trigger mode changed are reset).
    void setPreset(const Preset& p, bool keepVoices = false);
    const Preset& preset() const { return preset_; }

    void setPixelCount(int n);
    int pixelCount() const { return pixelCount_; }

    // Overrides a macro value (0..1), e.g. from a UI knob.
    void setMacro(int index, float value);
    float macroValue(int index) const;

    // --- MIDI ---------------------------------------------------------------
    // One complete MIDI message (no running status, no SysEx).
    void handleMidi(const uint8_t* data, int len);
    void noteOn(int channel, int note, int velocity);  // channel 0..15
    void noteOff(int channel, int note);
    void controlChange(int channel, int cc, int value);
    void pitchBend(int channel, int value14);  // 0..16383, centre 8192
    void channelPressure(int channel, int value);
    void polyPressure(int channel, int note, int value);
    void allNotesOff();  // release everything
    void panic();        // silence everything immediately

    // --- Timing -----------------------------------------------------------------
    // Host transport (VST). When called, MIDI clock messages are ignored.
    void setHostTransport(double bpm, double beats, bool playing);
    double bpm() const { return bpm_; }
    double beats() const { return beats_; }
    bool transportPlaying() const { return playing_; }

    // Advances all modulators by dt seconds.
    void advance(float dt);
    double time() const { return time_; }

    // Renders the current state into pixels().
    void render();
    const Rgb* pixels() const { return out_; }

    // --- Introspection (UI) -----------------------------------------------------
    int activeVoices(int layer) const;
    int activeVoices() const;
    // Modulated destination values of the newest voice of a layer (the values
    // the UI shows as "live" modulation). Valid when layerHasVoice().
    const float* liveValues(int layer) const { return live_[layer]; }
    bool layerHasVoice(int layer) const { return liveValid_[layer]; }

private:
    struct LayerState {
        Voice voices[kMaxVoices];
        LfoState freeLfo[kNumLfos];
        float freeRate[kNumLfos] = {};  // effective free LFO rates from last render
        uint8_t monoStack[16] = {};
        uint8_t monoCount = 0;
    };

    struct Smoothed {
        float target = 0.0f;
        float current = 0.0f;
    };

    void resetLayer(int i);
    void ensureDrone(int i);
    bool acceptsNote(const Layer& l, int channel, int note) const;
    Voice* allocVoice(int layerIndex, int channel, int note);
    void startVoice(int layerIndex, Voice& v, int channel, int note, float velocity, bool retrigger);
    void releaseVoice(Voice& v);
    void monoNoteOn(int li, int channel, int note, float velocity);
    void monoNoteOff(int li, int channel, int note);

    float baseLfoRate(const LfoParams& p) const;
    float sourceValue(const Layer& l, const LayerState& ls, const Voice* v, ModSource s) const;
    void evalMods(const Layer& l, const LayerState& ls, const Voice* v, float* sum) const;
    void advanceEnv(EnvState& st, const EnvParams& p, float dt) const;
    void advanceLfo(LfoState& st, const LfoParams& p, float rate, float dt, bool lockToBeat);
    float lfoOutput(LfoState& st, const LfoParams& p) const;

    void drawEmitter(Rgb* buf, float pos, float width, float soft, float hue, float sat, float bri, float spread,
                     bool wrap) const;
    void blendLayer(BlendMode mode, float opacity);

    float randomUnit();  // 0..1

    Preset preset_;
    LayerState layers_[kMaxLayers];
    int pixelCount_ = 60;

    Smoothed cc_[128];
    Smoothed pitchBend_;
    Smoothed pressure_;
    Smoothed macros_[kNumMacros];
    bool sustainPedal_ = false;

    double time_ = 0.0;
    double bpm_ = 120.0;
    double beats_ = 0.0;
    bool playing_ = false;
    bool hostTransport_ = false;
    bool clockRunning_ = false;
    double lastTickTime_ = -1.0;
    double tickInterval_ = 0.0;
    uint32_t ticks_ = 0;

    uint32_t serial_ = 0;
    uint32_t rng_ = 0x12345678u;

    float live_[kMaxLayers][kNumDests] = {};
    bool liveValid_[kMaxLayers] = {};

    Rgb layerBuf_[kMaxPixels];
    Rgb out_[kMaxPixels];
};

} // namespace mpx
