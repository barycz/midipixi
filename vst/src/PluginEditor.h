#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "PluginProcessor.h"
#include "ui/GearPanel.h"
#include "ui/LayerEditor.h"
#include "ui/LayerList.h"
#include "ui/LedStripView.h"
#include "ui/ParamKnob.h"
#include "ui/Theme.h"

class MidipixiEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener {
public:
    explicit MidipixiEditor(MidipixiProcessor& p);
    ~MidipixiEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshAll();
    void loadFromFile();
    void saveToFile();
    void buildMacros();

    MidipixiProcessor& proc_;
    theme::LookAndFeel lnf_;
    juce::TooltipWindow tooltips_{this, 600};

    // Header
    juce::TextEditor presetName_;
    juce::ComboBox factory_;
    juce::TextButton init_{"Init"}, load_{"Load..."}, save_{"Save..."};
    juce::Slider ledCount_;
    juce::Label ledLabel_, bpmLabel_;

    LedStripView strip_;
    LayerList layers_;
    std::unique_ptr<LayerEditor> layerEditor_;
    GearPanel gearPanel_;
    juce::Rectangle<int> macroArea_;
    std::vector<std::unique_ptr<ParamKnob>> macroKnobs_;

    std::unique_ptr<FrameSnapshot> frame_;
    uint32_t frameSerial_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool updating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidipixiEditor)
};
