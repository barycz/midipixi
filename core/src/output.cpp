#include "midipixi/output.h"

#include <cmath>
#include <cstring>

#include "midipixi/hot.h"

namespace mpx {

namespace {
const char* const kOrderNames[] = {"rgb", "rbg", "grb", "gbr", "brg", "bgr"};
static_assert(sizeof(kOrderNames) / sizeof(kOrderNames[0]) == static_cast<size_t>(ColorOrder::Count), "");
// Byte position of r, g, b for each color order.
const uint8_t kOrderIndex[][3] = {
    {0, 1, 2},  // rgb
    {0, 2, 1},  // rbg
    {1, 0, 2},  // grb
    {2, 0, 1},  // gbr
    {1, 2, 0},  // brg
    {2, 1, 0},  // bgr
};
} // namespace

const char* toString(ColorOrder o) {
    size_t i = static_cast<size_t>(o);
    return i < static_cast<size_t>(ColorOrder::Count) ? kOrderNames[i] : "grb";
}

bool parseColorOrder(const char* s, ColorOrder& out) {
    for (size_t i = 0; i < static_cast<size_t>(ColorOrder::Count); ++i) {
        if (std::strcmp(s, kOrderNames[i]) == 0) {
            out = static_cast<ColorOrder>(i);
            return true;
        }
    }
    return false;
}

void OutputConfig::sanitize() {
    if (ledCount < 1) ledCount = 1;
    if (ledCount > kMaxPixels) ledCount = kMaxPixels;
    if (static_cast<int>(colorOrder) >= static_cast<int>(ColorOrder::Count)) colorOrder = ColorOrder::GRB;
    gamma = clampf(gamma, 1.0f, 4.0f);
    brightness = clamp01(brightness);
    if (maPerChannel < 0.0f) maPerChannel = 0.0f;
    if (idleMaPerLed < 0.0f) idleMaPerLed = 0.0f;
    if (ditherLevels < 1) ditherLevels = 1;
    if (ditherLevels > 256) ditherLevels = 256;
    while (ditherLevels & (ditherLevels - 1)) ditherLevels &= static_cast<uint16_t>(ditherLevels - 1);  // power of two
    if (fps < 10) fps = 10;
    if (fps > 200) fps = 200;
    if (dataPin > 29) dataPin = 2;
}

OutputStage::OutputStage() {
    std::memset(level_, 0, sizeof(level_));
    configure(OutputConfig{});
}

void OutputStage::configure(const OutputConfig& cfg) {
    cfg_ = cfg;
    cfg_.sanitize();
    for (int i = 0; i <= kLutSize; ++i) {
        float x = static_cast<float>(i) / kLutSize;
        lut_[i] = static_cast<uint16_t>(std::pow(x, cfg_.gamma) * 65535.0f + 0.5f);
    }
    resetDither();
}

void OutputStage::resetDither() {
    // Each LED starts at a different dither phase (golden-ratio sequence), so
    // LEDs with the same dim value take turns instead of switching on and off
    // in unison. The channels of one LED share a phase to avoid color flicker.
    for (int i = 0; i < kMaxPixels; ++i) {
        uint8_t phase = static_cast<uint8_t>((static_cast<uint32_t>(i) * 158u) & 0xFFu);
        residual_[i * 3] = residual_[i * 3 + 1] = residual_[i * 3 + 2] = phase;
    }
}

// One float multiply and conversion; the curve is interpolated in fixed point
// (8-bit fraction), which is cheap without an FPU.
MPX_HOT uint32_t OutputStage::linear(float v) const {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return lut_[kLutSize];
    uint32_t f = static_cast<uint32_t>(v * static_cast<float>(kLutSize * 256));
    uint32_t i = f >> 8;
    uint32_t t = f & 0xFFu;
    uint32_t a = lut_[i], b = lut_[i + 1];  // the curve rises: b >= a
    return a + (((b - a) * t) >> 8);
}

void OutputStage::process(const Rgb* in, int count, uint8_t* out) {
    prepare(in, count);
    emit(out);
}

MPX_HOT void OutputStage::prepare(const Rgb* in, int count) {
    if (count < 0) count = 0;
    if (count > kMaxPixels) count = kMaxPixels;
    count_ = count;

    // Pass 1: gamma curve into level_ (output order), summed for the current
    // estimate. The gear reports the estimate in its stats, so it always runs.
    const uint8_t* idx = kOrderIndex[static_cast<int>(cfg_.colorOrder)];
    uint32_t sum = 0;  // at most kMaxPixels * 3 * 65535 < 2^32
    for (int i = 0; i < count; ++i) {
        const Rgb& px = in[cfg_.reverse ? count - 1 - i : i];
        uint32_t r = linear(px.r), g = linear(px.g), b = linear(px.b);
        uint16_t* lv = level_ + i * 3;
        lv[idx[0]] = static_cast<uint16_t>(r);
        lv[idx[1]] = static_cast<uint16_t>(g);
        lv[idx[2]] = static_cast<uint16_t>(b);
        sum += r + g + b;
    }

    // Master brightness and the power limiter both scale linear light.
    float scale = cfg_.brightness;
    float idle = cfg_.idleMaPerLed * static_cast<float>(count);
    float activeMa = static_cast<float>(sum) / 65535.0f * cfg_.maPerChannel * scale;
    float limiter = 1.0f;
    if (cfg_.powerLimitMa > 0) {
        float budget = static_cast<float>(cfg_.powerLimitMa) - idle;
        if (budget < 0.0f) budget = 0.0f;
        if (activeMa > budget) limiter = activeMa > 0.0f ? budget / activeMa : 0.0f;
    }
    lastScale_ = limiter;
    lastCurrentMa_ = idle + activeMa * limiter;
    scale *= limiter;

    // Pass 2, integer only: scale, then round dithered levels to multiples of
    // 1/ditherLevels of an 8-bit step.
    const uint32_t fixedScale = static_cast<uint32_t>(clamp01(scale) * 65536.0f);
    const uint32_t quantum = cfg_.dither ? 256u / cfg_.ditherLevels : 1u;
    const uint32_t mask = ~(quantum - 1u);
    const uint32_t top = 65535u & mask;
    const int n = count * 3;
    for (int i = 0; i < n; ++i) {
        // 65535 * 65536 < 2^32: no 64-bit multiply (a library call on Cortex-M0+).
        uint32_t v = (static_cast<uint32_t>(level_[i]) * fixedScale) >> 16;
        v = (v + quantum / 2u) & mask;
        level_[i] = static_cast<uint16_t>(v > 65535u ? top : v);
    }
}

MPX_HOT void OutputStage::emit(uint8_t* out) {
    const int n = count_ * 3;
    if (cfg_.dither) {
        for (int i = 0; i < n; ++i) {
            uint32_t v = static_cast<uint32_t>(level_[i]) + residual_[i];
            uint32_t q = v >> 8;
            if (q > 255) q = 255;
            uint32_t err = v - (q << 8);
            residual_[i] = static_cast<uint8_t>(err > 255 ? 255 : err);
            out[i] = static_cast<uint8_t>(q);
        }
    } else {
        for (int i = 0; i < n; ++i) {
            uint32_t q = (static_cast<uint32_t>(level_[i]) + 128) >> 8;
            out[i] = static_cast<uint8_t>(q > 255 ? 255 : q);
        }
    }
}

} // namespace mpx
