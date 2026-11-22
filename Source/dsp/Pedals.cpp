#include "Pedals.h"

namespace laas
{
const char* pedalShortName (PedalId p) noexcept
{
    switch (p)
    {
        case PedalId::gate:  return "GATE";
        case PedalId::comp:  return "COMP";
        case PedalId::drive: return "DRIVE";
        case PedalId::boost: return "BOOST";
        case PedalId::numPedals: break;
    }
    return "";
}

void pedalOrderToString (const PedalOrder& o, char* out) noexcept
{
    for (int i = 0; i < kNumPedals; ++i)
        out[i] = (char) ('0' + o[(size_t) i]);
    out[kNumPedals] = 0;
}

bool isValidPedalOrder (const PedalOrder& o) noexcept
{
    int seen = 0;
    for (int v : o)
    {
        if (v < 0 || v >= kNumPedals || (seen & (1 << v)) != 0)
            return false;
        seen |= 1 << v;
    }
    return true;
}

PedalOrder pedalOrderFromString (const char* s) noexcept
{
    PedalOrder o = defaultPedalOrder();
    if (s == nullptr)
        return o;
    for (int i = 0; i < kNumPedals; ++i)
    {
        if (s[i] < '0' || s[i] > '9')
            return defaultPedalOrder();
        o[(size_t) i] = s[i] - '0';
    }
    return isValidPedalOrder (o) ? o : defaultPedalOrder();
}

//==============================================================================
void NoiseGate::prepare (double sampleRate)
{
    sr = sampleRate;
    keyHp.setHighPass (sr, 70.0);
    keyLp.setLowPass (sr, 5000.0);
    env.setTimes (sr, 0.2f, 12.0f);
    attCoeff = msCoeff (sr, 0.6f);
    reset();
}

void NoiseGate::reset() noexcept
{
    keyHp.reset();
    keyLp.reset();
    env.reset();
    gain = 1.0f;
    open = false;
    hold = 0;
    grDb = 0.0f;
}

void NoiseGate::computeGains (const float* key, float* gains, int n) noexcept
{
    if (! params.enabled)
    {
        for (int i = 0; i < n; ++i)
            gains[i] = 1.0f;
        gain = 1.0f;
        grDb = 0.0f;
        return;
    }
    const float openLevel = dbToGain (params.thresholdDb);
    const float closeLevel = dbToGain (params.thresholdDb - 6.0f); // hysteresis
    const float relCoeff = msCoeff (sr, std::max (5.0f, params.releaseMs));
    const int holdSamples = (int) (0.025 * sr);
    for (int i = 0; i < n; ++i)
    {
        const float e = env.process (std::abs (keyLp.process (keyHp.process (key[i]))));
        if (e > openLevel)
        {
            open = true;
            hold = holdSamples;
        }
        else if (e < closeLevel)
        {
            if (hold > 0)
                --hold;
            else
                open = false;
        }
        const float target = open ? 1.0f : 0.0f;
        gain = target + (open ? attCoeff : relCoeff) * (gain - target);
        gains[i] = gain;
    }
    grDb = -gainToDb (std::max (1.0e-4f, gain));
}

//==============================================================================
void CompPedal::prepare (double rate)
{
    sr = rate;
    rmsCoeff = msCoeff (sr, 5.0f);
    reset();
}

void CompPedal::reset() noexcept
{
    envDb = -100.0f;
    rmsState = 0.0f;
    grDb = 0.0f;
}

void CompPedal::process (float* x, int n) noexcept
{
    if (! params.enabled)
    {
        grDb = 0.0f;
        return;
    }
    const float sustain = clampf (params.sustain * 0.1f, 0.0f, 1.0f);
    const float threshold = -12.0f - 36.0f * sustain;
    const float ratio = 2.0f + 10.0f * sustain;
    const float knee = 8.0f;
    const float att = msCoeff (sr, std::max (0.1f, params.attackMs));
    const float rel = msCoeff (sr, 120.0f + 200.0f * sustain);
    const float makeup = dbToGain (-threshold * (1.0f - 1.0f / ratio) * 0.55f + params.levelDb);
    const float blend = clampf (params.blend, 0.0f, 1.0f);
    const float dryGain = dbToGain (params.levelDb);
    float maxGr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        rmsState = x[i] * x[i] + rmsCoeff * (rmsState - x[i] * x[i]);
        const float lvl = 10.0f * std::log10 (rmsState + 1.0e-12f) + 3.0f;
        envDb = lvl + (lvl > envDb ? att : rel) * (envDb - lvl);
        const float over = envDb - threshold;
        float gr = 0.0f;
        if (over > knee * 0.5f)
            gr = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f)
        {
            const float o = over + knee * 0.5f;
            gr = (1.0f - 1.0f / ratio) * o * o / (2.0f * knee);
        }
        maxGr = std::max (maxGr, gr);
        const float wet = x[i] * dbToGain (-gr) * makeup;
        x[i] = wet * blend + x[i] * dryGain * (1.0f - blend);
    }
    grDb = maxGr;
}

