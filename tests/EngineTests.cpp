// Headless tests for the LA-AmpSuite DSP engine (no JUCE needed).
#include "dsp/AmpEngine.h"
#include "dsp/Tuner.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace laas;

namespace
{
int failures = 0;
bool verbose = false;
void check (bool ok, const std::string& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (! ok)
        ++failures;
}
std::string fmt (double v, int dec = 1)
{
    char b[64];
    std::snprintf (b, sizeof (b), "%.*f", dec, v);
    return b;
}

constexpr double sr = 48000.0;

std::vector<float> sine (double hz, double seconds, float amp)
{
    std::vector<float> v ((size_t) (seconds * sr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * (float) std::sin (2.0 * kPi * hz * (double) i / sr);
    return v;
}

/** A plucked-string-like guitar DI: decaying harmonics, a new note every 0.5 s, some pick noise. */
std::vector<float> guitarDI (double seconds, float amp = 0.3f, float baseMidi = 40.0f)
{
    std::vector<float> v ((size_t) (seconds * sr));
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    const float riff[] = { 0, 0, 7, 5, 0, 3, 10, 12 };
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double) i / sr;
        const int note = (int) (t / 0.5);
        const double tn = t - note * 0.5;
        const double f0 = 440.0 * std::pow (2.0, (baseMidi + riff[note % 8] - 69.0) / 12.0);
        double s = 0.0;
        for (int h = 1; h <= 14; ++h)
            s += std::sin (2.0 * kPi * f0 * h * tn) * std::exp (-tn * (2.0 + h * 0.8)) / (double) h;
        v[i] = amp * (float) s + (tn < 0.004 ? 0.05f * noise (rng) : 0.0f) + 0.0005f * noise (rng);
    }
    return v;
}

double rms (const float* x, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, n));
}
double rmsDb (const std::vector<float>& x, size_t from = 0)
{
    return 20.0 * std::log10 (rms (x.data() + from, x.size() - from) + 1.0e-12);
}

/** Single-frequency DFT magnitude. */
double dftMag (const float* x, size_t n, double hz)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1));
        re += x[i] * w * std::cos (2.0 * kPi * hz * (double) i / sr);
        im -= x[i] * w * std::sin (2.0 * kPi * hz * (double) i / sr);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (double) n * 2.0;
}

/** THD of a signal containing f0 (first 10 harmonics). */
double thd (const std::vector<float>& x, size_t from, double f0)
{
    const size_t n = x.size() - from;
    const double fund = dftMag (x.data() + from, n, f0);
    double harm = 0.0;
    for (int h = 2; h <= 10; ++h)
        if (f0 * h < sr * 0.45)
            harm += std::pow (dftMag (x.data() + from, n, f0 * h), 2.0);
    return std::sqrt (harm) / std::max (1.0e-12, fund);
}

EngineParams dryParams()
{
    EngineParams p;
    p.ampOn = false;
    p.cab.enabled = false;
    p.limiterOn = false;
    return p;
}

/** Runs a mono signal through an engine in blocks; returns the left output. */
std::vector<float> runEngine (AmpEngine& e, const std::vector<float>& in, int block = 256)
{
    std::vector<float> out (in.size()), l ((size_t) block), r ((size_t) block);
    for (size_t start = 0; start < in.size(); start += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - start);
        std::copy (in.begin() + (long) start, in.begin() + (long) start + n, l.begin());
        std::copy (in.begin() + (long) start, in.begin() + (long) start + n, r.begin());
        float* ch[2] = { l.data(), r.data() };
        e.process (ch, 2, n);
        std::copy (l.begin(), l.begin() + n, out.begin() + (long) start);
    }
    return out;
}

/** Runs only the amp module (4x oversampled like the engine) on a signal. */
std::vector<float> runAmp (const AmpParams& ap, const std::vector<float>& in)
{
    AmpEngine e;
    auto p = dryParams();
    p.ampOn = true;
    p.amp = ap;
    e.setParams (p);
    e.prepare (sr, 512);
    return runEngine (e, in, 512);
}

std::vector<float> directConvolve (const std::vector<float>& x, const std::vector<float>& h)
{
    std::vector<float> y (x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n)
    {
        double acc = 0.0;
        for (size_t k = 0; k < h.size() && k <= n; ++k)
            acc += (double) h[k] * x[n - k];
        y[n] = (float) acc;
    }
    return y;
}

