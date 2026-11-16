#pragma once
// WS2812 output through PIO + DMA: the CPU only converts the frame into FIFO
// words, the bitstream is generated without CPU attention.

#include <cstdint>

#include "midipixi/limits.h"

namespace gear {

class Ws2812Output {
public:
    bool init(unsigned pin);
    // Sends `count` pixels of 3 bytes each (already in the strip's color order).
    // Waits for the previous frame (plus the latch gap) before starting.
    void show(const uint8_t* bytes, int count);
    bool busy() const;

private:
    unsigned sm_ = 0;
    int dma_ = -1;
    uint64_t readyAtUs_ = 0;
    uint32_t words_[mpx::kMaxPixels];
};

} // namespace gear
