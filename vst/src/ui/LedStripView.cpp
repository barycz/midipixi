#include "LedStripView.h"

#include "Theme.h"

void LedStripView::setPixels(const mpx::Rgb* px, int count) {
    pixels_.assign(px, px + count);
    if (static_cast<int>(history_.size()) != kHistory || (!history_.empty() && static_cast<int>(history_[0].size()) != count)) {
        history_.assign(kHistory, std::vector<mpx::Rgb>(static_cast<size_t>(count)));
        historyPos_ = 0;
    }
    history_[static_cast<size_t>(historyPos_)] = pixels_;
    historyPos_ = (historyPos_ + 1) % kHistory;
    repaint();
}

void LedStripView::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0b0c0f));
    g.fillRoundedRectangle(area, 6.0f);

    const int n = static_cast<int>(pixels_.size());
    if (n == 0) return;

    auto stripArea = area.reduced(10.0f, 8.0f);
    auto waterfall = stripArea.removeFromBottom(stripArea.getHeight() * 0.45f);
    stripArea.removeFromBottom(4.0f);

    const float pitch = stripArea.getWidth() / static_cast<float>(n);
    const float cy = stripArea.getCentreY();
    const float dot = juce::jmin(pitch * 0.7f, stripArea.getHeight() * 0.45f);

    auto colourOf = [](const mpx::Rgb& c) {
        return juce::Colour::fromFloatRGBA(mpx::clamp01(c.r), mpx::clamp01(c.g), mpx::clamp01(c.b), 1.0f);
    };

    // Glow
    for (int i = 0; i < n; ++i) {
        const mpx::Rgb& p = pixels_[static_cast<size_t>(i)];
        float lum = mpx::maxChannel(p);
        if (lum <= 0.01f) continue;
        float cx = stripArea.getX() + pitch * (static_cast<float>(i) + 0.5f);
        float glowR = dot * (1.2f + 1.6f * lum);
        juce::ColourGradient grad(colourOf(p).withAlpha(0.55f * lum), cx, cy, colourOf(p).withAlpha(0.0f), cx + glowR,
                                  cy, true);
        g.setGradientFill(grad);
        g.fillEllipse(cx - glowR, cy - glowR, glowR * 2, glowR * 2);
    }
    // LEDs
    for (int i = 0; i < n; ++i) {
        const mpx::Rgb& p = pixels_[static_cast<size_t>(i)];
        float cx = stripArea.getX() + pitch * (static_cast<float>(i) + 0.5f);
        // An unlit LED is black, so dim values fade smoothly into it; the
        // outline shows where the LEDs are.
        juce::Rectangle<float> led(cx - dot / 2, cy - dot / 2, dot, dot);
        g.setColour(mpx::maxChannel(p) > 0.0f ? colourOf(p) : juce::Colours::black);
        g.fillEllipse(led);
        g.setColour(juce::Colour(0xff3a3f49));
        g.drawEllipse(led.reduced(0.5f), 1.0f);
    }

    // Waterfall: newest row at the top
    if (!history_.empty()) {
        const float rowH = waterfall.getHeight() / static_cast<float>(kHistory);
        for (int r = 0; r < kHistory; ++r) {
            int idx = (historyPos_ - 1 - r + kHistory * 2) % kHistory;
            const auto& row = history_[static_cast<size_t>(idx)];
            float y = waterfall.getY() + rowH * static_cast<float>(r);
            float fade = 1.0f - static_cast<float>(r) / static_cast<float>(kHistory);
            for (int i = 0; i < n && i < static_cast<int>(row.size()); ++i) {
                const mpx::Rgb& p = row[static_cast<size_t>(i)];
                if (mpx::maxChannel(p) <= 0.01f) continue;
                g.setColour(colourOf(p).withMultipliedAlpha(0.35f + 0.65f * fade));
                g.fillRect(stripArea.getX() + pitch * static_cast<float>(i), y, pitch, rowH + 0.5f);
            }
        }
    }
    g.setColour(theme::outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);
}
