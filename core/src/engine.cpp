#include "midipixi/engine.h"

#include <cmath>
#include <cstring>

namespace mpx {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kSmoothingTau = 0.03f;  // seconds, for CC / pitch bend / pressure / macros
constexpr float kOctaveRange = 4.0f;    // rate/time modulation: amount 1 = 4 octaves

int destIndex(Dest d) { return static_cast<int>(d); }

float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }

float fract(double x) { return static_cast<float>(x - std::floor(x)); }

} // namespace

Engine::Engine() {
    for (int i = 0; i < 128; ++i) cc_[i] = {};
    setPreset(Preset{});
}

float Engine::randomUnit() {
    // xorshift32
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

void Engine::resetLayer(int i) {
    LayerState& ls = layers_[i];
    for (Voice& v : ls.voices) v = Voice{};
    ls.monoCount = 0;
    const Layer& l = preset_.layers[i];
    for (int k = 0; k < kNumLfos; ++k) {
        ls.freeLfo[k] = LfoState{};
        ls.freeLfo[k].phase = wrap01(l.lfo[k].phase);
        ls.freeLfo[k].randA = randomUnit() * 2.0f - 1.0f;
        ls.freeLfo[k].randB = randomUnit() * 2.0f - 1.0f;
        ls.freeRate[k] = baseLfoRate(l.lfo[k]);
    }
    liveValid_[i] = false;
}

void Engine::setPreset(const Preset& p, bool keepVoices) {
    // Only the bits needed for change detection are kept (no full Preset copy on
    // the stack: this runs on the MCU's small core 1 stack).
    int oldCount = preset_.layerCount;
    TriggerMode oldTrigger[kMaxLayers];
    float oldMacro[kNumMacros];
    for (int i = 0; i < kMaxLayers; ++i) oldTrigger[i] = preset_.layers[i].trigger;
    for (int m = 0; m < kNumMacros; ++m) oldMacro[m] = preset_.macros[m].value;

    preset_ = p;
    if (preset_.layerCount > kMaxLayers) preset_.layerCount = kMaxLayers;
    for (int i = 0; i < kMaxLayers; ++i) {
        bool reset = !keepVoices || i >= oldCount || i >= preset_.layerCount || oldTrigger[i] != preset_.layers[i].trigger;
        if (reset) resetLayer(i);
    }
    for (int m = 0; m < kNumMacros; ++m) {
        float v = clamp01(preset_.macros[m].value);
        if (!keepVoices) {
            macros_[m].target = macros_[m].current = v;
        } else if (oldMacro[m] != preset_.macros[m].value) {
            macros_[m].target = v;
        }
    }
    for (int i = 0; i < preset_.layerCount; ++i) ensureDrone(i);
}

void Engine::setPixelCount(int n) {
    if (n < 1) n = 1;
    if (n > kMaxPixels) n = kMaxPixels;
    pixelCount_ = n;
}

void Engine::setMacro(int index, float value) {
    if (index < 0 || index >= kNumMacros) return;
    macros_[index].target = clamp01(value);
}

float Engine::macroValue(int index) const {
    return index >= 0 && index < kNumMacros ? macros_[index].current : 0.0f;
}

// --- Voices -------------------------------------------------------------------

bool Engine::acceptsNote(const Layer& l, int channel, int note) const {
    if (!l.enabled) return false;
    if (l.channel != 0 && l.channel != channel + 1) return false;
    return note >= l.noteLow && note <= l.noteHigh;
}

void Engine::ensureDrone(int i) {
    const Layer& l = preset_.layers[i];
    LayerState& ls = layers_[i];
    if (l.trigger != TriggerMode::Drone) return;
    Voice& v = ls.voices[0];
    if (!v.active) startVoice(i, v, 0, 60, 1.0f, true);
}

void Engine::startVoice(int layerIndex, Voice& v, int channel, int note, float velocity, bool retrigger) {
    const Layer& l = preset_.layers[layerIndex];
    bool wasActive = v.active;
    v.active = true;
    v.gate = true;
    v.sustained = false;
    v.note = static_cast<uint8_t>(note);
    v.channel = static_cast<uint8_t>(channel);
    v.velocity = velocity;
    v.aftertouch = 0.0f;
    v.random = randomUnit();
    v.serial = ++serial_;
    if (!retrigger) return;
    for (int e = 0; e < kNumEnvs; ++e) {
        v.envEff[e] = l.env[e];
        v.env[e].stage = EnvState::Attack;
        if (!wasActive) v.env[e].level = 0.0f;  // retriggering a playing voice attacks from its current level
    }
    for (int k = 0; k < kNumLfos; ++k) {
        v.lfoRate[k] = baseLfoRate(l.lfo[k]);
        if (isPerVoice(l.lfo[k])) {
            v.lfo[k].phase = wrap01(l.lfo[k].phase);
            v.lfo[k].randA = randomUnit() * 2.0f - 1.0f;
            v.lfo[k].randB = randomUnit() * 2.0f - 1.0f;
            v.lfo[k].value = lfoOutput(v.lfo[k], l.lfo[k]);
        }
    }
}

void Engine::releaseVoice(Voice& v) {
    v.gate = false;
    v.sustained = false;
    for (EnvState& e : v.env) {
        if (e.stage != EnvState::Idle && e.stage != EnvState::Release) {
            e.stage = EnvState::Release;
            e.releaseFrom = e.level;
        }
    }
}

Voice* Engine::allocVoice(int layerIndex, int channel, int note) {
    const Layer& l = preset_.layers[layerIndex];
    LayerState& ls = layers_[layerIndex];
    int maxV = l.voices < 1 ? 1 : (l.voices > kMaxVoices ? kMaxVoices : l.voices);
    // Same note already sounding: retrigger it.
    for (int i = 0; i < maxV; ++i) {
        Voice& v = ls.voices[i];
        if (v.active && v.note == note && v.channel == channel) return &v;
    }
    for (int i = 0; i < maxV; ++i)
        if (!ls.voices[i].active) return &ls.voices[i];
    // Steal: oldest released voice first, else the oldest voice.
    Voice* best = nullptr;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        for (int i = 0; i < maxV; ++i) {
            Voice& v = ls.voices[i];
            if (pass == 0 && v.gate) continue;
            if (!best || v.serial < best->serial) best = &v;
        }
    }
    return best;
}