double bandEnergyDb (const std::array<float, kResponsePoints>& r, float lo, float hi)
{
    double s = 0.0;
    int c = 0;
    for (int i = 0; i < kResponsePoints; ++i)
        if (responsePointHz (i) >= lo && responsePointHz (i) <= hi)
        {
            s += std::pow (10.0, r[(size_t) i] / 10.0);
            ++c;
        }
    return 10.0 * std::log10 (s / std::max (1, c) + 1.0e-20);
}
} // namespace

//==============================================================================
static void testToneStack()
{
    std::printf ("Tone stack\n");
    const auto& m800 = ampModelSpec (AmpModel::brit800);
    ToneStackSettings noon;
    auto db = [&] (ToneStackSettings s, double f) { return ToneStack::magnitudeDb (ToneStackType::tmb, m800.tmb, s, f, sr * 4); };

    check (db (noon, 500.0) < db (noon, 100.0) - 1.0 && db (noon, 500.0) < db (noon, 3000.0) - 1.0,
           "Marshall stack at noon has the classic mid scoop (100 Hz " + fmt (db (noon, 100.0)) + ", 500 Hz " + fmt (db (noon, 500.0))
               + ", 3 kHz " + fmt (db (noon, 3000.0)) + " dB)");
    double maxDb = -100.0;
    for (float b : { 0.0f, 1.0f })
        for (float m : { 0.0f, 1.0f })
            for (float t : { 0.0f, 1.0f })
                for (double f = 20.0; f < 20000.0; f *= 1.1)
                {
                    ToneStackSettings s;
                    s.bass = b; s.mid = m; s.treble = t;
                    maxDb = std::max (maxDb, db (s, f));
                }
    check (maxDb < 0.5, "passive stack never boosts (max " + fmt (maxDb, 2) + " dB)");

    ToneStackSettings bassUp = noon, midUp = noon, trebleUp = noon, bassDown = noon, midDown = noon, trebleDown = noon;
    bassUp.bass = 1.0f; midUp.mid = 1.0f; trebleUp.treble = 1.0f;
    bassDown.bass = 0.0f; midDown.mid = 0.0f; trebleDown.treble = 0.0f;
    check (db (bassUp, 80.0) > db (bassDown, 80.0) + 4.0, "bass knob sweeps the lows (" + fmt (db (bassUp, 80.0) - db (bassDown, 80.0)) + " dB)");
    check (db (midUp, 600.0) > db (midDown, 600.0) + 6.0, "mid knob sweeps the mids (" + fmt (db (midUp, 600.0) - db (midDown, 600.0)) + " dB)");
    check (db (trebleUp, 4000.0) > db (trebleDown, 4000.0) + 6.0, "treble knob sweeps the highs (" + fmt (db (trebleUp, 4000.0) - db (trebleDown, 4000.0)) + " dB)");

    // Digital (bilinear) filter matches the analog model at 1 kHz
    ToneStack ts;
    ts.prepare (sr * 4);
    ts.configure (ToneStackType::tmb, m800.tmb);
    ts.set (noon);
    const auto x = sine (1000.0 / 4.0, 1.0, 0.5f); // at 4x rate, 250 Hz "sample rate relative" = 1 kHz
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = ts.process (x[i]);
    const double measured = 20.0 * std::log10 (rms (y.data() + 20000, y.size() - 20000) / rms (x.data() + 20000, x.size() - 20000));
    const double expected = db (noon, 1000.0);
    check (std::abs (measured - expected) < 0.3, "discretised stack matches the analog response (" + fmt (measured, 2) + " vs " + fmt (expected, 2) + " dB)");

    // SVT stack: ultra lo cuts 500 Hz, mid selector moves the mid band
    ToneStackSettings svt;
    svt.mid = 1.0f;
    svt.midFreq = 0;
    const double at220 = ToneStack::magnitudeDb (ToneStackType::svt, {}, svt, 220.0, sr);
    svt.midFreq = 4;
    const double at3k = ToneStack::magnitudeDb (ToneStackType::svt, {}, svt, 3000.0, sr);
    check (at220 > 10.0 && at3k > 10.0, "SVT mid boost follows the 5-position frequency selector");
    ToneStackSettings ul;
    ul.ultraLo = true;
    check (ToneStack::magnitudeDb (ToneStackType::svt, {}, ul, 500.0, sr) < -8.0, "SVT ultra lo scoops 500 Hz");
}

