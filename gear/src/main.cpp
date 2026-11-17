// midipixi gear firmware - core 0: USB (MIDI + CDC config port), protocol,
// flash storage, status LED. Core 1 renders (see render.cpp).

#include <cstring>

#include "factory_presets.h"
#include "hardware/clocks.h"
#include "midipixi/preset_json.h"
#include "midipixi/protocol.h"
#include "midipixi/version.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "render.h"
#include "storage.h"
#include "tusb.h"

#ifndef MPX_BOARD_NAME
#define MPX_BOARD_NAME "pico"
#endif

namespace {

#if PICO_RP2040
// RP2040 has 264 KB of RAM: smaller request buffers (presets still fit).
constexpr int kNodes = 1280;
constexpr size_t kLineMax = 20 * 1024;
#else
constexpr int kNodes = mpx::kPresetJsonNodes + 64;
constexpr size_t kLineMax = mpx::kMaxRequestLine;
#endif

gear::Storage g_storage;
mpx::JsonNode g_nodes[kNodes];
mpx::Preset g_serverPreset;  // GearServer scratch
mpx::Preset g_loadPreset;    // boot / program change / select
char g_scratch[mpx::kMaxStoredPresetJson];
char g_configJson[gear::Storage::kMaxConfigJson];

char g_line[kLineMax];
size_t g_lineLen = 0;
bool g_lineOverflow = false;

mpx::OutputConfig g_config;
int g_activeSlot = -1;   // running slot (-1: live preset)
int g_bootSlot = -1;     // slot stored in flash as boot preset
uint32_t g_midiEvents = 0;
uint64_t g_ledOffAtUs = 0;

bool saveMeta() {
    size_t n = mpx::configToJson(g_config, g_configJson, sizeof(g_configJson), false);
    return n > 0 && g_storage.writeMeta(g_bootSlot, g_configJson, n);
}

// Parses a slot and hands it to the renderer. persist: make it the boot preset.
bool loadSlot(int slot, bool persist) {
    const char* json;
    size_t len;
    if (!g_storage.readSlot(slot, json, len)) return false;
    char err[96];
    if (!mpx::parsePreset(json, len, g_loadPreset, g_nodes, kNodes, err, sizeof(err))) return false;
    gear::renderSubmitPreset(g_loadPreset, false);
    g_activeSlot = slot;
    if (persist && slot != g_bootSlot) {
        g_bootSlot = slot;
        saveMeta();
    }
    return true;
}

class Backend final : public mpx::GearBackend {
public:
    int slotCount() const override { return gear::Storage::kSlots; }
    int activeSlot() const override { return g_activeSlot; }
    bool readSlot(int slot, const char*& json, size_t& len) override { return g_storage.readSlot(slot, json, len); }
    bool writeSlot(int slot, const char* json, size_t len) override { return g_storage.writeSlot(slot, json, len); }
    bool eraseSlot(int slot) override { return g_storage.eraseSlot(slot); }
    bool selectSlot(int slot) override { return loadSlot(slot, true); }
    void runPreset(const mpx::Preset& p) override {
        gear::renderSubmitPreset(p, true);  // live edit: playing voices continue
        g_activeSlot = -1;
    }
    mpx::OutputConfig config() const override { return g_config; }
    bool setConfig(const mpx::OutputConfig& c) override {
        g_config = c;
        g_config.sanitize();
        gear::renderSubmitConfig(g_config);
        return saveMeta();
    }
    const char* boardName() const override { return MPX_BOARD_NAME; }
    void writeStats(mpx::JsonWriter& w) override {
        gear::RenderStats s = gear::renderReadStats(true);
        w.field("frames", static_cast<unsigned>(s.frames));
        w.field("fps", static_cast<int>(g_config.fps));
        w.field("renderUs", static_cast<unsigned>(s.lastRenderUs));
        w.field("renderAvgUs", static_cast<unsigned>(s.avgRenderUs));
        w.field("renderMaxUs", static_cast<unsigned>(s.maxRenderUs));
        w.field("frameBudgetUs", static_cast<unsigned>(1000000u / g_config.fps));
        w.field("lateFrames", static_cast<unsigned>(s.lateFrames));
        w.field("refreshesPerFrame", static_cast<unsigned>(s.refreshes));
        w.field("voices", s.voices);
        w.field("currentMa", s.currentMa);
        w.field("limiter", s.limiter);
        w.field("bpm", s.bpm);
        w.field("midiEvents", static_cast<unsigned>(g_midiEvents));
        w.field("droppedMidi", static_cast<unsigned>(s.droppedMidi));
        w.field("uptimeMs", static_cast<unsigned>(to_ms_since_boot(get_absolute_time())));
        w.field("sysClockHz", static_cast<unsigned>(clock_get_hz(clk_sys)));
    }
};

Backend g_backend;
mpx::GearServer g_server(g_backend, g_nodes, kNodes, g_serverPreset, g_scratch, sizeof(g_scratch));

// --- Status LED -----------------------------------------------------------------

void ledInit() {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
}

void ledBlink() {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    g_ledOffAtUs = time_us_64() + 30000;
#endif
}

void ledTask() {
#ifdef PICO_DEFAULT_LED_PIN
    if (g_ledOffAtUs && time_us_64() >= g_ledOffAtUs) {
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        g_ledOffAtUs = 0;
    }
#endif
}

// --- MIDI -------------------------------------------------------------------------

int cinLength(uint8_t cin) {
    switch (cin) {
        case 0x2: case 0xC: case 0xD: return 2;
        case 0x3: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: return 3;
        case 0x5: case 0xF: return 1;
        default: return 0;  // reserved, cable events, SysEx (ignored)
    }
}

void midiTask() {
    uint8_t packet[4];
    while (tud_midi_available() && tud_midi_packet_read(packet)) {
        int len = cinLength(packet[0] & 0x0F);
        if (len == 0) continue;
        const uint8_t* msg = packet + 1;
        if (msg[0] == 0xF7 || (msg[0] < 0xF8 && msg[0] >= 0xF0 && msg[0] != 0xF2)) continue;
        ++g_midiEvents;
        if ((msg[0] & 0xF0) == 0xC0) {  // Program Change selects a slot (not persisted)
            loadSlot(msg[1], false);
            ledBlink();
            continue;
        }
        gear::MidiEvent ev{time_us_64(), {msg[0], len > 1 ? msg[1] : uint8_t(0), len > 2 ? msg[2] : uint8_t(0)},
                           static_cast<uint8_t>(len)};
        gear::renderPushMidi(ev);
        if (msg[0] < 0xF8) ledBlink();  // don't blink on clock
    }
}

// --- Config port (CDC) ---------------------------------------------------------------

void cdcWrite(void*, const char* data, size_t len) {
    // Gives up when the host stops reading for 0.5 s rather than blocking forever.
    uint64_t deadline = time_us_64() + 500000;
    while (len) {
        uint32_t n = tud_cdc_write(data, static_cast<uint32_t>(len));
        data += n;
        len -= n;
        if (len) {
            tud_cdc_write_flush();
            tud_task();
            if (time_us_64() > deadline) return;
        } else {
            deadline = time_us_64() + 500000;
        }
    }
}

void cdcTask() {
    while (tud_cdc_available()) {
        char buf[64];
        uint32_t n = tud_cdc_read(buf, sizeof(buf));
        for (uint32_t i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                if (g_lineOverflow) {
                    static const char kErr[] = "{\"ok\":false,\"error\":\"request too long\"}\n";
                    cdcWrite(nullptr, kErr, sizeof(kErr) - 1);
                } else if (g_lineLen > 0) {
                    g_server.handleLine(g_line, g_lineLen, cdcWrite, nullptr);
                }
                tud_cdc_write_flush();
                g_lineLen = 0;
                g_lineOverflow = false;
                if (g_server.bootloaderRequested()) {
                    // Let the response leave before rebooting into the USB bootloader.
                    for (int k = 0; k < 50; ++k) {
                        tud_task();
                        sleep_ms(2);
                    }
                    reset_usb_boot(0, 0);
                }
                continue;
            }
            if (g_lineLen < sizeof(g_line))
                g_line[g_lineLen++] = c;
            else
                g_lineOverflow = true;
        }
    }
}

