#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "midipixi/preset.h"

class MidipixiProcessor;

// Table view of the selected layer's modulation matrix.
class ModMatrixView : public juce::Component {
public:
    explicit ModMatrixView(MidipixiProcessor& p);

    void setLayer(int index);
    void refresh();  // rebuild rows from the preset

    // Called after any change (knobs repaint their rings).
    std::function<void()> onChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Row;
    mpx::Layer* layer();
    void changed(bool structural);

    MidipixiProcessor& proc_;
    int layer_ = 0;
    juce::Viewport viewport_;
    juce::Component content_;
    std::vector<std::unique_ptr<Row>> rows_;
    juce::TextButton add_{"+ Add modulation"};
    juce::Label hint_;
};
