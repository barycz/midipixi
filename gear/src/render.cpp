#include "render.h"

#include "midipixi/engine.h"
#include "pico/critical_section.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "pico/util/queue.h"
#include "ws2812_output.h"

namespace gear {

namespace {

mpx::Engine g_engine;
mpx::OutputStage g_output;
Ws2812Output g_strip;
uint8_t g_bytes[mpx::kMaxPixels * 3];

queue_t g_midi;
critical_section_t g_lock;

// Mailbox (guarded by g_lock)
mpx::Preset g_pendingPreset;
bool g_hasPendingPreset = false;
bool g_pendingKeepVoices = false;
mpx::OutputConfig g_pendingConfig;
bool g_hasPendingConfig = false;

// Initial state
mpx::OutputConfig g_initConfig;

// Stats (written by core 1, read by core 0; word-sized fields, races are benign)
volatile RenderStats g_stats;
volatile bool g_resetMax = false;
volatile bool g_core1Ready = false;

uint32_t g_core1Stack[2048];  // 8 KB

void applyConfig(const mpx::OutputConfig& c) {
    g_output.configure(c);
    g_engine.setPixelCount(c.ledCount);
}

void core1Main() {
    flash_safe_execute_core_init();  // lets core 0 pause us while writing flash
    g_core1Ready = true;

    g_strip.init(g_initConfig.dataPin);
    applyConfig(g_initConfig);

    uint64_t engineUs = time_us_64();
    uint64_t nextFrameUs = engineUs;
    uint64_t renderSum = 0;
    uint32_t renderCount = 0;

    for (;;) {
        // Mailbox
        if (g_hasPendingConfig || g_hasPendingPreset) {
            critical_section_enter_blocking(&g_lock);
            if (g_hasPendingConfig) {
                applyConfig(g_pendingConfig);
                g_hasPendingConfig = false;
            }
            if (g_hasPendingPreset) {
                g_engine.setPreset(g_pendingPreset, g_pendingKeepVoices);
                g_hasPendingPreset = false;
            }
            critical_section_exit(&g_lock);
        }

        const uint64_t frameStart = time_us_64();

        // MIDI events, applied at their arrival time.
        MidiEvent ev;
        while (queue_try_peek(&g_midi, &ev) && ev.timeUs <= frameStart) {
            queue_try_remove(&g_midi, &ev);
            if (ev.timeUs > engineUs) {
                g_engine.advance(static_cast<float>(ev.timeUs - engineUs) * 1e-6f);
                engineUs = ev.timeUs;
            }
            g_engine.handleMidi(ev.data, ev.len);
        }
        if (frameStart > engineUs) {
            g_engine.advance(static_cast<float>(frameStart - engineUs) * 1e-6f);
            engineUs = frameStart;
        }

        g_engine.render();
        const int n = g_engine.pixelCount();
        g_output.prepare(g_engine.pixels(), n);
        g_output.emit(g_bytes);
        const uint32_t renderUs = static_cast<uint32_t>(time_us_64() - frameStart);
        g_strip.show(g_bytes, n);

        const uint32_t frameUs = 1000000u / g_output.config().fps;
        nextFrameUs += frameUs;

        // Dither refreshes: while another whole transfer fits before the next
        // frame is due, resend this frame with the next dither step. Dim LEDs
        // toggle between two 8-bit levels; at the strip's refresh rate
        // (~250 Hz at 120 LEDs) instead of the frame rate that is not visible.
        uint32_t refreshes = 1;
        if (g_output.config().dither) {
            const uint64_t txUs = Ws2812Output::transferUs(n);
            while (g_strip.readyAtUs() + txUs <= nextFrameUs) {
                g_output.emit(g_bytes);
                g_strip.show(g_bytes, n);
                ++refreshes;
            }
        }

        // Stats
        renderSum += renderUs;
        ++renderCount;
        if (g_resetMax) {
            g_stats.maxRenderUs = 0;
            g_resetMax = false;
        }
        g_stats.frames = g_stats.frames + 1;
        g_stats.lastRenderUs = renderUs;
        if (renderUs > g_stats.maxRenderUs) g_stats.maxRenderUs = renderUs;
        if (renderCount >= 64) {
            g_stats.avgRenderUs = static_cast<uint32_t>(renderSum / renderCount);
            renderSum = 0;
            renderCount = 0;
        }
        g_stats.voices = g_engine.activeVoices();
        g_stats.currentMa = g_output.lastCurrentMa();
        g_stats.limiter = g_output.lastLimiterScale();
        g_stats.bpm = static_cast<float>(g_engine.bpm());
        g_stats.refreshes = refreshes;

        // Frame pacing
        uint64_t now = time_us_64();
        if (nextFrameUs < now) {
            g_stats.lateFrames = g_stats.lateFrames + 1;
            nextFrameUs = now;
        } else {
            sleep_until(from_us_since_boot(nextFrameUs));
        }
    }
}

} // namespace

void renderInit(const mpx::OutputConfig& cfg, const mpx::Preset& preset) {
    queue_init(&g_midi, sizeof(MidiEvent), 256);
    critical_section_init(&g_lock);
    g_initConfig = cfg;
    g_initConfig.sanitize();
    g_engine.setPreset(preset);
}

void renderLaunch() {
    multicore_launch_core1_with_stack(core1Main, g_core1Stack, sizeof(g_core1Stack));
    while (!g_core1Ready) tight_loop_contents();
}

bool renderPushMidi(const MidiEvent& ev) {
    if (queue_try_add(&g_midi, &ev)) return true;
    g_stats.droppedMidi = g_stats.droppedMidi + 1;
    return false;
}

void renderSubmitPreset(const mpx::Preset& p, bool keepVoices) {
    critical_section_enter_blocking(&g_lock);
    g_pendingPreset = p;
    // A still-pending preset that needs a voice reset must keep needing it.
    g_pendingKeepVoices = keepVoices && (!g_hasPendingPreset || g_pendingKeepVoices);
    g_hasPendingPreset = true;
    critical_section_exit(&g_lock);
}

void renderSubmitConfig(const mpx::OutputConfig& c) {
    critical_section_enter_blocking(&g_lock);
    g_pendingConfig = c;
    g_pendingConfig.sanitize();
    g_hasPendingConfig = true;
    critical_section_exit(&g_lock);
}

RenderStats renderReadStats(bool resetMax) {
    RenderStats s;
    s.frames = g_stats.frames;
    s.lastRenderUs = g_stats.lastRenderUs;
    s.maxRenderUs = g_stats.maxRenderUs;
    s.avgRenderUs = g_stats.avgRenderUs;
    s.droppedMidi = g_stats.droppedMidi;
    s.lateFrames = g_stats.lateFrames;
    s.voices = g_stats.voices;
    s.currentMa = g_stats.currentMa;
    s.limiter = g_stats.limiter;
    s.bpm = g_stats.bpm;
    s.refreshes = g_stats.refreshes;
    if (resetMax) g_resetMax = true;
    return s;
}

} // namespace gear
