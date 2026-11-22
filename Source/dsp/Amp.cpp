#include "Amp.h"

namespace laas
{
namespace
{
// Shorthand for stage tables: { gain, couplingHz, cathodeHz, cathodeDb, millerHz, bias }
constexpr StageSpec st (float g, float hp, float cHz, float cDb, float lp, float bias) { return { g, hp, cHz, cDb, lp, bias }; }

TmbComponents tmb (double r1, double r2, double r3, double r4, double c1, double c2, double c3)
{
    TmbComponents c;
    c.rTreble = r1; c.rBass = r2; c.rMid = r3; c.rSlope = r4;
    c.cTreble = c1; c.cBass = c2; c.cMid = c3;
    return c;
}

const std::array<AmpModelSpec, (size_t) AmpModel::numModels>& specs()
{
    static const std::array<AmpModelSpec, (size_t) AmpModel::numModels> s { {
        // ---- Plexi: two triodes, cathode follower, Marshall stack. Open, cranked-power-amp crunch.
        { "Brit Plexi", "PLEXI", "Marshall 1959 Super Lead (jumpered)",
          "Classic rock crunch. Little preamp gain, the power amp does most of the work: push the master.",
          false, 2,
          { st (3.0f, 20.0f, 150.0f, -4.0f, 16000.0f, 0.0f), st (6.5f, 25.0f, 110.0f, -3.0f, 9000.0f, -0.1f), {}, {}, {} },
          1, -30.0f, 6.0f, 1400.0f, 9.0f, 1, ToneStackType::tmb,
          tmb (250e3, 1e6, 25e3, 33e3, 500e-12, 22e-9, 22e-9), 0.0f, 0.6f,
          2.2f, 0.08f, 4000.0f, 7.0f, 100.0f, 4.0f, 0.35f, 45.0f, 9000.0f, 2.6f },

        // ---- JCM800 2203: V1a, gain pot with bright cap, V1b, cold-biased V2a clipper, CF, stack.
        { "Brit 800", "800", "Marshall JCM800 2203",
          "The punk and hard rock sound: tight, mid-forward, raspy top end. Great with a boost in front.",
          false, 3,
          { st (3.0f, 20.0f, 90.0f, -6.0f, 16000.0f, 0.0f), st (8.0f, 30.0f, 0.0f, 0.0f, 12000.0f, 0.0f),
            st (5.0f, 35.0f, 0.0f, 0.0f, 7000.0f, -0.55f), {}, {} },
          1, -36.0f, 4.0f, 1200.0f, 6.0f, 2, ToneStackType::tmb,
          tmb (220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9), 0.0f, 0.5f,
          1.4f, 0.06f, 4500.0f, 8.0f, 100.0f, 5.0f, 0.25f, 45.0f, 9500.0f, -1.1f },

        // ---- 5150 / 6505 lead: cascaded stages with small coupling caps (tight), post stack.
        { "American 51", "51", "Peavey 5150 / 6505 (lead channel)",
          "Thrash and metal rhythm: aggressive, tight low end, cutting upper mids. Responds hard to presence/resonance.",
          false, 4,
          { st (2.5f, 30.0f, 200.0f, -4.0f, 13000.0f, 0.0f), st (9.0f, 90.0f, 0.0f, 0.0f, 9000.0f, 0.0f),
            st (8.0f, 150.0f, 0.0f, 0.0f, 6500.0f, -0.2f), st (6.0f, 120.0f, 0.0f, 0.0f, 6000.0f, -0.5f), {} },
          1, -46.0f, 0.0f, 2500.0f, 4.0f, 3, ToneStackType::tmb,
          tmb (250e3, 1e6, 25e3, 47e3, 250e-12, 47e-9, 22e-9), 0.0f, 0.4f,
          1.0f, 0.05f, 5000.0f, 9.0f, 90.0f, 8.0f, 0.15f, 40.0f, 10000.0f, -0.8f },

        // ---- Dual Rectifier modern: high gain, looser lows, fizzy top, tube rectifier sag.
        { "Recto Modern", "RECTO", "Mesa/Boogie Dual Rectifier (modern red channel)",
          "Modern metal: huge saturated low end, scooped mids, sizzling top. Try the mids around 3-4.",
          false, 4,
          { st (2.5f, 25.0f, 120.0f, -3.0f, 14000.0f, 0.0f), st (9.0f, 50.0f, 0.0f, 0.0f, 10000.0f, 0.0f),
            st (9.0f, 70.0f, 0.0f, 0.0f, 8000.0f, -0.3f), st (6.0f, 60.0f, 0.0f, 0.0f, 7000.0f, -0.55f), {} },
          1, -46.0f, 0.0f, 2000.0f, 5.0f, 3, ToneStackType::tmb,
          tmb (250e3, 1e6, 25e3, 47e3, 500e-12, 22e-9, 22e-9), 0.0f, 0.45f,
          1.2f, 0.07f, 4000.0f, 9.0f, 80.0f, 9.0f, 0.45f, 38.0f, 10500.0f, -4.7f },

        // ---- Mark IIC+ lead: tone stack before the lead cascade, so treble/bass shape the distortion.
        { "Mark C+ Lead", "MARK", "Mesa/Boogie Mark IIC+ (lead channel)",
          "Thrash rhythm and singing leads. The stack sits before the gain stages: treble drives the gain. Use the GEQ in a V.",
          false, 4,
          { st (2.5f, 20.0f, 250.0f, -3.0f, 15000.0f, 0.0f), st (8.0f, 130.0f, 0.0f, 0.0f, 9000.0f, 0.0f),
            st (8.0f, 150.0f, 0.0f, 0.0f, 7000.0f, -0.3f), st (5.0f, 100.0f, 0.0f, 0.0f, 6500.0f, -0.5f), {} },
          1, -42.0f, 4.0f, 2200.0f, 4.0f, 0, ToneStackType::tmb,
          tmb (250e3, 250e3, 10e3, 100e3, 750e-12, 100e-9, 47e-9), 0.0f, 0.35f,
          1.1f, 0.05f, 4000.0f, 7.0f, 100.0f, 5.0f, 0.2f, 45.0f, 10000.0f, 0.9f },

        // ---- SVT Classic: active stack with 5-position mid and ultra lo/hi, huge 6550 headroom.
        { "SVT Classic", "SVT", "Ampeg SVT Classic (bass)",
          "The rock bass amp. Clean and round at low gain, growling tube grind as the gain goes up. Pair with the 8x10.",
          true, 2,
          { st (2.0f, 10.0f, 0.0f, 0.0f, 20000.0f, 0.0f), st (2.5f, 12.0f, 0.0f, 0.0f, 15000.0f, -0.1f), {}, {}, {} },
          1, -24.0f, 14.0f, 3000.0f, 3.0f, 0, ToneStackType::svt, {}, 0.0f, 0.2f,
          0.7f, 0.04f, 3000.0f, 3.0f, 60.0f, 4.0f, 0.1f, 25.0f, 10000.0f, 5.3f },

        // ---- B-15: low gain, warm and round, tube rectifier.
        { "Flip-Top B15", "B15", "Ampeg B-15 Portaflex (bass)",
          "Vintage, warm, round bass. Little top end, soft compression. Classic punk and garage bass when pushed.",
          true, 2,
          { st (2.0f, 15.0f, 0.0f, 0.0f, 9000.0f, 0.0f), st (2.5f, 15.0f, 0.0f, 0.0f, 6000.0f, -0.15f), {}, {}, {} },
          1, -20.0f, 12.0f, 2000.0f, 2.0f, 1, ToneStackType::vintage, {}, 0.0f, 0.3f,
          1.0f, 0.06f, 2500.0f, 3.0f, 80.0f, 3.0f, 0.5f, 35.0f, 7000.0f, 5.1f },

        // ---- Bass 400+: tube bass head, aggressive mids, more gain on tap.
        { "Bass 400", "B400", "Mesa/Boogie Bass 400+ (bass)",
          "Aggressive tube bass with lots of mid punch and grind. Good for punk and metal bass.",
          true, 3,
          { st (2.5f, 12.0f, 0.0f, 0.0f, 15000.0f, 0.0f), st (5.0f, 30.0f, 0.0f, 0.0f, 9000.0f, -0.2f),
            st (3.0f, 25.0f, 0.0f, 0.0f, 8000.0f, -0.4f), {}, {} },
          1, -30.0f, 8.0f, 2500.0f, 3.0f, 2, ToneStackType::tmb,
          tmb (250e3, 1e6, 25e3, 56e3, 500e-12, 100e-9, 47e-9), 0.0f, 0.3f,
          0.9f, 0.05f, 3500.0f, 4.0f, 70.0f, 5.0f, 0.2f, 25.0f, 10000.0f, 1.4f },
    } };
    return s;
}

/** Triode transfer curve: harder grid-conduction limit for positive swings, softer cutoff for negative. */
inline float triode (float u) noexcept
{
    return u >= 0.0f ? u / std::sqrt (1.0f + u * u) : u / (1.0f - u * (1.0f / 1.6f));
}

/** Push-pull power stage curve with a slight mismatch between the halves. */
inline float powerTubes (float v, float asym) noexcept
{
    const float k = v >= 0.0f ? 1.0f : 1.0f / (1.0f - asym);
    const float w = v * k;
    return w / (k * std::sqrt (1.0f + w * w));
}
} // namespace

const AmpModelSpec& ampModelSpec (AmpModel m) noexcept
{
    const int i = (int) m;
    return specs()[(size_t) (i < 0 || i >= (int) AmpModel::numModels ? 1 : i)];
}

const char* ampModelName (int m) noexcept { return ampModelSpec ((AmpModel) m).name; }

//==============================================================================
void TriodeStage::prepare (double osRate, const StageSpec& s)
{
    spec = s;
    coupling.setHighPass (osRate, std::max (5.0f, s.couplingHz));
    if (s.cathodeHz > 0.0f)
        cathode.setLowShelf (osRate, s.cathodeHz, s.cathodeDb);
    else
        cathode.setBypass();
    miller.setLowPass (osRate, s.millerHz);
    gridAtt = msCoeff (osRate, 0.4f);
    gridRel = msCoeff (osRate, 40.0f);
    restOut = triode (s.bias);
    reset();
}

void TriodeStage::reset() noexcept
{
    coupling.reset();
    cathode.reset();
    miller.reset();
    gridEnv = 0.0f;
}

inline float TriodeStage::process (float x) noexcept
{
    const float v = cathode.process (coupling.process (x));
    // Grid current charges the coupling cap and shifts the bias (blocking): dynamic, "spongy" clipping
    const float u = spec.gain * v + spec.bias - 0.35f * gridEnv;
    const float g = u > 0.0f ? u : 0.0f;
    gridEnv = g + (g > gridEnv ? gridAtt : gridRel) * (gridEnv - g);
    return miller.process (-(triode (u) - restOut));
}

//==============================================================================
void AmpModule::prepare (double osRate, int)
{
    osSr = osRate;
    gainSm.setTime (osSr, 30.0f);
    masterSm.setTime (osSr, 30.0f);
    levelSm.setTime (osSr, 30.0f);
    blendSm.setTime (osSr, 30.0f);
    sagEnv.setTimes (osSr, 8.0f, 220.0f);
    stack.prepare (osSr);
    configured = AmpModel::numModels;
    configureModel();
    reset();
}

void AmpModule::reset() noexcept
{
    for (auto& s : stages) s.reset();
    stack.reset();
    for (auto* b : { &presence, &depth, &xfmrHp, &xfmrLp, &blendLp1, &blendLp2, &inputHp })
        b->reset();
    bright.reset();
    sagEnv.reset();
    gainSm.snap (lastGain > -99.0f ? lastGain : 0.0f);
}

void AmpModule::setParams (const AmpParams& p) noexcept
{
    params = p;
    if (p.model != configured)
        configureModel();
}

void AmpModule::configureModel() noexcept
{
    configured = params.model;
    spec = &ampModelSpec (configured);
    for (int i = 0; i < spec->numStages; ++i)
        stages[(size_t) i].prepare (osSr, spec->stages[(size_t) i]);
    stack.configure (spec->stackType, spec->tmb);
    inputHp.setHighPass (osSr, spec->bass ? 15.0 : 25.0, 0.6);
    xfmrHp.setHighPass (osSr, spec->xfmrLowHz, 0.6);
    xfmrLp.setLowPass (osSr, spec->xfmrHighHz, 0.6);
    blendLp1.setLowPass (osSr, 180.0, 0.70710678);
    blendLp2.setLowPass (osSr, 180.0, 0.70710678);
    lastBright = lastPresence = lastDepth = -1.0f;

    // Make up the passive stack's insertion loss (measured with every knob at noon)
    double maxDb = -100.0;
    for (double f = 80.0; f < 6000.0; f *= 1.12)
        maxDb = std::max (maxDb, ToneStack::magnitudeDb (spec->stackType, spec->tmb, {}, f, osSr));
    stackMakeup = dbToGain ((float) (-maxDb) + spec->stackMakeupDb);
    updateFilters();
}

void AmpModule::updateFilters() noexcept
{
    const float gainNorm = clampf (params.gain * 0.1f, 0.0f, 1.0f);
    const float brightAmt = params.bright ? spec->brightDb * (1.0f - 0.75f * gainNorm) : 0.0f;
    if (std::abs (brightAmt - lastBright) > 0.01f || params.bright != lastBrightOn)
    {
        bright.setHighShelf (osSr, spec->brightHz, brightAmt);
        lastBright = brightAmt;
        lastBrightOn = params.bright;
    }
    const float pres = clampf (params.presence * 0.1f, 0.0f, 1.0f) * spec->presenceMaxDb;
    if (std::abs (pres - lastPresence) > 0.01f)
    {
        presence.setHighShelf (osSr, spec->presenceHz, pres, 0.7);
        lastPresence = pres;
    }
    const float dep = clampf (params.depth * 0.1f, 0.0f, 1.0f) * spec->depthMaxDb;
    if (std::abs (dep - lastDepth) > 0.01f)
    {
        depth.setPeak (osSr, spec->depthHz, 0.8, dep);
        lastDepth = dep;
    }

    ToneStackSettings ts;
    ts.bass = clampf (params.bass * 0.1f, 0.0f, 1.0f);
    ts.mid = clampf (params.mid * 0.1f, 0.0f, 1.0f);
    ts.treble = clampf (params.treble * 0.1f, 0.0f, 1.0f);
    ts.midFreq = params.midFreq;
    ts.ultraLo = params.ultraLo;
    ts.ultraHi = params.ultraHi;
    stack.set (ts);
}

void AmpModule::process (float* x, int n) noexcept
{
    if (spec == nullptr)
        return;
    updateFilters();

    const float gainNorm = clampf (params.gain * 0.1f, 0.0f, 1.0f);
    const float gainDb = spec->gainMinDb + (spec->gainMaxDb - spec->gainMinDb) * gainNorm;
    lastGain = dbToGain (gainDb);
    const float masterNorm = clampf (params.master * 0.1f, 0.0f, 1.0f);
    const float masterTarget = dbToGain (-30.0f + 36.0f * masterNorm) * spec->powerDrive;
    // Turning the master up gets louder, but less than a real amp would
    const float levelTarget = dbToGain (params.levelDb + spec->outputTrimDb) / std::pow (std::max (0.02f, masterTarget), 0.6f);
    const float blendTarget = clampf (params.blend, 0.0f, 1.0f) * 2.5f;
    const float sagAmount = clampf (spec->sag * params.sag * 2.0f, 0.0f, 0.9f);
    const int numStages = spec->numStages;
    const int potBefore = spec->gainPotBefore;
    const int stackAfter = spec->stackAfter;
    const float cf = spec->followerDrive;
    const float asym = spec->powerAsym;

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float in = inputHp.process (x[i]);
        const float clean = blendLp2.process (blendLp1.process (in));
        float v = in;
        const float g = gainSm.next (lastGain);
        for (int s = 0; s < numStages; ++s)
        {
            if (s == potBefore)
                v = bright.process (v * g);
            v = stages[(size_t) s].process (v);
            if (s == stackAfter)
            {
                if (s == numStages - 1 && cf > 0.0f)
                    v = fastTanh (v * (1.0f + cf)) / (1.0f + cf * 0.5f); // cathode follower
                v = stack.process (v) * stackMakeup;
            }
        }
        if (stackAfter != numStages - 1 && cf > 0.0f)
            v = fastTanh (v * (1.0f + cf)) / (1.0f + cf * 0.5f);
        const float pre = v;
        peak = std::max (peak, std::abs (pre));

        // Power amp: depth/presence (negative feedback), push-pull curve, supply sag
        v = presence.process (depth.process (v)) * masterSm.next (masterTarget);
        const float supply = 1.0f - sagAmount * clampf (sagEnv.env, 0.0f, 1.0f) * 0.45f;
        v = supply * powerTubes (v / supply, asym);
        sagEnv.process (std::abs (v));
        v = xfmrLp.process (xfmrHp.process (v));
        x[i] = v * levelSm.next (levelTarget) + clean * blendSm.next (blendTarget);
    }
    driveMeter = peak;
}
} // namespace laas
