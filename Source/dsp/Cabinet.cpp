#include "Cabinet.h"

#include <algorithm>

namespace laas
{
namespace
{
using cd = std::complex<double>;

// ---- Analog magnitude helpers (dB) -------------------------------------------------------------
double bellDb (double f, double f0, double g, double q)
{
    const double A = std::pow (10.0, g / 40.0), w = f / f0, a = 1.0 - w * w;
    const double num = a * a + std::pow (w * A / q, 2.0), den = a * a + std::pow (w / (A * q), 2.0);
    return 10.0 * std::log10 (num / den);
}
double hp2Db (double f, double f0, double q)
{
    const double w = f / f0, a = 1.0 - w * w;
    return 10.0 * std::log10 (std::pow (w, 4.0) / (a * a + w * w / (q * q)) + 1.0e-30);
}
double lp2Db (double f, double f0, double q)
{
    const double w = f / f0, a = 1.0 - w * w;
    return 10.0 * std::log10 (1.0 / (a * a + w * w / (q * q)));
}
double hp1Db (double f, double f0) { const double w = f / f0; return 10.0 * std::log10 (w * w / (1.0 + w * w) + 1.0e-30); }

struct Bell { double f, g, q; };

struct SpeakerSpec
{
    const char* name;
    const char* shortName;
    double resHz, resQ;
    double openBackHz;           // > 0: extra first order low cut (open back cancellation)
    std::array<Bell, 5> bells;
    double lpHz, lpQ, lp2Hz;
    bool horn;
    uint32_t seed;
    double ripple;               // cone breakup irregularity (dB)
    double heightM;              // speaker centre height above the floor
};

const std::array<SpeakerSpec, (size_t) CabModel::numCabs>& speakers()
{
    static const std::array<SpeakerSpec, (size_t) CabModel::numCabs> s { {
        { "4x12 V30 (Recto)", "4x12 V30", 95.0, 1.3, 0.0,
          { Bell { 650.0, -2.0, 1.2 }, { 1300.0, 1.0, 1.4 }, { 2500.0, 5.0, 1.6 }, { 3500.0, -3.0, 3.0 }, { 4600.0, 4.0, 2.5 } },
          5500.0, 0.9, 7000.0, false, 0x1234u, 3.5, 0.33 },
        { "4x12 Greenback (1960)", "4x12 GB", 80.0, 1.0, 0.0,
          { Bell { 250.0, 1.5, 0.8 }, { 1800.0, 3.0, 1.0 }, { 3000.0, -2.0, 2.0 }, { 4200.0, 2.0, 3.0 }, { 900.0, 0.5, 1.0 } },
          4300.0, 0.8, 6000.0, false, 0x2345u, 2.5, 0.33 },
        { "4x12 T75 (1960A)", "4x12 T75", 105.0, 1.5, 0.0,
          { Bell { 700.0, -3.0, 0.8 }, { 2500.0, 2.0, 1.5 }, { 4800.0, 5.0, 2.0 }, { 180.0, 1.0, 1.0 }, { 3600.0, -1.5, 3.0 } },
          6500.0, 0.9, 8000.0, false, 0x3456u, 3.0, 0.33 },
        { "2x12 Open Back", "2x12 OPEN", 85.0, 0.6, 110.0,
          { Bell { 200.0, 1.0, 0.8 }, { 2800.0, 3.0, 1.2 }, { 5000.0, 2.0, 2.0 }, { 1200.0, -1.0, 1.0 }, { 700.0, 0.5, 1.0 } },
          6000.0, 0.8, 8000.0, false, 0x4567u, 3.0, 0.45 },
        { "8x10 Bass (SVT)", "8x10", 55.0, 1.0, 0.0,
          { Bell { 120.0, 2.0, 1.0 }, { 600.0, 3.0, 0.7 }, { 1800.0, 2.0, 1.5 }, { 3200.0, -3.0, 2.0 }, { 300.0, 0.5, 1.0 } },
          4200.0, 0.9, 5500.0, false, 0x5678u, 2.5, 0.75 },
        { "1x15 Bass (B-15)", "1x15", 45.0, 0.9, 0.0,
          { Bell { 90.0, 2.0, 1.0 }, { 800.0, -2.0, 1.0 }, { 1500.0, 1.5, 1.5 }, { 400.0, 0.5, 1.0 }, { 2200.0, -1.0, 2.0 } },
          2600.0, 0.8, 3500.0, false, 0x6789u, 2.0, 0.4 },
        { "4x10 Bass + Horn", "4x10 HORN", 55.0, 1.1, 0.0,
          { Bell { 300.0, 1.5, 0.8 }, { 2000.0, 2.0, 1.5 }, { 900.0, -1.0, 1.0 }, { 120.0, 1.0, 1.0 }, { 3500.0, -1.0, 2.0 } },
          4800.0, 0.9, 6000.0, true, 0x789au, 2.0, 0.55 },
    } };
    return s;
}

struct MicSpec
{
    const char* name;
    double hpHz;
    std::array<Bell, 3> bells;
    double lpHz, lpQ;
    double gradient; // pressure-gradient share (proximity effect)
};

const std::array<MicSpec, (size_t) MicModel::numMics>& mics()
{
    static const std::array<MicSpec, (size_t) MicModel::numMics> m { {
        { "Dynamic 57", 110.0, { Bell { 5500.0, 5.0, 1.3 }, { 9000.0, -2.0, 2.0 }, { 200.0, -1.0, 0.7 } }, 13000.0, 0.7, 0.5 },
        { "Dynamic 421", 65.0, { Bell { 2000.0, -1.0, 1.0 }, { 5000.0, 3.0, 1.0 }, { 150.0, 0.5, 0.7 } }, 15000.0, 0.7, 0.5 },
        { "Ribbon 121", 35.0, { Bell { 200.0, 1.5, 0.8 }, { 3000.0, -0.5, 1.0 }, { 12000.0, -2.0, 1.0 } }, 6500.0, 0.55, 1.0 },
        { "Condenser 87", 30.0, { Bell { 10000.0, 2.5, 0.8 }, { 4000.0, 0.5, 1.0 }, { 100.0, 0.0, 1.0 } }, 19000.0, 0.7, 0.35 },
        { "Dynamic 7B", 70.0, { Bell { 4000.0, 2.0, 1.0 }, { 8000.0, -1.0, 1.0 }, { 250.0, 0.5, 0.8 } }, 11000.0, 0.6, 0.5 },
    } };
    return m;
}

/** Deterministic cone breakup ripple (dB) between ~900 Hz and 9 kHz. */
double rippleDb (double f, uint32_t seed, double amount)
{
    Random rng (seed);
    double db = 0.0;
    for (int i = 0; i < 22; ++i)
    {
        const double f0 = 900.0 * std::pow (10.0, rng.nextFloat()); // 0.9 .. 9 kHz
        const double g = (rng.nextFloat() * 2.0 - 1.0) * amount * std::min (1.0, f0 / 2500.0);
        const double q = 3.0 + 7.0 * rng.nextFloat();
        db += bellDb (f, f0, g, q);
    }
    return db;
}

int nextPow2 (int v) { int n = 1; while (n < v) n <<= 1; return n; }

double speakerMicDb (double f, const SpeakerSpec& sp, const MicSpec& ms, double pos, double distM, double distNorm)
{
    double db = hp2Db (f, sp.resHz, sp.resQ);
    if (sp.openBackHz > 0.0)
        db += hp1Db (f, sp.openBackHz);
    for (const auto& b : sp.bells)
        db += bellDb (f, b.f, b.g, b.q);
    double cone = lp2Db (f, sp.lpHz, sp.lpQ) + lp2Db (f, sp.lp2Hz, 0.6);
    if (sp.horn)
    {
        const double hornDb = -5.0 + hp2Db (f, 4000.0, 0.7) + lp2Db (f, 13000.0, 0.7);
        cone = 10.0 * std::log10 (std::pow (10.0, cone / 10.0) + std::pow (10.0, hornDb / 10.0));
    }
    db += cone;

    // Position on the cone: centre = bright and focused, edge = darker, warmer, more breakup notches
    const double posEff = clampf ((float) (pos * (1.0 - 0.5 * distNorm)), 0.0f, 1.0f);
    db += lp2Db (f, 9500.0 * std::pow (2600.0 / 9500.0, posEff), 0.7);
    db += bellDb (f, 3500.0, 3.0 * (1.0 - posEff) * (1.0 - posEff), 1.5);
    db += bellDb (f, 300.0, 2.5 * posEff, 0.8);
    db += bellDb (f, 1600.0 + 700.0 * posEff, -4.0 * posEff, 4.0);
    const double rip = sp.ripple * (1.0 - 0.5 * distNorm);
    db += (1.0 - posEff) * rippleDb (f, sp.seed, rip) + posEff * rippleDb (f, sp.seed * 7u + 3u, rip);

    // Microphone
    db += hp2Db (f, ms.hpHz, 0.6) + lp2Db (f, ms.lpHz, ms.lpQ);
    for (const auto& b : ms.bells)
        if (b.g != 0.0)
            db += bellDb (f, b.f, b.g, b.q);

    // Proximity effect relative to 4 cm, and air / beaming loss with distance
    const double c = 343.0, dref = 0.04;
    const double k = c / (2.0 * kPi * std::max (f, 10.0));
    const double prox = 10.0 * std::log10 ((1.0 + std::pow (k / distM, 2.0)) / (1.0 + std::pow (k / dref, 2.0)));
    db += ms.gradient * std::clamp (prox, -14.0, 12.0);
    db += -3.0 * distNorm * (f * f / (f * f + 6000.0 * 6000.0));
    return db;
}

void addTap (std::vector<double>& r, double pos, double amp)
{
    const int i = (int) std::floor (pos);
    const double frac = pos - i;
    if (i >= 0 && i + 1 < (int) r.size())
    {
        r[(size_t) i] += amp * (1.0 - frac);
        r[(size_t) i + 1] += amp * frac;
    }
}
} // namespace

const char* cabModelName (int c) noexcept { return speakers()[(size_t) std::clamp (c, 0, (int) CabModel::numCabs - 1)].name; }
const char* cabShortName (int c) noexcept { return speakers()[(size_t) std::clamp (c, 0, (int) CabModel::numCabs - 1)].shortName; }
const char* micModelName (int m) noexcept { return mics()[(size_t) std::clamp (m, 0, (int) MicModel::numMics - 1)].name; }

float responsePointHz (int i) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) i / (float) (kResponsePoints - 1));
}

