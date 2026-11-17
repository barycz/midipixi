#include "LayerEditor.h"

#include "../PluginProcessor.h"
#include "Theme.h"

namespace {

juce::String noteName(int n) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[n % 12]) + juce::String(n / 12 - 1);
}

struct BeatChoice {
    const char* label;
    float beats;
};
const BeatChoice kBeats[] = {{"1/16", 0.25f},  {"1/8T", 1.0f / 3.0f}, {"1/8", 0.5f},  {"1/4T", 2.0f / 3.0f},
                             {"1/4", 1.0f},    {"1/2", 2.0f},         {"1 bar", 4.0f}, {"2 bars", 8.0f},
                             {"4 bars", 16.0f}, {"8 bars", 32.0f},     {"16 bars", 64.0f}};
constexpr int kNumBeats = static_cast<int>(sizeof(kBeats) / sizeof(kBeats[0]));

int beatIndex(float beats) {
    int best = 0;
    for (int i = 1; i < kNumBeats; ++i)
        if (std::abs(kBeats[i].beats - beats) < std::abs(kBeats[best].beats - beats)) best = i;
    return best;
}

void setupIncDec(juce::Slider& s, double min, double max) {
    s.setSliderStyle(juce::Slider::IncDecButtons);
    s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, 22);
    s.setRange(min, max, 1.0);
    s.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
}

}  // namespace

