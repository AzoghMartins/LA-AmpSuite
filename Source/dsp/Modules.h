#pragma once

#include <array>

#include "DspUtils.h"

namespace laas
{
//==============================================================================
constexpr int kEqBands = 5;

struct EqParams
{
    bool enabled = true;
    std::array<float, kEqBands> freq { 100.0f, 400.0f, 1500.0f, 3500.0f, 9000.0f };
    std::array<float, kEqBands> gain {};
    std::array<float, kEqBands> q { 0.7f, 1.0f, 1.0f, 1.0f, 0.7f };
};

/** 5-band post EQ: low shelf, three bells, high shelf. */
class EqModule
{
public:
    void prepare (double sampleRate) { sr = sampleRate; last = {}; last.freq[0] = -1.0f; }
    void reset() { for (auto& f : filtL) f.reset(); for (auto& f : filtR) f.reset(); }
    void setParams (const EqParams& p) noexcept;
    void process (float* l, float* r, int n) noexcept;

    /** Designs the filter for one band (shared with the UI to draw the curve). */
    static void design (Biquad& b, double sr, int band, float freq, float gainDb, float q) noexcept;

private:
    double sr = 48000.0;
    EqParams params, last;
    std::array<Biquad, kEqBands> filtL, filtR;
};

//==============================================================================
constexpr int kGeqBands = 5;

/** Mark-style five band graphic EQ in the effects loop (80, 240, 750, 2200, 6600 Hz). */
class GraphicEq
{
public:
    static float bandHz (int b) noexcept
    {
        static const float f[kGeqBands] = { 80.0f, 240.0f, 750.0f, 2200.0f, 6600.0f };
        return f[b];
    }
    static void design (Biquad& f, double sr, int band, float gainDb) noexcept { f.setPeak (sr, bandHz (band), 1.1, gainDb); }

    void prepare (double sampleRate) { sr = sampleRate; last.fill (-100.0f); reset(); }
    void reset() { for (auto& f : filt) f.reset(); }
    void setGains (const std::array<float, kGeqBands>& g) noexcept;
    void process (float* x, int n) noexcept;

private:
    double sr = 48000.0;
    std::array<float, kGeqBands> gains {}, last {};
    std::array<Biquad, kGeqBands> filt;
};

//==============================================================================
/** Zero-latency output limiter (instant attack peak limiter with smooth release). */
class LimiterModule
{
public:
    void prepare (double sampleRate) { sr = sampleRate; relCoeff = msCoeff (sr, 120.0f); reset(); }
    void reset() noexcept { gain = 1.0f; grDb = 0.0f; }
    void process (float* l, float* r, int n, float ceilingDb, bool enabled) noexcept;
    float getReductionDb() const noexcept { return grDb; }

private:
    double sr = 48000.0;
    float gain = 1.0f, relCoeff = 0.0f, grDb = 0.0f;
};
} // namespace laas