double micDelaySamples (double sampleRate, float distanceCm) noexcept
{
    return (double) std::max (0.0f, distanceCm) * 0.01 / 343.0 * sampleRate;
}

std::vector<float> buildCabinetIR (double sr, CabModel cab, const MicPlacement& mp, double extraDelay)
{
    const auto& sp = speakers()[(size_t) std::clamp ((int) cab, 0, (int) CabModel::numCabs - 1)];
    const auto& ms = mics()[(size_t) std::clamp ((int) mp.mic, 0, (int) MicModel::numMics - 1)];
    const double distCm = std::clamp ((double) mp.distanceCm, 1.0, 100.0);
    const double distM = distCm * 0.01;
    const double distNorm = std::log (distCm) / std::log (100.0);
    const double pos = std::clamp ((double) mp.position, 0.0, 1.0);

    const int N = nextPow2 ((int) (sr * 0.8));
    FFT<double> fft;
    fft.prepare (N);

    // ---- Minimum phase speaker + mic response (homomorphic / cepstrum method) -----------------
    std::vector<cd> spec ((size_t) N);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = std::max (1.0, (double) k * sr / N);
        double db = speakerMicDb (f, sp, ms, pos, distM, distNorm);
        if (f > sr * 0.45)
            db -= 60.0 * (f - sr * 0.45) / (sr * 0.05); // keep it clean towards Nyquist
        db = std::max (db, -100.0);
        spec[(size_t) k] = std::log (std::pow (10.0, db / 20.0));
    }
    for (int k = N / 2 + 1; k < N; ++k)
        spec[(size_t) k] = spec[(size_t) (N - k)];
    fft.inverse (spec.data());
    for (int n = 1; n < N / 2; ++n)
        spec[(size_t) n] *= 2.0;
    for (int n = N / 2 + 1; n < N; ++n)
        spec[(size_t) n] = 0.0;
    for (auto& v : spec) v = cd (v.real(), 0.0);
    fft.forward (spec.data());
    for (auto& v : spec) v = std::exp (v);

    // ---- Reflections: direct sound, floor bounce, a few room reflections, diffuse tail ------------
    const int length = std::min (N / 2, (int) (sr * (0.06 + 0.30 * distNorm)) + (int) extraDelay);
    std::vector<double> refl ((size_t) N, 0.0);
    addTap (refl, extraDelay, 1.0);
    const double dEff = std::max (distM, 0.03);
    const double toSamples = sr / 343.0;
    {
        const double h = sp.heightM;
        const double floorPath = 2.0 * std::sqrt (h * h + 0.25 * distM * distM);
        addTap (refl, extraDelay + (floorPath - distM) * toSamples, 0.8 * dEff / floorPath);
        const double rooms[] = { 2.2, 2.9, 3.7, 4.6, 5.5 };
        std::vector<double> late ((size_t) N, 0.0);
        for (double L : rooms)
            addTap (late, extraDelay + L * toSamples, 0.55 * dEff / L * (std::fmod (L * 7.3, 2.0) > 1.0 ? 1.0 : -1.0));
        // Diffuse tail (small room), energy relative to the direct sound grows with distance
        Random rng (0xC0FFEEu + (uint32_t) cab);
        const double t60 = 0.35, amp = 0.0115 * distM;
        const int start = (int) (extraDelay + 0.012 * sr);
        for (int n = start; n < length; ++n)
        {
            const double t = (n - start) / sr;
            late[(size_t) n] += amp * rng.nextBipolar() * std::exp (-6.9 * t / t60) * std::min (1.0, t / 0.01);
        }
        // Reflections lose high end on the walls
        const double a = std::exp (-2.0 * kPi * 4000.0 / sr);
        double z = 0.0;
        for (size_t n = 0; n < late.size(); ++n)
        {
            z = late[n] * (1.0 - a) + a * z;
            refl[n] += z;
        }
    }
    std::vector<cd> rs ((size_t) N);
    for (int n = 0; n < N; ++n)
        rs[(size_t) n] = refl[(size_t) n];
    fft.forward (rs.data());
    for (int k = 0; k < N; ++k)
        spec[(size_t) k] *= rs[(size_t) k];
    fft.inverse (spec.data());

    std::vector<float> ir ((size_t) length);
    const int fade = std::max (16, length / 10);
    for (int n = 0; n < length; ++n)
    {
        double w = 1.0;
        if (n > length - fade)
            w = 0.5 + 0.5 * std::cos (kPi * (n - (length - fade)) / fade);
        ir[(size_t) n] = (float) (spec[(size_t) n].real() * w);
    }
    return ir;
}