LayerEditor::LayerEditor(MidipixiProcessor& p) : proc_(p) {
    factory_ = [](const juce::String& name, double min, double max, double def, ParamKnob::Getter g,
                  ParamKnob::Setter s, ParamKnob::Formatter f, double skew, std::optional<mpx::Dest>) {
        return std::make_unique<ParamKnob>(name, min, max, def, std::move(g), std::move(s), std::move(f), skew);
    };

    addAndMakeVisible(name_);
    name_.setInputRestrictions(mpx::kNameLen - 1);
    name_.onTextChange = [this] {
        if (updating_) return;
        mpx::copyName(layer().name, mpx::kNameLen, name_.getText().toRawUTF8());
        edited();
    };

    trigger_.addItemList({"Poly", "Mono", "Legato", "Drone"}, 1);
    trigger_.onChange = [this] {
        if (updating_) return;
        layer().trigger = static_cast<mpx::TriggerMode>(trigger_.getSelectedItemIndex());
        edited();
        refresh();
    };
    trigger_.setTooltip("Poly: a voice per note.\n"
                        "Mono: one voice, every note restarts envelopes and LFOs.\n"
                        "Legato: one voice, overlapping notes only move it (with glide).\n"
                        "Drone: always on, ignores notes.");
    addAndMakeVisible(trigger_);

    channel_.addItem("Omni", 1);
    for (int c = 1; c <= 16; ++c) channel_.addItem("Ch " + juce::String(c), c + 1);
    channel_.onChange = [this] {
        if (updating_) return;
        layer().channel = static_cast<uint8_t>(channel_.getSelectedId() - 1);
        edited();
    };
    addAndMakeVisible(channel_);

    for (int b = 0; b < static_cast<int>(mpx::BlendMode::Count); ++b)
        blend_.addItem(mpx::displayName(static_cast<mpx::BlendMode>(b)), b + 1);
    blend_.onChange = [this] {
        if (updating_) return;
        layer().blend = static_cast<mpx::BlendMode>(blend_.getSelectedId() - 1);
        edited();
    };
    blend_.setTooltip("How this layer is combined with the layers below it");
    addAndMakeVisible(blend_);

    for (auto* s : {&noteLow_, &noteHigh_}) {
        setupIncDec(*s, 0, 127);
        s->textFromValueFunction = [](double v) { return juce::String(static_cast<int>(v)) + " " + noteName(static_cast<int>(v)); };
        s->valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        addAndMakeVisible(*s);
    }
    noteLow_.onValueChange = [this] {
        if (updating_) return;
        layer().noteLow = static_cast<uint8_t>(noteLow_.getValue());
        if (layer().noteHigh < layer().noteLow) layer().noteHigh = layer().noteLow;
        edited();
        refresh();
    };
    noteHigh_.onValueChange = [this] {
        if (updating_) return;
        layer().noteHigh = static_cast<uint8_t>(noteHigh_.getValue());
        if (layer().noteLow > layer().noteHigh) layer().noteLow = layer().noteHigh;
        edited();
        refresh();
    };
    noteLow_.setTooltip("Lowest note of the key zone (= position 0 of the 'note' source)");
    noteHigh_.setTooltip("Highest note of the key zone (= position 1 of the 'note' source)");
    setupIncDec(voices_, 1, mpx::kMaxVoices);
    voices_.onValueChange = [this] {
        if (updating_) return;
        layer().voices = static_cast<uint8_t>(voices_.getValue());
        edited();
    };
    addAndMakeVisible(voices_);
    for (auto [label, text] : {std::pair{&noteLowLabel_, "Low"}, {&noteHighLabel_, "High"}, {&voicesLabel_, "Voices"}}) {
        label->setText(text, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, theme::textDim);
        label->setFont(juce::Font(juce::FontOptions(11.5f)));
        addAndMakeVisible(*label);
    }

    wrap_.onClick = [this] {
        if (updating_) return;
        layer().wrap = wrap_.getToggleState();
        edited();
    };
    wrap_.setTooltip("Positions wrap around the strip ends (rings, loops)");
    addAndMakeVisible(wrap_);
    stack_.onClick = [this] {
        if (updating_) return;
        layer().stack = stack_.getToggleState();
        edited();
    };
    stack_.setTooltip("Repeating a note that is still sounding starts another emitter instead of restarting it "
                      "(e.g. rapid laser shots)");
    addAndMakeVisible(stack_);
    glideMode_.addItemList({"Fixed time", "Per octave"}, 1);
    glideMode_.setTooltip("Fixed time: every glide takes the Glide time.\n"
                          "Per octave: the Glide time is per octave, so wider jumps take longer.");
    glideMode_.onChange = [this] {
        if (updating_) return;
        layer().glideMode = static_cast<mpx::GlideMode>(glideMode_.getSelectedItemIndex());
        edited();
    };
    addAndMakeVisible(glideMode_);

    for (int k = 0; k < mpx::kNumLfos; ++k) {
        for (int s = 0; s < static_cast<int>(mpx::LfoShape::Count); ++s) {
            static const char* labels[] = {"Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & hold", "Smooth random"};
            lfoShape_[k].addItem(labels[s], s + 1);
        }
        lfoShape_[k].onChange = [this, k] {
            if (updating_) return;
            layer().lfo[k].shape = static_cast<mpx::LfoShape>(lfoShape_[k].getSelectedId() - 1);
            edited();
        };
        for (int b = 0; b < kNumBeats; ++b) lfoBeats_[k].addItem(kBeats[b].label, b + 1);
        lfoBeats_[k].onChange = [this, k] {
            if (updating_) return;
            layer().lfo[k].beats = kBeats[lfoBeats_[k].getSelectedId() - 1].beats;
            edited();
        };
        lfoSync_[k].setButtonText("Sync");
        lfoSync_[k].setTooltip("Tempo sync (host transport or MIDI clock)");
        lfoSync_[k].onClick = [this, k] {
            if (updating_) return;
            layer().lfo[k].sync = lfoSync_[k].getToggleState();
            edited();
            refresh();
        };
        lfoMode_[k].addItemList({"Free", "Retrig", "One shot"}, 1);
        lfoMode_[k].setTooltip("Free: runs continuously, shared by all voices.\n"
                               "Retrig: restarts on every note (per voice).\n"
                               "One shot: runs a single cycle on every note, then holds at the end.");
        lfoMode_[k].onChange = [this, k] {
            if (updating_) return;
            int id = lfoMode_[k].getSelectedId();
            layer().lfo[k].retrigger = id >= 2;
            layer().lfo[k].oneShot = id == 3;
            edited();
        };
        lfoBipolar_[k].setButtonText("+/-");
        lfoBipolar_[k].setTooltip("Bipolar output (-1..1). Off: 0..1");
        lfoBipolar_[k].onClick = [this, k] {
            if (updating_) return;
            layer().lfo[k].bipolar = lfoBipolar_[k].getToggleState();
            edited();
        };
        for (juce::Component* c : std::initializer_list<juce::Component*>{&lfoShape_[k], &lfoMode_[k], &lfoBeats_[k], &lfoSync_[k], &lfoBipolar_[k]})
            addAndMakeVisible(c);
    }

    build();
}

