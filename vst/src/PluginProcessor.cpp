#include "PluginProcessor.h"

#include <cstring>
#include <vector>

#include "PluginEditor.h"
#include "factory_presets.h"
#include "midipixi/preset_json.h"

namespace {

void stringSink(void* ctx, const char* d, size_t n) { static_cast<std::string*>(ctx)->append(d, n); }

bool parsePresetText(const std::string& text, mpx::Preset& out, juce::String& error) {
    std::vector<mpx::JsonNode> nodes(mpx::kPresetJsonNodes);
    char err[160] = "";
    if (!mpx::parsePreset(text.data(), text.size(), out, nodes.data(), static_cast<int>(nodes.size()), err,
                          sizeof(err))) {
        error = err;
        return false;
    }
    return true;
}

} // namespace

MidipixiProcessor::MidipixiProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    engine_ = std::make_unique<mpx::Engine>();
    editPreset_ = std::make_unique<mpx::Preset>();
    pendingPreset_ = std::make_unique<mpx::Preset>();
    frame_ = std::make_unique<FrameSnapshot>();
    loadFactoryPreset(0);
}

MidipixiProcessor::~MidipixiProcessor() = default;

bool MidipixiProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Silent output; any mono/stereo layout is fine.
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MidipixiProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
    samplesToNextFrame_ = 0.0;
}

// --- Audio thread -------------------------------------------------------------------

void MidipixiProcessor::renderFrame() {
    engine_->render();
    if (!frameLock_.tryEnter()) return;  // UI is reading; drop this frame
    FrameSnapshot& f = *frame_;
    f.pixelCount = engine_->pixelCount();
    std::memcpy(f.pixels.data(), engine_->pixels(), sizeof(mpx::Rgb) * static_cast<size_t>(f.pixelCount));
    for (int l = 0; l < mpx::kMaxLayers; ++l) {
        f.liveValid[l] = engine_->layerHasVoice(l);
        f.voices[l] = engine_->activeVoices(l);
        if (f.liveValid[l]) std::memcpy(f.live[l], engine_->liveValues(l), sizeof(f.live[l]));
    }
    f.bpm = engine_->bpm();
    f.serial = ++frameSerial_;
    frameLock_.exit();
}

void MidipixiProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (presetLock_.tryEnter()) {
        if (pendingDirty_) {
            engine_->setPreset(*pendingPreset_, !pendingReset_);
            pendingDirty_ = false;
            pendingReset_ = false;
        }
        presetLock_.exit();
    }
    engine_->setPixelCount(ledCount_.load());

    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            double bpm = pos->getBpm().orFallback(120.0);
            double ppq = pos->getPpqPosition().orFallback(0.0);
            engine_->setHostTransport(bpm, ppq, pos->getIsPlaying());
        }
    }

    const double sr = sampleRate_;
    const double frameSamples = sr / kFps;
    double pos = 0.0;

    // Advances the engine to sample position t, rendering frames at 60 fps of audio time.
    auto advanceTo = [&](double t) {
        while (pos + samplesToNextFrame_ <= t) {
            engine_->advance(static_cast<float>(samplesToNextFrame_ / sr));
            pos += samplesToNextFrame_;
            renderFrame();
            samplesToNextFrame_ = frameSamples;
        }
        if (t > pos) {
            engine_->advance(static_cast<float>((t - pos) / sr));
            samplesToNextFrame_ -= t - pos;
            pos = t;
        }
    };

    for (const auto meta : midi) {
        advanceTo(static_cast<double>(meta.samplePosition));
        engine_->handleMidi(meta.data, meta.numBytes);
    }
    advanceTo(static_cast<double>(buffer.getNumSamples()));
}

// --- Frames for the UI --------------------------------------------------------------

bool MidipixiProcessor::readFrame(FrameSnapshot& out, uint32_t lastSerial) {
    const juce::SpinLock::ScopedLockType sl(frameLock_);
    if (frame_->serial == lastSerial) return false;
    out = *frame_;
    return true;
}

// --- Presets (message thread) ---------------------------------------------------------

void MidipixiProcessor::publishPreset(bool resetVoices) {
    {
        const juce::SpinLock::ScopedLockType sl(presetLock_);
        *pendingPreset_ = *editPreset_;
        pendingDirty_ = true;
        pendingReset_ = pendingReset_ || resetVoices;
    }
    gear_.pushLive(*editPreset_);
}

void MidipixiProcessor::presetEdited() { publishPreset(false); }

void MidipixiProcessor::loadPreset(const mpx::Preset& p) {
    *editPreset_ = p;
    publishPreset(true);
    presetReplaced.sendChangeMessage();
}

int MidipixiProcessor::numFactoryPresets() { return kFactoryPresetCount; }

juce::String MidipixiProcessor::factoryPresetName(int index) {
    if (index < 0 || index >= kFactoryPresetCount) return {};
    auto p = std::make_unique<mpx::Preset>();
    juce::String err;
    if (!parsePresetText(kFactoryPresets[index], *p, err)) return "?";
    return p->name;
}

void MidipixiProcessor::loadFactoryPreset(int index) {
    if (index < 0 || index >= kFactoryPresetCount) return;
    auto p = std::make_unique<mpx::Preset>();
    juce::String err;
    if (parsePresetText(kFactoryPresets[index], *p, err)) {
        currentProgram_ = index;
        loadPreset(*p);
    }
}

int MidipixiProcessor::getNumPrograms() { return kFactoryPresetCount; }

void MidipixiProcessor::setCurrentProgram(int index) { loadFactoryPreset(index); }

const juce::String MidipixiProcessor::getProgramName(int index) { return factoryPresetName(index); }

juce::String MidipixiProcessor::presetToJson(bool pretty) const {
    std::string s;
    mpx::JsonWriter w(stringSink, &s, pretty);
    mpx::writePreset(*editPreset_, w);
    return juce::String::fromUTF8(s.data(), static_cast<int>(s.size()));
}

bool MidipixiProcessor::presetFromJson(const juce::String& json, juce::String& error) {
    auto p = std::make_unique<mpx::Preset>();
    if (!parsePresetText(json.toStdString(), *p, error)) return false;
    loadPreset(*p);
    return true;
}

void MidipixiProcessor::setLedCount(int n) { ledCount_ = juce::jlimit(1, mpx::kMaxPixels, n); }

// --- Plugin state ---------------------------------------------------------------------
// {"ledCount":60,"preset":{...}}  (the preset is the regular preset JSON)

void MidipixiProcessor::getStateInformation(juce::MemoryBlock& destData) {
    std::string s = "{\"ledCount\":" + std::to_string(ledCount_.load()) + ",\"preset\":";
    s += presetToJson(false).toStdString();
    s += "}";
    destData.replaceAll(s.data(), s.size());
}

void MidipixiProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (!data || sizeInBytes <= 0) return;
    std::string text(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes));
    std::vector<mpx::JsonNode> nodes(mpx::kPresetJsonNodes + 16);
    mpx::JsonDoc doc(nodes.data(), static_cast<int>(nodes.size()));
    if (!doc.parse(text.data(), text.size())) return;
    setLedCount(static_cast<int>(doc.getNumber(doc.root(), "ledCount", 60)));
    auto p = std::make_unique<mpx::Preset>();
    char err[160];
    if (mpx::readPreset(doc, doc.find(doc.root(), "preset"), *p, err, sizeof(err))) loadPreset(*p);
}

juce::AudioProcessorEditor* MidipixiProcessor::createEditor() { return new MidipixiEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MidipixiProcessor(); }