void Engine::monoNoteOn(int li, int channel, int note, float velocity) {
    LayerState& ls = layers_[li];
    // Move note to the top of the stack.
    int n = 0;
    for (int i = 0; i < ls.monoCount; ++i)
        if (ls.monoStack[i] != note) ls.monoStack[n++] = ls.monoStack[i];
    if (n >= static_cast<int>(sizeof(ls.monoStack))) {
        for (int i = 1; i < n; ++i) ls.monoStack[i - 1] = ls.monoStack[i];
        --n;
    }
    ls.monoStack[n++] = static_cast<uint8_t>(note);
    ls.monoCount = static_cast<uint8_t>(n);
    Voice& v = ls.voices[0];
    bool legato = v.active && v.gate;
    startVoice(li, v, channel, note, legato ? v.velocity : velocity, !legato);
}

void Engine::monoNoteOff(int li, int channel, int note) {
    LayerState& ls = layers_[li];
    int n = 0;
    for (int i = 0; i < ls.monoCount; ++i)
        if (ls.monoStack[i] != note) ls.monoStack[n++] = ls.monoStack[i];
    ls.monoCount = static_cast<uint8_t>(n);
    Voice& v = ls.voices[0];
    if (!v.active || v.note != note) return;
    if (n > 0) {
        // Legato back to the previously held note.
        v.note = ls.monoStack[n - 1];
        v.channel = static_cast<uint8_t>(channel);
    } else if (sustainPedal_) {
        v.sustained = true;
    } else {
        releaseVoice(v);
    }
}