void LayerEditor::setKnobFactory(KnobFactory f) {
    factory_ = std::move(f);
    build();
    resized();
}

mpx::Layer& LayerEditor::layer() {
    auto& pr = proc_.preset();
    return pr.layers[juce::jlimit(0, mpx::kMaxLayers - 1, layer_)];
}

void LayerEditor::edited() { proc_.presetEdited(); }

void LayerEditor::build() {
    using D = mpx::Dest;
    auto make = [this](std::vector<std::unique_ptr<ParamKnob>>& into, const juce::String& name, double min, double max,
                       double def, std::function<float&()> ref, ParamKnob::Formatter f = fmt::percent,
                       double skew = 0.0, std::optional<mpx::Dest> dest = std::nullopt) {
        auto k = factory_(
            name, min, max, def, [ref] { return ref(); },
            [this, ref](float v) {
                ref() = v;
                edited();
            },
            std::move(f), skew, dest);
        addAndMakeVisible(*k);
        into.push_back(std::move(k));
    };

    layerKnobs_.clear();
    emitterKnobs_.clear();
    for (auto& v : envKnobs_) v.clear();
    for (auto& v : lfoKnobs_) v.clear();

    make(layerKnobs_, "Opacity", 0, 1, 1, [this]() -> float& { return layer().opacity; }, fmt::percent, 0, D::Opacity);
    make(layerKnobs_, "Glide", 0, 5, 0, [this]() -> float& { return layer().glide; }, fmt::seconds, 0.3);

    make(emitterKnobs_, "Position", 0, 1, 0.5, [this]() -> float& { return layer().emitter.position; }, fmt::percent, 0, D::Position);
    make(emitterKnobs_, "Width", 0, 1, 0, [this]() -> float& { return layer().emitter.width; }, fmt::percent, 0.1, D::Width);
    make(emitterKnobs_, "Softness", 0, 1, 0, [this]() -> float& { return layer().emitter.softness; }, fmt::percent, 0, D::Softness);
    make(emitterKnobs_, "Hue", 0, 1, 0, [this]() -> float& { return layer().emitter.hue; }, fmt::turns, 0, D::Hue);
    make(emitterKnobs_, "Saturation", 0, 1, 1, [this]() -> float& { return layer().emitter.saturation; }, fmt::percent, 0, D::Saturation);
    make(emitterKnobs_, "Brightness", 0, 1, 1, [this]() -> float& { return layer().emitter.brightness; }, fmt::percent, 0, D::Brightness);
    make(emitterKnobs_, "Hue spread", -2, 2, 0, [this]() -> float& { return layer().emitter.hueSpread; }, fmt::plain, 0, D::HueSpread);

    for (int e = 0; e < mpx::kNumEnvs; ++e) {
        D base = e == 0 ? D::Env1Attack : D::Env2Attack;
        auto dest = [base](int o) { return static_cast<D>(static_cast<int>(base) + o); };
        make(envKnobs_[e], "Attack", 0, 10, 0.005, [this, e]() -> float& { return layer().env[e].attack; }, fmt::seconds, 0.3, dest(0));
        make(envKnobs_[e], "Decay", 0, 10, 0.3, [this, e]() -> float& { return layer().env[e].decay; }, fmt::seconds, 0.5, dest(1));
        make(envKnobs_[e], "Sustain", 0, 1, 0, [this, e]() -> float& { return layer().env[e].sustain; }, fmt::percent, 0, dest(2));
        make(envKnobs_[e], "Release", 0, 10, 0.2, [this, e]() -> float& { return layer().env[e].release; }, fmt::seconds, 0.5, dest(3));
    }
    for (int k = 0; k < mpx::kNumLfos; ++k) {
        make(lfoKnobs_[k], "Rate", 0.01, 20, 1, [this, k]() -> float& { return layer().lfo[k].rate; }, fmt::hertz, 1.0,
             static_cast<D>(static_cast<int>(D::Lfo1Rate) + k));
        make(lfoKnobs_[k], "Phase", 0, 1, 0, [this, k]() -> float& { return layer().lfo[k].phase; }, fmt::turns);
    }
    refresh();
}

