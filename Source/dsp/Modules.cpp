#include "Modules.h"

namespace laas
{
void EqModule::design (Biquad& b, double sr, int band, float freq, float gainDb, float q) noexcept
{
    if (band == 0)
        b.setLowShelf (sr, freq, gainDb, std::max (0.3f, std::min (1.0f, q)));
    else if (band == kEqBands - 1)
        b.setHighShelf (sr, freq, gainDb, std::max (0.3f, std::min (1.0f, q)));
    else
        b.setPeak (sr, freq, std::max (0.1f, q), gainDb);
}

void EqModule::setParams (const EqParams& p) noexcept
{
    params = p;
    for (int b = 0; b < kEqBands; ++b)
    {
        const size_t i = (size_t) b;
        if (p.freq[i] != last.freq[i] || p.gain[i] != last.gain[i] || p.q[i] != last.q[i])
        {
            design (filtL[i], sr, b, p.freq[i], p.gain[i], p.q[i]);
            design (filtR[i], sr, b, p.freq[i], p.gain[i], p.q[i]);
        }
    }
    last = p;
}

void EqModule::process (float* l, float* r, int n) noexcept
{
    if (! params.enabled)
        return;
    for (int b = 0; b < kEqBands; ++b)
    {
        if (std::abs (params.gain[(size_t) b]) < 0.01f)
            continue;
        auto& fl = filtL[(size_t) b];
        auto& fr = filtR[(size_t) b];
        for (int i = 0; i < n; ++i)
        {
            l[i] = fl.process (l[i]);
            r[i] = fr.process (r[i]);
        }
    }
}

//==============================================================================
void GraphicEq::setGains (const std::array<float, kGeqBands>& g) noexcept
{
    gains = g;
    for (int b = 0; b < kGeqBands; ++b)
        if (std::abs (g[(size_t) b] - last[(size_t) b]) > 1.0e-3f)
        {
            design (filt[(size_t) b], sr, b, g[(size_t) b]);
            last[(size_t) b] = g[(size_t) b];
        }
}

void GraphicEq::process (float* x, int n) noexcept
{
    for (int b = 0; b < kGeqBands; ++b)
    {
        if (std::abs (gains[(size_t) b]) < 0.01f)
            continue;
        auto& f = filt[(size_t) b];
        for (int i = 0; i < n; ++i)
            x[i] = f.process (x[i]);
    }
}

//==============================================================================
void LimiterModule::process (float* l, float* r, int n, float ceilingDb, bool enabled) noexcept
{
    if (! enabled)
    {
        gain = 1.0f;
        grDb = 0.0f;
        return;
    }
    const float ceiling = dbToGain (ceilingDb);
    float minGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
        const float target = peak > ceiling ? ceiling / peak : 1.0f;
        gain = target < gain ? target : target + relCoeff * (gain - target);
        l[i] *= gain;
        r[i] *= gain;
        minGain = std::min (minGain, gain);
    }
    grDb = -gainToDb (minGain);
}
} // namespace laas