void Engine::noteOn(int channel, int note, int velocity) {
    if (velocity <= 0) {
        noteOff(channel, note);
        return;
    }
    float vel = static_cast<float>(velocity) / 127.0f;
    for (int i = 0; i < preset_.layerCount; ++i) {
        const Layer& l = preset_.layers[i];
        if (!acceptsNote(l, channel, note)) continue;
        if (l.trigger == TriggerMode::Drone) continue;
        if (l.trigger == TriggerMode::Mono) {
            monoNoteOn(i, channel, note, vel);
            continue;
        }
        if (Voice* v = allocVoice(i, channel, note)) startVoice(i, *v, channel, note, vel, true);
    }
}

void Engine::noteOff(int channel, int note) {
    for (int i = 0; i < preset_.layerCount; ++i) {
        const Layer& l = preset_.layers[i];
        if (l.trigger == TriggerMode::Drone) continue;
        if (l.channel != 0 && l.channel != channel + 1) continue;
        if (l.trigger == TriggerMode::Mono) {
            monoNoteOff(i, channel, note);
            continue;
        }
        for (Voice& v : layers_[i].voices) {
            if (!v.active || !v.gate || v.note != note || v.channel != channel) continue;
            if (sustainPedal_)
                v.sustained = true;
            else
                releaseVoice(v);
        }
    }
}

void Engine::allNotesOff() {
    for (int i = 0; i < preset_.layerCount; ++i) {
        if (preset_.layers[i].trigger == TriggerMode::Drone) continue;
        layers_[i].monoCount = 0;
        for (Voice& v : layers_[i].voices)
            if (v.active) releaseVoice(v);
    }
}

void Engine::panic() {
    for (int i = 0; i < kMaxLayers; ++i) {
        for (Voice& v : layers_[i].voices) v = Voice{};
        layers_[i].monoCount = 0;
        liveValid_[i] = false;
    }
    sustainPedal_ = false;
    for (int i = 0; i < preset_.layerCount; ++i) ensureDrone(i);
}

void Engine::controlChange(int channel, int cc, int value) {
    (void)channel;
    if (cc < 0 || cc > 127) return;
    float v = static_cast<float>(value) / 127.0f;
    cc_[cc].target = v;
    for (int m = 0; m < kNumMacros; ++m)
        if (preset_.macros[m].cc == cc) macros_[m].target = v;
    switch (cc) {
        case 64: {
            bool down = value >= 64;
            if (sustainPedal_ && !down) {
                for (int i = 0; i < preset_.layerCount; ++i)
                    for (Voice& v2 : layers_[i].voices)
                        if (v2.active && v2.sustained) releaseVoice(v2);
            }
            sustainPedal_ = down;
            break;
        }
        case 120: panic(); break;
        case 121:
            for (Smoothed& s : cc_) s.target = 0.0f;
            pitchBend_.target = 0.0f;
            pressure_.target = 0.0f;
            break;
        case 123: allNotesOff(); break;
        default: break;
    }
}

void Engine::pitchBend(int channel, int value14) {
    (void)channel;
    pitchBend_.target = clampf((static_cast<float>(value14) - 8192.0f) / 8192.0f, -1.0f, 1.0f);
}

void Engine::channelPressure(int channel, int value) {
    (void)channel;
    pressure_.target = static_cast<float>(value) / 127.0f;
}

void Engine::polyPressure(int channel, int note, int value) {
    float v = static_cast<float>(value) / 127.0f;
    for (int i = 0; i < preset_.layerCount; ++i)
        for (Voice& vo : layers_[i].voices)
            if (vo.active && vo.note == note && vo.channel == channel) vo.aftertouch = v;
}

