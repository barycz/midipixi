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
    // Dither resolution: levels between two 8-bit steps (power of two, 1..256).
    // A level of k/ditherLevels repeats every ditherLevels strip refreshes or
    // faster, so the slowest possible flicker is refresh rate / ditherLevels
    // (e.g. 240 Hz / 4 = 60 Hz). Finer levels make dim fades smoother but let
    // the very dimmest levels blink slowly; values below half a level are off.
    uint16_t ditherLevels = 4;
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

    // Converts `count` pixels into 3*count bytes in the configured color order
    // (prepare() followed by emit()).
    void process(const Rgb* in, int count, uint8_t* out);

    // Applies gamma, master brightness and the power limiter to a new frame.
    void prepare(const Rgb* in, int count);
    // Writes the prepared frame as 3*preparedCount() bytes. With dithering on,
    // every call is a new dither step, so the gear calls it several times per
    // engine frame and refreshes the strip faster than it renders: the 0/1
    // toggling of dim LEDs then happens too fast to be seen as flicker.
    void emit(uint8_t* out);
    int preparedCount() const { return count_; }

    // Estimated current draw of the last frame after limiting, and the scale
    // factor the limiter applied (1 = not limiting).
    float lastCurrentMa() const { return lastCurrentMa_; }
    float lastLimiterScale() const { return lastScale_; }

private:
    static constexpr int kLutSize = 1024;
    uint32_t linear(float v) const;  // perceptual 0..1 -> linear 0..65535
    void resetDither();

    OutputConfig cfg_;
    uint16_t lut_[kLutSize + 1];
    uint16_t level_[kMaxPixels * 3];     // prepared frame, linear 0..65535, output order
    uint8_t residual_[kMaxPixels * 3];   // dither error carried to the next emit()
    int count_ = 0;
    float lastCurrentMa_ = 0.0f;
    float lastScale_ = 1.0f;
};

} // namespace mpx
