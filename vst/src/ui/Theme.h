#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "midipixi/preset.h"

namespace theme {

inline const juce::Colour background{0xff15171c};
inline const juce::Colour panel{0xff1e2128};
inline const juce::Colour panelLight{0xff272b34};
inline const juce::Colour outline{0xff353a45};
inline const juce::Colour text{0xffe4e7ee};
inline const juce::Colour textDim{0xff8a91a0};
inline const juce::Colour accent{0xff4fc3f7};
inline const juce::Colour warning{0xffffb74d};
inline const juce::Colour error{0xffef5350};
inline const juce::Colour ok{0xff81c784};

// Colour for a modulation source (used by chips, rings and matrix rows).
juce::Colour sourceColour(mpx::SourceType t);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;
};

// Draws a section frame with a title, returns the content area.
juce::Rectangle<int> drawSection(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);

} // namespace theme
