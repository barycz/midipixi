#include <cmath>
#include <memory>

#include "midipixi/engine.h"
#include "midipixi/output.h"
#include "mpx_test.h"
#include "test_util.h"

using namespace mpx;
using namespace mpxtest;

namespace {

constexpr int kN = 60;

std::unique_ptr<Engine> engineWith(const char* presetFile) {
    auto e = std::make_unique<Engine>();
    Preset p;
    std::string err;
    if (!loadPreset(readFile(presetPath(presetFile)), p, &err)) std::printf("    load %s: %s\n", presetFile, err.c_str());
    e->setPreset(p);
    e->setPixelCount(kN);
    return e;
}

void run(Engine& e, float seconds, float step = 1.0f / 120.0f) {
    for (float t = 0; t < seconds; t += step) e.advance(step);
    e.render();
}

float lum(const Rgb& c) { return maxChannel(c); }

float livePos(const Engine& e) { return e.liveValues(0)[static_cast<int>(Dest::Position)]; }

int brightest(const Engine& e) {
    int best = 0;
    for (int i = 1; i < e.pixelCount(); ++i)
        if (lum(e.pixels()[i]) > lum(e.pixels()[best])) best = i;
    return best;
}

int litCount(const Engine& e, float threshold = 0.01f) {
    int c = 0;
    for (int i = 0; i < e.pixelCount(); ++i) c += lum(e.pixels()[i]) > threshold ? 1 : 0;
    return c;
}

} // namespace

TEST_CASE("engine: note visualisation lights the LED of the note") {
    auto e = engineWith("note-visualisation.json");  // range 36..96
    e->render();
    CHECK(litCount(*e) == 0);
    e->noteOn(0, 36, 100);
    run(*e, 0.05f);
    CHECK(brightest(*e) == 0);
    CHECK(litCount(*e) <= 2);
    e->noteOff(0, 36);
    e->noteOn(0, 96, 100);
    run(*e, 0.05f);
    CHECK(brightest(*e) == kN - 1);
    e->noteOff(0, 96);
    run(*e, 1.0f);
    CHECK(litCount(*e) == 0);
    CHECK(e->activeVoices() == 0);
    // Middle of the range lands in the middle of the strip
    e->noteOn(0, 66, 100);
    run(*e, 0.05f);
    CHECK(std::abs(brightest(*e) - (kN - 1) / 2) <= 1);
}

TEST_CASE("engine: notes outside the key zone are ignored") {
    auto e = engineWith("note-visualisation.json");
    e->noteOn(0, 20, 100);
    run(*e, 0.05f);
    CHECK(e->activeVoices() == 0);
    CHECK(litCount(*e) == 0);
}

TEST_CASE("engine: strobe flashes the whole strip and decays") {
    auto e = engineWith("strobe.json");
    e->noteOn(0, 60, 127);
    run(*e, 0.01f);
    CHECK(litCount(*e, 0.5f) == kN);
    run(*e, 0.2f);  // decay 0.12 s, sustain 0
    CHECK(litCount(*e) == 0);
    CHECK(e->activeVoices() == 1);  // key still held
    e->noteOff(0, 60);
    run(*e, 0.1f);
    CHECK(e->activeVoices() == 0);
}

TEST_CASE("engine: ADSR timing") {
    Preset p;
    Layer l;
    l.emitter.brightness = 0;
    l.emitter.saturation = 0;
    l.emitter.width = 1;
    l.env[0] = EnvParams{0.1f, 0.1f, 0.5f, 0.2f};
    l.addMod({SourceType::Env1, 0}, Dest::Brightness, 1.0f);
    p.addLayer(l);
    Engine e;
    e.setPreset(p);
    e.setPixelCount(kN);
    e.noteOn(0, 60, 100);
    float dt = 0.001f;
    for (int i = 0; i < 50; ++i) e.advance(dt);
    e.render();
    CHECK_NEAR(lum(e.pixels()[30]), 0.5, 0.02);  // halfway through the attack
    for (int i = 0; i < 100; ++i) e.advance(dt);
    e.render();
    CHECK_NEAR(lum(e.pixels()[30]), 0.75, 0.02);  // halfway through the decay
    for (int i = 0; i < 100; ++i) e.advance(dt);
    e.render();
    CHECK_NEAR(lum(e.pixels()[30]), 0.5, 0.01);  // sustain
    e.noteOff(0, 60);
    for (int i = 0; i < 100; ++i) e.advance(dt);
    e.render();
    CHECK_NEAR(lum(e.pixels()[30]), 0.25, 0.02);  // halfway through the release
    for (int i = 0; i < 110; ++i) e.advance(dt);
    CHECK(e.activeVoices() == 0);
}

