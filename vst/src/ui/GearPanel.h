#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class MidipixiProcessor;

// Connection to the hardware: port, slots, upload, live edit.
class GearPanel : public juce::Component, private juce::ChangeListener {
public:
    explicit GearPanel(MidipixiProcessor& p);
    ~GearPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void update();

    MidipixiProcessor& proc_;
    juce::ComboBox port_;
    juce::TextButton refresh_{"Scan"}, connect_{"Connect"};
    juce::ComboBox slot_;
    juce::TextButton upload_{"Upload"}, run_{"Run slot"};
    juce::ToggleButton selectAfter_{"Run after upload"};
    juce::ToggleButton live_{"Live edit"};
    juce::Label info_, message_;
    juce::StringArray portNames_;
};
