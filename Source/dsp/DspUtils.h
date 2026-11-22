#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Biquad.h"

namespace laas
{
inline float dbToGain (float db) noexcept { return db <= -120.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

/** One-pole smoothing coefficient for a time constant in ms. */
inline float msCoeff (double sr, float ms) noexcept
{
    return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * sr));
}

/** Smooths a parameter towards its target (one-pole). */
struct Smoothed
{
    float value = 0.0f, coeff = 0.0f;
    void setTime (double sr, float ms) noexcept { coeff = msCoeff (sr, ms); }
    void snap (float v) noexcept { value = v; }
    inline float next (float target) noexcept { value = target + coeff * (value - target); return value; }
};

/** Peak / RMS style attack-release envelope follower (linear domain). */
struct EnvelopeFollower
{
    float env = 0.0f, att = 0.0f, rel = 0.0f;
    void setTimes (double sr, float attackMs, float releaseMs) noexcept { att = msCoeff (sr, attackMs); rel = msCoeff (sr, releaseMs); }
    void reset() noexcept { env = 0.0f; }
    inline float process (float x) noexcept
    {
        const float c = x > env ? att : rel;
        env = x + c * (env - x);
        return env;
    }
};

/** Power-of-two circular delay line with fractional (cubic) reads. */
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buffer.assign ((size_t) n, 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
    int capacity() const noexcept { return mask - 3; }

    inline void push (float x) noexcept { buffer[(size_t) pos] = x; pos = (pos + 1) & mask; }

    /** readInt (1) is the most recently pushed sample, readInt (k) the one pushed k - 1 samples earlier. */
    inline float readInt (int d) const noexcept { return buffer[(size_t) ((pos - d) & mask)]; }

    /** Fractional read, `d` samples behind the most recent sample (d >= 1). */
    inline float read (float d) const noexcept
    {
        d = clampf (d, 1.0f, (float) capacity());
        const int i = (int) d;
        const float f = d - (float) i;
        const float y0 = readInt (i), y1 = readInt (i + 1), y2 = readInt (i + 2), y3 = readInt (i + 3);
        // Catmull-Rom style cubic interpolation
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

private:
    std::vector<float> buffer { 0.0f, 0.0f, 0.0f, 0.0f };
    int mask = 3, pos = 0;
};

/** Small, fast deterministic random generator (xorshift). */
struct Random
{
    uint32_t state = 0x12345678u;
    explicit Random (uint32_t seed = 0x12345678u) : state (seed ? seed : 1u) {}
    inline uint32_t nextInt() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    inline float nextFloat() noexcept { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    inline float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }
};

/** Smoothly wandering random modulation source in [-1, 1] (cosine interpolated random steps). */
struct RandomLfo
{
    Random rng;
    float a = 0.0f, b = 0.0f, phase = 1.0f, inc = 0.0f;
    explicit RandomLfo (uint32_t seed = 1u) : rng (seed) {}
    void setRate (double sr, float hz) noexcept { inc = (float) (hz / sr); }
    inline float next() noexcept
    {
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            a = b;
            b = rng.nextBipolar();
        }
        const float t = 0.5f - 0.5f * std::cos ((float) kPi * phase);
        return a + (b - a) * t;
    }
};

/** Sine LFO. */
struct SineLfo
{
    double phase = 0.0, inc = 0.0;
    void setRate (double sr, double hz) noexcept { inc = hz / sr; }
    inline float next (double offset = 0.0) noexcept
    {
        const float v = (float) std::sin (2.0 * kPi * (phase + offset));
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return v;
    }
};

/** 2x oversampler using a linear-phase half-band FIR (used around non-linearities). */
template <int Taps = 31>
class Oversampler2xT
{
public:
    static constexpr int taps = Taps; // odd; half-band so every other tap (except centre) is zero
    Oversampler2xT()
    {
        const int c = taps / 2;
        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const int n = i - c;
            double h = n == 0 ? 0.5 : std::sin (kPi * n * 0.5) / (kPi * n);
            const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / (taps - 1)) + 0.08 * std::cos (4.0 * kPi * i / (taps - 1));
            coeffs[(size_t) i] = (float) (h * w);
            sum += h * w;
        }
        for (auto& v : coeffs) v = (float) (v / sum);
    }

    void reset() noexcept { up.fill (0.0f); down.fill (0.0f); upPos = downPos = 0; }

    /** Round-trip latency in base-rate samples (taps - 1 samples at the doubled rate). */
    static constexpr int latency() { return taps / 2; }

    /** Upsamples x into two samples (o0, o1). */
    inline void upsample (float x, float& o0, float& o1) noexcept
    {
        o0 = filter (up, upPos, 2.0f * x);
        o1 = filter (up, upPos, 0.0f);
    }

    /** Downsamples two oversampled samples into one. */
    inline float downsample (float i0, float i1) noexcept
    {
        // Keep the even-phase output so the round trip is an integer number of base samples
        const float y = filter (down, downPos, i0);
        down[(size_t) downPos] = i1;
        down[(size_t) downPos + (size_t) taps] = i1;
        downPos = (downPos + 1) % taps;
        return y;
    }