//==============================================================================
std::vector<float> resampleIR (const std::vector<float>& in, double fromRate, double toRate)
{
    if (in.empty() || fromRate <= 0.0 || toRate <= 0.0 || std::abs (fromRate - toRate) < 0.5)
        return in;
    const double ratio = toRate / fromRate;
    const double cutoff = std::min (1.0, ratio) * 0.97;
    const int halfWidth = (int) std::ceil (24.0 / cutoff);
    const int outLen = (int) std::ceil ((double) in.size() * ratio);
    std::vector<float> out ((size_t) outLen);
    for (int j = 0; j < outLen; ++j)
    {
        const double t = j / ratio;
        const int c = (int) std::floor (t);
        double acc = 0.0;
        for (int k = c - halfWidth + 1; k <= c + halfWidth; ++k)
        {
            if (k < 0 || k >= (int) in.size())
                continue;
            const double x = t - k;
            const double arg = kPi * cutoff * x;
            const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
            const double wpos = x / (halfWidth + 1);
            const double win = std::abs (wpos) >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos (kPi * wpos);
            acc += in[(size_t) k] * cutoff * sinc * win;
        }
        out[(size_t) j] = (float) (acc / ratio); // keep the continuous-time gain (sum of taps scales with the rate)
    }
    return out;
}

