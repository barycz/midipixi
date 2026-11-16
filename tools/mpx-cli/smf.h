#pragma once
// Minimal Standard MIDI File (type 0/1) reader: returns channel and system
// real-time-free events with absolute times in seconds, honouring the tempo map.

#include <cstdint>
#include <string>
#include <vector>

namespace mpxcli {

struct TimedMidi {
    double time = 0.0;
    uint8_t data[3] = {};
    int len = 0;
};

bool readSmf(const std::string& path, std::vector<TimedMidi>& out, std::string& error);

// Built-in test patterns: "set" (kick on C1 + melody + CC1 sweep), "notes"
// (rising arpeggio across the keyboard), "kick" (C1 four on the floor).
bool demoPattern(const std::string& name, double seconds, std::vector<TimedMidi>& out);

} // namespace mpxcli