static void testAmps()
{
    std::printf ("Amp models\n");
    const auto di = guitarDI (2.0, 0.3f);
    const auto bassDi = guitarDI (2.0, 0.4f, 28.0f);
    std::vector<double> levels;
    bool allFinite = true;
    for (int m = 0; m < (int) AmpModel::numModels; ++m)
    {
        AmpParams ap;
        ap.model = (AmpModel) m;
        const bool bass = ampModelSpec (ap.model).bass;
        const auto y = runAmp (ap, bass ? bassDi : di);
        for (float v : y) allFinite &= std::isfinite (v) && std::abs (v) < 8.0f;
        const double lvl = rmsDb (y, (size_t) (0.2 * sr));

        AmpParams lo = ap, hi = ap;
        lo.gain = 0.5f;
        hi.gain = 9.0f;
        const auto s = sine (bass ? 82.4 : 196.0, 0.6, bass ? 0.2f : 0.1f);
        const double tLo = thd (runAmp (lo, s), (size_t) (0.25 * sr), bass ? 82.4 : 196.0);
        const double tHi = thd (runAmp (hi, s), (size_t) (0.25 * sr), bass ? 82.4 : 196.0);
        levels.push_back (lvl);
        std::printf ("        %-14s level %6.1f dB   THD gain 0.5: %5.1f %%   gain 9: %5.1f %%\n", ampModelName (m), lvl, tLo * 100.0, tHi * 100.0);
        check (tHi > tLo * 1.5 && tHi > 0.05, std::string (ampModelName (m)) + ": more gain = more distortion");
    }
    check (allFinite, "all models stay finite and bounded");
    double lo = 1e9, hi = -1e9;
    for (double v : levels) { lo = std::min (lo, v); hi = std::max (hi, v); }
    check (hi - lo < 4.0, "models are level matched within 4 dB (" + fmt (lo) + " .. " + fmt (hi) + " dB)");

    // High-gain voicings distort more than the plexi at the same knob setting
    auto thdAt = [&] (AmpModel m)
    {
        AmpParams ap;
        ap.model = m;
        ap.gain = 6.0f;
        return thd (runAmp (ap, sine (196.0, 0.6, 0.1f)), (size_t) (0.25 * sr), 196.0);
    };
    check (thdAt (AmpModel::recto) > thdAt (AmpModel::plexi) && thdAt (AmpModel::american51) > thdAt (AmpModel::plexi),
           "Recto and 51 have more preamp gain than the Plexi");

    // Aliasing: a 3.1 kHz tone through a high-gain amp; energy between the harmonics stays low
    {
        AmpParams ap;
        ap.model = AmpModel::american51;
        ap.gain = 9.0f;
        ap.treble = 8.0f;
        const auto y = runAmp (ap, sine (3100.0, 0.5, 0.3f));
        const size_t from = (size_t) (0.2 * sr), n = y.size() - from;
        double harm = 0.0, inharm = 0.0;
        for (int h = 1; h * 3100.0 < 20000.0; ++h)
            harm += std::pow (dftMag (y.data() + from, n, 3100.0 * h), 2.0);
        for (double f = 500.0; f < 18000.0; f += 731.0)
        {
            bool nearH = false;
            for (int h = 1; h < 8; ++h) nearH |= std::abs (f - 3100.0 * h) < 150.0;
            if (! nearH) inharm = std::max (inharm, std::pow (dftMag (y.data() + from, n, f), 2.0));
        }
        const double ratioDb = 10.0 * std::log10 (inharm / harm);
        check (ratioDb < -45.0, "oversampling keeps aliasing low (" + fmt (ratioDb) + " dB)");
    }

    // Presence and depth do what they say
    {
        auto band = [&] (AmpParams ap, double f) { return 20.0 * std::log10 (dftMag (runAmp (ap, sine (f, 0.5, 0.002f)).data() + 12000, 12000, f) + 1e-12); };
        AmpParams a;
        a.model = AmpModel::recto;
        a.gain = 0.0f;
        AmpParams p = a, d = a;
        a.presence = 0.0f; a.depth = 0.0f;
        p.presence = 10.0f; p.depth = 0.0f;
        d.depth = 10.0f; d.presence = 0.0f;
        check (band (p, 5000.0) > band (a, 5000.0) + 3.0, "presence lifts the top end");
        check (band (d, 80.0) > band (a, 80.0) + 3.0, "depth / resonance lifts the low end");
    }
}