std::array<float, kResponsePoints> irResponse (const std::vector<float>& ir, double sr)
{
    std::array<float, kResponsePoints> r {};
    if (ir.empty())
    {
        r.fill (-100.0f);
        return r;
    }
    const int N = nextPow2 (std::max ((int) ir.size() * 2, 16384));
    FFT<double> fft;
    fft.prepare (N);
    std::vector<cd> s ((size_t) N);
    for (size_t i = 0; i < ir.size(); ++i)
        s[i] = ir[i];
    fft.forward (s.data());
    for (int i = 0; i < kResponsePoints; ++i)
    {
        // average power over 1/12 octave around the display frequency
        const double f = responsePointHz (i);
        const int k0 = std::max (1, (int) (f * std::pow (2.0, -1.0 / 24.0) * N / sr));
        const int k1 = std::min (N / 2, std::max (k0, (int) (f * std::pow (2.0, 1.0 / 24.0) * N / sr)));
        double p = 0.0;
        for (int k = k0; k <= k1; ++k)
            p += std::norm (s[(size_t) k]);
        p /= (k1 - k0 + 1);
        r[(size_t) i] = (float) (10.0 * std::log10 (p + 1.0e-20));
    }
    return r;
}

void conditionIR (std::vector<float>& ir, double sr, double maxSeconds)
{
    if (ir.empty())
        return;
    const size_t maxLen = (size_t) std::max (64.0, maxSeconds * sr);
    if (ir.size() > maxLen)
        ir.resize (maxLen);
    float peak = 0.0f;
    for (float v : ir) peak = std::max (peak, std::abs (v));
    if (peak <= 0.0f)
        return;
    // trim trailing silence below -80 dB of the peak
    size_t last = ir.size();
    while (last > 64 && std::abs (ir[last - 1]) < peak * 1.0e-4f)
        --last;
    ir.resize (last);
    const size_t fade = std::min<size_t> (ir.size() / 8, (size_t) (0.005 * sr));
    for (size_t i = 0; i < fade; ++i)
        ir[ir.size() - 1 - i] *= (float) i / (float) fade;

    // Loudness: average magnitude over 100 Hz .. 4 kHz at 0 dB
    const auto resp = irResponse (ir, sr);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < kResponsePoints; ++i)
        if (responsePointHz (i) >= 100.0f && responsePointHz (i) <= 4000.0f)
        {
            sum += std::pow (10.0, resp[(size_t) i] / 20.0);
            ++count;
        }
    const double avg = count > 0 ? sum / count : 1.0;
    const float g = (float) (avg > 1.0e-9 ? 1.0 / avg : 1.0);
    for (auto& v : ir) v *= g;
}

