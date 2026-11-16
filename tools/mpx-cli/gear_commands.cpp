#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "commands.h"
#include "midipixi/host/gear_client.h"
#include "midipixi/json.h"
#include "midipixi/preset_json.h"

namespace mpxcli {

namespace {

using mpx::host::GearClient;

// Extracts "--port X" from the argument list.
std::vector<std::string> parseArgs(int argc, char** argv, std::string& port) {
    std::vector<std::string> rest;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = argv[++i];
        else
            rest.emplace_back(argv[i]);
    }
    return rest;
}

bool connect(GearClient& c, const std::string& port) {
    if (c.connect(port)) return true;
    std::fprintf(stderr, "error: %s\n", c.lastError().c_str());
    return false;
}

int fail(const GearClient& c) {
    std::fprintf(stderr, "error: %s\n", c.lastError().c_str());
    return 1;
}

bool parseSlot(const std::string& s, int& slot) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end || v < 0 || v > 127) {
        std::fprintf(stderr, "error: invalid slot '%s'\n", s.c_str());
        return false;
    }
    slot = static_cast<int>(v);
    return true;
}

} // namespace

void printGearUsage() {
    std::printf(
        "\nGear commands (auto-detect the gear unless --port COMx is given):\n"
        "  mpx-cli ports                          list serial ports\n"
        "  mpx-cli info                           device info and render statistics\n"
        "  mpx-cli list                           list stored presets\n"
        "  mpx-cli upload <slot> <preset.json> [--select]\n"
        "  mpx-cli download <slot> <out.json>\n"
        "  mpx-cli select <slot>                  run a slot and make it the boot preset\n"
        "  mpx-cli erase <slot>\n"
        "  mpx-cli live <preset.json>             run a preset without storing it\n"
        "  mpx-cli config [key=value ...]         show or change the LED configuration\n"
        "                                         e.g. ledCount=60 colorOrder=grb powerLimitMa=2000\n"
        "  mpx-cli send '<json>'                  send a raw protocol request\n"
        "  mpx-cli bootsel                        reboot the gear into the USB bootloader\n");
}