static void testPedals()
{
    std::printf ("Pedals\n");
    const auto di = guitarDI (1.5, 0.25f);

    // Screamer: mid hump; distortion > screamer distortion at max
    {
        AmpEngine e;
        auto p = dryParams();
        p.drive.enabled = true;
        p.drive.type = DriveType::screamer;
        p.drive.drive = 0.0f;
        e.setParams (p);
        e.prepare (sr, 256);
        auto lowOut = runEngine (e, sine (100.0, 0.5, 0.05f));
        e.reset();
        auto midOut = runEngine (e, sine (1000.0, 0.5, 0.05f));
        const double l = rmsDb (lowOut, 12000), mdb = rmsDb (midOut, 12000);
        check (mdb > l + 3.0, "screamer has a mid hump (100 Hz " + fmt (l - rmsDb (sine (100, 0.5, 0.05f))) + " dB, 1 kHz "
                                  + fmt (mdb - rmsDb (sine (1000, 0.5, 0.05f))) + " dB)");
    }
    for (int t = 0; t < (int) DriveType::numTypes; ++t)
    {
        AmpEngine e;
        auto p = dryParams();
        p.drive.enabled = true;
        p.drive.type = (DriveType) t;
        p.drive.drive = 8.0f;
        e.setParams (p);
        e.prepare (sr, 256);
        const auto y = runEngine (e, sine (196.0, 0.6, 0.2f));
        bool finite = true;
        for (float v : y) finite &= std::isfinite (v) && std::abs (v) < 4.0f;
        const double d = thd (y, 12000, 196.0);
        check (finite && d > 0.05, std::string (driveTypeName (t)) + " distorts (THD " + fmt (d * 100.0) + " %)");
    }

    // Boost tight: cuts lows before the amp
    {
        AmpEngine e;
        auto p = dryParams();
        p.boost.enabled = true;
        p.boost.gainDb = 0.0f;
        p.boost.tightHz = 400.0f;
        e.setParams (p);
        e.prepare (sr, 256);
        check (rmsDb (runEngine (e, sine (80.0, 0.5, 0.1f)), 12000) < rmsDb (sine (80.0, 0.5, 0.1f)) - 15.0, "boost 'tight' removes low end");
    }

    // Compressor evens out levels
    {
        AmpEngine e;
        auto p = dryParams();
        p.comp.enabled = true;
        p.comp.sustain = 8.0f;
        e.setParams (p);
        e.prepare (sr, 256);
        const double loud = rmsDb (runEngine (e, sine (300.0, 1.0, 0.5f)), 24000);
        e.reset();
        const double soft = rmsDb (runEngine (e, sine (300.0, 1.0, 0.05f)), 24000);
        check (loud - soft < 12.0, "compressor reduces a 20 dB level difference to " + fmt (loud - soft) + " dB");
    }

    // Pedal order matters: fuzz into boost vs boost into fuzz
    {
        auto run = [&] (PedalOrder o)
        {
            AmpEngine e;
            auto p = dryParams();
            p.drive.enabled = true;
            p.drive.type = DriveType::fuzz;
            p.drive.drive = 3.0f;
            p.boost.enabled = true;
            p.boost.gainDb = 18.0f;
            p.pedalOrder = o;
            e.setParams (p);
            e.prepare (sr, 256);
            return rmsDb (runEngine (e, di), 4800);
        };
        const double a = run ({ 0, 1, 2, 3 }), b = run ({ 0, 1, 3, 2 });
        check (std::abs (a - b) > 3.0, "pedal order changes the sound (" + fmt (a) + " vs " + fmt (b) + " dB)");
    }

    // Gate: hiss between notes is removed even after a high gain amp (tracking gate)
    {
        auto noiseLevel = [&] (bool gateOn)
        {
            AmpEngine e;
            auto p = dryParams();
            p.ampOn = true;
            p.amp.model = AmpModel::recto;
            p.amp.gain = 9.0f;
            p.gate.enabled = gateOn;
            p.gate.thresholdDb = -50.0f;
            e.setParams (p);
            e.prepare (sr, 256);
            std::vector<float> x = guitarDI (0.5, 0.3f);
            std::mt19937 rng (3);
            std::normal_distribution<float> nd (0.0f, 0.0005f);
            for (int i = 0; i < (int) sr; ++i) x.push_back (nd (rng)); // 1 s of hiss
            const auto y = runEngine (e, x);
            return rmsDb (std::vector<float> (y.end() - (long) (0.6 * sr), y.end()));
        };
        const double open = noiseLevel (false), gated = noiseLevel (true);
        check (gated < open - 30.0, "gate removes amplified hiss (" + fmt (open) + " -> " + fmt (gated) + " dB)");
    }
}

