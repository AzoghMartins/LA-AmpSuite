#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <string>
#include <vector>

#include "DspUtils.h"
#include "FFT.h"

namespace laas
{
//==============================================================================
enum class CabModel
{
    v30x412,       // Mesa Rectifier 4x12 (Celestion Vintage 30)
    greenback412,  // Marshall 1960 4x12 (Celestion G12M Greenback)
    t75x412,       // Marshall 1960A 4x12 (Celestion G12T-75)
    open212,       // 2x12 open back combo
    bass810,       // Ampeg SVT 8x10
    bass115,       // Ampeg B-15 1x15
    bass410,       // 4x10 bass cab with horn
    numCabs
};

enum class MicModel
{
    dyn57,         // Shure SM57 style dynamic
    dyn421,        // Sennheiser MD421 style dynamic
    ribbon121,     // Royer R-121 style ribbon
    cond87,        // large diaphragm condenser (U87 / C414 style)
    dyn7b,         // Shure SM7B style broadcast dynamic
    numMics
};

const char* cabModelName (int c) noexcept;
const char* cabShortName (int c) noexcept;
const char* micModelName (int m) noexcept;

struct MicPlacement
{
    MicModel mic = MicModel::dyn57;
    float position = 0.3f;  // 0 = centre of the dust cap, 1 = edge of the cone
    float distanceCm = 2.0f; // 1 .. 100 cm from the grille
};

/** Number of points of the stored magnitude responses (log spaced 20 Hz .. 20 kHz). */
constexpr int kResponsePoints = 160;
float responsePointHz (int i) noexcept;

/**
    Builds a cabinet + microphone impulse response from a physically motivated model:
    speaker/cabinet magnitude response (box resonance, cone breakup, roll-off), microphone response,
    position on the cone (beaming), distance (proximity effect, air, floor and room reflections and
    time of flight). The magnitude part is made minimum phase (cepstrum), reflections are added on top.
    extraDelaySamples delays the result (used to time-align two microphones at different distances).
*/
std::vector<float> buildCabinetIR (double sampleRate, CabModel cab, const MicPlacement& mic, double extraDelaySamples = 0.0);

/** Time of flight from the speaker to a mic at the given distance, in samples. */
double micDelaySamples (double sampleRate, float distanceCm) noexcept;

/** Resamples an impulse response (windowed sinc, anti-aliased when downsampling). */
std::vector<float> resampleIR (const std::vector<float>& in, double fromRate, double toRate);

/** Trims trailing silence, limits the length and normalises loudness so different IRs sit at the same level. */
void conditionIR (std::vector<float>& ir, double sampleRate, double maxSeconds = 0.5);

/** Magnitude response (dB) of an impulse response at the kResponsePoints display frequencies. */
std::array<float, kResponsePoints> irResponse (const std::vector<float>& ir, double sampleRate);

//==============================================================================
/** An impulse response split into partitions and transformed, ready for the convolver. */
struct PreparedIR
{
    int partition = 0;
    int numParts = 0;
    std::vector<float> head;                       // first partition (direct form)
    std::vector<std::complex<float>> spectra;      // partitions 1..numParts-1, (partition + 1) bins each
    int length = 0;
};

PreparedIR* prepareIR (const std::vector<float>& ir, int partition);

/**
    Zero-latency uniformly partitioned convolution (overlap-save) with a direct-form first partition.
    Several impulse responses can share one input history; each gets its own weight, changed at block
    boundaries, which is how the cabinet crossfades between IRs and blends two microphones.
*/
class Convolver
{
public:
    static constexpr int kMaxSources = 4;

    void prepare (int partition, int maxPartitions);
    void reset() noexcept;
    int getPartition() const noexcept { return P; }
    int getMaxPartitions() const noexcept { return maxParts; }

    /** Sets the sources used from the next block boundary on. */
    void setSource (int index, const PreparedIR* ir, float weight) noexcept
    {
        sources[(size_t) index] = { ir, weight };
    }
    /** Rebuilds the direct-form head from the current sources (done automatically at boundaries). */
    void refreshHead() noexcept;

    /** Called at each block boundary so the owner can update the weights. */
    template <typename Fn>
    void process (const float* in, float* out, int n, Fn&& onBoundary) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            history[(size_t) histPos] = x;
            history[(size_t) (histPos + P)] = x;
            block[(size_t) blockPos] = x;

            // Direct-form head: y = sum h[t] x[n - t]
            const float* h = combinedHead.data();
            const float* xh = &history[(size_t) histPos + (size_t) P]; // most recent at xh[0], older at xh[-t]
            float sum = tail[(size_t) blockPos];
            for (int t = 0; t < headLen; ++t)
                sum += h[t] * xh[-t];
            out[i] = sum;

            histPos = (histPos + 1) % P;
            if (++blockPos == P)
            {
                blockPos = 0;
                onBoundary();
                boundary();
            }
        }
    }

private:
    void boundary() noexcept;

    struct Source
    {
        const PreparedIR* ir = nullptr;
        float weight = 0.0f;
    };

    int P = 128, maxParts = 1, fdlPos = 0, blockPos = 0, histPos = 0, headLen = 0;
    FFT<float> fft;
    std::vector<float> history, block, prevBlock, tail, combinedHead;
    std::vector<std::complex<float>> fdl, work, acc;
    std::array<Source, kMaxSources> sources {};
};
} // namespace laas
