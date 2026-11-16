#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mpxcli {

// Writes an 8-bit RGB PNG (uncompressed deflate blocks; no zlib dependency).
bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);

} // namespace mpxcli
