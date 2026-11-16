#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class MidipixiProcessor;

// Layer stack: select, add, duplicate, delete, reorder, enable.
class LayerList : public juce::Component {
public:
    explicit LayerList(MidipixiProcessor& p);

    int selected() const { return selected_; }
    void setSelected(int index);
    void refresh();
    // Voice activity per layer for the little activity dots.
    void setActivity(const int* voices, int count);

    std::function<void(int)> onSelect;
    std::function<void()> onStructureChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> rowBounds(int i) const;
    void changed();

    MidipixiProcessor& proc_;
    int selected_ = 0;
    int activity_[8] = {};
    juce::TextButton add_{"+"}, dup_{"Dup"}, del_{"Del"}, up_{juce::String::fromUTF8("\xe2\x96\xb2")},
        down_{juce::String::fromUTF8("\xe2\x96\xbc")};
};
