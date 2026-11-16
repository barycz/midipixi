#include "ParamKnob.h"

#include "Theme.h"

namespace fmt {
juce::String percent(float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }
juce::String seconds(float v) {
    return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
}
juce::String hertz(float v) { return v < 1.0f ? juce::String(v, 3) + " Hz" : juce::String(v, 2) + " Hz"; }
juce::String turns(float v) { return juce::String(juce::roundToInt(v * 360.0f)) + juce::String::fromUTF8("\xc2\xb0"); }
juce::String plain(float v) { return juce::String(v, 2); }
}  // namespace fmt

ParamKnob::ParamKnob(const juce::String& name, double min, double max, double defaultValue, Getter get, Setter set,
                     Formatter fmt, double skewMidPoint)
    : name_(name), get_(std::move(get)), set_(std::move(set)), fmt_(std::move(fmt)) {
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider_.setRange(min, max, 0.0);
    if (skewMidPoint > min && skewMidPoint < max) slider_.setSkewFactorFromMidPoint(skewMidPoint);
    slider_.setDoubleClickReturnValue(true, defaultValue);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider_.setPopupDisplayEnabled(true, true, nullptr);
    if (fmt_) slider_.textFromValueFunction = [this](double v) { return fmt_(static_cast<float>(v)); };
    slider_.onValueChange = [this] {
        if (!updating_ && set_) set_(static_cast<float>(slider_.getValue()));
        repaint();
    };
    addAndMakeVisible(slider_);
    refresh();
}

void ParamKnob::refresh() {
    if (!get_) return;
    updating_ = true;
    slider_.setValue(get_(), juce::dontSendNotification);
    updating_ = false;
    repaint();
}

void ParamKnob::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(14);
    r.removeFromBottom(14);
    int s = juce::jmin(r.getWidth(), r.getHeight());
    slider_.setBounds(r.withSizeKeepingCentre(s, s));
}

void ParamKnob::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    g.setColour(theme::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText(name_, r.removeFromTop(14), juce::Justification::centred);
    g.setColour(theme::text);
    float v = static_cast<float>(slider_.getValue());
    g.drawText(fmt_ ? fmt_(v) : fmt::plain(v), r.removeFromBottom(14), juce::Justification::centred);
}