private:
    inline float filter (std::array<float, (size_t) Taps * 2>& line, int& p, float x) noexcept
    {
        line[(size_t) p] = x;
        line[(size_t) p + (size_t) taps] = x;
        float acc = 0.0f;
        const float* d = &line[(size_t) (p + 1)];
        for (int i = 0; i < taps; ++i)
            acc += coeffs[(size_t) i] * d[i];
        p = (p + 1) % taps;
        return acc;
    }

    std::array<float, (size_t) Taps> coeffs {};
    std::array<float, (size_t) Taps * 2> up {}, down {};
    int upPos = 0, downPos = 0;
};

using Oversampler2x = Oversampler2xT<31>;

/** Pure delay used to time-align dry paths with processed paths. */
class LatencyDelay
{
public:
    void prepare (int maxDelay) { line.prepare (maxDelay + 2); }
    void setDelay (int d) noexcept { delay = d; }
    void reset() noexcept { line.reset(); }
    inline float process (float x) noexcept
    {
        if (delay <= 0) return x;
        line.push (x);
        return line.readInt (delay + 1);
    }

private:
    DelayLine line;
    int delay = 0;
};

/** Rational tanh approximation (accurate to ~0.2 %, exact limits of +-1). */
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** First order filter designed with the bilinear transform. */
class OnePole
{
public:
    void reset() noexcept { z = 0.0f; }
    void setLowPass (double sr, double f) noexcept
    {
        const double k = std::tan (kPi * clampHz (sr, f) / sr);
        set (k / (1.0 + k), k / (1.0 + k), (k - 1.0) / (k + 1.0));
    }
    void setHighPass (double sr, double f) noexcept
    {
        const double k = std::tan (kPi * clampHz (sr, f) / sr);
        set (1.0 / (1.0 + k), -1.0 / (1.0 + k), (k - 1.0) / (k + 1.0));
    }
    /** Low shelf: gain `db` below f (first order). */
    void setLowShelf (double sr, double f, double db) noexcept
    {
        const double g = std::pow (10.0, db / 20.0), k = std::tan (kPi * clampHz (sr, f) / sr);
        const double sq = std::sqrt (g);
        // H(s) = (s + w*sqrt(g)) / (s + w/sqrt(g)) * ... normalised so that HF gain = 1
        const double nb0 = 1.0 + k * sq, nb1 = k * sq - 1.0, na0 = 1.0 + k / sq, na1 = k / sq - 1.0;
        set (nb0 / na0, nb1 / na0, na1 / na0);
    }
    /** High shelf: gain `db` above f (first order). */
    void setHighShelf (double sr, double f, double db) noexcept
    {
        const double g = std::pow (10.0, db / 20.0), k = std::tan (kPi * clampHz (sr, f) / sr);
        const double sq = std::sqrt (g);
        const double nb0 = sq + k, nb1 = k - sq, na0 = 1.0 / sq + k, na1 = k - 1.0 / sq;
        set (nb0 / na0, nb1 / na0, na1 / na0);
    }
    void setBypass() noexcept { b0 = 1.0f; b1 = 0.0f; a1 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }

private:
    static double clampHz (double sr, double f) noexcept { return f < 1.0 ? 1.0 : (f > sr * 0.49 ? sr * 0.49 : f); }
    void set (double nb0, double nb1, double na1) noexcept { b0 = (float) nb0; b1 = (float) nb1; a1 = (float) na1; }
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, z = 0.0f;
};

/** 4x oversampling made from two cascaded half-band stages. */
class Oversampler4x
{
public:
    void reset() noexcept { s1.reset(); s2.reset(); }
    /** Round-trip latency in base-rate samples (rounded). */
    // 31 taps at 2x (30 samples = 15 base samples) + 33 taps at 4x (32 samples = 8 base samples)
    static constexpr int latency() { return 15 + 8; }

    /** Upsamples n samples from in into 4n samples in out. */
    void upsample (const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float a, b;
            s1.upsample (in[i], a, b);
            s2.upsample (a, out[4 * i], out[4 * i + 1]);
            s2.upsample (b, out[4 * i + 2], out[4 * i + 3]);
        }
    }

    void downsample (const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = s2.downsample (in[4 * i], in[4 * i + 1]);
            const float b = s2.downsample (in[4 * i + 2], in[4 * i + 3]);
            out[i] = s1.downsample (a, b);
        }
    }

private:
    Oversampler2xT<31> s1;
    Oversampler2xT<33> s2;
};
} // namespace laas