void Engine::handleMidi(const uint8_t* d, int len) {
    if (len < 1) return;
    uint8_t status = d[0];
    if (status >= 0xF8) {
        if (hostTransport_) return;
        switch (status) {
            case 0xF8: {  // clock tick
                if (lastTickTime_ >= 0.0) {
                    double dt = time_ - lastTickTime_;
                    if (dt > 0.0 && dt < 0.25) {
                        tickInterval_ = tickInterval_ > 0.0 ? tickInterval_ + (dt - tickInterval_) * 0.1 : dt;
                        bpm_ = 60.0 / (tickInterval_ * 24.0);
                        if (bpm_ < 20.0) bpm_ = 20.0;
                        if (bpm_ > 400.0) bpm_ = 400.0;
                    }
                }
                lastTickTime_ = time_;
                clockRunning_ = true;
                if (playing_) {
                    ++ticks_;
                    beats_ = ticks_ / 24.0;
                }
                break;
            }
            case 0xFA:  // start
                ticks_ = 0;
                beats_ = 0.0;
                playing_ = true;
                break;
            case 0xFB: playing_ = true; break;   // continue
            case 0xFC: playing_ = false; break;  // stop
            default: break;
        }
        return;
    }
    if (status == 0xF2 && len >= 3) {  // song position pointer (sixteenths)
        if (hostTransport_) return;
        ticks_ = static_cast<uint32_t>((d[1] | (d[2] << 7)) * 6);
        beats_ = ticks_ / 24.0;
        return;
    }
    if (status < 0x80 || status >= 0xF0) return;
    int ch = status & 0x0F;
    int d1 = len > 1 ? d[1] & 0x7F : 0;
    int d2 = len > 2 ? d[2] & 0x7F : 0;
    switch (status & 0xF0) {
        case 0x80: noteOff(ch, d1); break;
        case 0x90: noteOn(ch, d1, d2); break;
        case 0xA0: polyPressure(ch, d1, d2); break;
        case 0xB0: controlChange(ch, d1, d2); break;
        case 0xD0: channelPressure(ch, d1); break;
        case 0xE0: pitchBend(ch, d1 | (d2 << 7)); break;
        default: break;
    }
}

void Engine::setHostTransport(double bpm, double beats, bool playing) {
    hostTransport_ = true;
    if (bpm > 1.0) bpm_ = bpm;
    playing_ = playing;
    if (playing) beats_ = beats;
}

// --- Modulators -----------------------------------------------------------------

float Engine::baseLfoRate(const LfoParams& p) const {
    if (p.sync) {
        float beats = p.beats > 0.0001f ? p.beats : 0.0001f;
        return static_cast<float>(bpm_ / 60.0) / beats;
    }
    return p.rate > 0.0f ? p.rate : 0.0f;
}

float Engine::lfoOutput(LfoState& st, const LfoParams& p) const {
    float ph = st.phase;
    float v;
    switch (p.shape) {
        case LfoShape::Sine: v = std::sin(ph * kTwoPi); break;
        case LfoShape::Triangle: v = ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f); break;
        case LfoShape::SawUp: v = 2.0f * ph - 1.0f; break;
        case LfoShape::SawDown: v = 1.0f - 2.0f * ph; break;
        case LfoShape::Square: v = ph < 0.5f ? 1.0f : -1.0f; break;
        case LfoShape::SampleHold: v = st.randB; break;
        case LfoShape::SmoothRandom: v = lerpf(st.randA, st.randB, smoothstep01(ph)); break;
        default: v = 0.0f; break;
    }
    return p.bipolar ? v : (v + 1.0f) * 0.5f;
}

void Engine::advanceLfo(LfoState& st, const LfoParams& p, float rate, float dt, bool lockToBeat) {
    if (p.oneShot) {
        // Single cycle: run up to the end of the waveform (phase 1) and hold there.
        st.phase = st.phase + rate * dt;
        if (st.phase > 1.0f) st.phase = 1.0f;
        st.value = lfoOutput(st, p);
        return;
    }
    float newPhase;
    if (lockToBeat) {
        float beats = p.beats > 0.0001f ? p.beats : 0.0001f;
        newPhase = fract(beats_ / static_cast<double>(beats) + static_cast<double>(p.phase));
    } else {
        newPhase = st.phase + rate * dt;
    }
    bool wrapped = lockToBeat ? newPhase < st.phase : newPhase >= 1.0f;
    st.phase = wrap01(newPhase);
    if (wrapped) {
        st.randA = st.randB;
        st.randB = randomUnit() * 2.0f - 1.0f;
    }
    st.value = lfoOutput(st, p);
}