static void testCab()
{
    std::printf ("Cabinet\n");
    // ---- Built-in IRs -----
    MicPlacement mic;
    for (int c = 0; c < (int) CabModel::numCabs; ++c)
    {
        auto ir = buildCabinetIR (sr, (CabModel) c, mic);
        conditionIR (ir, sr);
        const auto r = irResponse (ir, sr);
        const double mid = bandEnergyDb (r, 1500, 4000), top = bandEnergyDb (r, 9000, 16000), low = bandEnergyDb (r, 25, 40);
        bool finite = true;
        for (float v : ir) finite &= std::isfinite (v);
        if (verbose)
            std::printf ("        %-24s len %5d  low %6.1f  mid %6.1f  top %6.1f\n", cabModelName (c), (int) ir.size(), low, mid, top);
        check (finite && top < mid - 15.0 && low < mid, std::string (cabModelName (c)) + ": speaker band-limits the amp (top " + fmt (top - mid) + " dB)");
    }
    auto ir810 = buildCabinetIR (sr, CabModel::bass810, mic), ir412 = buildCabinetIR (sr, CabModel::v30x412, mic);
    conditionIR (ir810, sr);
    conditionIR (ir412, sr);
    check (bandEnergyDb (irResponse (ir810, sr), 40, 70) > bandEnergyDb (irResponse (ir412, sr), 40, 70) + 4.0, "bass cab goes deeper than a guitar 4x12");

    // Position: centre is brighter than the edge
    {
        MicPlacement c = mic, e = mic;
        c.position = 0.0f;
        e.position = 1.0f;
        auto a = buildCabinetIR (sr, CabModel::v30x412, c), b = buildCabinetIR (sr, CabModel::v30x412, e);
        conditionIR (a, sr);
        conditionIR (b, sr);
        const double d = bandEnergyDb (irResponse (a, sr), 3000, 8000) - bandEnergyDb (irResponse (b, sr), 3000, 8000);
        check (d > 4.0, "mic on the cap is brighter than on the cone edge (+" + fmt (d) + " dB at 3-8 kHz)");
    }
    // Distance: proximity effect and room
    {
        MicPlacement n = mic, f = mic;
        n.distanceCm = 1.0f;
        f.distanceCm = 60.0f;
        auto a = buildCabinetIR (sr, CabModel::v30x412, n), b = buildCabinetIR (sr, CabModel::v30x412, f);
        const bool longer = b.size() > a.size() + (size_t) (0.05 * sr);
        conditionIR (a, sr);
        conditionIR (b, sr);
        const double d = bandEnergyDb (irResponse (a, sr), 80, 200) - bandEnergyDb (irResponse (b, sr), 80, 200);
        check (d > 3.0, "close mic has more low end (proximity effect, +" + fmt (d) + " dB)");
        check (longer, "distant mic picks up the room (longer IR)");
    }
    // Mic types differ
    {
        MicPlacement a = mic, b = mic;
        a.mic = MicModel::dyn57;
        b.mic = MicModel::ribbon121;
        auto x = buildCabinetIR (sr, CabModel::v30x412, a), y = buildCabinetIR (sr, CabModel::v30x412, b);
        conditionIR (x, sr);
        conditionIR (y, sr);
        check (bandEnergyDb (irResponse (x, sr), 4000, 8000) > bandEnergyDb (irResponse (y, sr), 4000, 8000) + 3.0, "ribbon mic is darker than the 57");
    }

    // ---- Convolver: matches direct convolution, zero latency, odd block sizes -----
    {
        std::mt19937 rng (11);
        std::normal_distribution<float> nd (0.0f, 1.0f);
        std::vector<float> h (3000), x (9000);
        for (size_t i = 0; i < h.size(); ++i) h[i] = nd (rng) * std::exp (-(float) i / 600.0f);
        for (auto& v : x) v = nd (rng);
        const auto ref = directConvolve (x, h);
        Convolver conv;
        conv.prepare (128, 40);
        std::unique_ptr<PreparedIR> pir (prepareIR (h, 128));
        conv.setSource (0, pir.get(), 1.0f);
        conv.refreshHead();
        std::vector<float> y (x.size());
        size_t pos = 0;
        int blk = 1;
        while (pos < x.size())
        {
            const int n = (int) std::min<size_t> ((size_t) blk, x.size() - pos);
            conv.process (x.data() + pos, y.data() + pos, n, [] {});
            pos += (size_t) n;
            blk = blk * 3 % 197 + 1;
        }
        double err = 0.0;
        for (size_t i = 0; i < y.size(); ++i) err = std::max (err, (double) std::abs (y[i] - ref[i]));
        check (err < 2.0e-3, "partitioned convolution matches direct convolution (max error " + fmt (err, 5) + ")");

        conv.reset();
        std::vector<float> imp (512, 0.0f), out (512);
        imp[0] = 1.0f;
        conv.process (imp.data(), out.data(), 512, [] {});
        check (std::abs (out[0] - h[0]) < 1.0e-5f && std::abs (out[300] - h[300]) < 1.0e-4f, "convolution has zero latency");
    }

    // ---- IR swap crossfades without clicks -----
    {
        AmpEngine e;
        auto p = dryParams();
        p.cab.enabled = true;
        e.setParams (p);
        e.prepare (sr, 256);
        auto a = buildCabinetIR (sr, CabModel::v30x412, mic);
        conditionIR (a, sr);
        e.installImpulseNow (0, prepareIR (a, AmpEngine::kPartition));
        e.reset();
        const auto x = sine (220.0, 1.0, 0.2f);
        std::vector<float> y;
        std::vector<float> l (256), r (256);
        double maxJump = 0.0, maxStep = 0.0;
        float prev = 0.0f;
        for (size_t start = 0; start + 256 <= x.size(); start += 256)
        {
            if (start == 24000)
            {
                MicPlacement edge = mic;
                edge.position = 1.0f;
                edge.mic = MicModel::ribbon121;
                auto b = buildCabinetIR (sr, CabModel::greenback412, edge);
                conditionIR (b, sr);
                e.postImpulse (0, prepareIR (b, AmpEngine::kPartition));
            }
            std::copy (x.begin() + (long) start, x.begin() + (long) start + 256, l.begin());
            std::copy (l.begin(), l.end(), r.begin());
            float* ch[2] = { l.data(), r.data() };
            e.process (ch, 2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const double jump = std::abs (l[(size_t) i] - prev);
                if (start > 12000 && start < 22000) maxStep = std::max (maxStep, jump);
                if (start >= 24000 && start < 30000) maxJump = std::max (maxJump, jump);
                prev = l[(size_t) i];
            }
        }
        e.collectGarbage();
        check (maxJump < maxStep * 2.0, "IR changes are crossfaded (largest step " + fmt (maxJump, 4) + " vs steady " + fmt (maxStep, 4) + ")");
    }

    // ---- Resampling keeps the response -----
    {
        auto a = buildCabinetIR (44100.0, CabModel::greenback412, mic);
        const auto r44 = irResponse (a, 44100.0);
        const auto b = resampleIR (a, 44100.0, 96000.0);
        const auto r96 = irResponse (b, 96000.0);
        double worst = 0.0, worstHz = 0.0;
        for (int i = 0; i < kResponsePoints; ++i)
            if (responsePointHz (i) > 60.0f && responsePointHz (i) < 8000.0f && std::abs (r44[(size_t) i] - r96[(size_t) i]) > worst)
                worst = std::abs (r44[(size_t) i] - r96[(size_t) i]), worstHz = responsePointHz (i);
        if (verbose) std::printf ("        worst at %.0f Hz\n", worstHz);
        check (worst < 1.0, "IR resampling 44.1 -> 96 kHz keeps the response (worst " + fmt (worst, 2) + " dB)");
    }
}

