// mpx-cli: command line companion for midipixi.
//
//   mpx-cli validate <preset.json>...
//   mpx-cli format <preset.json>                 (prints normalized JSON)
//   mpx-cli render <preset.json> (--midi <file.mid> | --demo set|notes|kick) -o <out.png>
//                  [--leds 60] [--fps 60] [--seconds 10] [--scale 8]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "midipixi/engine.h"
#include "midipixi/preset_json.h"
#include "midipixi/version.h"
#include "png.h"
#include "smf.h"
#include "commands.h"

namespace {

int usage() {
    std::printf(
        "mpx-cli %s - midipixi command line tool\n\n"
        "  mpx-cli validate <preset.json>...\n"
        "  mpx-cli format <preset.json>\n"
        "  mpx-cli render <preset.json> (--midi <file.mid> | --demo set|notes|kick) -o <out.png>\n"
        "                 [--leds 60] [--fps 60] [--seconds 10] [--scale 8]\n"
        "      Renders a 'waterfall' image: one row per frame (time flows down), one column per LED.\n",
        mpx::versionString());
    mpxcli::printGearUsage();
    return 2;
}

int cmdValidate(int argc, char** argv) {
    if (argc < 1) return usage();
    int failed = 0;
    for (int i = 0; i < argc; ++i) {
        mpx::Preset p;
        std::string err;
        if (!mpxcli::loadPresetFile(argv[i], p, err)) {
            std::printf("FAIL %s: %s\n", argv[i], err.c_str());
            ++failed;
        } else {
            std::printf("ok   %s: \"%s\", %d layer(s)\n", argv[i], p.name, p.layerCount);
        }
    }
    return failed ? 1 : 0;
}

int cmdFormat(int argc, char** argv) {
    if (argc != 1) return usage();
    mpx::Preset p;
    std::string err;
    if (!mpxcli::loadPresetFile(argv[0], p, err)) {
        std::fprintf(stderr, "%s: %s\n", argv[0], err.c_str());
        return 1;
    }
    std::printf("%s\n", mpxcli::presetJson(p, true).c_str());
    return 0;
}

int cmdRender(int argc, char** argv) {
    std::string presetPath, midiPath, demo, outPath;
    int leds = 60, fps = 60, scale = 8;
    double seconds = -1;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--midi")
            midiPath = next();
        else if (a == "--demo")
            demo = next();
        else if (a == "-o")
            outPath = next();
        else if (a == "--leds")
            leds = std::atoi(next());
        else if (a == "--fps")
            fps = std::atoi(next());
        else if (a == "--seconds")
            seconds = std::atof(next());
        else if (a == "--scale")
            scale = std::atoi(next());
        else if (presetPath.empty())
            presetPath = a;
        else
            return usage();
    }
    if (presetPath.empty() || outPath.empty() || (midiPath.empty() == demo.empty())) return usage();
    if (leds < 1 || leds > mpx::kMaxPixels || fps < 1 || fps > 1000 || scale < 1 || scale > 64) return usage();

    mpx::Preset p;
    std::string err;
    if (!mpxcli::loadPresetFile(presetPath, p, err)) {
        std::fprintf(stderr, "%s: %s\n", presetPath.c_str(), err.c_str());
        return 1;
    }
    std::vector<mpxcli::TimedMidi> events;
    if (!midiPath.empty()) {
        if (!mpxcli::readSmf(midiPath, events, err)) {
            std::fprintf(stderr, "%s: %s\n", midiPath.c_str(), err.c_str());
            return 1;
        }
        if (seconds < 0) seconds = (events.empty() ? 0.0 : events.back().time) + 1.0;
    } else {
        if (seconds < 0) seconds = 10;
        if (!mpxcli::demoPattern(demo, seconds, events)) {
            std::fprintf(stderr, "unknown demo pattern '%s'\n", demo.c_str());
            return 1;
        }
    }

    auto engine = std::make_unique<mpx::Engine>();
    engine->setPreset(p);
    engine->setPixelCount(leds);

    int frames = static_cast<int>(seconds * fps);
    if (frames < 1) frames = 1;
    if (frames > 20000) frames = 20000;
    int width = leds * scale;
    std::vector<uint8_t> img(static_cast<size_t>(width) * frames * 3);
    double now = 0.0;
    size_t ev = 0;
    for (int f = 0; f < frames; ++f) {
        double frameTime = static_cast<double>(f) / fps;
        while (ev < events.size() && events[ev].time <= frameTime) {
            engine->advance(static_cast<float>(events[ev].time - now));
            now = events[ev].time > now ? events[ev].time : now;
            engine->handleMidi(events[ev].data, events[ev].len);
            ++ev;
        }
        engine->advance(static_cast<float>(frameTime - now));
        now = frameTime;
        engine->render();
        const mpx::Rgb* px = engine->pixels();
        uint8_t* row = img.data() + static_cast<size_t>(f) * width * 3;
        for (int i = 0; i < leds; ++i) {
            uint8_t r = static_cast<uint8_t>(mpx::clamp01(px[i].r) * 255.0f + 0.5f);
            uint8_t g = static_cast<uint8_t>(mpx::clamp01(px[i].g) * 255.0f + 0.5f);
            uint8_t b = static_cast<uint8_t>(mpx::clamp01(px[i].b) * 255.0f + 0.5f);
            for (int s = 0; s < scale; ++s) {
                uint8_t* o = row + (static_cast<size_t>(i) * scale + s) * 3;
                // 1-px dark gap between LEDs for readability
                bool gap = scale >= 4 && s == scale - 1;
                o[0] = gap ? r / 4 : r;
                o[1] = gap ? g / 4 : g;
                o[2] = gap ? b / 4 : b;
            }
        }
    }
    if (!mpxcli::writePng(outPath, width, frames, img)) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("wrote %s (%d x %d: %d LEDs, %d frames at %d fps)\n", outPath.c_str(), width, frames, leds, frames, fps);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "validate") return cmdValidate(argc - 2, argv + 2);
    if (cmd == "format") return cmdFormat(argc - 2, argv + 2);
    if (cmd == "render") return cmdRender(argc - 2, argv + 2);
    if (cmd == "--version") {
        std::printf("%s\n", mpx::versionString());
        return 0;
    }
    int r = mpxcli::runGearCommand(cmd, argc - 2, argv + 2);
    return r >= 0 ? r : usage();
}