void Engine::advanceEnv(EnvState& st, const EnvParams& p, float dt) const {
    float s = clamp01(p.sustain);
    switch (st.stage) {
        case EnvState::Idle: st.level = 0.0f; break;
        case EnvState::Attack:
            if (p.attack <= 0.0f) {
                st.level = 1.0f;
            } else {
                st.level += dt / p.attack;
            }
            if (st.level >= 1.0f) {
                st.level = 1.0f;
                st.stage = EnvState::Decay;
            }
            break;
        case EnvState::Decay:
            if (p.decay <= 0.0f || st.level <= s) {
                st.level = s;
            } else {
                st.level -= dt * (1.0f - s) / p.decay;
            }
            if (st.level <= s) {
                st.level = s;
                st.stage = EnvState::Sustain;
            }
            break;
        case EnvState::Sustain: st.level = s; break;
        case EnvState::Release:
            if (p.release <= 0.0f || st.releaseFrom <= 0.0f) {
                st.level = 0.0f;
            } else {
                st.level -= dt * st.releaseFrom / p.release;
            }
            if (st.level <= 0.0f) {
                st.level = 0.0f;
                st.stage = EnvState::Idle;
            }
            break;
    }
}

void Engine::advance(float dt) {
    if (!(dt > 0.0f)) return;
    time_ += static_cast<double>(dt);
    beats_ += static_cast<double>(dt) * bpm_ / 60.0;
    if (!hostTransport_ && clockRunning_ && playing_) {
        double cap = (ticks_ + 1) / 24.0;
        if (beats_ > cap) beats_ = cap;
    }
    if (clockRunning_ && lastTickTime_ >= 0.0 && time_ - lastTickTime_ > 0.5) clockRunning_ = false;

    float k = 1.0f - std::exp(-dt / kSmoothingTau);
    for (Smoothed& s : cc_) s.current += (s.target - s.current) * k;
    pitchBend_.current += (pitchBend_.target - pitchBend_.current) * k;
    pressure_.current += (pressure_.target - pressure_.current) * k;
    for (Smoothed& s : macros_) s.current += (s.target - s.current) * k;

    for (int li = 0; li < preset_.layerCount; ++li) {
        const Layer& l = preset_.layers[li];
        LayerState& ls = layers_[li];
        for (int k2 = 0; k2 < kNumLfos; ++k2) {
            const LfoParams& p = l.lfo[k2];
            if (!isPerVoice(p)) advanceLfo(ls.freeLfo[k2], p, ls.freeRate[k2], dt, p.sync && playing_);
        }
        for (Voice& v : ls.voices) {
            if (!v.active) continue;
            for (int e = 0; e < kNumEnvs; ++e) advanceEnv(v.env[e], v.envEff[e], dt);
            for (int k2 = 0; k2 < kNumLfos; ++k2)
                if (isPerVoice(l.lfo[k2])) advanceLfo(v.lfo[k2], l.lfo[k2], v.lfoRate[k2], dt, false);
            if (!v.gate && v.env[0].stage == EnvState::Idle) v.active = false;
        }
        ensureDrone(li);
    }
}

// --- Modulation matrix -------------------------------------------------------------

