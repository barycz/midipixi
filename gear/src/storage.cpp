#include "storage.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"

namespace gear {

namespace {

constexpr uint32_t kSlotMagic = 0x5350584D;  // "MXPS"
constexpr uint32_t kMetaMagic = 0x4D50584D;  // "MXPM"

struct Header {
    uint32_t magic;
    uint32_t a;  // slot: length            meta: active slot (int32)
    uint32_t b;  // slot: crc32             meta: config length
    uint32_t c;  // slot: 0                 meta: crc32 of config
};
static_assert(sizeof(Header) == 16, "");

// RAM staging buffer for one slot (flash is programmed from RAM).
uint8_t g_buffer[Storage::kSlotSize] __attribute__((aligned(4)));

struct FlashOp {
    uint32_t offset;
    const uint8_t* data;
    size_t len;
    bool erase;
};

void flashOp(void* param) {
    auto* op = static_cast<FlashOp*>(param);
    if (op->erase)
        flash_range_erase(op->offset, op->len);
    else
        flash_range_program(op->offset, op->data, op->len);
}

const uint8_t* xip(uint32_t offset) { return reinterpret_cast<const uint8_t*>(XIP_BASE + offset); }

} // namespace

uint32_t crc32(const void* data, size_t len) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void Storage::init() { base_ = PICO_FLASH_SIZE_BYTES - (kSlots * kSlotSize + kMetaSize); }

bool Storage::program(uint32_t offset, const uint8_t* data, size_t len) {
    // Erase and program sector by sector so interrupts are never off for long.
    size_t padded = (len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
    for (size_t done = 0; done < padded; done += FLASH_SECTOR_SIZE) {
        FlashOp erase{offset + static_cast<uint32_t>(done), nullptr, FLASH_SECTOR_SIZE, true};
        if (flash_safe_execute(flashOp, &erase, 1000) != PICO_OK) return false;
        size_t chunk = padded - done < FLASH_SECTOR_SIZE ? padded - done : FLASH_SECTOR_SIZE;
        FlashOp prog{offset + static_cast<uint32_t>(done), data + done, chunk, false};
        if (flash_safe_execute(flashOp, &prog, 1000) != PICO_OK) return false;
    }
    return std::memcmp(xip(offset), data, len) == 0;
}

bool Storage::readSlot(int slot, const char*& json, size_t& len) const {
    if (slot < 0 || slot >= kSlots) return false;
    const uint8_t* p = xip(slotOffset(slot));
    Header h;
    std::memcpy(&h, p, sizeof(h));
    if (h.magic != kSlotMagic || h.a == 0 || h.a > kMaxJson) return false;
    if (crc32(p + sizeof(Header), h.a) != h.b) return false;
    json = reinterpret_cast<const char*>(p + sizeof(Header));
    len = h.a;
    return true;
}

bool Storage::writeSlot(int slot, const char* json, size_t len) {
    if (slot < 0 || slot >= kSlots || len == 0 || len > kMaxJson) return false;
    Header h{kSlotMagic, static_cast<uint32_t>(len), crc32(json, len), 0};
    std::memset(g_buffer, 0xFF, sizeof(g_buffer));
    std::memcpy(g_buffer, &h, sizeof(h));
    std::memcpy(g_buffer + sizeof(h), json, len);
    return program(slotOffset(slot), g_buffer, sizeof(h) + len);
}

bool Storage::eraseSlot(int slot) {
    if (slot < 0 || slot >= kSlots) return false;
    FlashOp erase{slotOffset(slot), nullptr, FLASH_SECTOR_SIZE, true};
    return flash_safe_execute(flashOp, &erase, 1000) == PICO_OK;
}

bool Storage::readMeta(int& activeSlot, const char*& configJson, size_t& configLen) const {
    const uint8_t* p = xip(metaOffset());
    Header h;
    std::memcpy(&h, p, sizeof(h));
    if (h.magic != kMetaMagic || h.b > kMaxConfigJson) return false;
    if (crc32(p + sizeof(Header), h.b) != h.c) return false;
    activeSlot = static_cast<int32_t>(h.a);
    configJson = reinterpret_cast<const char*>(p + sizeof(Header));
    configLen = h.b;
    return true;
}

bool Storage::writeMeta(int activeSlot, const char* configJson, size_t configLen) {
    if (configLen > kMaxConfigJson) return false;
    Header h{kMetaMagic, static_cast<uint32_t>(activeSlot), static_cast<uint32_t>(configLen), crc32(configJson, configLen)};
    std::memset(g_buffer, 0xFF, kMetaSize);
    std::memcpy(g_buffer, &h, sizeof(h));
    std::memcpy(g_buffer + sizeof(h), configJson, configLen);
    return program(metaOffset(), g_buffer, sizeof(h) + configLen);
}

} // namespace gear
