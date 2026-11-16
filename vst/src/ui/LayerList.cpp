#include "LayerList.h"

#include "../PluginProcessor.h"
#include "Theme.h"

namespace {
constexpr int kRowH = 30;
constexpr int kHeaderH = 22;
}  // namespace

LayerList::LayerList(MidipixiProcessor& p) : proc_(p) {
    for (auto* b : {&add_, &dup_, &del_, &up_, &down_}) addAndMakeVisible(b);
    add_.setTooltip("Add a layer");
    dup_.setTooltip("Duplicate the selected layer");
    del_.setTooltip("Delete the selected layer");
    up_.setTooltip("Move layer up (rendered later, on top)");
    down_.setTooltip("Move layer down (rendered earlier, underneath)");

    add_.onClick = [this] {
        auto& pr = proc_.preset();
        mpx::Layer l = mpx::makeInitLayer();
        mpx::copyName(l.name, sizeof(l.name), ("Layer " + juce::String(pr.layerCount + 1)).toRawUTF8());
        int i = pr.addLayer(l);
        if (i >= 0) selected_ = i;
        changed();
    };
    dup_.onClick = [this] {
        auto& pr = proc_.preset();
        if (selected_ >= pr.layerCount) return;
        int i = pr.addLayer(pr.layers[selected_]);
        if (i >= 0) selected_ = i;
        changed();
    };
    del_.onClick = [this] {
        auto& pr = proc_.preset();
        pr.removeLayer(selected_);
        selected_ = juce::jlimit(0, juce::jmax(0, pr.layerCount - 1), selected_);
        changed();
    };
    // The list shows the top of the stack (highest index) first.
    up_.onClick = [this] {
        if (selected_ + 1 >= proc_.preset().layerCount) return;
        proc_.preset().moveLayer(selected_, selected_ + 1);
        ++selected_;
        changed();
    };
    down_.onClick = [this] {
        if (selected_ <= 0) return;
        proc_.preset().moveLayer(selected_, selected_ - 1);
        --selected_;
        changed();
    };
}

void LayerList::changed() {
    proc_.presetEdited();
    refresh();
    if (onStructureChanged) onStructureChanged();
    if (onSelect) onSelect(selected_);
}

void LayerList::setSelected(int index) {
    selected_ = juce::jlimit(0, juce::jmax(0, proc_.preset().layerCount - 1), index);
    refresh();
}

void LayerList::refresh() {
    const auto& pr = proc_.preset();
    add_.setEnabled(pr.layerCount < mpx::kMaxLayers);
    dup_.setEnabled(pr.layerCount > 0 && pr.layerCount < mpx::kMaxLayers);
    del_.setEnabled(pr.layerCount > 0);
    up_.setEnabled(selected_ + 1 < pr.layerCount);
    down_.setEnabled(selected_ > 0);
    repaint();
}

void LayerList::setActivity(const int* voices, int count) {
    bool diff = false;
    for (int i = 0; i < 8 && i < count; ++i) {
        diff |= (activity_[i] > 0) != (voices[i] > 0);
        activity_[i] = voices[i];
    }
    if (diff) repaint();
}

juce::Rectangle<int> LayerList::rowBounds(int i) const {
    // Top of the list = top of the stack (rendered last), like layers in an image editor.
    int n = proc_.preset().layerCount;
    int visual = n - 1 - i;
    return {4, kHeaderH + visual * kRowH, getWidth() - 8, kRowH - 2};
}

void LayerList::paint(juce::Graphics& g) {
    g.setColour(theme::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(theme::textDim);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.drawText("LAYERS", 10, 2, getWidth() - 20, kHeaderH - 4, juce::Justification::centredLeft);

    const auto& pr = proc_.preset();
    for (int i = 0; i < pr.layerCount; ++i) {
        auto r = rowBounds(i);
        const auto& l = pr.layers[i];
        bool sel = i == selected_;
        g.setColour(sel ? theme::accent.withAlpha(0.25f) : theme::panelLight);
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
        if (sel) {
            g.setColour(theme::accent);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
        }
        // enabled checkbox
        auto box = r.removeFromLeft(24).withSizeKeepingCentre(12, 12).toFloat();
        g.setColour(theme::textDim);
        g.drawRoundedRectangle(box, 2.0f, 1.0f);
        if (l.enabled) {
            g.setColour(theme::accent);
            g.fillRoundedRectangle(box.reduced(2.5f), 1.5f);
        }
        // activity dot
        auto dot = r.removeFromRight(16).withSizeKeepingCentre(7, 7).toFloat();
        g.setColour(activity_[i] > 0 ? theme::ok : theme::outline);
        g.fillEllipse(dot);
        g.setColour(l.enabled ? theme::text : theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(juce::String::fromUTF8(l.name), r.removeFromTop(16), juce::Justification::centredLeft);
        g.setColour(theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText(juce::String(mpx::toString(l.trigger)) + " | " + mpx::displayName(l.blend), r,
                   juce::Justification::centredLeft);
    }
    if (pr.layerCount == 0) {
        g.setColour(theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("No layers - press +", getLocalBounds().withTrimmedTop(kHeaderH).withHeight(40),
                   juce::Justification::centred);
    }
}

void LayerList::resized() {
    auto r = getLocalBounds().reduced(4);
    auto row = r.removeFromBottom(24);
    int w = row.getWidth() / 5;
    add_.setBounds(row.removeFromLeft(w).reduced(1));
    dup_.setBounds(row.removeFromLeft(w).reduced(1));
    del_.setBounds(row.removeFromLeft(w).reduced(1));
    down_.setBounds(row.removeFromLeft(w).reduced(1));
    up_.setBounds(row.reduced(1));
}

void LayerList::mouseDown(const juce::MouseEvent& e) {
    auto& pr = proc_.preset();
    for (int i = 0; i < pr.layerCount; ++i) {
        auto r = rowBounds(i);
        if (!r.contains(e.getPosition())) continue;
        if (e.x < r.getX() + 24) {
            pr.layers[i].enabled = !pr.layers[i].enabled;
            proc_.presetEdited();
            repaint();
            return;
        }
        selected_ = i;
        refresh();
        if (onSelect) onSelect(i);
        return;
    }
}