float Engine::sourceValue(const Layer& l, const LayerState& ls, const Voice* v, ModSource s) const {
    switch (s.type) {
        case SourceType::None: return 1.0f;
        case SourceType::Note: {
            if (!v) return 0.0f;
            int span = l.noteHigh - l.noteLow;
            return span > 0 ? clamp01(static_cast<float>(v->note - l.noteLow) / static_cast<float>(span)) : 0.0f;
        }
        case SourceType::Velocity: return v ? v->velocity : 0.0f;
        case SourceType::Aftertouch: return v ? v->aftertouch : 0.0f;
        case SourceType::Gate: return v && v->gate ? 1.0f : 0.0f;
        case SourceType::Random: return v ? v->random : 0.0f;
        case SourceType::Env1: return v ? v->env[0].level : 0.0f;
        case SourceType::Env2: return v ? v->env[1].level : 0.0f;
        case SourceType::Lfo1:
        case SourceType::Lfo2:
        case SourceType::Lfo3: {
            int k = static_cast<int>(s.type) - static_cast<int>(SourceType::Lfo1);
            if (!isPerVoice(l.lfo[k])) return ls.freeLfo[k].value;
            return v ? v->lfo[k].value : 0.0f;
        }
        case SourceType::Macro: return s.index < kNumMacros ? macros_[s.index].current : 0.0f;
        case SourceType::CC: return s.index < 128 ? cc_[s.index].current : 0.0f;
        case SourceType::PitchBend: return pitchBend_.current;
        case SourceType::Pressure: return pressure_.current;
        case SourceType::Beat: return fract(beats_);
        default: return 0.0f;
    }
}

void Engine::evalMods(const Layer& l, const LayerState& ls, const Voice* v, float* sum) const {
    for (int i = 0; i < kNumDests; ++i) sum[i] = 0.0f;
    for (int r = 0; r < l.modCount; ++r) {
        const ModRow& row = l.mods[r];
        if (row.amount == 0.0f || row.source.isNone()) continue;
        float s = sourceValue(l, ls, v, row.source);
        if (!row.via.isNone()) s *= sourceValue(l, ls, v, row.via);
        int d = destIndex(row.dest);
        if (d >= 0 && d < kNumDests) sum[d] += s * row.amount;
    }
}

// --- Rendering ------------------------------------------------------------------------

void Engine::drawEmitter(Rgb* buf, float pos, float width, float soft, float hue, float sat, float bri, float spread,
                         bool wrap) const {
    if (bri <= 0.0f) return;
    const int n = pixelCount_;
    const float nf = static_cast<float>(n);
    float c = wrap ? wrap01(pos) * nf : pos * (nf - 1.0f);
    float h = 0.5f + width * nf * 0.5f;  // half extent, including the centre LED
    float e = 2.0f * h * soft;           // edge (fade) length
    if (e < 1.0f) e = 1.0f;              // at least one pixel: anti-aliasing
    float b = h + e * 0.5f;              // outer reach
    float invE = 1.0f / e;

    Rgb base = hsvToRgb(hue, sat, bri);
    bool perPixelHue = spread != 0.0f;

    auto shade = [&](int idx, float d) {
        float t = (b - d) * invE;
        if (t <= 0.0f) return;
        if (t > 1.0f) t = 1.0f;
        if (soft > 0.0f) t = lerpf(t, smoothstep01(t), soft);
        Rgb col = perPixelHue ? hsvToRgb(hue + spread * (static_cast<float>(idx) - c) / nf, sat, bri) : base;
        Rgb& px = buf[idx];
        px.r += col.r * t;
        px.g += col.g * t;
        px.b += col.b * t;
    };

    if (wrap && 2.0f * b >= nf) {
        for (int i = 0; i < n; ++i) {
            float d = std::fabs(static_cast<float>(i) - c);
            if (d > nf * 0.5f) d = nf - d;
            shade(i, d);
        }
        return;
    }
    int lo = static_cast<int>(std::ceil(c - b));
    int hi = static_cast<int>(std::floor(c + b));
    if (!wrap) {
        if (lo < 0) lo = 0;
        if (hi > n - 1) hi = n - 1;
    }
    for (int k = lo; k <= hi; ++k) {
        int idx = k;
        if (wrap) idx = ((k % n) + n) % n;
        shade(idx, std::fabs(static_cast<float>(k) - c));
    }
}

