#pragma once
// Preset slots and settings in the last part of the flash.
//
// Layout (at the end of flash):  [slot 0][slot 1]...[slot 31][meta sector]
//   slot: 16 KB = header {magic, length, crc32, 0} + compact preset JSON
//   meta: 4 KB  = header {magic, activeSlot, configLength, crc32} + config JSON

#include <cstddef>
#include <cstdint>

namespace gear {

class Storage {
public:
    static constexpr int kSlots = 32;
    static constexpr uint32_t kSlotSize = 16 * 1024;
    static constexpr uint32_t kMetaSize = 4096;
    static constexpr size_t kMaxJson = kSlotSize - 16;
    static constexpr size_t kMaxConfigJson = kMetaSize - 16;

    void init();

    bool readSlot(int slot, const char*& json, size_t& len) const;
    bool writeSlot(int slot, const char* json, size_t len);
    bool eraseSlot(int slot);

    // Meta data. Returns false when no (valid) meta sector exists yet (first boot).
    bool readMeta(int& activeSlot, const char*& configJson, size_t& configLen) const;
    bool writeMeta(int activeSlot, const char* configJson, size_t configLen);

private:
    uint32_t slotOffset(int slot) const { return base_ + static_cast<uint32_t>(slot) * kSlotSize; }
    uint32_t metaOffset() const { return base_ + kSlots * kSlotSize; }
    bool program(uint32_t offset, const uint8_t* data, size_t len);  // erases + programs, len padded

    uint32_t base_ = 0;
};

uint32_t crc32(const void* data, size_t len);

} // namespace gear
