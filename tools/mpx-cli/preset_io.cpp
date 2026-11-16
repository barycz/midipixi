#include <cstdio>
#include <vector>

#include "commands.h"
#include "midipixi/preset_json.h"

namespace mpxcli {

bool loadPresetFile(const std::string& path, mpx::Preset& out, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open file";
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    std::vector<mpx::JsonNode> nodes(mpx::kPresetJsonNodes);
    char err[160] = "";
    if (!mpx::parsePreset(text.data(), text.size(), out, nodes.data(), static_cast<int>(nodes.size()), err,
                          sizeof(err))) {
        error = err;
        return false;
    }
    return true;
}

std::string presetJson(const mpx::Preset& p, bool pretty) {
    std::vector<char> buf(128 * 1024);
    size_t n = mpx::presetToJson(p, buf.data(), buf.size(), pretty);
    return std::string(buf.data(), n);
}

} // namespace mpxcli
