#include "Theme.h"

namespace theme {

juce::Colour sourceColour(mpx::SourceType t) {
    using S = mpx::SourceType;
    switch (t) {
        case S::Env1: return juce::Colour(0xff66bb6a);
        case S::Env2: return juce::Colour(0xff9ccc65);
        case S::Lfo1: return juce::Colour(0xff42a5f5);
        case S::Lfo2: return juce::Colour(0xff5c6bc0);
        case S::Lfo3: return juce::Colour(0xff7e57c2);
        case S::Note: return juce::Colour(0xffffca28);
        case S::Velocity: return juce::Colour(0xffffa726);
        case S::Aftertouch: return juce::Colour(0xffff7043);
        case S::Gate: return juce::Colour(0xffbdbdbd);
        case S::Random: return juce::Colour(0xffec407a);
        case S::Macro: return juce::Colour(0xff26c6da);
        case S::CC: return juce::Colour(0xff26a69a);
        case S::PitchBend: return juce::Colour(0xffd4e157);
        case S::Pressure: return juce::Colour(0xffff8a65);
        case S::Beat: return juce::Colour(0xffab47bc);
        default: return textDim;
    }
}

LookAndFeel::LookAndFeel() {
    setColourScheme({background, panel, panelLight, outline, text, accent, juce::Colours::black, panelLight, text});
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, outline);
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Label::textColourId, text);
    setColour(juce::ComboBox::backgroundColourId, panelLight);
    setColour(juce::ComboBox::outlineColourId, outline);
    setColour(juce::ComboBox::arrowColourId, textDim);
    setColour(juce::PopupMenu::backgroundColourId, panelLight);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.3f));
    setColour(juce::TextButton::buttonColourId, panelLight);
    setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.6f));
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::TextEditor::backgroundColourId, panelLight);
    setColour(juce::TextEditor::outlineColourId, outline);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::ListBox::backgroundColourId, panel);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, outline);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
    setColour(juce::TooltipWindow::backgroundColourId, panelLight);
    setColour(juce::TooltipWindow::textColourId, text);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                   float startAngle, float endAngle, juce::Slider& slider) {
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    float lineW = juce::jmax(2.5f, radius * 0.14f);
    float arcRadius = radius - lineW * 0.5f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(outline);
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar sliders fill from the centre.
    bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    float zeroAngle = bipolar ? startAngle + (endAngle - startAngle) *
                                                static_cast<float>(-slider.getMinimum() /
                                                                   (slider.getMaximum() - slider.getMinimum()))
                              : startAngle;
    float valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    if (slider.isEnabled()) {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin(zeroAngle, valueAngle),
                            juce::jmax(zeroAngle, valueAngle), true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(value, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    float knobR = arcRadius - lineW * 1.4f;
    g.setColour(panelLight);
    g.fillEllipse(centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2);
    juce::Point<float> tip(centre.x + knobR * 0.8f * std::sin(valueAngle), centre.y - knobR * 0.8f * std::cos(valueAngle));
    g.setColour(text);
    g.drawLine(centre.x, centre.y, tip.x, tip.y, 2.0f);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted,
                                       bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto c = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : bg;
    if (highlighted) c = c.brighter(0.15f);
    if (down) c = c.brighter(0.3f);
    g.setColour(c);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(outline);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return juce::Font(juce::FontOptions(juce::jmin(14.0f, static_cast<float>(buttonHeight) * 0.6f)));
}

juce::Font LookAndFeel::getLabelFont(juce::Label& l) {
    return juce::Font(juce::FontOptions(l.getFont().getHeight()));
}

juce::Rectangle<int> drawSection(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) {
    g.setColour(panel);
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    g.setColour(outline);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 6.0f, 1.0f);
    auto content = area.reduced(8, 6);
    if (title.isNotEmpty()) {
        g.setColour(textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(title.toUpperCase(), content.removeFromTop(16), juce::Justification::centredLeft);
    }
    return content;
}

} // namespace theme
