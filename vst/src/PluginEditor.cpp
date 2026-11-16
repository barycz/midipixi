#include "PluginEditor.h"

namespace {
constexpr int kWidth = 1240;
constexpr int kHeight = 860;
}  // namespace

MidipixiEditor::MidipixiEditor(MidipixiProcessor& p)
    : AudioProcessorEditor(p), proc_(p), layers_(p), gearPanel_(p) {
    setLookAndFeel(&lnf_);
    frame_ = std::make_unique<FrameSnapshot>();

    // Layer editor whose destination knobs are modulation targets.
    layerEditor_ = std::make_unique<LayerEditor>(p);
    layerEditor_->setKnobFactory([this](const juce::String& name, double min, double max, double def,
                                        ParamKnob::Getter g, ParamKnob::Setter s, ParamKnob::Formatter f, double skew,
                                        std::optional<mpx::Dest> dest) -> std::unique_ptr<ParamKnob> {
        if (!dest)
            return std::make_unique<ParamKnob>(name, min, max, def, std::move(g), std::move(s), std::move(f), skew);
        ModKnob::Context ctx;
        ctx.layer = [this] { return currentLayer(); };
        ctx.modsChanged = [this] {
            proc_.presetEdited();
            matrix_->refresh();
            modulationChanged();
        };
        return std::make_unique<ModKnob>(name, min, max, def, std::move(g), std::move(s), std::move(f), skew, *dest,
                                         std::move(ctx));
    });
    matrix_ = std::make_unique<ModMatrixView>(p);
    matrix_->onChanged = [this] { modulationChanged(); };

    tabs_.setTabBarDepth(26);
    tabs_.setOutline(0);
    tabs_.addTab("Layer", theme::panel, layerEditor_.get(), false);
    tabs_.addTab("Mod matrix", theme::panel, matrix_.get(), false);
    addAndMakeVisible(tabs_);

    presetName_.setInputRestrictions(mpx::kNameLen - 1);
    presetName_.setTooltip("Preset name");
    presetName_.onTextChange = [this] {
        if (updating_) return;
        mpx::copyName(proc_.preset().name, mpx::kNameLen, presetName_.getText().toRawUTF8());
        proc_.presetEdited();
    };
    addAndMakeVisible(presetName_);

    factory_.setTextWhenNothingSelected("Factory presets");
    for (int i = 0; i < MidipixiProcessor::numFactoryPresets(); ++i)
        factory_.addItem(MidipixiProcessor::factoryPresetName(i), i + 1);
    factory_.onChange = [this] {
        int i = factory_.getSelectedId() - 1;
        if (i >= 0) proc_.loadFactoryPreset(i);
        factory_.setSelectedId(0, juce::dontSendNotification);
    };
    addAndMakeVisible(factory_);

    init_.onClick = [this] { proc_.loadPreset(mpx::makeInitPreset()); };
    init_.setTooltip("Start from the init patch (one note layer)");
    load_.onClick = [this] { loadFromFile(); };
    save_.onClick = [this] { saveToFile(); };
    for (auto* b : {&init_, &load_, &save_}) addAndMakeVisible(b);

    ledCount_.setSliderStyle(juce::Slider::IncDecButtons);
    ledCount_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 22);
    ledCount_.setRange(1, mpx::kMaxPixels, 1);
    ledCount_.setValue(proc_.ledCount(), juce::dontSendNotification);
    ledCount_.onValueChange = [this] { proc_.setLedCount(static_cast<int>(ledCount_.getValue())); };
    ledCount_.setTooltip("Number of LEDs in the simulator (the gear has its own setting)");
    addAndMakeVisible(ledCount_);
    ledLabel_.setText("LEDs", juce::dontSendNotification);
    ledLabel_.setColour(juce::Label::textColourId, theme::textDim);
    addAndMakeVisible(ledLabel_);
    bpmLabel_.setColour(juce::Label::textColourId, theme::textDim);
    bpmLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmLabel_);

    addAndMakeVisible(strip_);
    addAndMakeVisible(layers_);
    addAndMakeVisible(gearPanel_);

    layers_.onSelect = [this](int i) { selectLayer(i); };
    layers_.onStructureChanged = [this] { selectLayer(layers_.selected()); };

    buildSources();
    buildMacros();
    proc_.presetReplaced.addChangeListener(this);
    refreshAll();

    setSize(kWidth, kHeight);
    startTimerHz(60);
}

