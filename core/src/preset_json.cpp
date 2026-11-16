#include "midipixi/preset_json.h"

#include <cstdio>
#include <cstring>

#include "midipixi/color.h"
#include "midipixi/version.h"

namespace mpx {

namespace {

void setError(char* err, size_t cap, const char* fmt, const char* arg = "", const char* arg2 = "") {
    if (err && cap) std::snprintf(err, cap, fmt, arg, arg2);
}

float num(const JsonDoc& d, int obj, const char* key, float def) {
    return static_cast<float>(d.getNumber(obj, key, def));
}

int inum(const JsonDoc& d, int obj, const char* key, int def, int lo, int hi) {
    double v = d.getNumber(obj, key, def);
    int i = static_cast<int>(v + (v >= 0 ? 0.5 : -0.5));
    return i < lo ? lo : (i > hi ? hi : i);
}

void writeEmitter(const EmitterParams& e, JsonWriter& w) {
    w.beginObject();
    w.field("position", e.position);
    w.field("width", e.width);
    w.field("softness", e.softness);
    w.field("hue", e.hue);
    w.field("saturation", e.saturation);
    w.field("brightness", e.brightness);
    w.field("hueSpread", e.hueSpread);
    w.endObject();
}

void writeLayer(const Layer& l, JsonWriter& w) {
    char buf[24];
    w.beginObject();
    w.field("name", static_cast<const char*>(l.name));
    w.field("enabled", l.enabled);
    w.field("trigger", toString(l.trigger));
    w.field("channel", static_cast<int>(l.channel));
    w.key("noteRange").beginArray().value(static_cast<int>(l.noteLow)).value(static_cast<int>(l.noteHigh)).endArray();
    w.field("voices", static_cast<int>(l.voices));
    w.field("wrap", l.wrap);
    w.field("blend", toString(l.blend));
    w.field("opacity", l.opacity);
    w.key("emitter");
    writeEmitter(l.emitter, w);
    w.key("envs").beginArray();
    for (const EnvParams& e : l.env) {
        w.beginObject();
        w.field("attack", e.attack);
        w.field("decay", e.decay);
        w.field("sustain", e.sustain);
        w.field("release", e.release);
        w.endObject();
    }
    w.endArray();
    w.key("lfos").beginArray();
    for (const LfoParams& o : l.lfo) {
        w.beginObject();
        w.field("shape", toString(o.shape));
        w.field("rate", o.rate);
        w.field("sync", o.sync);
        w.field("beats", o.beats);
        w.field("retrigger", o.retrigger);
        if (o.oneShot) w.field("oneShot", true);
        w.field("phase", o.phase);
        w.field("bipolar", o.bipolar);
        w.endObject();
    }
    w.endArray();
    w.key("mods").beginArray();
    for (int i = 0; i < l.modCount; ++i) {
        const ModRow& m = l.mods[i];
        w.beginObject();
        w.field("source", static_cast<const char*>(toString(m.source, buf, sizeof(buf))));
        if (!m.via.isNone()) w.field("via", static_cast<const char*>(toString(m.via, buf, sizeof(buf))));
        w.field("dest", toString(m.dest));
        w.field("amount", m.amount);
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

bool readEnum(const JsonDoc& d, int obj, const char* key, char* err, size_t errCap, bool (*parse)(const char*, void*),
              void* out) {
    int n = d.find(obj, key);
    if (n < 0) return true;
    char s[32] = "";
    if (!d.string(n, s, sizeof(s)) || !parse(s, out)) {
        setError(err, errCap, "invalid value for '%s'", key);
        return false;
    }
    return true;
}

template <typename E, bool (*P)(const char*, E&)>
bool parseThunk(const char* s, void* out) {
    return P(s, *static_cast<E*>(out));
}

bool readLayer(const JsonDoc& d, int obj, Layer& l, char* err, size_t errCap) {
    if (d.type(obj) != JsonType::Object) {
        setError(err, errCap, "layer must be an object");
        return false;
    }
    d.getString(obj, "name", l.name, sizeof(l.name));
    l.enabled = d.getBool(obj, "enabled", l.enabled);
    if (!readEnum(d, obj, "trigger", err, errCap, parseThunk<TriggerMode, parseTriggerMode>, &l.trigger)) return false;
    if (!readEnum(d, obj, "blend", err, errCap, parseThunk<BlendMode, parseBlendMode>, &l.blend)) return false;
    l.channel = static_cast<uint8_t>(inum(d, obj, "channel", l.channel, 0, 16));
    int range = d.find(obj, "noteRange");
    if (range >= 0) {
        int lo = static_cast<int>(d.number(d.at(range, 0), l.noteLow));
        int hi = static_cast<int>(d.number(d.at(range, 1), l.noteHigh));
        lo = lo < 0 ? 0 : (lo > 127 ? 127 : lo);
        hi = hi < 0 ? 0 : (hi > 127 ? 127 : hi);
        if (hi < lo) {
            int t = lo;
            lo = hi;
            hi = t;
        }
        l.noteLow = static_cast<uint8_t>(lo);
        l.noteHigh = static_cast<uint8_t>(hi);
    }
    l.voices = static_cast<uint8_t>(inum(d, obj, "voices", l.voices, 1, kMaxVoices));
    l.wrap = d.getBool(obj, "wrap", l.wrap);
    l.opacity = num(d, obj, "opacity", l.opacity);

    int em = d.find(obj, "emitter");
    if (em >= 0) {
        EmitterParams& e = l.emitter;
        e.position = num(d, em, "position", e.position);
        e.width = num(d, em, "width", e.width);
        e.softness = num(d, em, "softness", e.softness);
        e.hue = num(d, em, "hue", e.hue);
        e.saturation = num(d, em, "saturation", e.saturation);
        e.brightness = num(d, em, "brightness", e.brightness);
        e.hueSpread = num(d, em, "hueSpread", e.hueSpread);
    }

    int envs = d.find(obj, "envs");
    for (int i = 0; i < kNumEnvs; ++i) {
        int en = d.at(envs, i);
        if (en < 0) break;
        EnvParams& e = l.env[i];
        e.attack = num(d, en, "attack", e.attack);
        e.decay = num(d, en, "decay", e.decay);
        e.sustain = num(d, en, "sustain", e.sustain);
        e.release = num(d, en, "release", e.release);
    }

    int lfos = d.find(obj, "lfos");
    for (int i = 0; i < kNumLfos; ++i) {
        int ln = d.at(lfos, i);
        if (ln < 0) break;
        LfoParams& o = l.lfo[i];
        if (!readEnum(d, ln, "shape", err, errCap, parseThunk<LfoShape, parseLfoShape>, &o.shape)) return false;
        o.rate = num(d, ln, "rate", o.rate);
        o.sync = d.getBool(ln, "sync", o.sync);
        o.beats = num(d, ln, "beats", o.beats);
        o.retrigger = d.getBool(ln, "retrigger", o.retrigger);
        o.oneShot = d.getBool(ln, "oneShot", o.oneShot);
        o.phase = num(d, ln, "phase", o.phase);
        o.bipolar = d.getBool(ln, "bipolar", o.bipolar);
    }

    l.modCount = 0;
    int mods = d.find(obj, "mods");
    for (int m = d.firstChild(mods); m >= 0; m = d.next(m)) {
        if (l.modCount >= kMaxModRows) {
            setError(err, errCap, "too many modulation rows (max %s)", "24");
            return false;
        }
        char s[32] = "";
        ModRow row;
        if (!d.getString(m, "source", s, sizeof(s)) || !parseModSource(s, row.source)) {
            setError(err, errCap, "invalid modulation source '%s'", s);
            return false;
        }
        s[0] = 0;
        if (d.getString(m, "via", s, sizeof(s)) && !parseModSource(s, row.via)) {
            setError(err, errCap, "invalid modulation via '%s'", s);
            return false;
        }
        s[0] = 0;
        if (!d.getString(m, "dest", s, sizeof(s)) || !parseDest(s, row.dest)) {
            setError(err, errCap, "invalid modulation destination '%s'", s);
            return false;
        }
        row.amount = num(d, m, "amount", 0.0f);
        l.mods[l.modCount++] = row;
    }
    return true;
}

} // namespace

void writePreset(const Preset& p, JsonWriter& w) {
    w.beginObject();
    w.field("format", kPresetFormat);
    w.field("name", static_cast<const char*>(p.name));
    if (p.author[0]) w.field("author", static_cast<const char*>(p.author));
    w.key("macros").beginArray();
    for (const Macro& m : p.macros) {
        w.beginObject();
        w.field("name", static_cast<const char*>(m.name));
        w.field("value", m.value);
        if (m.cc <= 127)
            w.field("cc", static_cast<int>(m.cc));
        else
            w.key("cc").null();
        w.endObject();
    }
    w.endArray();
    w.key("layers").beginArray();
    for (int i = 0; i < p.layerCount; ++i) writeLayer(p.layers[i], w);
    w.endArray();
    w.endObject();
}

bool readPreset(const JsonDoc& d, int node, Preset& out, char* err, size_t errCap) {
    if (d.type(node) != JsonType::Object) {
        setError(err, errCap, "preset must be a JSON object");
        return false;
    }
    char fmt[32] = "";
    if (d.getString(node, "format", fmt, sizeof(fmt))) {
        if (std::strncmp(fmt, "midipixi/", 9) != 0) {
            setError(err, errCap, "not a midipixi preset (format '%s')", fmt);
            return false;
        }
        if (std::strcmp(fmt, kPresetFormat) != 0) {
            setError(err, errCap, "unsupported preset format '%s' (this build reads %s)", fmt, kPresetFormat);
            return false;
        }
    }
    Preset p;
    d.getString(node, "name", p.name, sizeof(p.name));
    d.getString(node, "author", p.author, sizeof(p.author));

    int macros = d.find(node, "macros");
    for (int i = 0; i < kNumMacros; ++i) {
        int m = d.at(macros, i);
        if (m < 0) break;
        d.getString(m, "name", p.macros[i].name, sizeof(p.macros[i].name));
        p.macros[i].value = clamp01(num(d, m, "value", p.macros[i].value));
        int cc = d.find(m, "cc");
        if (cc >= 0) {
            if (d.type(cc) == JsonType::Null)
                p.macros[i].cc = 255;
            else
                p.macros[i].cc = static_cast<uint8_t>(inum(d, m, "cc", 255, 0, 127));
        }
    }

    int layers = d.find(node, "layers");
    for (int ln = d.firstChild(layers); ln >= 0; ln = d.next(ln)) {
        if (p.layerCount >= kMaxLayers) {
            setError(err, errCap, "too many layers (max %s)", "8");
            return false;
        }
        Layer l;
        if (!readLayer(d, ln, l, err, errCap)) return false;
        p.layers[p.layerCount++] = l;
    }
    out = p;
    return true;
}

void writeConfig(const OutputConfig& c, JsonWriter& w) {
    w.beginObject();
    w.field("format", kConfigFormat);
    w.field("ledCount", static_cast<int>(c.ledCount));
    w.field("colorOrder", toString(c.colorOrder));
    w.field("gamma", c.gamma);
    w.field("brightness", c.brightness);
    w.field("powerLimitMa", static_cast<unsigned>(c.powerLimitMa));
    w.field("maPerChannel", c.maPerChannel);
    w.field("idleMaPerLed", c.idleMaPerLed);
    w.field("dither", c.dither);
    w.field("reverse", c.reverse);
    w.field("fps", static_cast<int>(c.fps));
    w.field("dataPin", static_cast<int>(c.dataPin));
    w.endObject();
}

bool readConfig(const JsonDoc& d, int node, OutputConfig& out, char* err, size_t errCap) {
    if (d.type(node) != JsonType::Object) {
        setError(err, errCap, "config must be a JSON object");
        return false;
    }
    OutputConfig c = out;
    c.ledCount = static_cast<uint16_t>(inum(d, node, "ledCount", c.ledCount, 1, kMaxPixels));
    if (!readEnum(d, node, "colorOrder", err, errCap, parseThunk<ColorOrder, parseColorOrder>, &c.colorOrder))
        return false;
    c.gamma = num(d, node, "gamma", c.gamma);
    c.brightness = num(d, node, "brightness", c.brightness);
    c.powerLimitMa = static_cast<uint32_t>(inum(d, node, "powerLimitMa", static_cast<int>(c.powerLimitMa), 0, 1000000));
    c.maPerChannel = num(d, node, "maPerChannel", c.maPerChannel);
    c.idleMaPerLed = num(d, node, "idleMaPerLed", c.idleMaPerLed);
    c.dither = d.getBool(node, "dither", c.dither);
    c.reverse = d.getBool(node, "reverse", c.reverse);
    c.fps = static_cast<uint16_t>(inum(d, node, "fps", c.fps, 10, 200));
    c.dataPin = static_cast<uint8_t>(inum(d, node, "dataPin", c.dataPin, 0, 29));
    c.sanitize();
    out = c;
    return true;
}

bool parsePreset(const char* text, size_t len, Preset& out, JsonNode* nodes, int nodeCap, char* err, size_t errCap) {
    JsonDoc doc(nodes, nodeCap);
    if (!doc.parse(text, len)) {
        if (err && errCap) std::snprintf(err, errCap, "JSON error at offset %u: %s", static_cast<unsigned>(doc.errorOffset()), doc.error());
        return false;
    }
    return readPreset(doc, doc.root(), out, err, errCap);
}

bool parseConfig(const char* text, size_t len, OutputConfig& out, JsonNode* nodes, int nodeCap, char* err,
                 size_t errCap) {
    JsonDoc doc(nodes, nodeCap);
    if (!doc.parse(text, len)) {
        if (err && errCap) std::snprintf(err, errCap, "JSON error at offset %u: %s", static_cast<unsigned>(doc.errorOffset()), doc.error());
        return false;
    }
    return readConfig(doc, doc.root(), out, err, errCap);
}

size_t presetToJson(const Preset& p, char* buf, size_t cap, bool pretty) {
    FixedJsonSink sink(buf, cap);
    JsonWriter w(&FixedJsonSink::write, &sink, pretty);
    writePreset(p, w);
    return sink.overflow ? 0 : sink.len;
}

size_t configToJson(const OutputConfig& c, char* buf, size_t cap, bool pretty) {
    FixedJsonSink sink(buf, cap);
    JsonWriter w(&FixedJsonSink::write, &sink, pretty);
    writeConfig(c, w);
    return sink.overflow ? 0 : sink.len;
}

} // namespace mpx
