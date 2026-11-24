#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/AmpEngine.h"
#include "dsp/Tuner.h"

namespace laas
{
/** Block-based float FIFO (audio thread -> UI). */
class SampleFifo
{
public:
    void push (const float* data, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        std::copy (data, data + n1, buffer.begin() + s1);
        std::copy (data + n1, data + n1 + n2, buffer.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }
    int pop (float* dest, int maxN) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        std::copy (buffer.begin() + s1, buffer.begin() + s1 + n1, dest);
        std::copy (buffer.begin() + s2, buffer.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    static constexpr int size = 1 << 15;
    juce::AbstractFifo fifo { size };
    std::array<float, size> buffer {};
};

/** Meter values published by the audio thread. */
struct AtomicMeters
{
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, gate { 0.0f }, comp { 0.0f }, limit { 0.0f }, drive { 0.0f };
};

class AmpSuiteProcessor : public juce::AudioProcessor
{
public:
    AmpSuiteProcessor();
    ~AmpSuiteProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return (int) getFactoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI interface (message thread) ------------------------------------------------------
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

    PedalOrder getPedalOrder() const noexcept;
    void setPedalOrder (const PedalOrder& order);

    /** Loads an impulse response file (wav/aiff/flac) into mic slot 0 (A) or 1 (B). Returns an error or empty. */
    juce::String loadImpulseFile (int slot, const juce::File& file);
    void clearImpulseFile (int slot);
    juce::String getImpulseName (int slot) const;
    juce::File getImpulseFile (int slot) const;

    /** Builds any changed cabinet IRs now (normally done by a background thread). For tests and offline use. */
    void rebuildImpulsesNow();
    /** Magnitude response (dB) of the current cabinet (both mics blended) at kResponsePoints frequencies. */
    std::array<float, kResponsePoints> getCabResponse() const;
    int getCabResponseVersion() const noexcept { return responseVersion.load(); }

    // Tuner (UI pulls input samples and analyses them)
    void setTunerMute (bool m) noexcept { tunerMute.store (m); }
    bool isTunerMuted() const noexcept { return tunerMute.load(); }

    static juce::File getUserPresetFolder();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getCurrentPresetName() const { return currentPresetName; }

    double getEngineSampleRate() const noexcept { return engineSampleRate.load(); }

    SampleFifo spectrumFifo, tunerFifo;
    AtomicMeters meters;

private:
    class ImpulseBuilder;
    struct UserIR
    {
        juce::File file;
        juce::String name;
        std::vector<float> data;
        double rate = 0.0;
        int serial = 0;
    };

    /** Builds IRs whose settings changed; install = true puts them in directly (audio stopped). */
    void updateImpulses (bool install);
    /** Sets every parameter in one pass: listed ones to their value, the rest to their default. */
    void applyParameterValues (const std::vector<std::pair<const char*, float>>& values);
    void restoreExtraState (const juce::ValueTree& tree);
    void storeExtraState (juce::ValueTree& tree) const;

    juce::AudioProcessorValueTreeState apvts;
    ParameterCache params;
    AmpEngine engine;
    EngineParams engineParams;

    std::atomic<uint32_t> packedOrder { 0 };
    std::atomic<bool> tunerMute { false };
    std::atomic<double> engineSampleRate { 48000.0 };
    int preparedBlockSize = 512;
    double preparedRate = 0.0;
    int currentProgram = 0;
    juce::String currentPresetName { "Default" };

    // Impulse responses
    mutable juce::CriticalSection irLock;   // guards userIRs, built keys and responses
    juce::CriticalSection buildLock;        // serialises building / installing
    std::array<UserIR, 2> userIRs;
    std::array<juce::String, 2> builtKeys;
    std::array<std::vector<float>, 2> builtIRs;
    juce::String responseKey;
    std::array<float, kResponsePoints> cabResponse {};
    std::atomic<int> responseVersion { 0 };
    int irSerial = 0;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<ImpulseBuilder> builder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSuiteProcessor)
};
} // namespace laas