//==============================================================================
const char* driveTypeName (int t) noexcept
{
    static const char* names[] = { "Screamer", "Distortion", "Fuzz", "Bass Drive" };
    return names[t < 0 || t >= (int) DriveType::numTypes ? 0 : t];
}

void DrivePedal::prepare (double osRate)
{
    sr = osRate;
    driveSm.setTime (sr, 30.0f);
    levelSm.setTime (sr, 30.0f);
    needsDesign = true;
    design();
    reset();
}

void DrivePedal::reset() noexcept
{
    for (auto* f : { &clipHp, &preHp, &toneLp, &postLp, &fuzzHp2, &dcBlock })
        f->reset();
    for (auto* b : { &bassLow1, &bassLow2, &bassHigh1, &bassHigh2, &muffLp, &muffHp, &midPeak })
        b->reset();
}

void DrivePedal::setParams (const DrivePedalParams& p) noexcept
{
    if (p.type != designed.type || std::abs (p.tone - designed.tone) > 1.0e-3f)
        needsDesign = true;
    params = p;
}

void DrivePedal::design() noexcept
{
    designed = params;
    needsDesign = false;
    const float tone = clampf (params.tone * 0.1f, 0.0f, 1.0f);
    dcBlock.setHighPass (sr, 15.0);
    switch (params.type)
    {
        case DriveType::screamer:
            clipHp.setHighPass (sr, 720.0);                         // only mids and highs are clipped: the mid hump
            postLp.setLowPass (sr, 6000.0);
            toneLp.setLowPass (sr, 900.0 * std::pow (8.0, tone));   // 0.9 .. 7.2 kHz
            break;
        case DriveType::distortion:
            preHp.setHighPass (sr, 60.0);
            clipHp.setHighPass (sr, 1200.0);
            toneLp.setLowPass (sr, 600.0 * std::pow (20.0, tone));  // the "filter" control, 0.6 .. 12 kHz
            postLp.setLowPass (sr, 14000.0);
            break;
        case DriveType::fuzz:
            preHp.setHighPass (sr, 90.0);
            fuzzHp2.setHighPass (sr, 60.0);
            muffLp.setLowPass (sr, 450.0, 0.6);
            muffHp.setHighPass (sr, 1100.0, 0.6);
            postLp.setLowPass (sr, 9000.0);
            break;
        case DriveType::bassDrive:
            bassLow1.setLowPass (sr, 250.0, 0.70710678);
            bassLow2.setLowPass (sr, 250.0, 0.70710678);
            bassHigh1.setHighPass (sr, 250.0, 0.70710678);
            bassHigh2.setHighPass (sr, 250.0, 0.70710678);
            midPeak.setPeak (sr, 600.0, 0.8, 4.0);
            toneLp.setLowPass (sr, 1500.0 * std::pow (6.0, tone)); // 1.5 .. 9 kHz
            postLp.setLowPass (sr, 12000.0);
            break;
        case DriveType::numTypes: break;
    }
}

