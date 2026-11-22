#pragma once

#include <cmath>
#include <vector>

#include "DspUtils.h"

namespace laas
{
/**
    Chromatic tuner for guitar and bass (about 27 Hz .. 1.3 kHz). YIN pitch detection on a decimated
    copy of the input. Not real-time critical: the plugin runs it on the message thread.
*/
class Tuner
{
public:
    struct Result
    {
        bool valid = false;
        float hz = 0.0f;
        float midi = 0.0f;   // fractional MIDI note
        int note = 0;        // nearest MIDI note
        float cents = 0.0f;  // deviation from the nearest note
    };

    void prepare (double sampleRate, float referenceA = 440.0f)
    {
        sr = sampleRate;
        refA = referenceA;
        decim = std::max (1, (int) std::round (sr / 24000.0));
        dsr = sr / decim;
        lp1.setLowPass (sr, 1500.0, 0.7071);
        lp2.setLowPass (sr, 1500.0, 0.7071);
        hp.setHighPass (sr, 22.0, 0.7071);
        const int window = (int) (dsr * 0.075);
        buffer.assign ((size_t) (window * 2), 0.0f);
        diff.assign ((size_t) window, 0.0f);
        writePos = 0;
        phase = 0;
    }

    void setReference (float a4) noexcept { refA = a4; }

    /** Feed input samples (any block size). */
    void push (const float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float y = lp2.process (lp1.process (hp.process (x[i])));
            if (++phase >= decim)
            {
                phase = 0;
                buffer[(size_t) writePos] = y;
                writePos = (writePos + 1) % (int) buffer.size();
            }
        }
    }

    Result analyse() noexcept
    {
        Result r;
        const int size = (int) buffer.size();
        const int W = size / 2;
        std::vector<float>& d = diff;
        // Linearise the ring (oldest first)
        tmp.resize ((size_t) size);
        float energy = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            tmp[(size_t) i] = buffer[(size_t) ((writePos + i) % size)];
            energy += tmp[(size_t) i] * tmp[(size_t) i];
        }
        if (energy / (float) size < 1.0e-7f)
            return r;

        const int minTau = std::max (2, (int) (dsr / 1400.0));
        const int maxTau = std::min (W - 1, (int) (dsr / 26.0));
        const float* s = tmp.data() + (size - 2 * W);
        d[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float acc = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float e = s[j] - s[j + tau];
                acc += e * e;
            }
            running += acc;
            d[(size_t) tau] = running > 0.0f ? acc * (float) tau / running : 1.0f;
        }
        int best = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxTau && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
            return r;
        // Parabolic interpolation
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
        const float period = (float) best + clampf (shift, -1.0f, 1.0f);
        r.hz = (float) (dsr / period);
        r.midi = 69.0f + 12.0f * std::log2 (r.hz / refA);
        r.note = (int) std::lround (r.midi);
        r.cents = (r.midi - (float) r.note) * 100.0f;
        r.valid = r.hz > 25.0f && r.hz < 1500.0f;
        return r;
    }

private:
    double sr = 48000.0, dsr = 24000.0;
    float refA = 440.0f;
    int decim = 4, phase = 0, writePos = 0;
    Biquad lp1, lp2, hp;
    std::vector<float> buffer, diff, tmp;
};
} // namespace laas
