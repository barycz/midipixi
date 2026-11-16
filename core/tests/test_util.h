#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "midipixi/preset_json.h"

namespace mpxtest {

inline std::string readFile(const std::string& path) {
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

inline std::string presetPath(const char* name) { return std::string(MPX_SOURCE_DIR) + "/presets/" + name; }

inline bool loadPreset(const std::string& text, mpx::Preset& p, std::string* error = nullptr) {
    std::vector<mpx::JsonNode> nodes(mpx::kPresetJsonNodes);
    char err[128] = "";
    bool ok = mpx::parsePreset(text.data(), text.size(), p, nodes.data(), static_cast<int>(nodes.size()), err, sizeof(err));
    if (error) *error = err;
    return ok;
}

inline std::string toJson(const mpx::Preset& p, bool pretty = true) {
    std::vector<char> buf(64 * 1024);
    size_t n = mpx::presetToJson(p, buf.data(), buf.size(), pretty);
    return std::string(buf.data(), n);
}

} // namespace mpxtest