void Engine::blendLayer(BlendMode mode, float o) {
    const int n = pixelCount_;
    for (int i = 0; i < n; ++i) {
        Rgb L{clamp01(layerBuf_[i].r), clamp01(layerBuf_[i].g), clamp01(layerBuf_[i].b)};
        Rgb& B = out_[i];
        switch (mode) {
            case BlendMode::Add:
                B.r += L.r * o;
                B.g += L.g * o;
                B.b += L.b * o;
                break;
            case BlendMode::Screen:
                B.r = 1.0f - (1.0f - B.r) * (1.0f - L.r * o);
                B.g = 1.0f - (1.0f - B.g) * (1.0f - L.g * o);
                B.b = 1.0f - (1.0f - B.b) * (1.0f - L.b * o);
                break;
            case BlendMode::Multiply:
                B.r *= 1.0f - o + o * L.r;
                B.g *= 1.0f - o + o * L.g;
                B.b *= 1.0f - o + o * L.b;
                break;
            case BlendMode::Subtract:
                B.r -= L.r * o;
                B.g -= L.g * o;
                B.b -= L.b * o;
                break;
            case BlendMode::Lighten:
                B.r = lerpf(B.r, B.r > L.r ? B.r : L.r, o);
                B.g = lerpf(B.g, B.g > L.g ? B.g : L.g, o);
                B.b = lerpf(B.b, B.b > L.b ? B.b : L.b, o);
                break;
            case BlendMode::Darken:
                B.r = lerpf(B.r, B.r < L.r ? B.r : L.r, o);
                B.g = lerpf(B.g, B.g < L.g ? B.g : L.g, o);
                B.b = lerpf(B.b, B.b < L.b ? B.b : L.b, o);
                break;
            case BlendMode::Difference:
                B.r = lerpf(B.r, std::fabs(B.r - L.r), o);
                B.g = lerpf(B.g, std::fabs(B.g - L.g), o);
                B.b = lerpf(B.b, std::fabs(B.b - L.b), o);
                break;
            case BlendMode::Normal: {
                float a = maxChannel(L) * o;
                B.r = B.r * (1.0f - a) + L.r * o;
                B.g = B.g * (1.0f - a) + L.g * o;
                B.b = B.b * (1.0f - a) + L.b * o;
                break;
            }
            case BlendMode::Hue:
            case BlendMode::HueShift: {
                Hsv lh = rgbToHsv(L);
                float mix = lh.v * o;
                if (mix <= 0.0f) break;
                Hsv bh = rgbToHsv(B);
                Rgb c = mode == BlendMode::Hue ? hsvToRgb(lh.h, bh.s, bh.v) : hsvToRgb(bh.h + lh.h, bh.s, bh.v);
                B.r = lerpf(B.r, c.r, mix);
                B.g = lerpf(B.g, c.g, mix);
                B.b = lerpf(B.b, c.b, mix);
                break;
            }
            case BlendMode::Mask: {
                float m = 1.0f - o + o * maxChannel(L);
                B.r *= m;
                B.g *= m;
                B.b *= m;
                break;
            }
            default: break;
        }
        B.r = clamp01(B.r);
        B.g = clamp01(B.g);
        B.b = clamp01(B.b);
    }
}

