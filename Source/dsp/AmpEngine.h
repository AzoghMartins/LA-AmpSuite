#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Amp.h"
#include "Cabinet.h"
#include "Effects.h"
#include "Modules.h"
#include "Pedals.h"

namespace laas
{
enum class MicSource { builtIn, userIR };

struct CabParams
{
    bool enabled = true;
    CabModel cab = CabModel::v30x412;
    MicPlacement micA, micB { MicModel::ribbon121, 0.55f, 8.0f };
    MicSource sourceA = MicSource::builtIn, sourceB = MicSource::builtIn;
    bool micBOn = false;
    bool micBInvert = false;
    float mix = 0.5f;        // 0 = only A, 1 = only B (when B is on)
    float lowCutHz = 20.0f;  // 20 = off
    float highCutHz = 20000.0f; // 20000 = off
};

struct EngineParams
{
    float inputGainDb = 0.0f;
    PedalOrder pedalOrder = defaultPedalOrder();
    GateParams gate;
    CompPedalParams comp;
    DrivePedalParams drive;
    BoostParams boost;

    bool ampOn = true;
    AmpParams amp;
    bool geqOn = false;
    std::array<float, kGeqBands> geq {};
    ModParams mod;
    DelayParams delay;

    CabParams cab;

    EqParams eq;
    DoublerParams doubler;
    ReverbParams reverb;

    float outputGainDb = 0.0f;
    float width = 1.0f;
    bool limiterOn = true;
    float ceilingDb = -1.0f;
    bool mute = false;
    double bpm = 120.0;
};

struct EngineMeters
{
    float inPeak = 0.0f, outPeak = 0.0f, gateDb = 0.0f, compDb = 0.0f, limitDb = 0.0f, ampDrive = 0.0f;
};

/**
    The whole rig: input -> pedals (re-orderable) -> amp head -> effects loop (GEQ, modulation, delay)
    -> cabinet (two mics / user IRs) -> post (EQ, doubler, reverb) -> output.
    Pedals and the amp run 4x oversampled. No JUCE dependency.

    Modulation and delay are linear, so they are processed after the (linear) cabinet: that is
    equivalent to running them in the loop before the cab, and lets them be stereo.
*/
class AmpEngine
{
public:
    static constexpr int kPartition = 128;

    AmpEngine();
    ~AmpEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const EngineParams& p) noexcept;
    const EngineParams& getParams() const noexcept { return params; }

    /** Processes in place. 1 or 2 channels; the input is summed to mono, the output is stereo when possible. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    int getLatency() const noexcept { return Oversampler4x::latency(); }
    double getSampleRate() const noexcept { return sr; }
    const EngineMeters& getMeters() const noexcept { return meters; }
    /** Mono mix of the last processed block (for the spectrum display). */
    const float* getAnalysisBuffer() const noexcept { return analysis.data(); }
    /** Input after the input gain (for the tuner). */
    const float* getInputBuffer() const noexcept { return inputCopy.data(); }

    // ---- Cabinet impulse responses (called from a non-audio thread) ---------------------------
    /** Hands a new IR for mic slot 0 (A) or 1 (B) to the audio thread; ownership is taken. */
    void postImpulse (int slot, PreparedIR* ir) noexcept;
    /** Installs an IR immediately (only while the audio thread is stopped, e.g. in prepare). */
    void installImpulseNow (int slot, PreparedIR* ir) noexcept;
    /** Deletes IRs the audio thread has finished with. Call regularly from a non-audio thread. */
    void collectGarbage() noexcept;
    bool hasImpulse (int slot) const noexcept { return slots[(size_t) slot].current != nullptr; }

private:
    struct CabSlot
    {
        PreparedIR* current = nullptr;
        PreparedIR* fading = nullptr;
        float fade = 1.0f;
        std::atomic<PreparedIR*> pending { nullptr };
        float weight = 0.0f;
    };

    void processChunk (float* l, float* r, int n) noexcept;
    void cabBoundary() noexcept;
    void retire (PreparedIR* ir) noexcept;
    void updateCabFilters() noexcept;

    double sr = 48000.0;
    int maxBlock = 512;
    EngineParams params;
    EngineMeters meters;

    // Front end
    NoiseGate gate;
    CompPedal comp;
    DrivePedal drive;
    BoostPedal boost;
    AmpModule amp;
    Oversampler4x oversampler;
    LatencyDelay gateDelay;
    GraphicEq geq;

    // Cab
    Convolver convolver;
    std::array<CabSlot, 2> slots;
    std::array<std::atomic<PreparedIR*>, 32> retired;
    Biquad cabLow, cabHigh;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    // Stereo section
    ModulationModule mod;
    DelayModule delay;
    EqModule eq;
    DoublerModule doubler;
    ReverbModule reverb;
    LimiterModule limiter;
    Smoothed outGainSm, widthSm, inGainSm, muteSm;

    std::vector<float> mono, os, gateGains, gate4, cabOut, scratchL, scratchR, analysis, inputCopy;
};
} // namespace laas