void LayerEditor::setLayer(int index) {
    layer_ = index;
    refresh();
}

void LayerEditor::refresh() {
    const auto& pr = proc_.preset();
    bool valid = layer_ >= 0 && layer_ < pr.layerCount;
    setEnabled(valid);
    if (!valid) {
        repaint();
        return;
    }
    updating_ = true;
    const mpx::Layer& l = layer();
    if (name_.getText() != juce::String::fromUTF8(l.name)) name_.setText(juce::String::fromUTF8(l.name), false);
    trigger_.setSelectedItemIndex(static_cast<int>(l.trigger), juce::dontSendNotification);
    channel_.setSelectedId(l.channel + 1, juce::dontSendNotification);
    blend_.setSelectedId(static_cast<int>(l.blend) + 1, juce::dontSendNotification);
    noteLow_.setValue(l.noteLow, juce::dontSendNotification);
    noteHigh_.setValue(l.noteHigh, juce::dontSendNotification);
    voices_.setValue(l.voices, juce::dontSendNotification);
    voices_.setEnabled(l.trigger == mpx::TriggerMode::Poly);
    wrap_.setToggleState(l.wrap, juce::dontSendNotification);
    stack_.setToggleState(l.stack, juce::dontSendNotification);
    stack_.setEnabled(l.trigger == mpx::TriggerMode::Poly);
    const bool legato = l.trigger == mpx::TriggerMode::Legato;
    glideMode_.setSelectedItemIndex(static_cast<int>(l.glideMode), juce::dontSendNotification);
    glideMode_.setEnabled(legato);
    if (layerKnobs_.size() > 1) layerKnobs_[1]->setEnabled(legato);
    for (int k = 0; k < mpx::kNumLfos; ++k) {
        const auto& o = l.lfo[k];
        lfoShape_[k].setSelectedId(static_cast<int>(o.shape) + 1, juce::dontSendNotification);
        lfoBeats_[k].setSelectedId(beatIndex(o.beats) + 1, juce::dontSendNotification);
        lfoSync_[k].setToggleState(o.sync, juce::dontSendNotification);
        lfoMode_[k].setSelectedId(o.oneShot ? 3 : (o.retrigger ? 2 : 1), juce::dontSendNotification);
        lfoBipolar_[k].setToggleState(o.bipolar, juce::dontSendNotification);
        lfoBeats_[k].setVisible(o.sync);
        if (!lfoKnobs_[k].empty()) lfoKnobs_[k][0]->setVisible(!o.sync);
    }
    for (auto* group : {&layerKnobs_, &emitterKnobs_, &envKnobs_[0], &envKnobs_[1], &lfoKnobs_[0], &lfoKnobs_[1], &lfoKnobs_[2]})
        for (auto& k : *group) k->refresh();
    updating_ = false;
    repaint();
}

void LayerEditor::setLiveValues(const float* values) {
    for (auto* group : {&layerKnobs_, &emitterKnobs_, &envKnobs_[0], &envKnobs_[1], &lfoKnobs_[0], &lfoKnobs_[1], &lfoKnobs_[2]})
        for (auto& k : *group) k->setLive(values);
}

void LayerEditor::paint(juce::Graphics& g) {
    theme::drawSection(g, secLayer_, "Layer");
    theme::drawSection(g, secEmitter_, "Emitter");
    theme::drawSection(g, secEnv_[0], "Envelope 1 (voice lifetime)");
    theme::drawSection(g, secEnv_[1], "Envelope 2");
    for (int k = 0; k < mpx::kNumLfos; ++k) theme::drawSection(g, secLfo_[k], "LFO " + juce::String(k + 1));
    if (!isEnabled()) {
        g.setColour(theme::background.withAlpha(0.6f));
        g.fillRect(getLocalBounds());
    }
}

