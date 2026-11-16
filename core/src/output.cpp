#include "midipixi/output.h"

#include <cmath>
#include <cstring>

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
    if (fps < 10) fps = 10;
    if (fps > 200) fps = 200;
    if (dataPin > 29) dataPin = 2;
}

OutputStage::OutputStage() {
    std::memset(residual_, 0, sizeof(residual_));
    configure(OutputConfig{});
}

void OutputStage::configure(const OutputConfig& cfg) {
    cfg_ = cfg;
    cfg_.sanitize();
    for (int i = 0; i <= kLutSize; ++i) {
        float x = static_cast<float>(i) / kLutSize;
        lut_[i] = static_cast<uint16_t>(std::pow(x, cfg_.gamma) * 65535.0f + 0.5f);
    }
    std::memset(residual_, 0, sizeof(residual_));
}

uint32_t OutputStage::linear(float v) const {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return lut_[kLutSize];
    float f = v * kLutSize;
    int i = static_cast<int>(f);
    float t = f - static_cast<float>(i);
    float a = lut_[i], b = lut_[i + 1];
    return static_cast<uint32_t>(a + (b - a) * t);
}

void OutputStage::process(const Rgb* in, int count, uint8_t* out) {
    if (count > kMaxPixels) count = kMaxPixels;

    // Master brightness and the power limiter both scale linear light.
    float scale = cfg_.brightness;
    float idle = cfg_.idleMaPerLed * static_cast<float>(count);
    // Current estimate is always computed: the gear reports it in its stats.
    uint64_t sum = 0;
    for (int i = 0; i < count; ++i) sum += linear(in[i].r) + linear(in[i].g) + linear(in[i].b);
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

    uint32_t fixedScale = static_cast<uint32_t>(clamp01(scale) * 65536.0f);
    const uint8_t* idx = kOrderIndex[static_cast<int>(cfg_.colorOrder)];
    for (int i = 0; i < count; ++i) {
        int src = cfg_.reverse ? count - 1 - i : i;
        uint32_t ch[3] = {linear(in[src].r), linear(in[src].g), linear(in[src].b)};
        uint8_t* px = out + i * 3;
        uint8_t* res = residual_ + i * 3;
        for (int c = 0; c < 3; ++c) {
            uint32_t v = static_cast<uint32_t>((static_cast<uint64_t>(ch[c]) * fixedScale) >> 16);  // 0..65535
            if (cfg_.dither) {
                v += res[c];
                uint32_t q = v >> 8;
                if (q > 255) q = 255;
                res[c] = static_cast<uint8_t>(v - (q << 8) > 255 ? 255 : v - (q << 8));
                px[idx[c]] = static_cast<uint8_t>(q);
            } else {
                uint32_t q = (v + 128) >> 8;
                px[idx[c]] = static_cast<uint8_t>(q > 255 ? 255 : q);
            }
        }
    }
}

} // namespace mpx
