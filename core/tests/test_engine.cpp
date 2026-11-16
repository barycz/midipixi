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
    run(*e, 2.0f);  // one-shot LFO holds at the end of its cycle
    CHECK(brightest(*e) == kN - 1);
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
    run(*e, 1.0f);
    CHECK(e->activeVoices(0) == 1);
    e->noteOff(0, 60);
    run(*e, 1.0f);
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
    const float times[3] = {0.1f, 0.3f, 0.5f};
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

TEST_CASE("engine: mono layer is legato with last-note priority") {
    Preset p;
    Layer l = makeInitLayer();
    l.trigger = TriggerMode::Mono;
    l.noteLow = 0;
    l.noteHigh = 127;
    p.addLayer(l);
    Engine e;
    e.setPreset(p);
    e.setPixelCount(128);
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
