#include "ModMatrixView.h"

#include "../PluginProcessor.h"
#include "ModKnob.h"
#include "Theme.h"

namespace {

constexpr int kRowH = 30;

// Source combo items: id = 1 + type * 256 + index; CC and macros use one entry
// plus a number box.
const mpx::SourceType kSourceOrder[] = {
    mpx::SourceType::Env1,     mpx::SourceType::Env2,   mpx::SourceType::Lfo1,      mpx::SourceType::Lfo2,
    mpx::SourceType::Lfo3,     mpx::SourceType::Note,   mpx::SourceType::Velocity,  mpx::SourceType::Aftertouch,
    mpx::SourceType::Gate,     mpx::SourceType::Random, mpx::SourceType::Macro,     mpx::SourceType::CC,
    mpx::SourceType::PitchBend, mpx::SourceType::Pressure, mpx::SourceType::Beat};

juce::String sourceTypeLabel(mpx::SourceType t) {
    using S = mpx::SourceType;
    switch (t) {
        case S::Env1: return "Envelope 1";
        case S::Env2: return "Envelope 2";
        case S::Lfo1: return "LFO 1";
        case S::Lfo2: return "LFO 2";
        case S::Lfo3: return "LFO 3";
        case S::Note: return "Note";
        case S::Velocity: return "Velocity";
        case S::Aftertouch: return "Aftertouch";
        case S::Gate: return "Gate";
        case S::Random: return "Random";
        case S::Macro: return "Macro";
        case S::CC: return "MIDI CC";
        case S::PitchBend: return "Pitch bend";
        case S::Pressure: return "Pressure";
        case S::Beat: return "Beat";
        default: return "-";
    }
}

void fillSourceCombo(juce::ComboBox& c, bool withNone) {
    if (withNone) c.addItem("(none)", 1);
    for (auto t : kSourceOrder) c.addItem(sourceTypeLabel(t), 2 + static_cast<int>(t));
}

mpx::SourceType comboType(const juce::ComboBox& c) {
    int id = c.getSelectedId();
    return id <= 1 ? mpx::SourceType::None : static_cast<mpx::SourceType>(id - 2);
}

bool hasIndex(mpx::SourceType t) { return t == mpx::SourceType::CC || t == mpx::SourceType::Macro; }

}  // namespace

class ModMatrixView::Row : public juce::Component {
public:
    Row(ModMatrixView& owner, int index) : owner_(owner), index_(index) {
        fillSourceCombo(source_, false);
        fillSourceCombo(via_, true);
        for (int d = 0; d < mpx::kNumDests; ++d) dest_.addItem(mpx::displayName(static_cast<mpx::Dest>(d)), d + 1);
        for (auto* s : {&sourceIndex_, &viaIndex_}) {
            s->setSliderStyle(juce::Slider::IncDecButtons);
            s->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 22);
            s->setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
        }
        amount_.setSliderStyle(juce::Slider::LinearBar);
        amount_.setRange(-1.0, 1.0, 0.001);
        amount_.setDoubleClickReturnValue(true, 0.5);
        amount_.setColour(juce::Slider::trackColourId, theme::accent.withAlpha(0.5f));
        amount_.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
        remove_.setButtonText("x");
        remove_.setTooltip("Remove this modulation");
        for (juce::Component* c : std::initializer_list<juce::Component*>{&source_, &sourceIndex_, &via_, &viaIndex_,
                                                                          &dest_, &amount_, &remove_})
            addAndMakeVisible(c);
        load();

        source_.onChange = [this] { store(true); };
        via_.onChange = [this] { store(true); };
        sourceIndex_.onValueChange = [this] { store(false); };
        viaIndex_.onValueChange = [this] { store(false); };
        dest_.onChange = [this] { store(true); };
        amount_.onValueChange = [this] { store(false); };
        remove_.onClick = [this] {
            if (auto* l = owner_.layer()) l->removeMod(index_);
            owner_.changed(true);
        };
    }

    void paint(juce::Graphics& g) override {
        auto* l = owner_.layer();
        if (!l || index_ >= l->modCount) return;
        g.setColour(theme::sourceColour(l->mods[index_].source.type));
        g.fillRoundedRectangle(getLocalBounds().removeFromLeft(4).reduced(0, 4).toFloat(), 2.0f);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(0, 3).withTrimmedLeft(10);
        auto place = [&r](juce::Component& c, int w) {
            c.setBounds(r.removeFromLeft(w));
            r.removeFromLeft(6);
        };
        place(source_, 120);
        place(sourceIndex_, 80);
        place(via_, 120);
        place(viaIndex_, 80);
        place(dest_, 130);
        remove_.setBounds(r.removeFromRight(26));
        r.removeFromRight(6);
        amount_.setBounds(r);
    }

