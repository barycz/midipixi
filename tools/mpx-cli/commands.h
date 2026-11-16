#pragma once

#include <string>

#include "midipixi/preset.h"

namespace mpxcli {

bool loadPresetFile(const std::string& path, mpx::Preset& out, std::string& error);
std::string presetJson(const mpx::Preset& p, bool pretty);

// Gear commands (ports, list, upload, ...). Returns -1 when `cmd` is not a gear command.
int runGearCommand(const std::string& cmd, int argc, char** argv);
void printGearUsage();

} // namespace mpxcli
