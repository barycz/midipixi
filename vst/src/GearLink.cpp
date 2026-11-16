#include "GearLink.h"

GearLink::GearLink() : juce::Thread("midipixi gear link") {
    livePreset_ = std::make_unique<mpx::Preset>();
    startThread(juce::Thread::Priority::low);
    refreshPorts();
}

GearLink::~GearLink() {
    signalThreadShouldExit();
    wake_.signal();
    stopThread(4000);
}

void GearLink::post(Job job) {
    {
        const juce::ScopedLock sl(lock_);
        jobs_.push_back(std::move(job));
        status_.busy = true;
    }
    wake_.signal();
    changed.sendChangeMessage();
}

GearLink::Status GearLink::status() const {
    const juce::ScopedLock sl(lock_);
    return status_;
}

void GearLink::setMessage(const juce::String& msg, bool error) {
    {
        const juce::ScopedLock sl(lock_);
        status_.message = msg;
        status_.messageIsError = error;
    }
    changed.sendChangeMessage();
}

void GearLink::updateConnection(mpx::host::GearClient& c) {
    const juce::ScopedLock sl(lock_);
    status_.connected = c.isConnected();
    status_.port = c.portName();
    if (!status_.connected) {
        status_.description.clear();
        status_.slots.clear();
        status_.activeSlot = -1;
    }
}

void GearLink::readSlots(mpx::host::GearClient& c) {
    std::vector<mpx::host::SlotInfo> slots;
    int active = -1;
    if (c.list(slots, active)) {
        const juce::ScopedLock sl(lock_);
        status_.slots = std::move(slots);
        status_.activeSlot = active;
    }
}

void GearLink::refreshPorts() {
    post([this](mpx::host::GearClient&) {
        auto ports = mpx::host::listSerialPorts();
        const juce::ScopedLock sl(lock_);
        status_.ports = std::move(ports);
    });
}

void GearLink::connect(const juce::String& port) {
    post([this, port](mpx::host::GearClient& c) {
        if (!c.connect(port.toStdString())) {
            updateConnection(c);
            setMessage(c.lastError(), true);
            return;
        }
        mpx::host::GearInfo info;
        if (!c.hello(info)) {
            juce::String err = c.lastError();
            c.disconnect();
            updateConnection(c);
            setMessage("not a midipixi gear: " + err, true);
            return;
        }
        {
            const juce::ScopedLock sl(lock_);
            status_.description = juce::String(info.device) + " " + info.version + " (" + info.board + ")";
            status_.slotCount = info.slots;
            status_.activeSlot = info.active;
        }
        updateConnection(c);
        readSlots(c);
        setMessage("connected to " + juce::String(c.portName()), false);
    });
}

void GearLink::disconnect() {
    setLiveMode(false);
    post([this](mpx::host::GearClient& c) {
        c.disconnect();
        updateConnection(c);
        setMessage("disconnected", false);
    });
}

void GearLink::refreshSlots() {
    post([this](mpx::host::GearClient& c) {
        if (c.isConnected()) readSlots(c);
    });
}

void GearLink::upload(int slot, const mpx::Preset& p, bool select) {
    auto copy = std::make_shared<mpx::Preset>(p);
    post([this, slot, copy, select](mpx::host::GearClient& c) {
        if (!c.isConnected()) return setMessage("not connected", true);
        if (!c.put(slot, *copy, select)) {
            setMessage("upload failed: " + juce::String(c.lastError()), true);
        } else {
            setMessage("uploaded \"" + juce::String(copy->name) + "\" to slot " + juce::String(slot), false);
        }
        readSlots(c);
        updateConnection(c);
    });
}

void GearLink::select(int slot) {
    post([this, slot](mpx::host::GearClient& c) {
        if (!c.isConnected()) return setMessage("not connected", true);
        if (!c.select(slot))
            setMessage("select failed: " + juce::String(c.lastError()), true);
        else
            setMessage("gear is running slot " + juce::String(slot), false);
        readSlots(c);
    });
}

void GearLink::setLiveMode(bool on) {
    liveMode_ = on;
    changed.sendChangeMessage();
}

void GearLink::pushLive(const mpx::Preset& p) {
    if (!liveMode_) return;
    {
        const juce::ScopedLock sl(lock_);
        *livePreset_ = p;
        liveDirty_ = true;
    }
    wake_.signal();
}

void GearLink::run() {
    mpx::host::GearClient client;
    auto preset = std::make_unique<mpx::Preset>();
    int idleTicks = 0;
    while (!threadShouldExit()) {
        wake_.wait(200);
        for (;;) {
            Job job;
            {
                const juce::ScopedLock sl(lock_);
                if (jobs_.empty()) {
                    status_.busy = false;
                    break;
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job(client);
            changed.sendChangeMessage();
            if (threadShouldExit()) return;
        }

        // Live edits: send the newest preset, at most ~30 times per second.
        bool send = false;
        {
            const juce::ScopedLock sl(lock_);
            if (liveDirty_ && liveMode_) {
                *preset = *livePreset_;
                send = true;
            }
            liveDirty_ = false;
        }
        if (send && client.isConnected()) {
            if (!client.live(*preset)) {
                setMessage("live update failed: " + juce::String(client.lastError()), true);
                if (!client.isConnected()) updateConnection(client);
            } else {
                const juce::ScopedLock sl(lock_);
                status_.activeSlot = -1;
            }
            wait(30);
        }

        // Detect unplugging while idle.
        if (client.isConnected() && !liveMode_) {
            if (++idleTicks >= 10) {  // ~2 s
                idleTicks = 0;
                mpx::host::GearInfo info;
                if (!client.hello(info)) {
                    client.disconnect();
                    updateConnection(client);
                    setMessage("gear disconnected", true);
                }
            }
        }
    }
}
