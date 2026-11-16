#include <cstring>

#include "mpx_test.h"
#include "test_util.h"

using namespace mpx;
using namespace mpxtest;

TEST_CASE("preset: source and destination names round trip") {
    for (int t = 1; t < static_cast<int>(SourceType::Count); ++t) {
        ModSource s{static_cast<SourceType>(t), static_cast<uint8_t>(t == static_cast<int>(SourceType::CC) ? 74 : 2)};
        if (s.type != SourceType::CC && s.type != SourceType::Macro) s.index = 0;
        char buf[16];
        ModSource back;
        CHECK(parseModSource(toString(s, buf, sizeof(buf)), back));
        CHECK(back == s);
    }
    ModSource m;
    CHECK(parseModSource("macro1", m) && m.type == SourceType::Macro && m.index == 0);
    CHECK(parseModSource("cc127", m) && m.index == 127);
    CHECK(!parseModSource("cc128", m));
    CHECK(!parseModSource("macro0", m));
    CHECK(!parseModSource("bogus", m));
    for (int d = 0; d < kNumDests; ++d) {
        Dest back;
        CHECK(parseDest(toString(static_cast<Dest>(d)), back));
        CHECK(static_cast<int>(back) == d);
    }
}

TEST_CASE("preset: JSON round trip is lossless") {
    Preset p = makeInitPreset();
    Layer l = makeInitLayer();
    l.trigger = TriggerMode::Mono;
    l.blend = BlendMode::HueShift;
    l.channel = 10;
    l.noteLow = 40;
    l.noteHigh = 50;
    l.lfo[1].shape = LfoShape::SmoothRandom;
    l.lfo[1].sync = true;
    l.lfo[1].beats = 0.5f;
    l.addMod({SourceType::CC, 74}, Dest::Width, -0.25f, {SourceType::Velocity, 0});
    l.addMod({SourceType::Macro, 7}, Dest::Lfo2Rate, 0.5f);
    copyName(l.name, sizeof(l.name), "Ünïcode \"name\"");
    p.addLayer(l);
    p.macros[3].cc = 255;
    p.macros[3].value = 0.75f;
    std::string a = toJson(p);
    Preset q;
    std::string err;
    REQUIRE(loadPreset(a, q, &err));
    std::string b = toJson(q);
    CHECK(a == b);
    CHECK(q.layerCount == 2);
    CHECK(q.layers[1].mods[2].via.type == SourceType::Velocity);
    CHECK(q.macros[3].cc == 255);
    CHECK(std::strcmp(q.layers[1].name, "Ünïcode \"name\"") == 0);
}

TEST_CASE("preset: missing fields use defaults, bad names are errors") {
    Preset p;
    std::string err;
    REQUIRE(loadPreset(R"({"name":"x","layers":[{"emitter":{"hue":0.3}}]})", p, &err));
    CHECK(p.layerCount == 1);
    CHECK_NEAR(p.layers[0].emitter.hue, 0.3, 1e-6);
    CHECK_NEAR(p.layers[0].emitter.brightness, 1.0, 1e-6);

    CHECK(!loadPreset(R"({"layers":[{"blend":"glitter"}]})", p, &err));
    CHECK(err.find("blend") != std::string::npos);
    CHECK(!loadPreset(R"({"layers":[{"mods":[{"source":"lfo9","dest":"hue","amount":1}]}]})", p, &err));
    CHECK(!loadPreset(R"({"format":"midipixi/2","layers":[]})", p, &err));
    CHECK(!loadPreset(R"({"format":"other/1"})", p, &err));
    CHECK(!loadPreset("[]", p, &err));
}

TEST_CASE("preset: all factory presets load and round trip") {
    const char* files[] = {"note-visualisation.json", "laser.json",         "strobe.json",
                           "ambience.json",           "rainbow-chase.json", "live-set.json",
                           "stargate.json"};
    for (const char* f : files) {
        std::string text = readFile(presetPath(f));
        REQUIRE(!text.empty());
        Preset p;
        std::string err;
        bool ok = loadPreset(text, p, &err);
        if (!ok) std::printf("    %s: %s\n", f, err.c_str());
        CHECK(ok);
        CHECK(p.layerCount >= 1);
        Preset q;
        CHECK(loadPreset(toJson(p, false), q));
        CHECK(toJson(p) == toJson(q));
    }
}

TEST_CASE("preset: layer and mod row editing") {
    Preset p;
    for (int i = 0; i < kMaxLayers; ++i) CHECK(p.addLayer(makeInitLayer()) == i);
    CHECK(p.addLayer(Layer{}) == -1);
    copyName(p.layers[0].name, kNameLen, "first");
    p.moveLayer(0, 3);
    CHECK(std::strcmp(p.layers[3].name, "first") == 0);
    p.removeLayer(3);
    CHECK(p.layerCount == kMaxLayers - 1);

    Layer l;
    for (int i = 0; i < kMaxModRows; ++i) CHECK(l.addMod({SourceType::Env1, 0}, Dest::Hue, 0.1f * i) == i);
    CHECK(l.addMod({SourceType::Env1, 0}, Dest::Hue, 1) == -1);
    l.removeMod(0);
    CHECK(l.modCount == kMaxModRows - 1);
    CHECK_NEAR(l.mods[0].amount, 0.1, 1e-6);
}
