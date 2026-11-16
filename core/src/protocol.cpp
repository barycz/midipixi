#include "midipixi/protocol.h"

#include <cstring>

#include "midipixi/preset_json.h"
#include "midipixi/usb_ids.h"
#include "midipixi/version.h"

namespace mpx {

namespace {

struct Response {
    JsonWriter w;
    JsonWriter::Sink sink;
    void* ctx;
    bool hasId;
    double id;

    Response(JsonWriter::Sink s, void* c, bool hid, double i) : w(s, c, false), sink(s), ctx(c), hasId(hid), id(i) {}

    JsonWriter& begin(bool ok) {
        w.beginObject();
        if (hasId) w.field("id", id);
        w.field("ok", ok);
        return w;
    }
    void end() {
        w.endObject();
        sink(ctx, "\n", 1);
    }
    void error(const char* msg) {
        begin(false).field("error", msg);
        end();
    }
    void ok() {
        begin(true);
        end();
    }
};

} // namespace

bool GearServer::slotArg(const JsonDoc& d, int req, int& slot, const char*& err) const {
    int n = d.find(req, "slot");
    if (!d.isNumber(n)) {
        err = "missing 'slot'";
        return false;
    }
    double v = d.number(n, -1);
    slot = static_cast<int>(v);
    if (v != static_cast<double>(slot) || slot < 0 || slot >= backend_.slotCount()) {
        err = "slot out of range";
        return false;
    }
    return true;
}

void GearServer::handleLine(const char* line, size_t len, JsonWriter::Sink sink, void* ctx) {
    bootloader_ = false;
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) --len;

    JsonDoc d(nodes_, nodeCap_);
    if (!d.parse(line, len) || d.type(d.root()) != JsonType::Object) {
        Response r(sink, ctx, false, 0);
        r.error(d.error() ? d.error() : "request must be a JSON object");
        return;
    }
    const int req = d.root();
    int idNode = d.find(req, "id");
    Response r(sink, ctx, d.isNumber(idNode), d.number(idNode, 0));

    char cmd[16] = "";
    if (!d.getString(req, "cmd", cmd, sizeof(cmd))) {
        r.error("missing 'cmd'");
        return;
    }
    const char* err = nullptr;
    char msg[128];

    if (std::strcmp(cmd, "hello") == 0) {
        JsonWriter& w = r.begin(true);
        w.field("device", MPX_USB_PRODUCT);
        w.field("version", kVersion);
        w.field("protocol", kProtocolVersion);
        w.field("board", backend_.boardName());
        w.field("slots", backend_.slotCount());
        w.field("active", backend_.activeSlot());
        w.field("maxPixels", kMaxPixels);
        w.field("maxLayers", kMaxLayers);
        r.end();
        return;
    }

    if (std::strcmp(cmd, "list") == 0) {
        // The request document is no longer needed: node storage is reused to
        // parse each stored preset for its name.
        JsonWriter& w = r.begin(true);
        w.field("active", backend_.activeSlot());
        w.key("slots").beginArray();
        for (int s = 0; s < backend_.slotCount(); ++s) {
            const char* json;
            size_t jl;
            if (!backend_.readSlot(s, json, jl)) continue;
            JsonDoc pd(nodes_, nodeCap_);
            char name[kNameLen] = "?";
            if (pd.parse(json, jl)) pd.getString(pd.root(), "name", name, sizeof(name));
            w.beginObject().field("slot", s).field("name", static_cast<const char*>(name)).endObject();
        }
        w.endArray();
        r.end();
        return;
    }

    if (std::strcmp(cmd, "get") == 0) {
        int slot;
        if (!slotArg(d, req, slot, err)) return r.error(err);
        const char* json;
        size_t jl;
        if (!backend_.readSlot(slot, json, jl)) return r.error("slot is empty");
        JsonWriter& w = r.begin(true);
        w.field("slot", slot);
        w.key("preset").raw(json, jl);
        r.end();
        return;
    }

    if (std::strcmp(cmd, "put") == 0 || std::strcmp(cmd, "live") == 0) {
        bool live = cmd[0] == 'l';
        int slot = -1;
        if (!live && !slotArg(d, req, slot, err)) return r.error(err);
        int pn = d.find(req, "preset");
        if (pn < 0) return r.error("missing 'preset'");
        if (!readPreset(d, pn, preset_, msg, sizeof(msg))) return r.error(msg);
        if (live) {
            backend_.runPreset(preset_);
            return r.ok();
        }
        size_t n = presetToJson(preset_, scratch_, scratchCap_ < kMaxStoredPresetJson ? scratchCap_ : kMaxStoredPresetJson, false);
        if (n == 0) return r.error("preset too large");
        if (!backend_.writeSlot(slot, scratch_, n)) return r.error("flash write failed");
        if (d.getBool(req, "select", false) && !backend_.selectSlot(slot)) return r.error("select failed");
        JsonWriter& w = r.begin(true);
        w.field("slot", slot);
        w.field("bytes", static_cast<unsigned>(n));
        r.end();
        return;
    }

    if (std::strcmp(cmd, "delete") == 0) {
        int slot;
        if (!slotArg(d, req, slot, err)) return r.error(err);
        if (!backend_.eraseSlot(slot)) return r.error("flash erase failed");
        return r.ok();
    }

    if (std::strcmp(cmd, "select") == 0) {
        int slot;
        if (!slotArg(d, req, slot, err)) return r.error(err);
        if (!backend_.selectSlot(slot)) return r.error("slot is empty or invalid");
        return r.ok();
    }

    if (std::strcmp(cmd, "config") == 0) {
        int set = d.find(req, "set");
        OutputConfig c = backend_.config();
        if (set >= 0) {
            if (!readConfig(d, set, c, msg, sizeof(msg))) return r.error(msg);
            if (!backend_.setConfig(c)) return r.error("saving config failed");
            c = backend_.config();
        }
        JsonWriter& w = r.begin(true);
        w.key("config");
        writeConfig(c, w);
        r.end();
        return;
    }

    if (std::strcmp(cmd, "stats") == 0) {
        JsonWriter& w = r.begin(true);
        backend_.writeStats(w);
        r.end();
        return;
    }

    if (std::strcmp(cmd, "bootsel") == 0) {
        r.ok();
        bootloader_ = true;
        return;
    }

    std::strcpy(msg, "unknown command '");
    std::strncat(msg, cmd, sizeof(msg) - std::strlen(msg) - 2);
    std::strcat(msg, "'");
    r.error(msg);
}

} // namespace mpx
