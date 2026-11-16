#pragma once
// JSON (de)serialization of presets and the output/gear configuration.
// See docs/PRESET_FORMAT.md.

#include <cstddef>

#include "midipixi/json.h"
#include "midipixi/output.h"
#include "midipixi/preset.h"

namespace mpx {

// Node budget that comfortably fits a full preset (8 layers, full matrices).
inline constexpr int kPresetJsonNodes = 2048;

void writePreset(const Preset& p, JsonWriter& w);
void writeConfig(const OutputConfig& c, JsonWriter& w);

// Reads a preset from an already parsed object node. Missing fields keep their
// defaults; unknown names (sources, destinations, modes) are errors.
bool readPreset(const JsonDoc& doc, int node, Preset& out, char* err, size_t errCap);
// Reads config fields present in the object node into `out` (partial updates allowed).
bool readConfig(const JsonDoc& doc, int node, OutputConfig& out, char* err, size_t errCap);

// Convenience: parse text with caller-provided node storage.
bool parsePreset(const char* text, size_t len, Preset& out, JsonNode* nodes, int nodeCap, char* err, size_t errCap);
bool parseConfig(const char* text, size_t len, OutputConfig& out, JsonNode* nodes, int nodeCap, char* err,
                 size_t errCap);

// Serializes into a fixed buffer; returns the length or 0 on overflow.
size_t presetToJson(const Preset& p, char* buf, size_t cap, bool pretty);
size_t configToJson(const OutputConfig& c, char* buf, size_t cap, bool pretty);

} // namespace mpx
