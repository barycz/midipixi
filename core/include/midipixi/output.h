#pragma once
// Output stage: converts the engine's float frame into bytes for WS2812 LEDs.
// Applies gamma, master brightness, a power limiter, temporal dithering,
// color order and strip direction.

#include <cstddef>
#include <cstdint>

#include "midipixi/color.h"
#include "midipixi/limits.h"

namespace mpx {

enum class ColorOrder : uint8_t { RGB, RBG, GRB, GBR, BRG, BGR, Count };

const char* toString(ColorOrder o);
bool parseColorOrder(const char* s, ColorOrder& out);

// Installation/hardware settings. Kept separate from presets so presets stay
// portable between installations.
struct OutputConfig {
    uint16_t ledCount = 60;
    ColorOrder colorOrder = ColorOrder::GRB;
    float gamma = 2.2f;
    float brightness = 1.0f;      // master brightness 0..1 (applied in linear light)
    uint32_t powerLimitMa = 0;    // 0 = no limit
    float maPerChannel = 20.0f;   // current of one color channel at full duty
    float idleMaPerLed = 1.0f;    // quiescent current per LED
    bool dither = true;
    bool reverse = false;
    uint16_t fps = 60;
    uint8_t dataPin = 2;          // gear only; applied at boot

    void sanitize();
};

class OutputStage {
public:
    OutputStage();

    void configure(const OutputConfig& cfg);
    const OutputConfig& config() const { return cfg_; }

    // Converts `count` pixels into 3*count bytes in the configured color order.
    void process(const Rgb* in, int count, uint8_t* out);

    // Estimated current draw of the last frame after limiting, and the scale
    // factor the limiter applied (1 = not limiting).
    float lastCurrentMa() const { return lastCurrentMa_; }
    float lastLimiterScale() const { return lastScale_; }

private:
    static constexpr int kLutSize = 1024;
    uint32_t linear(float v) const;  // perceptual 0..1 -> linear 0..65535

    OutputConfig cfg_;
    uint16_t lut_[kLutSize + 1];
    uint8_t residual_[kMaxPixels * 3];
    float lastCurrentMa_ = 0.0f;
    float lastScale_ = 1.0f;
};

} // namespace mpx
