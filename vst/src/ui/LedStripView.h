#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "midipixi/color.h"

// The simulator: one glowing dot per LED, plus a short "waterfall" history.
class LedStripView : public juce::Component {
public:
    void setPixels(const mpx::Rgb* px, int count);
    void paint(juce::Graphics& g) override;

private:
    std::vector<mpx::Rgb> pixels_;
    static constexpr int kHistory = 48;
    std::vector<std::vector<mpx::Rgb>> history_;
    int historyPos_ = 0;
};