MidipixiEditor::~MidipixiEditor() {
    proc_.presetReplaced.removeChangeListener(this);
    tabs_.clearTabs();
    setLookAndFeel(nullptr);
}

mpx::Layer* MidipixiEditor::currentLayer() {
    auto& pr = proc_.preset();
    int i = layers_.selected();
    return i >= 0 && i < pr.layerCount ? &pr.layers[i] : nullptr;
}

void MidipixiEditor::buildSources() {
    using S = mpx::SourceType;
    for (S t : {S::Env1, S::Env2, S::Lfo1, S::Lfo2, S::Lfo3, S::Note, S::Velocity, S::Aftertouch, S::Gate, S::Random,
                S::PitchBend, S::Pressure, S::Beat}) {
        auto chip = std::make_unique<ModSourceChip>(mpx::ModSource{t, 0});
        chip->setTooltip("Drag onto a knob to modulate it");
        addAndMakeVisible(*chip);
        sourceChips_.push_back(std::move(chip));
    }
    auto cc = std::make_unique<ModSourceChip>(mpx::ModSource{S::CC, 1});
    addAndMakeVisible(*cc);
    sourceChips_.push_back(std::move(cc));
}

void MidipixiEditor::buildMacros() {
    macroKnobs_.clear();
    macroChips_.clear();
    for (int m = 0; m < mpx::kNumMacros; ++m) {
        auto k = std::make_unique<ParamKnob>(
            "", 0.0, 1.0, 0.0, [this, m] { return proc_.preset().macros[m].value; },
            [this, m](float v) {
                proc_.preset().macros[m].value = v;
                proc_.presetEdited();
            },
            fmt::percent);
        addAndMakeVisible(*k);
        macroKnobs_.push_back(std::move(k));
        auto chip = std::make_unique<ModSourceChip>(mpx::ModSource{mpx::SourceType::Macro, static_cast<uint8_t>(m)});
        addAndMakeVisible(*chip);
        macroChips_.push_back(std::move(chip));
    }
}

void MidipixiEditor::changeListenerCallback(juce::ChangeBroadcaster*) { refreshAll(); }

void MidipixiEditor::selectLayer(int index) {
    layers_.setSelected(index);
    layerEditor_->setLayer(layers_.selected());
    matrix_->setLayer(layers_.selected());
}

void MidipixiEditor::modulationChanged() {
    layerEditor_->repaint();
    layers_.repaint();
}

void MidipixiEditor::refreshAll() {
    updating_ = true;
    presetName_.setText(juce::String::fromUTF8(proc_.preset().name), false);
    updating_ = false;
    selectLayer(layers_.selected());
    for (int m = 0; m < static_cast<int>(macroKnobs_.size()); ++m) {
        const auto& macro = proc_.preset().macros[m];
        auto tip = juce::String::fromUTF8(macro.name) +
                   (macro.cc <= 127 ? " - follows MIDI CC " + juce::String(macro.cc) : juce::String(" - no CC"));
        macroKnobs_[static_cast<size_t>(m)]->refresh();
        macroKnobs_[static_cast<size_t>(m)]->slider().setTooltip(tip);
        macroChips_[static_cast<size_t>(m)]->setTooltip(tip + ". Drag onto a knob to modulate it.");
    }
    repaint();
}

void MidipixiEditor::timerCallback() {
    if (!proc_.readFrame(*frame_, frameSerial_)) return;
    frameSerial_ = frame_->serial;
    strip_.setPixels(frame_->pixels.data(), frame_->pixelCount);
    layers_.setActivity(frame_->voices, mpx::kMaxLayers);
    int sel = layers_.selected();
    bool valid = sel >= 0 && sel < mpx::kMaxLayers && frame_->liveValid[sel];
    layerEditor_->setLiveValues(valid ? frame_->live[sel] : nullptr);
    bpmLabel_.setText(juce::String(frame_->bpm, 1) + " BPM", juce::dontSendNotification);
}