// --- Boot ---------------------------------------------------------------------------

void seedFactoryPresets() {
    for (int i = 0; i < kFactoryPresetCount && i < gear::Storage::kSlots; ++i) {
        char err[96];
        const char* text = kFactoryPresets[i];
        if (!mpx::parsePreset(text, std::strlen(text), g_loadPreset, g_nodes, kNodes, err, sizeof(err))) continue;
        size_t n = mpx::presetToJson(g_loadPreset, g_scratch, sizeof(g_scratch), false);
        if (n) g_storage.writeSlot(i, g_scratch, n);
    }
}

} // namespace

int main() {
    ledInit();
    g_storage.init();

    // Settings and boot preset from flash.
    const char* cfgJson;
    size_t cfgLen;
    bool firstBoot = !g_storage.readMeta(g_bootSlot, cfgJson, cfgLen);
    if (!firstBoot) {
        char err[96];
        mpx::parseConfig(cfgJson, cfgLen, g_config, g_nodes, kNodes, err, sizeof(err));
    }
    g_config.sanitize();

    gear::renderInit(g_config, mpx::makeInitPreset());
    gear::renderLaunch();

    if (firstBoot) {
        // Flash writes need core 1 running (it is paused during each write).
        seedFactoryPresets();
        g_bootSlot = 0;
        saveMeta();
    }
    loadSlot(g_bootSlot, false);

    tusb_init();

    for (;;) {
        tud_task();
        midiTask();
        cdcTask();
        ledTask();
    }
}
