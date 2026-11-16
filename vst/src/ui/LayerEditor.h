#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

#include "ParamKnob.h"
#include "midipixi/preset.h"

class MidipixiProcessor;

// Editor for the selected layer: trigger/zone, emitter, envelopes, LFOs.
class LayerEditor : public juce::Component {
public:
    explicit LayerEditor(MidipixiProcessor& p);

    void setLayer(int index);
    int layerIndex() const { return layer_; }
    void refresh();
    // Live modulated values of the newest voice (nullptr when the layer is idle).
    virtual void setLiveValues(const float* values);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Creates the knob for a parameter; ModKnob-aware editors override this.
    using KnobFactory = std::function<std::unique_ptr<ParamKnob>(const juce::String& name, double min, double max,
                                                                 double def, ParamKnob::Getter, ParamKnob::Setter,
                                                                 ParamKnob::Formatter, double skew,
                                                                 std::optional<mpx::Dest> dest)>;
    void setKnobFactory(KnobFactory f);

protected:
    mpx::Layer& layer();
    void edited();
    void build();

    MidipixiProcessor& proc_;
    int layer_ = 0;
    KnobFactory factory_;

    // Section areas (computed in resized, painted in paint)
    juce::Rectangle<int> secLayer_, secEmitter_, secEnv_[2], secLfo_[3];

    juce::TextEditor name_;
    juce::ComboBox trigger_, channel_, blend_;
    juce::Slider noteLow_, noteHigh_, voices_;
    juce::Label noteLowLabel_, noteHighLabel_, voicesLabel_;
    juce::ToggleButton wrap_{"Wrap"};

    std::vector<std::unique_ptr<ParamKnob>> layerKnobs_, emitterKnobs_, envKnobs_[2], lfoKnobs_[3];
    juce::ComboBox lfoShape_[3], lfoBeats_[3];
    juce::ToggleButton lfoSync_[3], lfoRetrig_[3], lfoBipolar_[3];
    bool updating_ = false;
};
