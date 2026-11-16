#include "GearPanel.h"

#include "../PluginProcessor.h"
#include "Theme.h"

GearPanel::GearPanel(MidipixiProcessor& p) : proc_(p) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&port_, &refresh_, &connect_, &slot_, &upload_,
                                                                      &run_, &selectAfter_, &live_, &info_, &message_})
        addAndMakeVisible(c);

    refresh_.onClick = [this] { proc_.gear().refreshPorts(); };
    connect_.onClick = [this] {
        auto& g = proc_.gear();
        if (g.status().connected) {
            g.disconnect();
        } else {
            int i = port_.getSelectedItemIndex();
            g.connect(i > 0 && i - 1 < portNames_.size() ? portNames_[i - 1] : juce::String());
        }
    };
    upload_.onClick = [this] {
        int slot = slot_.getSelectedId() - 1;
        if (slot >= 0) proc_.gear().upload(slot, proc_.preset(), selectAfter_.getToggleState());
    };
    run_.onClick = [this] {
        int slot = slot_.getSelectedId() - 1;
        if (slot >= 0) proc_.gear().select(slot);
    };
    live_.onClick = [this] {
        proc_.gear().setLiveMode(live_.getToggleState());
        if (live_.getToggleState()) proc_.gear().pushLive(proc_.preset());
    };
    selectAfter_.setToggleState(true, juce::dontSendNotification);
    upload_.setTooltip("Store the current preset in the selected gear slot (flash)");
    run_.setTooltip("Make the gear run the selected slot (and boot with it)");
    live_.setTooltip("Send every edit to the gear immediately (not stored)");
    info_.setColour(juce::Label::textColourId, theme::textDim);
    info_.setFont(juce::Font(juce::FontOptions(11.5f)));
    message_.setFont(juce::Font(juce::FontOptions(11.5f)));
    message_.setMinimumHorizontalScale(0.7f);

    proc_.gear().changed.addChangeListener(this);
    update();
}

GearPanel::~GearPanel() { proc_.gear().changed.removeChangeListener(this); }

void GearPanel::changeListenerCallback(juce::ChangeBroadcaster*) { update(); }

void GearPanel::update() {
    auto st = proc_.gear().status();

    // Ports
    juce::StringArray names;
    for (const auto& p : st.ports) names.add(p.name);
    if (names != portNames_ || port_.getNumItems() == 0) {
        juce::String current = port_.getText();
        portNames_ = names;
        port_.clear(juce::dontSendNotification);
        port_.addItem("Auto-detect", 1);
        for (int i = 0; i < static_cast<int>(st.ports.size()); ++i) {
            const auto& p = st.ports[static_cast<size_t>(i)];
            port_.addItem(juce::String(p.name) + (p.isGear ? "  (midipixi gear)" : ""), i + 2);
        }
        port_.setSelectedItemIndex(0, juce::dontSendNotification);
        for (int i = 0; i < port_.getNumItems(); ++i)
            if (port_.getItemText(i) == current) port_.setSelectedItemIndex(i, juce::dontSendNotification);
    }

    // Slots
    int selectedSlot = slot_.getSelectedId();
    slot_.clear(juce::dontSendNotification);
    int slotCount = st.connected && st.slotCount > 0 ? st.slotCount : 32;
    for (int s = 0; s < slotCount; ++s) {
        juce::String label = juce::String(s) + ": ";
        bool found = false;
        for (const auto& si : st.slots)
            if (si.slot == s) {
                label += juce::String::fromUTF8(si.name.c_str());
                found = true;
            }
        if (!found) label += "(empty)";
        if (s == st.activeSlot) label += "  *";
        slot_.addItem(label, s + 1);
    }
    slot_.setSelectedId(selectedSlot > 0 ? selectedSlot : 1, juce::dontSendNotification);

    connect_.setButtonText(st.connected ? "Disconnect" : "Connect");
    port_.setEnabled(!st.connected);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&slot_, &upload_, &run_, &live_, &selectAfter_})
        c->setEnabled(st.connected && !st.busy);
    live_.setToggleState(proc_.gear().liveMode(), juce::dontSendNotification);
    if (!st.connected) live_.setEnabled(false);

    info_.setText(st.connected ? st.description + " on " + st.port : "not connected", juce::dontSendNotification);
    message_.setText(st.busy ? juce::String("working...") : st.message, juce::dontSendNotification);
    message_.setColour(juce::Label::textColourId, st.messageIsError ? theme::error : theme::ok);
    repaint();
}

void GearPanel::paint(juce::Graphics& g) {
    theme::drawSection(g, getLocalBounds(), "Gear");
    auto st = proc_.gear().status();
    auto dot = getLocalBounds().reduced(8, 6).removeFromTop(16).removeFromRight(12).withSizeKeepingCentre(9, 9).toFloat();
    g.setColour(st.connected ? (proc_.gear().liveMode() ? theme::warning : theme::ok) : theme::outline);
    g.fillEllipse(dot);
}

void GearPanel::resized() {
    auto c = getLocalBounds().reduced(8, 6).withTrimmedTop(20);
    auto row = [&c](int h) {
        auto r = c.removeFromTop(h);
        c.removeFromTop(5);
        return r;
    };
    auto r1 = row(24);
    port_.setBounds(r1.removeFromLeft(r1.getWidth() - 52));
    r1.removeFromLeft(4);
    refresh_.setBounds(r1);
    connect_.setBounds(row(24));
    info_.setBounds(row(18));
    slot_.setBounds(row(24));
    auto r2 = row(24);
    upload_.setBounds(r2.removeFromLeft(r2.getWidth() / 2 - 2));
    r2.removeFromLeft(4);
    run_.setBounds(r2);
    selectAfter_.setBounds(row(22));
    live_.setBounds(row(22));
    message_.setBounds(row(34));
}
