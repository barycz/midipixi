#pragma once
// Gear configuration protocol: newline-delimited JSON over the USB CDC serial
// port. See docs/PROTOCOL.md. The request handler lives in core so it can be
// unit-tested on the desktop; the gear supplies a GearBackend.

#include <cstddef>

#include "midipixi/json.h"
#include "midipixi/output.h"
#include "midipixi/preset.h"

namespace mpx {

inline constexpr size_t kMaxRequestLine = 32 * 1024;      // longest accepted request line
inline constexpr size_t kMaxStoredPresetJson = 16 * 1024 - 64;  // compact preset JSON stored per slot

class GearBackend {
public:
    virtual ~GearBackend() = default;

    virtual int slotCount() const = 0;
    virtual int activeSlot() const = 0;  // -1 when running a live (unsaved) preset
    // Stored compact preset JSON of a slot; false when the slot is empty.
    virtual bool readSlot(int slot, const char*& json, size_t& len) = 0;
    virtual bool writeSlot(int slot, const char* json, size_t len) = 0;
    virtual bool eraseSlot(int slot) = 0;
    // Loads the slot's preset, runs it and remembers it as the boot preset.
    virtual bool selectSlot(int slot) = 0;
    // Runs a preset without storing it (live editing from the VST).
    virtual void runPreset(const Preset& p) = 0;
    virtual OutputConfig config() const = 0;
    // Applies and persists the configuration.
    virtual bool setConfig(const OutputConfig& c) = 0;

    virtual const char* boardName() const { return "unknown"; }
    virtual void writeStats(JsonWriter& w) { (void)w; }
};

class GearServer {
public:
    // nodes: JSON node storage for requests (>= kPresetJsonNodes + 32 recommended)
    // scratchPreset: working Preset (kept off the stack, it is a few KB)
    // scratch: buffer for serializing presets (>= kMaxStoredPresetJson)
    GearServer(GearBackend& backend, JsonNode* nodes, int nodeCap, Preset& scratchPreset, char* scratch,
               size_t scratchCap)
        : backend_(backend), nodes_(nodes), nodeCap_(nodeCap), preset_(scratchPreset), scratch_(scratch),
          scratchCap_(scratchCap) {}

    // Handles one request line (without the trailing newline) and writes exactly
    // one response line (with trailing newline) to the sink.
    void handleLine(const char* line, size_t len, JsonWriter::Sink sink, void* ctx);

    // True when the last handled request asked for a reboot into the bootloader.
    bool bootloaderRequested() const { return bootloader_; }

private:
    bool slotArg(const JsonDoc& d, int req, int& slot, const char*& err) const;

    GearBackend& backend_;
    JsonNode* nodes_;
    int nodeCap_;
    Preset& preset_;
    char* scratch_;
    size_t scratchCap_;
    bool bootloader_ = false;
};

} // namespace mpx
