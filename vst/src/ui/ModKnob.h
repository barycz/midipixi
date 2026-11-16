#pragma once
// A ParamKnob that is a modulation destination:
//  * drop a modulation source chip on it -> new matrix row
//  * coloured rings show each modulation's range, a dot shows the live value
//  * small depth handles under the knob: drag to set the amount,
//    double-click or right-click to remove

#include <functional>

#include "ParamKnob.h"
#include "midipixi/preset.h"

class ModKnob : public ParamKnob, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
public:
    struct Context {
        std::function<mpx::Layer*()> layer;  // current layer (nullptr if none)
        std::function<void()> modsChanged;   // after adding/removing/changing rows
    };

    ModKnob(const juce::String& name, double min, double max, double def, Getter get, Setter set, Formatter fmt,
            double skew, mpx::Dest dest, Context ctx);

    mpx::Dest dest() const { return dest_; }

    void setLive(const float* values) override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    bool isInterestedInDragSource(const SourceDetails& d) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails& d) override;

    // Maps a base value plus a modulation offset to this destination's value
    // (handles the octave scaling of rates/times). Shared with the matrix view.
    static float applyModulation(mpx::Dest d, float base, float amount);

private:
    struct Handle {
        int row;
        juce::Rectangle<float> bounds;
    };
    std::vector<Handle> handles() const;
    int handleAt(juce::Point<int> p) const;
    float baseValue() const { return static_cast<float>(slider_.getValue()); }
    float proportionOf(float v) const;

    mpx::Dest dest_;
    Context ctx_;
    bool dragHover_ = false;
    bool hasLive_ = false;
    float live_ = 0.0f;
    int dragRow_ = -1;
    float dragStartAmount_ = 0.0f;
};

// Draggable modulation source ("ENV 1", "LFO 2", "CC 74", ...).
class ModSourceChip : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit ModSourceChip(mpx::ModSource s, juce::String label = {});
    mpx::ModSource source() const { return source_; }
    void setSource(mpx::ModSource s);

    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    static juce::String dragDescription(mpx::ModSource s);
    static bool parseDragDescription(const juce::var& description, mpx::ModSource& out);
    static juce::String label(mpx::ModSource s);

private:
    mpx::ModSource source_;
    juce::String fixedLabel_;
    bool dragging_ = false;
};