void Engine::render() {
    const int n = pixelCount_;
    for (int i = 0; i < n; ++i) out_[i] = Rgb{};
    float sum[kNumDests];

    for (int li = 0; li < preset_.layerCount; ++li) {
        const Layer& l = preset_.layers[li];
        LayerState& ls = layers_[li];
        liveValid_[li] = false;
        if (!l.enabled) continue;

        // Newest active voice drives layer-level destinations and the UI's live values.
        const Voice* newest = nullptr;
        for (const Voice& v : ls.voices)
            if (v.active && (!newest || v.serial > newest->serial)) newest = &v;

        evalMods(l, ls, newest, sum);
        float opacity = clamp01(l.opacity + sum[destIndex(Dest::Opacity)]);
        for (int k = 0; k < kNumLfos; ++k)
            ls.freeRate[k] =
                baseLfoRate(l.lfo[k]) * std::exp2(sum[destIndex(Dest::Lfo1Rate) + k] * kOctaveRange);

        bool any = false;
        for (int i = 0; i < n; ++i) layerBuf_[i] = Rgb{};
        for (Voice& v : ls.voices) {
            if (!v.active) continue;
            any = true;
            evalMods(l, ls, &v, sum);
            const EmitterParams& e = l.emitter;
            float pos = e.position + sum[destIndex(Dest::Position)];
            float width = clamp01(e.width + sum[destIndex(Dest::Width)]);
            float soft = clamp01(e.softness + sum[destIndex(Dest::Softness)]);
            float hue = e.hue + sum[destIndex(Dest::Hue)];
            float sat = clamp01(e.saturation + sum[destIndex(Dest::Saturation)]);
            float bri = clamp01(e.brightness + sum[destIndex(Dest::Brightness)]);
            float spread = e.hueSpread + sum[destIndex(Dest::HueSpread)];

            // Modulated modulator parameters, used by the next advance().
            for (int k = 0; k < kNumLfos; ++k)
                v.lfoRate[k] = baseLfoRate(l.lfo[k]) * std::exp2(sum[destIndex(Dest::Lfo1Rate) + k] * kOctaveRange);
            for (int en = 0; en < kNumEnvs; ++en) {
                int base = destIndex(en == 0 ? Dest::Env1Attack : Dest::Env2Attack);
                const EnvParams& p = l.env[en];
                EnvParams& q = v.envEff[en];
                q.attack = p.attack * std::exp2(sum[base + 0] * kOctaveRange);
                q.decay = p.decay * std::exp2(sum[base + 1] * kOctaveRange);
                q.sustain = clamp01(p.sustain + sum[base + 2]);
                q.release = p.release * std::exp2(sum[base + 3] * kOctaveRange);
            }

            drawEmitter(layerBuf_, pos, width, soft, hue, sat, bri, spread, l.wrap);

            if (&v == newest) {
                float* lv = live_[li];
                lv[destIndex(Dest::Position)] = l.wrap ? wrap01(pos) : pos;
                lv[destIndex(Dest::Width)] = width;
                lv[destIndex(Dest::Softness)] = soft;
                lv[destIndex(Dest::Hue)] = hue;
                lv[destIndex(Dest::Saturation)] = sat;
                lv[destIndex(Dest::Brightness)] = bri;
                lv[destIndex(Dest::HueSpread)] = spread;
                lv[destIndex(Dest::Opacity)] = opacity;
                for (int k = 0; k < kNumLfos; ++k) lv[destIndex(Dest::Lfo1Rate) + k] = v.lfoRate[k];
                for (int en = 0; en < kNumEnvs; ++en) {
                    int base = destIndex(en == 0 ? Dest::Env1Attack : Dest::Env2Attack);
                    lv[base + 0] = v.envEff[en].attack;
                    lv[base + 1] = v.envEff[en].decay;
                    lv[base + 2] = v.envEff[en].sustain;
                    lv[base + 3] = v.envEff[en].release;
                }
                liveValid_[li] = true;
            }
        }

        // An empty layer is a no-op for these modes; masking modes still apply.
        bool emptyIsNoop = l.blend != BlendMode::Multiply && l.blend != BlendMode::Mask &&
                           l.blend != BlendMode::Darken;
        if (!any && emptyIsNoop) continue;
        if (opacity <= 0.0f) continue;
        blendLayer(l.blend, opacity);
    }
}

int Engine::activeVoices(int layer) const {
    if (layer < 0 || layer >= kMaxLayers) return 0;
    int c = 0;
    for (const Voice& v : layers_[layer].voices) c += v.active ? 1 : 0;
    return c;
}

int Engine::activeVoices() const {
    int c = 0;
    for (int i = 0; i < preset_.layerCount; ++i) c += activeVoices(i);
    return c;
}

} // namespace mpx