void LayerEditor::resized() {
    auto r = getLocalBounds();
    const int gap = 6;
    auto content = [](juce::Rectangle<int> sec) { return sec.reduced(8, 6).withTrimmedTop(18); };

    secLayer_ = r.removeFromTop(108);
    r.removeFromTop(gap);
    secEmitter_ = r.removeFromTop(112);
    r.removeFromTop(gap);
    auto envRow = r.removeFromTop(112);
    secEnv_[0] = envRow.removeFromLeft((envRow.getWidth() - gap) / 2);
    envRow.removeFromLeft(gap);
    secEnv_[1] = envRow;
    r.removeFromTop(gap);
    auto lfoRow = r;
    int lw = (lfoRow.getWidth() - 2 * gap) / 3;
    for (int k = 0; k < mpx::kNumLfos; ++k) {
        secLfo_[k] = lfoRow.removeFromLeft(lw);
        lfoRow.removeFromLeft(gap);
    }

    // Layer section
    {
        auto c = content(secLayer_);
        auto top = c.removeFromTop(24);
        name_.setBounds(top.removeFromLeft(170));
        top.removeFromLeft(8);
        trigger_.setBounds(top.removeFromLeft(90));
        top.removeFromLeft(8);
        channel_.setBounds(top.removeFromLeft(80));
        top.removeFromLeft(8);
        blend_.setBounds(top.removeFromLeft(110));
        top.removeFromLeft(8);
        wrap_.setBounds(top.removeFromLeft(70));
        c.removeFromTop(8);
        auto bottom = c.removeFromTop(24);
        noteLowLabel_.setBounds(bottom.removeFromLeft(34));
        noteLow_.setBounds(bottom.removeFromLeft(120));
        bottom.removeFromLeft(10);
        noteHighLabel_.setBounds(bottom.removeFromLeft(36));
        noteHigh_.setBounds(bottom.removeFromLeft(120));
        bottom.removeFromLeft(10);
        voicesLabel_.setBounds(bottom.removeFromLeft(44));
        voices_.setBounds(bottom.removeFromLeft(100));
        bottom.removeFromLeft(10);
        stack_.setBounds(bottom.removeFromLeft(70));
        bottom.removeFromLeft(10);
        glideMode_.setBounds(bottom.removeFromLeft(110));
        auto knobArea = content(secLayer_).removeFromRight(140);
        if (layerKnobs_.size() > 0) layerKnobs_[0]->setBounds(knobArea.removeFromRight(70));
        if (layerKnobs_.size() > 1) layerKnobs_[1]->setBounds(knobArea);
    }
    auto layoutKnobs = [](std::vector<std::unique_ptr<ParamKnob>>& knobs, juce::Rectangle<int> area, int width) {
        for (auto& k : knobs) k->setBounds(area.removeFromLeft(width).reduced(2, 0));
    };
    layoutKnobs(emitterKnobs_, content(secEmitter_), juce::jmin(90, content(secEmitter_).getWidth() / 7));
    for (int e = 0; e < mpx::kNumEnvs; ++e)
        layoutKnobs(envKnobs_[e], content(secEnv_[e]), juce::jmin(84, content(secEnv_[e]).getWidth() / 4));
    for (int k = 0; k < mpx::kNumLfos; ++k) {
        auto c = content(secLfo_[k]);
        auto top = c.removeFromTop(22);
        lfoShape_[k].setBounds(top.removeFromLeft(top.getWidth() * 3 / 5));
        top.removeFromLeft(4);
        lfoMode_[k].setBounds(top);
        c.removeFromTop(4);
        auto toggles = c.removeFromBottom(22);
        int tw = toggles.getWidth() / 2;
        lfoSync_[k].setBounds(toggles.removeFromLeft(tw));
        lfoBipolar_[k].setBounds(toggles);
        auto knobs = c;
        int kw = knobs.getWidth() / 2;
        auto rateArea = knobs.removeFromLeft(kw);
        if (lfoKnobs_[k].size() == 2) {
            lfoKnobs_[k][0]->setBounds(rateArea.reduced(2, 0));
            lfoKnobs_[k][1]->setBounds(knobs.reduced(2, 0));
        }
        lfoBeats_[k].setBounds(rateArea.withSizeKeepingCentre(rateArea.getWidth() - 8, 24));
    }
}
