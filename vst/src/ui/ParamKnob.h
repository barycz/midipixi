#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// A labelled rotary knob bound to a preset value through getter/setter lambdas.
class ParamKnob : public juce::Component {
public:
    using Getter = std::function<float()>;
    using Setter = std::function<void(float)>;
    using Formatter = std::function<juce::String(float)>;

    ParamKnob(const juce::String& name, double min, double max, double defaultValue, Getter get, Setter set,
              Formatter fmt = {}, double skewMidPoint = 0.0);

    virtual ~ParamKnob() override = default;
    // Re-reads the bound value (after preset or layer changes).
    virtual void refresh();
    // Live modulated values of the layer (indexed by mpx::Dest); nullptr when idle.
    virtual void setLive(const float* values) { (void)values; }
    void resized() override;
    void paint(juce::Graphics& g) override;

    juce::Slider& slider() { return slider_; }
    const juce::String& name() const { return name_; }

protected:
    // Area of the rotary itself (for subclasses drawing around it).
    juce::Rectangle<int> knobArea() const { return slider_.getBounds(); }
    double valueToProportion(double v) const { return slider_.valueToProportionOfLength(v); }

    juce::String name_;
    mutable juce::Slider slider_;  // mutable: Slider::valueToProportionOfLength is non-const
    Getter get_;
    Setter set_;
    Formatter fmt_;
    bool updating_ = false;
};

// Common value formatters
namespace fmt {
juce::String percent(float v);
juce::String seconds(float v);
juce::String hertz(float v);
juce::String turns(float v);  // hue as degrees
juce::String plain(float v);
}  // namespace fmt
