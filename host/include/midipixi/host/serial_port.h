#pragma once
// Minimal blocking serial port for talking to the gear's CDC config port.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mpx::host {

struct PortInfo {
    std::string name;         // "COM5" or "/dev/ttyACM0"
    std::string description;  // OS friendly name
    uint16_t vid = 0;
    uint16_t pid = 0;
    bool isGear = false;      // matches the midipixi gear VID/PID
};

// Lists serial ports, gear ports first.
std::vector<PortInfo> listSerialPorts();

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const std::string& name, std::string& error);
    void close();
    bool isOpen() const;

    bool write(const char* data, size_t len);
    // Reads one line (without '\n'). Returns false on timeout or error.
    bool readLine(std::string& line, int timeoutMs);
    // Drops any pending input.
    void flushInput();

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    std::string pending_;
};

} // namespace mpx::host
