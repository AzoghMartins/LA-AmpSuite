#include "ToneStack.h"

#include <complex>

namespace laas
{
namespace
{
/** Bass pot taper used by the TMB model (log/audio taper). */
double bassTaper (double x) noexcept { return std::exp ((clampf ((float) x, 0.0f, 1.0f) - 1.0) * 3.4); }

void designBiquads (std::array<Biquad, 5>& bq, ToneStackType type, const ToneStackSettings& s, double sr) noexcept
{
    auto knobDb = [] (float v, float range) { return (double) ((clampf (v, 0.0f, 1.0f) - 0.5f) * 2.0f * range); };
    if (type == ToneStackType::svt)
    {
        bq[0].setLowShelf (sr, 60.0, knobDb (s.bass, 15.0f), 0.8);
        bq[1].setPeak (sr, ToneStack::midFrequencyHz (s.midFreq), 0.9, s.mid >= 0.5f ? knobDb (s.mid, 12.0f) : knobDb (s.mid, 15.0f));
        bq[2].setHighShelf (sr, 4500.0, knobDb (s.treble, 15.0f), 0.8);
        // Ultra Lo: +3 dB at 40 Hz and a deep cut around 500 Hz. Ultra Hi: +9 dB at 8 kHz.
        if (s.ultraLo) bq[3].setPeak (sr, 500.0, 0.6, -12.0), bq[0].setLowShelf (sr, 60.0, knobDb (s.bass, 15.0f) + 3.0, 0.8);
        else bq[3].setBypass();
        if (s.ultraHi) bq[4].setHighShelf (sr, 8000.0, 9.0, 0.7);
        else bq[4].setBypass();
    }
    else
    {
        // Vintage flip-top: Baxandall bass/treble plus a gentle mid control
        bq[0].setLowShelf (sr, 100.0, knobDb (s.bass, 12.0f), 0.7);
        bq[1].setPeak (sr, 600.0, 0.7, knobDb (s.mid, 6.0f));
        bq[2].setHighShelf (sr, 2500.0, knobDb (s.treble, 12.0f), 0.7);
        bq[3].setBypass();
        bq[4].setBypass();
    }
}
} // namespace

float ToneStack::midFrequencyHz (int index) noexcept
{
    static const float f[] = { 220.0f, 450.0f, 800.0f, 1600.0f, 3000.0f };
    return f[index < 0 ? 0 : (index > 4 ? 4 : index)];
}

void ToneStack::tmbAnalog (const TmbComponents& c, double t, double m, double l, double bOut[4], double aOut[4]) noexcept
{
    const double R1 = c.rTreble, R2 = c.rBass, R3 = c.rMid, R4 = c.rSlope;
    const double C1 = c.cTreble, C2 = c.cBass, C3 = c.cMid;
    const double m2 = m * m;

    bOut[0] = 0.0;
    bOut[1] = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    bOut[2] = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    bOut[3] = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
           - m2 * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
           + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
           + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
           + t * l * C1 * C2 * C3 * R1 * R2 * R4;

    aOut[0] = 1.0;
    aOut[1] = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    aOut[2] = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m2 * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
           + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    aOut[3] = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
           - m2 * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
           + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
           + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
}

void ToneStack::set (const ToneStackSettings& s) noexcept
{
    if (! dirty && std::abs (s.bass - settings.bass) < 1.0e-4f && std::abs (s.mid - settings.mid) < 1.0e-4f
        && std::abs (s.treble - settings.treble) < 1.0e-4f && s.midFreq == settings.midFreq && s.ultraLo == settings.ultraLo
        && s.ultraHi == settings.ultraHi)
        return;
    settings = s;
    update();
}

void ToneStack::update() noexcept
{
    dirty = false;
    if (stackType != ToneStackType::tmb)
    {
        designBiquads (bq, stackType, settings, sr);
        return;
    }

    double bs[4], as[4];
    tmbAnalog (comps, clampf (settings.treble, 0.0f, 1.0f), clampf (settings.mid, 0.0f, 1.0f), bassTaper (settings.bass), bs, as);

    // Bilinear transform: s = c (1 - q) / (1 + q), q = z^-1
    const double c = 2.0 * sr;
    static const double P[4][4] = { { 1, 3, 3, 1 }, { 1, 1, -1, -1 }, { 1, -1, -1, 1 }, { 1, -3, 3, -1 } };
    double nb[4] = {}, na[4] = {};
    double ck = 1.0;
    for (int k = 0; k < 4; ++k)
    {
        for (int i = 0; i < 4; ++i)
        {
            nb[i] += bs[k] * ck * P[k][i];
            na[i] += as[k] * ck * P[k][i];
        }
        ck *= c;
    }
    for (int i = 0; i < 4; ++i)
    {
        b[(size_t) i] = nb[i] / na[0];
        a[(size_t) i] = na[i] / na[0];
    }
}

float ToneStack::process (float xf) noexcept
{
    if (stackType != ToneStackType::tmb)
    {
        float y = xf;
        for (auto& f : bq)
            y = f.process (y);
        return y;
    }
    const double x = xf;
    const double y = b[0] * x + z[0];
    z[0] = b[1] * x - a[1] * y + z[1];
    z[1] = b[2] * x - a[2] * y + z[2];
    z[2] = b[3] * x - a[3] * y;
    return (float) y;
}

double ToneStack::magnitudeDb (ToneStackType type, const TmbComponents& c, const ToneStackSettings& s, double f, double sr)
{
    if (type != ToneStackType::tmb)
    {
        std::array<Biquad, 5> tmp;
        designBiquads (tmp, type, s, sr);
        double db = 0.0;
        for (auto& q : tmp)
            db += q.magnitudeDb (sr, f);
        return db;
    }
    double bs[4], as[4];
    tmbAnalog (c, clampf (s.treble, 0.0f, 1.0f), clampf (s.mid, 0.0f, 1.0f), bassTaper (s.bass), bs, as);
    const std::complex<double> jw (0.0, 2.0 * kPi * f);
    std::complex<double> num = 0.0, den = 0.0, p = 1.0;
    for (int k = 0; k < 4; ++k)
    {
        num += bs[k] * p;
        den += as[k] * p;
        p *= jw;
    }
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (num / den)));
}
} // namespace laas