static void testEngine()
{
    std::printf ("Engine\n");
    // Dry path is unity gain, delayed by exactly the reported latency
    {
        AmpEngine e;
        auto p = dryParams();
        e.setParams (p);
        e.prepare (sr, 256);
        std::vector<float> x (4096, 0.0f);
        x[1000] = 1.0f;
        const auto y = runEngine (e, x, 100);
        size_t peak = 0;
        for (size_t i = 0; i < y.size(); ++i)
            if (std::abs (y[i]) > std::abs (y[peak])) peak = i;
        std::printf ("        impulse peak at +%d samples\n", (int) peak - 1000);
        check ((int) peak - 1000 == e.getLatency(), "reported latency matches the signal delay (" + std::to_string (e.getLatency()) + " samples = "
                                                         + fmt (1000.0 * e.getLatency() / sr, 2) + " ms)");
        const auto s = runEngine (e, sine (1000.0, 0.5, 0.25f));
        check (std::abs (rmsDb (s, 4800) - rmsDb (sine (1000.0, 0.5, 0.25f))) < 0.2, "bypassed rig is unity gain");
    }

    // Full rig with everything on: bounded, limited, finite, at several sample rates
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        AmpEngine e;
        EngineParams p;
        p.gate.enabled = true;
        p.comp.enabled = true;
        p.drive.enabled = true;
        p.boost.enabled = true;
        p.amp.model = AmpModel::recto;
        p.amp.gain = 8.0f;
        p.geqOn = true;
        p.geq = { 3.0f, 1.0f, -4.0f, 1.0f, 3.0f };
        p.mod.enabled = true;
        p.delay.enabled = true;
        p.cab.micBOn = true;
        p.doubler.enabled = true;
        p.reverb.enabled = true;
        p.eq.gain = { 2.0f, -2.0f, 0.0f, 1.0f, 2.0f };
        p.outputGainDb = 12.0f;
        e.setParams (p);
        e.prepare (rate, 512);
        for (int s = 0; s < 2; ++s)
        {
            MicPlacement m = s == 0 ? p.cab.micA : p.cab.micB;
            auto ir = buildCabinetIR (rate, p.cab.cab, m);
            conditionIR (ir, rate);
            e.installImpulseNow (s, prepareIR (ir, AmpEngine::kPartition));
        }
        e.reset();
        std::vector<float> l ((size_t) rate), r ((size_t) rate);
        const auto di = guitarDI (1.0, 0.3f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = di[std::min (i, di.size() - 1)];
        float* ch[2] = { l.data(), r.data() };
        for (size_t start = 0; start < l.size(); start += 512)
        {
            float* c2[2] = { ch[0] + start, ch[1] + start };
            e.process (c2, 2, (int) std::min<size_t> (512, l.size() - start));
        }
        bool finite = true;
        float peak = 0.0f, diff = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite &= std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
            diff = std::max (diff, std::abs (l[i] - r[i]));
        }
        check (finite && peak <= dbToGain (-1.0f) + 1.0e-3f && peak > 0.1f && diff > 0.01f,
               "full rig at " + fmt (rate / 1000.0) + " kHz: finite, stereo, limited to -1 dB (peak " + fmt (gainToDb (peak)) + " dB)");
    }
}

static void testTuner()
{
    std::printf ("Tuner\n");
    for (double hz : { 30.87, 41.2, 82.41, 110.0, 146.83, 329.63, 659.26 })
    {
        Tuner t;
        t.prepare (sr);
        const auto x = guitarDI (0.01, 0.0f); // silence first
        t.push (x.data(), (int) x.size());
        std::vector<float> s ((size_t) (0.3 * sr));
        for (size_t i = 0; i < s.size(); ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * kPi * hz * h * (double) i / sr) / h;
            s[i] = 0.2f * (float) v;
        }
        t.push (s.data(), (int) s.size());
        const auto r = t.analyse();
        const double cents = 1200.0 * std::log2 (r.hz / hz);
        check (r.valid && std::abs (cents) < 2.0, "detects " + fmt (hz, 2) + " Hz (" + fmt (cents, 2) + " cents)");
    }
}

int main (int argc, char** argv)
{
    verbose = argc > 1 && std::strcmp (argv[1], "-v") == 0;
    std::printf ("LA-AmpSuite engine tests\n");
    testToneStack();
    testAmps();
    testPedals();
    testCab();
    testEngine();
    testTuner();
    std::printf (failures == 0 ? "All engine tests passed\n" : "%d engine test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