TEST_CASE("engine: laser beam starts at the edge and sweeps the strip") {
    auto e = engineWith("laser.json");
    e->noteOn(0, 60, 100);
    run(*e, 0.02f);
    int p0 = brightest(*e);
    run(*e, 0.25f);
    int p1 = brightest(*e);
    run(*e, 0.25f);
    int p2 = brightest(*e);
    CHECK(p0 <= 2);
    CHECK(p0 < p1);
    CHECK(p1 < p2);
    run(*e, 2.0f);  // the beam has left the strip
    CHECK(litCount(*e) == 0);
}

TEST_CASE("engine: a short laser note still crosses the whole strip") {
    auto e = engineWith("laser.json");
    e->noteOn(0, 60, 100);
    run(*e, 0.05f);
    e->noteOff(0, 60);
    int last = 0;
    for (int f = 0; f < 120; ++f) {
        run(*e, 1.0f / 60.0f);
        if (litCount(*e) > 0) last = brightest(*e);
    }
    CHECK(last == kN - 1);
}

TEST_CASE("engine: stacked layer sends a new laser shot on each repeat of a note") {
    auto e = engineWith("laser.json");  // stack: true
    e->noteOn(0, 60, 100);
    run(*e, 0.3f);
    e->noteOff(0, 60);
    e->noteOn(0, 60, 100);  // first shot is still releasing
    run(*e, 0.02f);
    CHECK(e->activeVoices(0) == 2);
    CHECK(lum(e->pixels()[0]) > 0.1f);  // new shot at the start
    int far = 0;
    for (int i = 10; i < kN; ++i) far += lum(e->pixels()[i]) > 0.05f ? 1 : 0;
    CHECK(far > 0);  // first shot kept going

    // Each note-off releases one held copy of the note (oldest first).
    e->noteOn(0, 60, 100);
    e->noteOff(0, 60);
    run(*e, 2.5f);  // release (2 s) keeps a short shot alive until it has left the strip
    CHECK(e->activeVoices(0) == 1);
    e->noteOff(0, 60);
    run(*e, 2.5f);
    CHECK(e->activeVoices(0) == 0);
}

TEST_CASE("engine: unstacked layer retriggers a repeated note") {
    Preset p = makeInitPreset();
    Engine e;
    e.setPreset(p);
    e.setPixelCount(kN);
    e.noteOn(0, 60, 100);
    e.noteOff(0, 60);
    e.noteOn(0, 60, 100);
    run(e, 0.01f);
    CHECK(e.activeVoices(0) == 1);
}

TEST_CASE("engine: stargate sends two accelerating stars out from the centre") {
    auto e = engineWith("stargate.json");
    e->noteOn(0, 60, 100);
    auto brightestIn = [&](int from, int to) {
        int best = from;
        for (int i = from; i < to; ++i)
            if (lum(e->pixels()[i]) > lum(e->pixels()[best])) best = i;
        return best;
    };
    int left[3], right[3];
    const float times[3] = {0.2f, 0.6f, 1.0f};
    e->noteOff(0, 60);  // short note: the stars keep going
    float t = 0.0f;
    for (int k = 0; k < 3; ++k) {
        while (t < times[k]) {  // render every frame, like the plugin and the gear do
            e->advance(1.0f / 120.0f);
            e->render();
            t += 1.0f / 120.0f;
        }
        left[k] = brightestIn(0, kN / 2);
        right[k] = brightestIn(kN / 2, kN);
        CHECK(std::abs(left[k] + right[k] - (kN - 1)) <= 1);  // mirrored around the centre
    }
    CHECK(right[0] < right[1]);
    CHECK(right[1] - right[0] < right[2] - right[1]);  // speeding up
    CHECK(left[0] > left[1]);
}

TEST_CASE("engine: a note-off before the next frame still plays an instant-attack note") {
    auto e = engineWith("laser.json");  // attack 0, release 2 s
    e->noteOn(0, 60, 100);
    e->noteOff(0, 60);
    run(*e, 0.1f);
    CHECK(e->activeVoices(0) == 1);
    CHECK(litCount(*e) > 0);
}

