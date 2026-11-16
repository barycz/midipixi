#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

#include "GearLink.h"
#include "midipixi/engine.h"
#include "midipixi/preset.h"

// Snapshot of one rendered frame plus the live modulation values (for the UI).
struct FrameSnapshot {
    std::array<mpx::Rgb, mpx::kMaxPixels> pixels{};
    int pixelCount = 0;
    float live[mpx::kMaxLayers][mpx::kNumDests] = {};
    bool liveValid[mpx::kMaxLayers] = {};
    int voices[mpx::kMaxLayers] = {};
    double bpm = 120.0;
    uint32_t serial = 0;
};

class MidipixiProcessor : public juce::AudioProcessor {
public:
    MidipixiProcessor();
    ~MidipixiProcessor() override;

    // --- juce::AudioProcessor --------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "midipixi"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Preset editing (message thread) ------------------------------------------
    // The editable preset. After changing it call presetEdited().
    mpx::Preset& preset() { return *editPreset_; }
    // Publishes edits to the audio thread (voices keep playing) and the gear (live mode).
    void presetEdited();
    // Replaces the whole preset (resets voices) and notifies listeners.
    void loadPreset(const mpx::Preset& p);
    void loadFactoryPreset(int index);
    static int numFactoryPresets();
    static juce::String factoryPresetName(int index);

    juce::String presetToJson(bool pretty) const;
    bool presetFromJson(const juce::String& json, juce::String& error);

    // Broadcasts when the preset was replaced (load, init, state restore).
    juce::ChangeBroadcaster presetReplaced;

    int ledCount() const { return ledCount_.load(); }
    void setLedCount(int n);

    // Latest rendered frame. Returns false when nothing new since `lastSerial`.
    bool readFrame(FrameSnapshot& out, uint32_t lastSerial);

    GearLink& gear() { return gear_; }

private:
    void renderFrame();
    void publishPreset(bool resetVoices);

    std::unique_ptr<mpx::Engine> engine_;          // audio thread
    std::unique_ptr<mpx::Preset> editPreset_;      // message thread
    std::unique_ptr<mpx::Preset> pendingPreset_;   // handover, guarded by presetLock_
    juce::SpinLock presetLock_;
    bool pendingDirty_ = false;
    bool pendingReset_ = false;

    std::unique_ptr<FrameSnapshot> frame_;         // guarded by frameLock_
    juce::SpinLock frameLock_;
    uint32_t frameSerial_ = 0;

    double sampleRate_ = 48000.0;
    double samplesToNextFrame_ = 0.0;
    static constexpr double kFps = 60.0;

    std::atomic<int> ledCount_{60};
    int currentProgram_ = 0;
    GearLink gear_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidipixiProcessor)
};
