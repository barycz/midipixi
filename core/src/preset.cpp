#include "midipixi/preset.h"

#include <cstdio>
#include <cstring>

namespace mpx {

namespace {

const char* const kTriggerNames[] = {"poly", "mono", "legato", "drone"};
const char* const kGlideNames[] = {"time", "octave"};
const char* const kBlendNames[] = {"add",     "screen", "multiply",   "subtract", "lighten", "darken",
                                   "difference", "normal", "hue", "hueshift", "mask"};
const char* const kBlendLabels[] = {"Add",        "Screen", "Multiply", "Subtract",  "Lighten", "Darken",
                                    "Difference", "Normal", "Hue",      "Hue shift", "Mask"};
const char* const kShapeNames[] = {"sine", "triangle", "sawup", "sawdown", "square", "samplehold", "smoothrandom"};
const char* const kSourceNames[] = {"none", "note",  "velocity", "aftertouch", "gate",      "random",
                                    "env1", "env2",  "lfo1",     "lfo2",       "lfo3",      "macro",
                                    "cc",   "pitchbend", "pressure", "beat"};
const char* const kDestNames[] = {"position",     "width",        "softness",     "hue",          "saturation",
                                  "brightness",   "huespread",    "opacity",      "lfo1.rate",    "lfo2.rate",
                                  "lfo3.rate",    "env1.attack",  "env1.decay",   "env1.sustain", "env1.release",
                                  "env2.attack",  "env2.decay",   "env2.sustain", "env2.release"};
const char* const kDestLabels[] = {"Position",     "Width",        "Softness",     "Hue",          "Saturation",
                                   "Brightness",   "Hue spread",   "Opacity",      "LFO 1 rate",   "LFO 2 rate",
                                   "LFO 3 rate",   "Env 1 attack", "Env 1 decay",  "Env 1 sustain", "Env 1 release",
                                   "Env 2 attack", "Env 2 decay",  "Env 2 sustain", "Env 2 release"};

static_assert(sizeof(kTriggerNames) / sizeof(kTriggerNames[0]) == static_cast<size_t>(TriggerMode::Count), "");
static_assert(sizeof(kGlideNames) / sizeof(kGlideNames[0]) == static_cast<size_t>(GlideMode::Count), "");
static_assert(sizeof(kBlendNames) / sizeof(kBlendNames[0]) == static_cast<size_t>(BlendMode::Count), "");
static_assert(sizeof(kBlendLabels) / sizeof(kBlendLabels[0]) == static_cast<size_t>(BlendMode::Count), "");
static_assert(sizeof(kShapeNames) / sizeof(kShapeNames[0]) == static_cast<size_t>(LfoShape::Count), "");
static_assert(sizeof(kSourceNames) / sizeof(kSourceNames[0]) == static_cast<size_t>(SourceType::Count), "");
static_assert(sizeof(kDestNames) / sizeof(kDestNames[0]) == static_cast<size_t>(Dest::Count), "");
static_assert(sizeof(kDestLabels) / sizeof(kDestLabels[0]) == static_cast<size_t>(Dest::Count), "");

template <typename E, size_t N>
bool parseEnum(const char* s, const char* const (&names)[N], E& out) {
    for (size_t i = 0; i < N; ++i) {
        if (std::strcmp(s, names[i]) == 0) {
            out = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

template <typename E, size_t N>
const char* enumName(E e, const char* const (&names)[N]) {
    size_t i = static_cast<size_t>(e);
    return i < N ? names[i] : "?";
}

bool parseUInt(const char* s, int& out) {
    if (!*s) return false;
    int v = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (*s - '0');
        if (v > 100000) return false;
    }
    out = v;
    return true;
}

} // namespace

void copyName(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t i = 0;
    if (src)
        for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

int Layer::addMod(ModSource src, Dest dest, float amount, ModSource via) {
    if (modCount >= kMaxModRows) return -1;
    ModRow& r = mods[modCount];
    r.source = src;
    r.via = via;
    r.dest = dest;
    r.amount = amount;
    return modCount++;
}

void Layer::removeMod(int index) {
    if (index < 0 || index >= modCount) return;
    for (int i = index; i + 1 < modCount; ++i) mods[i] = mods[i + 1];
    --modCount;
    mods[modCount] = ModRow{};
}

int Layer::findMod(ModSource src, Dest dest) const {
    for (int i = 0; i < modCount; ++i)
        if (mods[i].source == src && mods[i].dest == dest) return i;
    return -1;
}

Preset::Preset() {
    for (int i = 0; i < kNumMacros; ++i) {
        std::snprintf(macros[i].name, sizeof(macros[i].name), "Macro %d", i + 1);
        macros[i].cc = static_cast<uint8_t>(20 + i);
    }
}

int Preset::addLayer(const Layer& l) {
    if (layerCount >= kMaxLayers) return -1;
    layers[layerCount] = l;
    return layerCount++;
}

void Preset::removeLayer(int index) {
    if (index < 0 || index >= layerCount) return;
    for (int i = index; i + 1 < layerCount; ++i) layers[i] = layers[i + 1];
    --layerCount;
    layers[layerCount] = Layer{};
}

void Preset::moveLayer(int from, int to) {
    if (from < 0 || from >= layerCount || to < 0 || to >= layerCount || from == to) return;
    Layer tmp = layers[from];
    if (from < to)
        for (int i = from; i < to; ++i) layers[i] = layers[i + 1];
    else
        for (int i = from; i > to; --i) layers[i] = layers[i - 1];
    layers[to] = tmp;
}

Layer makeInitLayer() {
    Layer l;
    copyName(l.name, sizeof(l.name), "Notes");
    l.emitter.position = 0.0f;
    l.emitter.width = 0.0f;
    l.emitter.brightness = 0.0f;
    l.emitter.hue = 0.6f;
    l.env[0] = EnvParams{0.005f, 0.4f, 0.6f, 0.3f};
    l.addMod({SourceType::Note, 0}, Dest::Position, 1.0f);
    l.addMod({SourceType::Env1, 0}, Dest::Brightness, 1.0f);
    return l;
}

Preset makeInitPreset() {
    Preset p;
    copyName(p.name, sizeof(p.name), "Init");
    p.addLayer(makeInitLayer());
    return p;
}

const char* toString(TriggerMode m) { return enumName(m, kTriggerNames); }
const char* toString(GlideMode m) { return enumName(m, kGlideNames); }
const char* toString(BlendMode m) { return enumName(m, kBlendNames); }
const char* toString(LfoShape s) { return enumName(s, kShapeNames); }
const char* toString(Dest d) { return enumName(d, kDestNames); }
const char* sourceTypeName(SourceType t) { return enumName(t, kSourceNames); }
const char* displayName(Dest d) { return enumName(d, kDestLabels); }
const char* displayName(BlendMode m) { return enumName(m, kBlendLabels); }

const char* toString(ModSource s, char* buf, size_t cap) {
    if (s.type == SourceType::CC || s.type == SourceType::Macro) {
        // macros are 1-based in names: macro1..macro8
        int n = s.type == SourceType::Macro ? s.index + 1 : s.index;
        std::snprintf(buf, cap, "%s%d", sourceTypeName(s.type), n);
    } else {
        copyName(buf, cap, sourceTypeName(s.type));
    }
    return buf;
}

bool parseTriggerMode(const char* s, TriggerMode& out) { return parseEnum(s, kTriggerNames, out); }
bool parseGlideMode(const char* s, GlideMode& out) { return parseEnum(s, kGlideNames, out); }
bool parseBlendMode(const char* s, BlendMode& out) { return parseEnum(s, kBlendNames, out); }
bool parseLfoShape(const char* s, LfoShape& out) { return parseEnum(s, kShapeNames, out); }
bool parseDest(const char* s, Dest& out) { return parseEnum(s, kDestNames, out); }

bool parseModSource(const char* s, ModSource& out) {
    if (std::strncmp(s, "cc", 2) == 0) {
        int n;
        if (!parseUInt(s + 2, n) || n > 127) return false;
        out = {SourceType::CC, static_cast<uint8_t>(n)};
        return true;
    }
    if (std::strncmp(s, "macro", 5) == 0) {
        int n;
        if (!parseUInt(s + 5, n) || n < 1 || n > kNumMacros) return false;
        out = {SourceType::Macro, static_cast<uint8_t>(n - 1)};
        return true;
    }
    SourceType t;
    if (!parseEnum(s, kSourceNames, t) || t == SourceType::CC || t == SourceType::Macro) return false;
    out = {t, 0};
    return true;
}

bool isVoiceSource(SourceType t) {
    switch (t) {
        case SourceType::Note:
        case SourceType::Velocity:
        case SourceType::Aftertouch:
        case SourceType::Gate:
        case SourceType::Random:
        case SourceType::Env1:
        case SourceType::Env2:
        case SourceType::Lfo1:
        case SourceType::Lfo2:
        case SourceType::Lfo3: return true;
        default: return false;
    }
}

bool isBipolarSource(const Layer& layer, ModSource s) {
    switch (s.type) {
        case SourceType::Lfo1: return layer.lfo[0].bipolar;
        case SourceType::Lfo2: return layer.lfo[1].bipolar;
        case SourceType::Lfo3: return layer.lfo[2].bipolar;
        case SourceType::PitchBend: return true;
        default: return false;
    }
}

} // namespace mpx
