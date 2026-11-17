#pragma once
// Core 1: runs the engine at a fixed frame rate and drives the strip.
// Core 0 talks to it through a MIDI event queue and a preset/config mailbox.

#include <cstdint>

#include "midipixi/output.h"
#include "midipixi/preset.h"

namespace gear {

struct MidiEvent {
    uint64_t timeUs;
    uint8_t data[3];
    uint8_t len;
};

struct RenderStats {
    uint32_t frames = 0;
    uint32_t lastRenderUs = 0;  // engine render + output conversion of the last frame
    uint32_t maxRenderUs = 0;   // worst case since the last stats read
    uint32_t avgRenderUs = 0;
    uint32_t droppedMidi = 0;
    uint32_t lateFrames = 0;    // frames that could not keep the frame rate
    uint32_t refreshes = 0;     // strip refreshes (dither steps) in the last frame
    int voices = 0;
    float currentMa = 0.0f;
    float limiter = 1.0f;
    float bpm = 0.0f;
};

// Must be called before renderLaunch().
void renderInit(const mpx::OutputConfig& cfg, const mpx::Preset& preset);
// Starts core 1 and waits until it accepts flash lockouts (flash writes are
// only possible after this).
void renderLaunch();

bool renderPushMidi(const MidiEvent& ev);  // false when the queue is full
void renderSubmitPreset(const mpx::Preset& p, bool keepVoices);
void renderSubmitConfig(const mpx::OutputConfig& c);
RenderStats renderReadStats(bool resetMax);

} // namespace gear
