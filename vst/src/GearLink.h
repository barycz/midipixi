#pragma once
// Talks to the gear on a background thread so the UI never blocks on USB.

#include <juce_events/juce_events.h>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "midipixi/host/gear_client.h"
#include "midipixi/preset.h"

class GearLink : private juce::Thread {
public:
    struct Status {
        bool connected = false;
        juce::String port;
        juce::String description;  // "midipixi gear 0.1.0 (pico2)"
        juce::String message;      // last result / error for the status line
        bool messageIsError = false;
        int slotCount = 0;
        int activeSlot = -1;
        std::vector<mpx::host::SlotInfo> slots;
        std::vector<mpx::host::PortInfo> ports;
        bool busy = false;
    };

    GearLink();
    ~GearLink() override;

    // All operations are asynchronous; `changed` fires (on the message thread) when Status changes.
    void refreshPorts();
    void connect(const juce::String& port);  // empty: auto-detect
    void disconnect();
    void refreshSlots();
    void upload(int slot, const mpx::Preset& p, bool select);
    void select(int slot);

    // Live mode: every pushLive() sends the preset to the gear (coalesced, latest wins).
    void setLiveMode(bool on);
    bool liveMode() const { return liveMode_.load(); }
    void pushLive(const mpx::Preset& p);

    Status status() const;
    juce::ChangeBroadcaster changed;

private:
    using Job = std::function<void(mpx::host::GearClient&)>;
    void post(Job job);
    void run() override;
    void setMessage(const juce::String& msg, bool error);
    void updateConnection(mpx::host::GearClient& c);
    void readSlots(mpx::host::GearClient& c);

    mutable juce::CriticalSection lock_;
    std::deque<Job> jobs_;
    Status status_;
    std::unique_ptr<mpx::Preset> livePreset_;
    bool liveDirty_ = false;
    std::atomic<bool> liveMode_{false};
    juce::WaitableEvent wake_;
};