//==============================================================================
PreparedIR* prepareIR (const std::vector<float>& ir, int P)
{
    auto* p = new PreparedIR();
    p->partition = P;
    p->length = (int) ir.size();
    p->numParts = std::max (1, ((int) ir.size() + P - 1) / P);
    p->head.assign ((size_t) P, 0.0f);
    for (int i = 0; i < P && i < (int) ir.size(); ++i)
        p->head[(size_t) i] = ir[(size_t) i];
    FFT<float> fft;
    fft.prepare (2 * P);
    std::vector<std::complex<float>> frame ((size_t) (2 * P));
    p->spectra.assign ((size_t) std::max (0, p->numParts - 1) * (size_t) (P + 1), {});
    for (int k = 1; k < p->numParts; ++k)
    {
        std::fill (frame.begin(), frame.end(), std::complex<float>());
        for (int i = 0; i < P; ++i)
        {
            const int idx = k * P + i;
            if (idx < (int) ir.size())
                frame[(size_t) i] = ir[(size_t) idx];
        }
        fft.forward (frame.data());
        std::copy (frame.begin(), frame.begin() + P + 1, p->spectra.begin() + (long) ((k - 1) * (P + 1)));
    }
    return p;
}

void Convolver::prepare (int partition, int maxPartitions)
{
    P = partition;
    maxParts = std::max (1, maxPartitions);
    fft.prepare (2 * P);
    history.assign ((size_t) (2 * P), 0.0f);
    block.assign ((size_t) P, 0.0f);
    prevBlock.assign ((size_t) P, 0.0f);
    tail.assign ((size_t) P, 0.0f);
    combinedHead.assign ((size_t) P, 0.0f);
    fdl.assign ((size_t) maxParts * (size_t) (P + 1), {});
    work.assign ((size_t) (2 * P), {});
    acc.assign ((size_t) (P + 1), {});
    reset();
}

