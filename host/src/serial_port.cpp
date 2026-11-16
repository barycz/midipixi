#include "midipixi/host/serial_port.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "midipixi/usb_ids.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
// clang-format on
#else
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace mpx::host {

namespace {

// Parses "VID_1209&PID_0001" style hardware ids (Windows) or hex strings.
uint16_t hexAfter(const std::string& s, const char* key) {
    size_t p = s.find(key);
    if (p == std::string::npos) return 0;
    return static_cast<uint16_t>(std::strtoul(s.c_str() + p + std::strlen(key), nullptr, 16));
}

void sortPorts(std::vector<PortInfo>& ports) {
    std::stable_sort(ports.begin(), ports.end(), [](const PortInfo& a, const PortInfo& b) {
        if (a.isGear != b.isGear) return a.isGear;
        return a.name < b.name;
    });
}

} // namespace

#ifdef _WIN32

std::vector<PortInfo> listSerialPorts() {
    std::vector<PortInfo> ports;
    HDEVINFO set = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return ports;
    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        char buf[512] = {};
        PortInfo p;
        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE) {
            DWORD size = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExA(key, "PortName", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS)
                p.name = buf;
            RegCloseKey(key);
        }
        if (p.name.rfind("COM", 0) != 0) continue;
        if (SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_FRIENDLYNAME, nullptr, reinterpret_cast<BYTE*>(buf),
                                              sizeof(buf), nullptr))
            p.description = buf;
        if (SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE*>(buf),
                                              sizeof(buf), nullptr)) {
            std::string hw = buf;
            p.vid = hexAfter(hw, "VID_");
            p.pid = hexAfter(hw, "PID_");
        }
        p.isGear = p.vid == MPX_USB_VID && p.pid == MPX_USB_PID;
        ports.push_back(p);
    }
    SetupDiDestroyDeviceInfoList(set);
    sortPorts(ports);
    return ports;
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& name, std::string& error) {
    close();
    std::string path = "\\\\.\\" + name;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        error = e == ERROR_ACCESS_DENIED ? name + " is in use by another program" : "cannot open " + name;
        return false;
    }
    DCB dcb;
    std::memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = 115200;  // ignored by USB CDC
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;  // the gear sees a connected terminal
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    SetCommState(h, &dcb);
    COMMTIMEOUTS t;
    t.ReadIntervalTimeout = MAXDWORD;  // return immediately with what is available...
    t.ReadTotalTimeoutMultiplier = MAXDWORD;
    t.ReadTotalTimeoutConstant = 20;  // ...or wait up to 20 ms for the first byte
    t.WriteTotalTimeoutMultiplier = 0;
    t.WriteTotalTimeoutConstant = 2000;
    SetCommTimeouts(h, &t);
    handle_ = h;
    pending_.clear();
    return true;
}

void SerialPort::close() {
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
    pending_.clear();
}

bool SerialPort::isOpen() const { return handle_ != nullptr; }

bool SerialPort::write(const char* data, size_t len) {
    if (!handle_) return false;
    while (len) {
        DWORD n = 0;
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(len, 4096));
        if (!WriteFile(static_cast<HANDLE>(handle_), data, chunk, &n, nullptr) || n == 0) return false;
        data += n;
        len -= n;
    }
    return true;
}

static int readSome(void* handle, char* buf, int cap) {
    DWORD n = 0;
    if (!ReadFile(static_cast<HANDLE>(handle), buf, static_cast<DWORD>(cap), &n, nullptr)) return -1;
    return static_cast<int>(n);
}

#else  // POSIX

std::vector<PortInfo> listSerialPorts() {
    std::vector<PortInfo> ports;
#ifdef __APPLE__
    const char* dirPath = "/dev";
#else
    const char* dirPath = "/dev";
#endif
    DIR* dir = opendir(dirPath);
    if (!dir) return ports;
    while (dirent* e = readdir(dir)) {
        std::string n = e->d_name;
        bool candidate = n.rfind("ttyACM", 0) == 0 || n.rfind("ttyUSB", 0) == 0 || n.rfind("cu.usbmodem", 0) == 0;
        if (!candidate) continue;
        PortInfo p;
        p.name = std::string(dirPath) + "/" + n;
#ifndef __APPLE__
        // /sys/class/tty/ttyACM0/device -> interface dir; idVendor lives one level up
        auto readHex = [&](const char* file) -> uint16_t {
            std::string path = "/sys/class/tty/" + n + "/device/../" + file;
            FILE* f = std::fopen(path.c_str(), "r");
            if (!f) return 0;
            unsigned v = 0;
            if (std::fscanf(f, "%x", &v) != 1) v = 0;
            std::fclose(f);
            return static_cast<uint16_t>(v);
        };
        p.vid = readHex("idVendor");
        p.pid = readHex("idProduct");
#endif
        p.isGear = p.vid == MPX_USB_VID && p.pid == MPX_USB_PID;
        p.description = p.isGear ? MPX_USB_PRODUCT : "serial port";
        ports.push_back(p);
    }
    closedir(dir);
    sortPorts(ports);
    return ports;
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& name, std::string& error) {
    close();
    int fd = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        error = "cannot open " + name;
        return false;
    }
    termios tio{};
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tcsetattr(fd, TCSANOW, &tio);
    fd_ = fd;
    pending_.clear();
    return true;
}

void SerialPort::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    pending_.clear();
}

bool SerialPort::isOpen() const { return fd_ >= 0; }

bool SerialPort::write(const char* data, size_t len) {
    while (len) {
        ssize_t n = ::write(fd_, data, len);
        if (n < 0) {
            pollfd p{fd_, POLLOUT, 0};
            if (::poll(&p, 1, 2000) <= 0) return false;
            continue;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

static int readSome(int fd, char* buf, int cap) {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, 20) <= 0) return 0;
    ssize_t n = ::read(fd, buf, static_cast<size_t>(cap));
    return n < 0 ? 0 : static_cast<int>(n);
}

#endif

bool SerialPort::readLine(std::string& line, int timeoutMs) {
    if (!isOpen()) return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        size_t nl = pending_.find('\n');
        if (nl != std::string::npos) {
            line = pending_.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending_.erase(0, nl + 1);
            return true;
        }
        if (std::chrono::steady_clock::now() > deadline) return false;
        char buf[4096];
#ifdef _WIN32
        int n = readSome(handle_, buf, sizeof(buf));
#else
        int n = readSome(fd_, buf, sizeof(buf));
#endif
        if (n < 0) return false;
        pending_.append(buf, static_cast<size_t>(n));
    }
}

void SerialPort::flushInput() {
    if (!isOpen()) return;
    char buf[4096];
#ifdef _WIN32
    PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR);
    while (readSome(handle_, buf, sizeof(buf)) > 0) {
    }
#else
    while (readSome(fd_, buf, sizeof(buf)) > 0) {
    }
#endif
    pending_.clear();
}

} // namespace mpx::host
