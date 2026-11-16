#pragma once

#include <cmath>

namespace mpx {

// Colors are floats in "perceptual" space (like sRGB values): 0 = off, 1 = full.
// The output stage applies the gamma curve that maps them to linear LED PWM.
struct Rgb {
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

struct Hsv {
    float h = 0.0f;  // turns, 0..1
    float s = 0.0f;
    float v = 0.0f;
};

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Wraps any real number into [0, 1).
inline float wrap01(float x) {
    float w = x - std::floor(x);
    return w >= 1.0f ? 0.0f : w;
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float maxChannel(const Rgb& c) {
    float m = c.r > c.g ? c.r : c.g;
    return m > c.b ? m : c.b;
}

// Hue in turns (any real number, wrapped).
inline Rgb hsvToRgb(float h, float s, float v) {
    h = wrap01(h) * 6.0f;
    int sector = static_cast<int>(h);
    float f = h - static_cast<float>(sector);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (sector) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

inline Hsv rgbToHsv(const Rgb& c) {
    float mx = maxChannel(c);
    float mn = c.r < c.g ? c.r : c.g;
    mn = mn < c.b ? mn : c.b;
    float d = mx - mn;
    Hsv out;
    out.v = mx;
    out.s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) return out;
    float h;
    if (mx == c.r)
        h = (c.g - c.b) / d;
    else if (mx == c.g)
        h = 2.0f + (c.b - c.r) / d;
    else
        h = 4.0f + (c.r - c.g) / d;
    out.h = wrap01(h / 6.0f);
    return out;
}

} // namespace mpx