int runGearCommand(const std::string& cmd, int argc, char** argv) {
    std::string port;
    std::vector<std::string> a = parseArgs(argc, argv, port);
    GearClient c;

    if (cmd == "ports") {
        auto ports = mpx::host::listSerialPorts();
        if (ports.empty()) std::printf("no serial ports\n");
        for (const auto& p : ports)
            std::printf("%-14s %04x:%04x  %s%s\n", p.name.c_str(), p.vid, p.pid, p.description.c_str(),
                        p.isGear ? "  <- midipixi gear" : "");
        return 0;
    }
    if (cmd == "info") {
        if (!connect(c, port)) return 1;
        mpx::host::GearInfo info;
        if (!c.hello(info)) return fail(c);
        std::printf("%s %s on %s (%s), protocol %d, %d slots, active slot %d\n", info.device.c_str(),
                    info.version.c_str(), c.portName().c_str(), info.board.c_str(), info.protocol, info.slots,
                    info.active);
        mpx::OutputConfig cfg;
        if (c.getConfig(cfg))
            std::printf("strip: %d LEDs, %s, %d fps, gamma %.2f, brightness %.2f, power limit %u mA\n", cfg.ledCount,
                        mpx::toString(cfg.colorOrder), cfg.fps, static_cast<double>(cfg.gamma),
                        static_cast<double>(cfg.brightness), static_cast<unsigned>(cfg.powerLimitMa));
        std::string stats;
        if (c.stats(stats)) std::printf("stats: %s\n", stats.c_str());
        return 0;
    }
    if (cmd == "list") {
        if (!connect(c, port)) return 1;
        std::vector<mpx::host::SlotInfo> slots;
        int active = -1;
        if (!c.list(slots, active)) return fail(c);
        if (slots.empty()) std::printf("no presets stored\n");
        for (const auto& s : slots) std::printf("%c %2d  %s\n", s.slot == active ? '*' : ' ', s.slot, s.name.c_str());
        return 0;
    }
    if (cmd == "upload") {
        if (a.size() < 2) return -1;
        int slot;
        if (!parseSlot(a[0], slot)) return 1;
        bool sel = a.size() > 2 && a[2] == "--select";
        mpx::Preset p;
        std::string err;
        if (!loadPresetFile(a[1], p, err)) {
            std::fprintf(stderr, "%s: %s\n", a[1].c_str(), err.c_str());
            return 1;
        }
        if (!connect(c, port)) return 1;
        if (!c.put(slot, p, sel)) return fail(c);
        std::printf("uploaded \"%s\" to slot %d%s\n", p.name, slot, sel ? " (selected)" : "");
        return 0;
    }
    if (cmd == "download") {
        if (a.size() != 2) return -1;
        int slot;
        if (!parseSlot(a[0], slot)) return 1;
        if (!connect(c, port)) return 1;
        mpx::Preset p;
        if (!c.get(slot, p)) return fail(c);
        FILE* f = std::fopen(a[1].c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", a[1].c_str());
            return 1;
        }
        std::string json = presetJson(p, true) + "\n";
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
        std::printf("saved slot %d (\"%s\") to %s\n", slot, p.name, a[1].c_str());
        return 0;
    }
    if (cmd == "select" || cmd == "erase") {
        if (a.size() != 1) return -1;
        int slot;
        if (!parseSlot(a[0], slot)) return 1;
        if (!connect(c, port)) return 1;
        bool ok = cmd == "select" ? c.select(slot) : c.erase(slot);
        if (!ok) return fail(c);
        std::printf("%s slot %d\n", cmd == "select" ? "selected" : "erased", slot);
        return 0;
    }
    if (cmd == "live") {
        if (a.size() != 1) return -1;
        mpx::Preset p;
        std::string err;
        if (!loadPresetFile(a[0], p, err)) {
            std::fprintf(stderr, "%s: %s\n", a[0].c_str(), err.c_str());
            return 1;
        }
        if (!connect(c, port)) return 1;
        if (!c.live(p)) return fail(c);
        std::printf("running \"%s\" (not stored)\n", p.name);
        return 0;
    }
    if (cmd == "config") {
        if (!connect(c, port)) return 1;
        std::string req = "{\"cmd\":\"config\"";
        if (!a.empty()) {
            // key=value pairs: numbers and true/false pass through, anything else becomes a string
            req += ",\"set\":{";
            for (size_t i = 0; i < a.size(); ++i) {
                size_t eq = a[i].find('=');
                if (eq == std::string::npos) return -1;
                std::string k = a[i].substr(0, eq), v = a[i].substr(eq + 1);
                char* end = nullptr;
                std::strtod(v.c_str(), &end);
                bool literal = (!v.empty() && end && *end == 0) || v == "true" || v == "false";
                req += (i ? "," : "") + std::string("\"") + k + "\":" + (literal ? v : "\"" + v + "\"");
            }
            req += "}";
        }
        req += "}";
        std::string r;
        if (!c.request(req, r)) return fail(c);
        std::printf("%s\n", r.c_str());
        return r.find("\"ok\":true") != std::string::npos ? 0 : 1;
    }
    if (cmd == "send") {
        if (a.size() != 1) return -1;
        if (!connect(c, port)) return 1;
        std::string r;
        if (!c.request(a[0], r, 10000)) return fail(c);
        std::printf("%s\n", r.c_str());
        return 0;
    }
    if (cmd == "bootsel") {
        if (!connect(c, port)) return 1;
        if (!c.bootsel()) return fail(c);
        std::printf("gear rebooted into the USB bootloader: copy midipixi_gear.uf2 to the RP2350/RPI-RP2 drive\n");
        return 0;
    }
    return -1;
}

} // namespace mpxcli