TEST_CASE("engine: one-shot LFO runs a single cycle per note, then holds") {
    Preset p = makeInitPreset();
    Layer& l = p.layers[0];
    l.emitter.position = 0.0f;
    l.emitter.width = 0.0f;
    l.lfo[0].shape = LfoShape::SawUp;
    l.lfo[0].rate = 1.0f;
    l.lfo[0].bipolar = false;
    l.lfo[0].oneShot = true;  // retrigger left off: one-shot implies per voice
    l.modCount = 0;
    l.addMod({SourceType::Lfo1, 0}, Dest::Position, 1.0f);
    Engine e;
    e.setPreset(p);
    e.setPixelCount(kN);
    e.noteOn(0, 60, 100);
    run(e, 0.5f);
    CHECK(std::abs(livePos(e) - 0.5f) < 0.05f);
    run(e, 1.0f);
    CHECK(std::abs(livePos(e) - 1.0f) < 0.001f);
    run(e, 1.0f);
    CHECK(std::abs(livePos(e) - 1.0f) < 0.001f);
    // A new note starts its own sweep from the beginning.
    e.noteOn(0, 64, 100);
    run(e, 0.1f);
    CHECK(livePos(e) < 0.2f);
}

TEST_CASE("engine: ambience drone runs without notes and drifts in hue") {
    auto e = engineWith("ambience.json");
    run(*e, 3.0f);
    CHECK(e->activeVoices() == 1);
    CHECK(litCount(*e) == kN);
    Hsv a = rgbToHsv(e->pixels()[30]);
    CHECK(a.v < 0.45f);  // calm brightness
    run(*e, 5.0f);
    Hsv b = rgbToHsv(e->pixels()[30]);
    CHECK(std::fabs(a.h - b.h) > 0.01f);
    e->noteOn(0, 60, 127);  // notes do nothing
    run(*e, 0.1f);
    CHECK(e->activeVoices() == 1);
    // macro 1 is mapped to CC 20 and raises brightness
    e->controlChange(0, 20, 127);
    run(*e, 0.5f);
    CHECK(rgbToHsv(e->pixels()[30]).v > 0.8f);
}

TEST_CASE("engine: voice stealing prefers released voices") {
    Preset p;
    Layer l = makeInitLayer();
    l.voices = 2;
    p.addLayer(l);
    Engine e;
    e.setPreset(p);
    e.noteOn(0, 40, 100);
    e.noteOn(0, 50, 100);
    e.noteOff(0, 40);
    e.advance(0.01f);
    e.noteOn(0, 60, 100);  // steals the released note 40, keeps held 50
    e.advance(0.01f);
    e.render();
    CHECK(e.activeVoices(0) == 2);
    e.noteOff(0, 50);
    e.noteOff(0, 60);
    run(e, 2.0f);
    CHECK(e.activeVoices(0) == 0);
}

namespace {

// One init layer (env1: a 5 ms, d 0.4 s, s 60 %, r 0.3 s) over the full key range,
// one LED per note.
Engine& monoEngine(Engine& e, TriggerMode mode, float glide = 0.0f, GlideMode gm = GlideMode::Time) {
    Preset p;
    Layer l = makeInitLayer();
    l.trigger = mode;
    l.noteLow = 0;
    l.noteHigh = 127;
    l.glide = glide;
    l.glideMode = gm;
    p.addLayer(l);
    e.setPreset(p);
    e.setPixelCount(128);
    return e;
}

}  // namespace

TEST_CASE("engine: mono and legato use one voice with last-note priority") {
    for (TriggerMode mode : {TriggerMode::Mono, TriggerMode::Legato}) {
        Engine e;
        monoEngine(e, mode);
        e.noteOn(0, 10, 100);
        run(e, 0.05f);
        CHECK(brightest(e) == 10);
        e.noteOn(0, 100, 100);
        run(e, 0.01f);
        CHECK(brightest(e) == 100);
        CHECK(e.activeVoices(0) == 1);
        e.noteOff(0, 100);
        run(e, 0.01f);
        CHECK(brightest(e) == 10);
        e.noteOff(0, 10);
        run(e, 2.0f);
        CHECK(e.activeVoices(0) == 0);
    }
}