void MidipixiEditor::loadFromFile() {
    chooser_ = std::make_unique<juce::FileChooser>("Load midipixi preset",
                                                   juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                   "*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (!f.existsAsFile()) return;
                              juce::String err;
                              if (!proc_.presetFromJson(f.loadFileAsString(), err))
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                         "Cannot load preset", err);
                          });
}

void MidipixiEditor::saveToFile() {
    juce::String name = juce::String::fromUTF8(proc_.preset().name);
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save midipixi preset", dir.getChildFile(juce::File::createLegalFileName(name) + ".json"), "*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (!f.hasFileExtension("json")) f = f.withFileExtension("json");
                              if (!f.replaceWithText(proc_.presetToJson(true) + "\n"))
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                         "Cannot save preset", f.getFullPathName());
                          });
}

void MidipixiEditor::paint(juce::Graphics& g) {
    g.fillAll(theme::background);
    auto header = getLocalBounds().removeFromTop(44).reduced(10, 0);
    g.setColour(theme::accent);
    g.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    g.drawText("midipixi", header.removeFromLeft(120), juce::Justification::centredLeft);
    theme::drawSection(g, sourceArea_, "Modulation sources - drag onto a knob");
    theme::drawSection(g, macroArea_, "Macros");
}

void MidipixiEditor::resized() {
    auto r = getLocalBounds();
    auto header = r.removeFromTop(44).reduced(10, 8);
    header.removeFromLeft(124);
    presetName_.setBounds(header.removeFromLeft(220));
    header.removeFromLeft(8);
    factory_.setBounds(header.removeFromLeft(170));
    header.removeFromLeft(8);
    init_.setBounds(header.removeFromLeft(56));
    header.removeFromLeft(4);
    load_.setBounds(header.removeFromLeft(70));
    header.removeFromLeft(4);
    save_.setBounds(header.removeFromLeft(70));
    bpmLabel_.setBounds(header.removeFromRight(90));
    ledCount_.setBounds(header.removeFromRight(110));
    ledLabel_.setBounds(header.removeFromRight(40));

    r.reduce(10, 0);
    strip_.setBounds(r.removeFromTop(140));
    r.removeFromTop(8);
    auto bottom = r.removeFromBottom(118);
    r.removeFromBottom(8);
    r.removeFromBottom(0);

    auto left = r.removeFromLeft(210);
    layers_.setBounds(left);
    r.removeFromLeft(8);
    auto right = r.removeFromRight(240);
    gearPanel_.setBounds(right.removeFromTop(290));
    r.removeFromRight(8);
    tabs_.setBounds(r);

    // Bottom: sources (left) and macros (right)
    sourceArea_ = bottom.removeFromLeft(bottom.getWidth() * 4 / 10);
    bottom.removeFromLeft(8);
    macroArea_ = bottom;
    {
        auto c = sourceArea_.reduced(8, 6).withTrimmedTop(20);
        const int cols = 7;
        int cw = c.getWidth() / cols;
        int ch = juce::jmin(30, c.getHeight() / 2);
        for (size_t i = 0; i < sourceChips_.size(); ++i) {
            int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
            sourceChips_[i]->setBounds(c.getX() + col * cw + 2, c.getY() + row * (ch + 6) + 2, cw - 4, ch);
        }
    }
    {
        auto c = macroArea_.reduced(8, 6).withTrimmedTop(18);
        int w = c.getWidth() / mpx::kNumMacros;
        for (int m = 0; m < mpx::kNumMacros; ++m) {
            auto col = c.removeFromLeft(w).reduced(3, 0);
            macroChips_[static_cast<size_t>(m)]->setBounds(col.removeFromTop(20).withSizeKeepingCentre(juce::jmin(46, col.getWidth()), 20));
            macroKnobs_[static_cast<size_t>(m)]->setBounds(col);
        }
    }
}
