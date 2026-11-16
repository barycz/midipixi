#include "midipixi/host/gear_client.h"

#include <vector>

#include "midipixi/json.h"
#include "midipixi/preset_json.h"

namespace mpx::host {

namespace {

void appendSink(void* ctx, const char* d, size_t n) { static_cast<std::string*>(ctx)->append(d, n); }

struct Parsed {
    std::vector<JsonNode> nodes = std::vector<JsonNode>(kPresetJsonNodes + 256);
    JsonDoc doc{nodes.data(), static_cast<int>(nodes.size())};
    bool parse(const std::string& s) { return doc.parse(s.data(), s.size()) && doc.type(doc.root()) == JsonType::Object; }
    int root() const { return doc.root(); }
};

std::string presetJsonLine(const Preset& p) {
    std::string s;
    JsonWriter w(appendSink, &s, false);
    writePreset(p, w);
    return s;
}

} // namespace

bool GearClient::connect(const std::string& port) {
    disconnect();
    std::string name = port;
    if (name.empty()) {
        for (const PortInfo& p : listSerialPorts()) {
            if (p.isGear) {
                name = p.name;
                break;
            }
        }
        if (name.empty()) {
            error_ = "no midipixi gear found (is it plugged in?)";
            return false;
        }
    }
    if (!port_.open(name, error_)) return false;
    portName_ = name;
    port_.flushInput();
    return true;
}

void GearClient::disconnect() {
    port_.close();
    portName_.clear();
}

bool GearClient::request(const std::string& json, std::string& response, int timeoutMs) {
    if (!port_.isOpen()) {
        error_ = "not connected";
        return false;
    }
    std::string line = json;
    line.push_back('\n');
    if (!port_.write(line.data(), line.size())) {
        error_ = "write failed (device unplugged?)";
        return false;
    }
    if (!port_.readLine(response, timeoutMs)) {
        error_ = "no response from gear";
        return false;
    }
    return true;
}

// Adds an id, sends, and checks "ok". Responses with a different id (stale
// answers to timed-out requests) are skipped.
bool GearClient::call(const std::string& json, std::string& response, int timeoutMs) {
    int id = nextId_++;
    std::string req = "{\"id\":" + std::to_string(id) + "," + json.substr(1);
    if (!request(req, response, timeoutMs)) return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        Parsed p;
        if (!p.parse(response)) {
            error_ = "invalid response";
            return false;
        }
        int idNode = p.doc.find(p.root(), "id");
        if (!p.doc.isNumber(idNode) || static_cast<int>(p.doc.number(idNode, 0)) == id) {
            if (!p.doc.getBool(p.root(), "ok", false)) {
                char msg[256] = "request failed";
                p.doc.getString(p.root(), "error", msg, sizeof(msg));
                error_ = msg;
                return false;
            }
            return true;
        }
        if (!port_.readLine(response, timeoutMs)) {
            error_ = "no response from gear";
            return false;
        }
    }
    error_ = "response id mismatch";
    return false;
}

bool GearClient::hello(GearInfo& info) {
    std::string r;
    if (!call(R"({"cmd":"hello"})", r)) return false;
    Parsed p;
    p.parse(r);
    char buf[64];
    if (p.doc.getString(p.root(), "device", buf, sizeof(buf))) info.device = buf;
    if (p.doc.getString(p.root(), "version", buf, sizeof(buf))) info.version = buf;
    if (p.doc.getString(p.root(), "board", buf, sizeof(buf))) info.board = buf;
    info.protocol = static_cast<int>(p.doc.getNumber(p.root(), "protocol", 0));
    info.slots = static_cast<int>(p.doc.getNumber(p.root(), "slots", 0));
    info.active = static_cast<int>(p.doc.getNumber(p.root(), "active", -1));
    return true;
}

bool GearClient::list(std::vector<SlotInfo>& slots, int& active) {
    std::string r;
    if (!call(R"({"cmd":"list"})", r, 10000)) return false;
    Parsed p;
    p.parse(r);
    slots.clear();
    active = static_cast<int>(p.doc.getNumber(p.root(), "active", -1));
    int arr = p.doc.find(p.root(), "slots");
    for (int s = p.doc.firstChild(arr); s >= 0; s = p.doc.next(s)) {
        SlotInfo si;
        si.slot = static_cast<int>(p.doc.getNumber(s, "slot", 0));
        char name[kNameLen] = "";
        p.doc.getString(s, "name", name, sizeof(name));
        si.name = name;
        slots.push_back(si);
    }
    return true;
}

bool GearClient::put(int slot, const Preset& preset, bool select) {
    std::string r;
    std::string req = "{\"cmd\":\"put\",\"slot\":" + std::to_string(slot) + ",\"select\":" + (select ? "true" : "false") +
                      ",\"preset\":" + presetJsonLine(preset) + "}";
    return call(req, r, 10000);  // flash writes take a while
}

bool GearClient::get(int slot, Preset& preset) {
    std::string r;
    if (!call("{\"cmd\":\"get\",\"slot\":" + std::to_string(slot) + "}", r)) return false;
    Parsed p;
    p.parse(r);
    char err[160] = "";
    if (!readPreset(p.doc, p.doc.find(p.root(), "preset"), preset, err, sizeof(err))) {
        error_ = err;
        return false;
    }
    return true;
}

bool GearClient::erase(int slot) {
    std::string r;
    return call("{\"cmd\":\"delete\",\"slot\":" + std::to_string(slot) + "}", r, 5000);
}

bool GearClient::select(int slot) {
    std::string r;
    return call("{\"cmd\":\"select\",\"slot\":" + std::to_string(slot) + "}", r, 5000);
}

bool GearClient::live(const Preset& preset) {
    std::string r;
    return call("{\"cmd\":\"live\",\"preset\":" + presetJsonLine(preset) + "}", r);
}

bool GearClient::getConfig(OutputConfig& c) {
    std::string r;
    if (!call(R"({"cmd":"config"})", r)) return false;
    Parsed p;
    p.parse(r);
    char err[160] = "";
    if (!readConfig(p.doc, p.doc.find(p.root(), "config"), c, err, sizeof(err))) {
        error_ = err;
        return false;
    }
    return true;
}

bool GearClient::setConfig(const OutputConfig& c) {
    std::string cfg;
    JsonWriter w(appendSink, &cfg, false);
    writeConfig(c, w);
    std::string r;
    return call("{\"cmd\":\"config\",\"set\":" + cfg + "}", r, 5000);
}

bool GearClient::stats(std::string& json) { return call(R"({"cmd":"stats"})", json); }

bool GearClient::bootsel() {
    std::string r;
    bool ok = call(R"({"cmd":"bootsel"})", r);
    disconnect();
    return ok;
}

} // namespace mpx::host