void DrivePedal::process (float* x, int n) noexcept
{
    if (! params.enabled)
        return;
    if (needsDesign)
        design();
    const float d = clampf (params.drive * 0.1f, 0.0f, 1.0f);
    const float blend = clampf (params.blend, 0.0f, 1.0f);
    float driveTarget = 1.0f;
    switch (params.type)
    {
        case DriveType::screamer:   driveTarget = dbToGain (41.0f * d); break;
        case DriveType::distortion: driveTarget = dbToGain (6.0f + 54.0f * d); break;
        case DriveType::fuzz:       driveTarget = dbToGain (10.0f + 30.0f * d); break;
        case DriveType::bassDrive:  driveTarget = dbToGain (6.0f + 40.0f * d); break;
        case DriveType::numTypes: break;
    }
    const float levelTarget = dbToGain (params.levelDb);
    const float tone = clampf (params.tone * 0.1f, 0.0f, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        const float in = x[i];
        const float g = driveSm.next (driveTarget);
        float y = 0.0f;
        switch (params.type)
        {
            case DriveType::screamer:
            {
                // Op-amp: x + diode-clipped (high-passed x * gain); soft symmetric silicon diodes
                const float v = clipHp.process (in) * g;
                y = toneLp.process (postLp.process (in + 0.45f * fastTanh (v / 0.45f)));
                break;
            }
            case DriveType::distortion:
            {
                // Lows get less gain than highs; hard diodes to ground after the op-amp
                const float p = preHp.process (in);
                const float v = (p * 0.25f + clipHp.process (p)) * g;
                const float c = v / std::sqrt (1.0f + v * v * 4.0f);
                y = toneLp.process (postLp.process (c)) * 1.6f;
                break;
            }
            case DriveType::fuzz:
            {
                const float s1 = fastTanh (preHp.process (in) * g);
                const float s2 = fastTanh (fuzzHp2.process (s1) * 6.0f);
                const float lp = muffLp.process (s2), hp = muffHp.process (s2);
                y = postLp.process (lp * (1.0f - tone) + hp * tone * 1.4f) * 0.7f;
                break;
            }
            case DriveType::bassDrive:
            {
                const float low = bassLow2.process (bassLow1.process (in));
                const float high = bassHigh2.process (bassHigh1.process (in));
                const float c = fastTanh (high * g) * 0.4f;
                y = low + toneLp.process (postLp.process (midPeak.process (c)));
                break;
            }
            case DriveType::numTypes: break;
        }
        y = dcBlock.process (y);
        x[i] = (y * blend + in * (1.0f - blend)) * levelSm.next (levelTarget);
    }
}

//==============================================================================
void BoostPedal::prepare (double osRate)
{
    sr = osRate;
    gainSm.setTime (sr, 30.0f);
    lastTight = lastTone = -1.0f;
    reset();
}

void BoostPedal::reset() noexcept
{
    hp.reset();
    tilt.reset();
}

void BoostPedal::setParams (const BoostParams& p) noexcept
{
    params = p;
    if (std::abs (p.tightHz - lastTight) > 0.5f)
    {
        hp.setHighPass (sr, std::max (10.0f, p.tightHz), 0.7);
        lastTight = p.tightHz;
    }
    if (std::abs (p.tone - lastTone) > 1.0e-3f)
    {
        tilt.setHighShelf (sr, 1500.0, (clampf (p.tone * 0.1f, 0.0f, 1.0f) - 0.5f) * 16.0f);
        lastTone = p.tone;
    }
}

void BoostPedal::process (float* x, int n) noexcept
{
    if (! params.enabled)
        return;
    const float target = dbToGain (params.gainDb);
    for (int i = 0; i < n; ++i)
        x[i] = tilt.process (hp.process (x[i])) * gainSm.next (target);
}
} // namespace laas