private:
    void load() {
        auto* l = owner_.layer();
        if (!l || index_ >= l->modCount) return;
        const mpx::ModRow& m = l->mods[index_];
        loading_ = true;
        source_.setSelectedId(2 + static_cast<int>(m.source.type), juce::dontSendNotification);
        via_.setSelectedId(m.via.isNone() ? 1 : 2 + static_cast<int>(m.via.type), juce::dontSendNotification);
        dest_.setSelectedId(static_cast<int>(m.dest) + 1, juce::dontSendNotification);
        amount_.setValue(m.amount, juce::dontSendNotification);
        setupIndex(sourceIndex_, m.source);
        setupIndex(viaIndex_, m.via);
        loading_ = false;
    }

    static void setupIndex(juce::Slider& s, mpx::ModSource src) {
        bool show = hasIndex(src.type);
        s.setVisible(show);
        if (!show) return;
        if (src.type == mpx::SourceType::Macro) {
            s.setRange(1, mpx::kNumMacros, 1);
            s.setValue(src.index + 1, juce::dontSendNotification);
        } else {
            s.setRange(0, 127, 1);
            s.setValue(src.index, juce::dontSendNotification);
        }
    }

    static mpx::ModSource read(const juce::ComboBox& c, const juce::Slider& idx) {
        mpx::ModSource s{comboType(c), 0};
        if (s.type == mpx::SourceType::Macro) s.index = static_cast<uint8_t>(juce::jlimit(1, mpx::kNumMacros, static_cast<int>(idx.getValue())) - 1);
        if (s.type == mpx::SourceType::CC) s.index = static_cast<uint8_t>(juce::jlimit(0, 127, static_cast<int>(idx.getValue())));
        return s;
    }

    void store(bool structural) {
        if (loading_) return;
        auto* l = owner_.layer();
        if (!l || index_ >= l->modCount) return;
        mpx::ModRow& m = l->mods[index_];
        mpx::SourceType oldSource = m.source.type, oldVia = m.via.type;
        m.source = read(source_, sourceIndex_);
        m.via = read(via_, viaIndex_);
        m.dest = static_cast<mpx::Dest>(juce::jmax(0, dest_.getSelectedId() - 1));
        m.amount = static_cast<float>(amount_.getValue());
        if (m.source.type != oldSource || m.via.type != oldVia) {
            loading_ = true;
            setupIndex(sourceIndex_, m.source);
            setupIndex(viaIndex_, m.via);
            loading_ = false;
            m.source = read(source_, sourceIndex_);
            m.via = read(via_, viaIndex_);
        }
        repaint();
        owner_.changed(structural);
    }

    ModMatrixView& owner_;
    int index_;
    bool loading_ = false;
    juce::ComboBox source_, via_, dest_;
    juce::Slider sourceIndex_, viaIndex_, amount_;
    juce::TextButton remove_;
};

ModMatrixView::ModMatrixView(MidipixiProcessor& p) : proc_(p) {
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    add_.onClick = [this] {
        if (auto* l = layer()) {
            if (l->addMod({mpx::SourceType::Lfo1, 0}, mpx::Dest::Hue, 0.5f) >= 0) changed(true);
        }
    };
    addAndMakeVisible(add_);
    hint_.setText("Tip: drag a source from the bottom bar onto any knob with a ring to add a row.",
                  juce::dontSendNotification);
    hint_.setColour(juce::Label::textColourId, theme::textDim);
    hint_.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(hint_);
}

mpx::Layer* ModMatrixView::layer() {
    auto& pr = proc_.preset();
    return layer_ >= 0 && layer_ < pr.layerCount ? &pr.layers[layer_] : nullptr;
}

void ModMatrixView::setLayer(int index) {
    layer_ = index;
    refresh();
}

void ModMatrixView::changed(bool structural) {
    proc_.presetEdited();
    if (structural) {
        // Rebuilding destroys the row that called us: defer it.
        juce::Component::SafePointer<ModMatrixView> safe(this);
        juce::MessageManager::callAsync([safe] {
            if (safe) safe->refresh();
        });
    }
    if (onChanged) onChanged();
}

void ModMatrixView::refresh() {
    rows_.clear();
    auto* l = layer();
    int n = l ? l->modCount : 0;
    for (int i = 0; i < n; ++i) {
        rows_.push_back(std::make_unique<Row>(*this, i));
        content_.addAndMakeVisible(*rows_.back());
    }
    add_.setEnabled(l && l->modCount < mpx::kMaxModRows);
    resized();
    repaint();
}

void ModMatrixView::paint(juce::Graphics& g) {
    auto c = theme::drawSection(g, getLocalBounds(), "Modulation matrix");
    auto header = c.removeFromTop(18).withTrimmedLeft(10);
    g.setColour(theme::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    for (auto [w, t] : {std::pair{206, "Source"}, {206, "Via (scales the source)"}, {136, "Destination"}, {200, "Amount"}}) {
        g.drawText(t, header.removeFromLeft(w), juce::Justification::centredLeft);
    }
    if (rows_.empty()) {
        g.setColour(theme::textDim);
        g.drawText(layer() ? "No modulation yet." : "No layer selected.", c.withHeight(60), juce::Justification::centred);
    }
}

void ModMatrixView::resized() {
    auto c = getLocalBounds().reduced(8, 6).withTrimmedTop(18 + 18);
    auto bottom = c.removeFromBottom(26);
    add_.setBounds(bottom.removeFromLeft(150));
    bottom.removeFromLeft(10);
    hint_.setBounds(bottom);
    c.removeFromBottom(6);
    viewport_.setBounds(c);
    int w = c.getWidth() - viewport_.getScrollBarThickness();
    content_.setSize(w, static_cast<int>(rows_.size()) * kRowH);
    for (size_t i = 0; i < rows_.size(); ++i) rows_[i]->setBounds(0, static_cast<int>(i) * kRowH, w, kRowH);
}