TEST_CASE("engine: mono retriggers envelopes on every note, legato does not") {
    Engine mono, legato;
    monoEngine(mono, TriggerMode::Mono);
    monoEngine(legato, TriggerMode::Legato);
    for (Engine* e : {&mono, &legato}) {
        e->noteOn(0, 10, 100);
        run(*e, 1.0f);  // settled at sustain (60 %)
        e->noteOn(0, 100, 100);
        run(*e, 0.01f);
    }
    CHECK(lum(mono.pixels()[100]) > 0.9f);  // new attack
    CHECK(std::abs(lum(legato.pixels()[100]) - 0.6f) < 0.05f);  // still sustaining

    // Returning to a held note: mono attacks again, legato keeps going.
    for (Engine* e : {&mono, &legato}) {
        run(*e, 1.0f);
        e->noteOff(0, 100);
        run(*e, 0.01f);
    }
    CHECK(lum(mono.pixels()[10]) > 0.9f);
    CHECK(std::abs(lum(legato.pixels()[10]) - 0.6f) < 0.05f);

    // Legato: a note after all keys are released starts fresh.
    legato.noteOff(0, 10);
    run(legato, 0.05f);
    legato.noteOn(0, 50, 100);
    run(legato, 0.01f);
    CHECK(lum(legato.pixels()[50]) > 0.9f);
}

TEST_CASE("engine: legato glide moves the note source over time") {
    Engine e;
    monoEngine(e, TriggerMode::Legato, 1.0f);
    e.noteOn(0, 10, 100);
    run(e, 0.1f);
    e.noteOn(0, 100, 100);
    run(e, 0.5f);
    CHECK(std::abs(brightest(e) - 55) <= 2);  // halfway
    run(e, 0.6f);
    CHECK(brightest(e) == 100);

    // Per octave: 0.1 s per octave, 90 semitones = 0.75 s.
    Engine o;
    monoEngine(o, TriggerMode::Legato, 0.1f, GlideMode::Octave);
    o.noteOn(0, 10, 100);
    run(o, 0.1f);
    o.noteOn(0, 100, 100);
    run(o, 0.375f);
    CHECK(std::abs(brightest(o) - 55) <= 2);

    // No glide in mono mode, nor on a fresh legato note.
    Engine m;
    monoEngine(m, TriggerMode::Mono, 1.0f);
    m.noteOn(0, 10, 100);
    run(m, 0.1f);
    m.noteOn(0, 100, 100);
    run(m, 0.01f);
    CHECK(brightest(m) == 100);
}

TEST_CASE("engine: sustain pedal holds notes") {
    auto e = engineWith("note-visualisation.json");
    e->controlChange(0, 64, 127);
    e->noteOn(0, 60, 100);
    run(*e, 0.05f);
    e->noteOff(0, 60);
    run(*e, 2.0f);
    CHECK(e->activeVoices() == 1);
    e->controlChange(0, 64, 0);
    run(*e, 2.0f);
    CHECK(e->activeVoices() == 0);
}

TEST_CASE("engine: MIDI clock sets tempo and beat position") {
    Engine e;
    uint8_t start = 0xFA, tick = 0xF8;
    e.handleMidi(&start, 1);
    double interval = 60.0 / (100.0 * 24.0);  // 100 BPM
    for (int i = 0; i < 96; ++i) {
        e.advance(static_cast<float>(interval));
        e.handleMidi(&tick, 1);
    }
    CHECK_NEAR(e.bpm(), 100.0, 1.0);
    CHECK_NEAR(e.beats(), 4.0, 0.01);
    uint8_t stop = 0xFC;
    e.handleMidi(&stop, 1);
    CHECK(!e.transportPlaying());
}

TEST_CASE("engine: tempo-synced LFO follows host beats") {
    auto e = engineWith("rainbow-chase.json");  // saw, 4 beats per cycle -> position
    Preset p = e->preset();
    p.layers[0].emitter.width = 0;  // single LED makes the position unambiguous
    p.layers[0].emitter.softness = 0;
    e->setPreset(p);
    e->setHostTransport(120.0, 0.0, true);
    e->advance(0.001f);
    e->render();
    int a = brightest(*e);
    e->setHostTransport(120.0, 2.0, true);  // half a cycle later
    e->advance(0.001f);
    e->render();
    int b = brightest(*e);
    int d = std::abs(b - a);
    CHECK(std::abs(d - kN / 2) <= 2);
}

