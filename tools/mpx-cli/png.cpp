#include "png.h"

#include <cstdio>

namespace mpxcli {

namespace {

uint32_t crcTable[256];
bool crcReady = false;

uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    if (!crcReady) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[i] = c;
        }
        crcReady = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = crcTable[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void chunk(std::vector<uint8_t>& file, const char* type, const std::vector<uint8_t>& data) {
    be32(file, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    file.insert(file.end(), body.begin(), body.end());
    be32(file, crc32(body.data(), body.size()));
}

} // namespace

bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        const uint8_t* row = rgb.data() + static_cast<size_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + width * 3);
    }

    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t len = raw.size() - pos;
        if (len > 65535) len = 65535;
        bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len));
        z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len));
        z.push_back(static_cast<uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(width));
    be32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, RGB
    chunk(file, "IHDR", ihdr);
    chunk(file, "IDAT", z);
    chunk(file, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace mpxcli
