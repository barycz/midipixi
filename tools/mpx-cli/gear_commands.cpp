#include "commands.h"

namespace mpxcli {

// Gear communication arrives with the gear firmware (phase 3).
int runGearCommand(const std::string&, int, char**) { return -1; }
void printGearUsage() {}

} // namespace mpxcli