void Convolver::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (block.begin(), block.end(), 0.0f);
    std::fill (prevBlock.begin(), prevBlock.end(), 0.0f);
    std::fill (tail.begin(), tail.end(), 0.0f);
    std::fill (fdl.begin(), fdl.end(), std::complex<float>());
    fdlPos = blockPos = histPos = 0;
    refreshHead();
}

void Convolver::refreshHead() noexcept
{
    std::fill (combinedHead.begin(), combinedHead.end(), 0.0f);
    headLen = 0;
    for (const auto& s : sources)
    {
        if (s.ir == nullptr || s.weight == 0.0f || s.ir->partition != P)
            continue;
        for (int t = 0; t < P; ++t)
            combinedHead[(size_t) t] += s.weight * s.ir->head[(size_t) t];
        headLen = P;
    }
}

void Convolver::boundary() noexcept
{
    // Spectrum of the newest frame [previous block, current block]
    for (int i = 0; i < P; ++i)
    {
        work[(size_t) i] = prevBlock[(size_t) i];
        work[(size_t) (i + P)] = block[(size_t) i];
    }
    fft.forward (work.data());
    std::copy (work.begin(), work.begin() + P + 1, fdl.begin() + (long) (fdlPos * (P + 1)));
    std::swap (prevBlock, block);

    // Tail of the next block: partitions k >= 1 use frames j - (k - 1)
    std::fill (acc.begin(), acc.end(), std::complex<float>());
    bool any = false;
    for (const auto& s : sources)
    {
        if (s.ir == nullptr || s.weight == 0.0f || s.ir->partition != P)
            continue;
        const int parts = std::min (s.ir->numParts, maxParts + 1);
        for (int k = 1; k < parts; ++k)
        {
            const int frame = (fdlPos - (k - 1) + maxParts) % maxParts;
            const auto* X = &fdl[(size_t) (frame * (P + 1))];
            const auto* H = &s.ir->spectra[(size_t) ((k - 1) * (P + 1))];
            const float w = s.weight;
            for (int b = 0; b <= P; ++b)
                acc[(size_t) b] += w * X[b] * H[b];
            any = true;
        }
    }
    fdlPos = (fdlPos + 1) % maxParts;

    if (any)
    {
        for (int b = 0; b <= P; ++b)
            work[(size_t) b] = acc[(size_t) b];
        for (int b = P + 1; b < 2 * P; ++b)
            work[(size_t) b] = std::conj (acc[(size_t) (2 * P - b)]);
        fft.inverse (work.data());
        for (int i = 0; i < P; ++i)
            tail[(size_t) i] = work[(size_t) (P + i)].real();
    }
    else
        std::fill (tail.begin(), tail.end(), 0.0f);

    refreshHead();
}
} // namespace laas
