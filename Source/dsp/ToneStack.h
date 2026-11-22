#pragma once

#include <array>

#include "DspUtils.h"

namespace laas
{
/** Component values of a passive treble/middle/bass ("TMB") tone stack (ohms / farads). */
struct TmbComponents
{
    double rTreble = 220e3, rBass = 1e6, rMid = 22e3, rSlope = 33e3;
    double cTreble = 470e-12, cBass = 22e-9, cMid = 22e-9;
};

enum class ToneStackType
{
    tmb,      // passive Fender/Marshall-family stack (Yeh & Smith model)
    svt,      // active bass stack: bass/treble shelves, 5-position mid, ultra lo/hi
    vintage,  // simple bass/treble Baxandall with a gentle mid control (flip-top bass amp)
};

struct ToneStackSettings
{
    float bass = 0.5f, mid = 0.5f, treble = 0.5f; // 0..1 knob positions
    int midFreq = 2;                               // svt: 0..4 (220, 450, 800, 1.6k, 3k)
    bool ultraLo = false, ultraHi = false;         // svt switches
};

/**
    The amp's tone stack. The TMB type is the third-order analog transfer function of the classic
    passive stack (D. Yeh, J. Smith: "Discretization of the '59 Fender Bassman Tone Stack", DAFx 2006),
    discretised with the bilinear transform. Component values select the amp family.
*/
class ToneStack
{
public:
    void prepare (double sampleRate) { sr = sampleRate; dirty = true; }
    void reset() noexcept { z = {}; for (auto& f : bq) f.reset(); }
    void configure (ToneStackType type, const TmbComponents& c) noexcept { stackType = type; comps = c; dirty = true; }
    void set (const ToneStackSettings& s) noexcept;
    float process (float x) noexcept;

    /** Magnitude in dB at f (analog prototype for TMB, used by tests and the display). */
    static double magnitudeDb (ToneStackType type, const TmbComponents& c, const ToneStackSettings& s, double f, double sr);

    /** Analog TMB coefficients: H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3). */
    static void tmbAnalog (const TmbComponents& c, double t, double m, double l, double bOut[4], double aOut[4]) noexcept;

    static float midFrequencyHz (int index) noexcept;

private:
    void update() noexcept;

    double sr = 48000.0;
    ToneStackType stackType = ToneStackType::tmb;
    TmbComponents comps;
    ToneStackSettings settings;
    bool dirty = true;

    // TMB: third order direct form II transposed
    std::array<double, 4> b {}, a {};
    std::array<double, 3> z {};
    // Biquad stacks
    std::array<Biquad, 5> bq;
};
} // namespace laas
