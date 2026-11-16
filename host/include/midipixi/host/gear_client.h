#pragma once
// Desktop client for the gear's configuration protocol (docs/PROTOCOL.md).
// Blocking; call from a background thread in UIs.

#include <string>
#include <vector>

#include "midipixi/host/serial_port.h"
#include "midipixi/output.h"
#include "midipixi/preset.h"

namespace mpx::host {

struct GearInfo {
    std::string device;
    std::string version;
    std::string board;
    int protocol = 0;
    int slots = 0;
    int active = -1;
};

struct SlotInfo {
    int slot = 0;
    std::string name;
};

class GearClient {
public:
    // Opens a port. Empty name: first port that matches the gear's VID/PID.
    bool connect(const std::string& port = {});
    void disconnect();
    bool isConnected() const { return port_.isOpen(); }
    const std::string& portName() const { return portName_; }
    const std::string& lastError() const { return error_; }

    bool hello(GearInfo& info);
    bool list(std::vector<SlotInfo>& slots, int& active);
    bool put(int slot, const Preset& p, bool select);
    bool get(int slot, Preset& p);
    bool erase(int slot);
    bool select(int slot);
    bool live(const Preset& p);
    bool getConfig(OutputConfig& c);
    bool setConfig(const OutputConfig& c);
    bool stats(std::string& json);
    bool bootsel();

    // Sends one raw request line, returns the raw response line.
    bool request(const std::string& json, std::string& response, int timeoutMs = 3000);

private:
    bool call(const std::string& json, std::string& response, int timeoutMs = 3000);

    SerialPort port_;
    std::string portName_;
    std::string error_;
    int nextId_ = 1;
};

} // namespace mpx::host
