#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "midipixi/protocol.h"
#include "midipixi/preset_json.h"
#include "mpx_test.h"
#include "test_util.h"

using namespace mpx;
using namespace mpxtest;

namespace {

class FakeBackend : public GearBackend {
public:
    std::map<int, std::string> slots;
    int active = -1;
    OutputConfig cfg;
    Preset running;
    int liveCount = 0;

    int slotCount() const override { return 4; }
    int activeSlot() const override { return active; }
    bool readSlot(int slot, const char*& json, size_t& len) override {
        auto it = slots.find(slot);
        if (it == slots.end()) return false;
        json = it->second.data();
        len = it->second.size();
        return true;
    }
    bool writeSlot(int slot, const char* json, size_t len) override {
        slots[slot] = std::string(json, len);
        return true;
    }
    bool eraseSlot(int slot) override {
        slots.erase(slot);
        if (active == slot) active = -1;
        return true;
    }
    bool selectSlot(int slot) override {
        auto it = slots.find(slot);
        if (it == slots.end()) return false;
        if (!loadPreset(it->second, running)) return false;
        active = slot;
        return true;
    }
    void runPreset(const Preset& p) override {
        running = p;
        active = -1;
        ++liveCount;
    }
    OutputConfig config() const override { return cfg; }
    bool setConfig(const OutputConfig& c) override {
        cfg = c;
        return true;
    }
    const char* boardName() const override { return "fake"; }
};

struct Harness {
    FakeBackend backend;
    std::vector<JsonNode> nodes = std::vector<JsonNode>(kPresetJsonNodes + 64);
    std::unique_ptr<Preset> scratchPreset = std::make_unique<Preset>();
    std::vector<char> scratch = std::vector<char>(kMaxStoredPresetJson);
    GearServer server{backend, nodes.data(), static_cast<int>(nodes.size()), *scratchPreset, scratch.data(),
                      scratch.size()};

    std::string call(const std::string& line) {
        std::string out;
        server.handleLine(line.data(), line.size(),
                          [](void* ctx, const char* d, size_t n) { static_cast<std::string*>(ctx)->append(d, n); },
                          &out);
        return out;
    }
};

bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

} // namespace

TEST_CASE("protocol: hello and errors") {
    Harness h;
    std::string r = h.call(R"({"cmd":"hello","id":7})");
    CHECK(contains(r, "\"id\":7"));
    CHECK(contains(r, "\"ok\":true"));
    CHECK(contains(r, "\"board\":\"fake\""));
    CHECK(contains(r, "\"slots\":4"));
    CHECK(r.back() == '\n');
    CHECK(std::count(r.begin(), r.end(), '\n') == 1);
    CHECK(contains(h.call("not json"), "\"ok\":false"));
    CHECK(contains(h.call(R"({"cmd":"dance"})"), "unknown command"));
    CHECK(contains(h.call(R"({"cmd":"select","slot":9})"), "out of range"));
    CHECK(contains(h.call(R"({"cmd":"get","slot":1})"), "empty"));
}

TEST_CASE("protocol: put, list, get, select, delete") {
    Harness h;
    std::string preset = readFile(presetPath("strobe.json"));
    // presets travel on a single line
    for (char& c : preset)
        if (c == '\n' || c == '\r') c = ' ';
    std::string r = h.call(R"({"cmd":"put","slot":2,"select":true,"preset":)" + preset + "}\r\n");
    CHECK(contains(r, "\"ok\":true"));
    CHECK(h.backend.active == 2);
    CHECK(std::strcmp(h.backend.running.name, "Strobe") == 0);
    // Stored JSON is compact and re-parseable
    REQUIRE(h.backend.slots.count(2) == 1);
    CHECK(h.backend.slots[2].find('\n') == std::string::npos);

    r = h.call(R"({"cmd":"list"})");
    CHECK(contains(r, R"({"slot":2,"name":"Strobe"})"));
    CHECK(contains(r, "\"active\":2"));

    r = h.call(R"({"cmd":"get","slot":2})");
    CHECK(contains(r, "\"preset\":{\"format\":\"midipixi/1\""));

    CHECK(contains(h.call(R"({"cmd":"delete","slot":2})"), "\"ok\":true"));
    CHECK(h.backend.slots.empty());
    CHECK(contains(h.call(R"({"cmd":"select","slot":2})"), "\"ok\":false"));
}

TEST_CASE("protocol: invalid preset is rejected with a message") {
    Harness h;
    std::string r = h.call(R"({"cmd":"put","slot":0,"preset":{"layers":[{"blend":"sparkle"}]}})");
    CHECK(contains(r, "\"ok\":false"));
    CHECK(contains(r, "blend"));
    CHECK(h.backend.slots.empty());
}

TEST_CASE("protocol: live preset and config") {
    Harness h;
    CHECK(contains(h.call(R"({"cmd":"live","preset":{"name":"L","layers":[]}})"), "\"ok\":true"));
    CHECK(h.backend.liveCount == 1);
    CHECK(std::strcmp(h.backend.running.name, "L") == 0);

    std::string r = h.call(R"({"cmd":"config","set":{"ledCount":144,"colorOrder":"rgb","powerLimitMa":2000}})");
    CHECK(contains(r, "\"ledCount\":144"));
    CHECK(h.backend.cfg.ledCount == 144);
    CHECK(h.backend.cfg.colorOrder == ColorOrder::RGB);
    CHECK(h.backend.cfg.powerLimitMa == 2000);
    CHECK(h.backend.cfg.gamma > 2.0f);  // untouched fields keep their values
    CHECK(contains(h.call(R"({"cmd":"config","set":{"colorOrder":"xyz"}})"), "\"ok\":false"));
    CHECK(contains(h.call(R"({"cmd":"bootsel"})"), "\"ok\":true"));
    CHECK(h.server.bootloaderRequested());
}