TEST_CASE("engine: blend modes") {
    auto render2 = [](BlendMode mode, float opacity) {
        Preset p;
        Layer base;
        base.trigger = TriggerMode::Drone;
        base.emitter = EmitterParams{0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f};  // red at 50%
        base.env[0] = EnvParams{0, 0, 1, 0};
        p.addLayer(base);
        Layer top = base;
        top.emitter.hue = 1.0f / 3.0f;  // green at 50%
        top.blend = mode;
        top.opacity = opacity;
        p.addLayer(top);
        Engine e;
        e.setPreset(p);
        e.setPixelCount(8);
        e.advance(0.01f);
        e.render();
        return e.pixels()[4];
    };
    Rgb add = render2(BlendMode::Add, 1);
    CHECK_NEAR(add.r, 0.5, 1e-3);
    CHECK_NEAR(add.g, 0.5, 1e-3);
    Rgb mul = render2(BlendMode::Multiply, 1);
    CHECK_NEAR(mul.r, 0.0, 1e-3);
    Rgb half = render2(BlendMode::Add, 0.5f);
    CHECK_NEAR(half.g, 0.25, 1e-3);
    Rgb normal = render2(BlendMode::Normal, 1);
    CHECK_NEAR(normal.r, 0.25, 1e-3);
    CHECK_NEAR(normal.g, 0.5, 1e-3);
    Rgb hue = render2(BlendMode::Hue, 1);  // colorize red with green hue, 50% mix (layer brightness)
    CHECK(hue.g > 0.2f);
    CHECK(hue.r < 0.3f);
    Rgb sub = render2(BlendMode::Subtract, 1);
    CHECK_NEAR(sub.r, 0.5, 1e-3);
    CHECK_NEAR(sub.g, 0.0, 1e-3);
}

TEST_CASE("engine: moving emitter is anti-aliased between LEDs") {
    Preset p;
    Layer l;
    l.trigger = TriggerMode::Drone;
    l.emitter.width = 0;
    l.emitter.saturation = 0;
    l.env[0] = EnvParams{0, 0, 1, 0};
    p.addLayer(l);
    Engine e;
    e.setPixelCount(11);
    p.layers[0].emitter.position = 0.45f;  // pixel 4.5
    e.setPreset(p);
    e.advance(0.01f);
    e.render();
    CHECK_NEAR(lum(e.pixels()[4]), 0.5, 1e-3);
    CHECK_NEAR(lum(e.pixels()[5]), 0.5, 1e-3);
    CHECK(lum(e.pixels()[3]) == 0.0f);
}

namespace {
Layer droneLayer(float position, float width) {
    Layer l;
    l.trigger = TriggerMode::Drone;
    l.emitter = EmitterParams{position, width, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};  // white
    l.env[0] = EnvParams{0, 0, 1, 0};
    return l;
}
} // namespace

TEST_CASE("engine: narrow layers blend where lit, masks cover the whole strip") {
    Preset p;
    p.addLayer(droneLayer(0.5f, 1.0f));  // white wash
    Layer mask = droneLayer(0.0f, 0.0f);  // keeps LED 0 only
    mask.blend = BlendMode::Mask;
    p.addLayer(mask);
    Layer spark = droneLayer(1.0f, 0.0f);  // red dot on LED 9
    spark.emitter.saturation = 1.0f;
    p.addLayer(spark);
    Engine e;
    e.setPixelCount(10);
    e.setPreset(p);
    e.advance(0.01f);
    e.render();
    CHECK_NEAR(lum(e.pixels()[0]), 1.0, 1e-4);
    CHECK(lum(e.pixels()[5]) == 0.0f);
    CHECK_NEAR(e.pixels()[9].r, 1.0, 1e-4);
    CHECK(e.pixels()[9].g == 0.0f);

    // Move the spark: nothing of it may stay behind in the layer buffer (the
    // mask below would otherwise let the wash through on LED 9).
    p.layers[2].emitter.position = 5.0f / 9.0f;
    e.setPreset(p, true);
    e.advance(0.01f);
    e.render();
    CHECK(lum(e.pixels()[9]) == 0.0f);
    CHECK_NEAR(e.pixels()[5].r, 1.0, 1e-4);
}

TEST_CASE("engine: wrapped emitter lights both strip ends") {
    Preset p;
    Layer l = droneLayer(0.0f, 0.2f);  // LED 0 and one either side
    l.wrap = true;
    p.addLayer(l);
    Engine e;
    e.setPixelCount(10);
    e.setPreset(p);
    e.advance(0.01f);
    e.render();
    CHECK_NEAR(lum(e.pixels()[9]), 1.0, 1e-4);
    CHECK_NEAR(lum(e.pixels()[0]), 1.0, 1e-4);
    CHECK_NEAR(lum(e.pixels()[1]), 1.0, 1e-4);
    CHECK(lum(e.pixels()[2]) == 0.0f);
    CHECK(lum(e.pixels()[8]) == 0.0f);
}

TEST_CASE("engine: layer-wide and per-voice mod rows add up") {
    Preset p;
    Layer l;
    l.emitter = EmitterParams{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    l.env[0] = EnvParams{0, 0, 1, 0};
    l.addMod(ModSource{SourceType::Macro, 0}, Dest::Position, 0.5f);  // same for every voice
    l.addMod(ModSource{SourceType::Velocity, 0}, Dest::Position, 0.5f);
    p.addLayer(l);
    Engine e;
    e.setPixelCount(11);
    e.setPreset(p);
    e.setMacro(0, 1.0f);
    e.advance(0.5f);
    e.noteOn(0, 60, 127);  // position 1 -> LED 10
    e.noteOn(0, 62, 1);    // position ~0.5 -> LED 5
    e.advance(0.01f);
    e.render();
    CHECK(e.activeVoices(0) == 2);
    CHECK_NEAR(lum(e.pixels()[10]), 1.0, 1e-4);
    CHECK(lum(e.pixels()[5]) > 0.9f);
    CHECK(lum(e.pixels()[0]) == 0.0f);
    CHECK_NEAR(livePos(e), 0.5f + 0.5f / 127.0f, 1e-4);  // newest voice
}

TEST_CASE("engine: smoothed CC settles exactly on its target") {
    Preset p;
    Layer l = droneLayer(0.5f, 1.0f);
    l.emitter.brightness = 0.0f;
    l.addMod(ModSource{SourceType::CC, 74}, Dest::Brightness, 1.0f);
    p.addLayer(l);
    Engine e;
    e.setPixelCount(10);
    e.setPreset(p);
    e.controlChange(0, 74, 127);
    run(e, 1.0f);
    CHECK(lum(e.pixels()[3]) == 1.0f);
    e.controlChange(0, 74, 0);
    run(e, 1.0f);
    CHECK(litCount(e, 0.0f) == 0);
}

TEST_CASE("engine: an LFO a live edit starts reading drives its row at once") {
    Preset p;
    Layer l = droneLayer(0.5f, 1.0f);
    l.emitter.brightness = 0.0f;
    l.lfo[0].shape = LfoShape::Square;  // phase 0: high
    l.lfo[0].rate = 0.0f;
    l.lfo[0].bipolar = false;
    p.addLayer(l);
    Engine e;
    e.setPixelCount(10);
    e.setPreset(p);
    e.advance(0.01f);
    e.render();
    CHECK(litCount(e) == 0);
    p.layers[0].addMod(ModSource{SourceType::Lfo1, 0}, Dest::Brightness, 1.0f);
    e.setPreset(p, true);
    e.render();  // no advance() in between
    CHECK_NEAR(lum(e.pixels()[3]), 1.0, 1e-4);
}

TEST_CASE("engine: live preset update keeps playing voices") {
    auto e = engineWith("note-visualisation.json");
    e->noteOn(0, 60, 100);
    run(*e, 0.05f);
    Preset p = e->preset();
    p.layers[0].emitter.hue = 0.0f;
    e->setPreset(p, true);
    run(*e, 0.01f);
    CHECK(e->activeVoices() == 1);
    p.layers[0].trigger = TriggerMode::Mono;  // trigger change resets the layer
    e->setPreset(p, true);
    CHECK(e->activeVoices() == 0);
}

TEST_CASE("output: gamma, color order, power limit and dithering") {
    OutputStage o;
    OutputConfig c;
    c.ledCount = 4;
    c.colorOrder = ColorOrder::GRB;
    c.gamma = 2.0f;
    c.dither = false;
    o.configure(c);
    Rgb in[4] = {{1, 0, 0}, {0, 1, 0}, {0.5f, 0.5f, 0.5f}, {0, 0, 0}};
    uint8_t out[12];
    o.process(in, 4, out);
    CHECK(out[0] == 0 && out[1] == 255 && out[2] == 0);  // red -> second byte in GRB
    CHECK(out[3] == 255 && out[4] == 0);                 // green -> first byte
    CHECK(std::abs(out[6] - 64) <= 1);                   // 0.5^2 * 255

    c.powerLimitMa = 0;
    c.maPerChannel = 20;
    c.idleMaPerLed = 0;
    o.configure(c);
    Rgb white[4] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
    o.process(white, 4, out);
    CHECK_NEAR(o.lastCurrentMa(), 240.0, 0.5);
    c.powerLimitMa = 120;
    o.configure(c);
    o.process(white, 4, out);
    CHECK_NEAR(o.lastCurrentMa(), 120.0, 0.5);
    CHECK_NEAR(o.lastLimiterScale(), 0.5, 0.01);
    CHECK(std::abs(out[0] - 128) <= 1);

    // Dithering: the average of many frames converges on the exact value.
    c.powerLimitMa = 0;
    c.dither = true;
    c.ditherLevels = 256;
    c.gamma = 1.0f;
    o.configure(c);
    Rgb dim[1] = {{0.0025f, 0, 0}};  // 0.6375 of one 8-bit step
    int sum = 0;
    const int frames = 400;
    for (int i = 0; i < frames; ++i) {
        o.process(dim, 1, out);
        sum += out[1];
    }
    CHECK_NEAR(static_cast<double>(sum) / frames, 0.6375, 0.02);
}

TEST_CASE("output: repeated emits dither one prepared frame, LEDs out of phase") {
    OutputStage o;
    OutputConfig c;
    c.ledCount = 60;
    c.gamma = 1.0f;
    c.colorOrder = ColorOrder::RGB;
    o.configure(c);
    Rgb dim[60];
    for (Rgb& p : dim) p = {0.001f, 0.001f, 0.001f};  // 0.255 of one 8-bit step -> 1/4 level
    o.prepare(dim, 60);
    CHECK(o.preparedCount() == 60);
    uint8_t out[180];
    int perLed[60] = {};
    const int refreshes = 200;
    for (int r = 0; r < refreshes; ++r) {
        o.emit(out);
        int lit = 0;
        for (int i = 0; i < 60; ++i) {
            CHECK(out[i * 3] == out[i * 3 + 1] && out[i * 3] == out[i * 3 + 2]);  // no color flicker
            lit += out[i * 3];
            perLed[i] += out[i * 3];
        }
        // About a quarter of the strip is on at any time, never all or nothing.
        CHECK(lit >= 10 && lit <= 20);
    }
    for (int i = 0; i < 60; ++i) CHECK(perLed[i] == 50);  // exactly 1 in 4 refreshes

    // With 4 levels no pattern is longer than 4 refreshes, at any level.
    for (float v : {0.0007f, 0.003f, 0.0045f, 0.0062f, 0.02f}) {
        Rgb px[1] = {{v, 0, 0}};
        o.prepare(px, 1);
        uint8_t seq[12][3];
        for (auto& s : seq) o.emit(s);
        for (int r = 0; r + 4 < 12; ++r) CHECK(seq[r][0] == seq[r + 4][0]);
    }
    // Below half a level the LED is off rather than blinking slowly.
    Rgb faint[1] = {{0.0004f, 0, 0}};  // 0.1 of a step
    o.prepare(faint, 1);
    for (int r = 0; r < 20; ++r) {
        o.emit(out);
        CHECK(out[0] == 0);
    }
    c.ditherLevels = 3;  // rounded down to a power of two
    o.configure(c);
    CHECK(o.config().ditherLevels == 2);

    // Without dithering every emit is the same rounded frame.
    c.dither = false;
    o.configure(c);
    Rgb half[1] = {{0.5f, 0, 0}};
    o.prepare(half, 1);
    uint8_t a[3], b[3];
    o.emit(a);
    o.emit(b);
    CHECK(a[0] == 128 && b[0] == 128);
}
