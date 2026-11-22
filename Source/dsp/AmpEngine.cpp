#include "AmpEngine.h"

namespace laas
{
AmpEngine::AmpEngine()
{
    for (auto& r : retired)
        r.store (nullptr);
}

AmpEngine::~AmpEngine()
{
    collectGarbage();
    for (auto& s : slots)
    {
        delete s.current;
        delete s.fading;
        delete s.pending.exchange (nullptr);
    }
}

void AmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (32, maxBlockSize);
    const double osRate = sr * 4.0;

    gate.prepare (sr);
    comp.prepare (osRate);
    drive.prepare (osRate);
    boost.prepare (osRate);
    amp.prepare (osRate, maxBlock * 4);
    gateDelay.prepare (Oversampler4x::latency() + 4);
    gateDelay.setDelay (Oversampler4x::latency());
    geq.prepare (sr);

    convolver.prepare (kPartition, (int) std::ceil (0.56 * sr / kPartition) + 2);
    lastLowCut = lastHighCut = -1.0f;

    mod.prepare (sr);
    delay.prepare (sr);
    eq.prepare (sr);
    doubler.prepare (sr);
    reverb.prepare (sr);
    limiter.prepare (sr);
    for (auto* s : { &outGainSm, &widthSm, &inGainSm, &muteSm })
        s->setTime (sr, 20.0f);

    for (auto* v : { &mono, &gateGains, &cabOut, &scratchL, &scratchR, &analysis, &inputCopy })
        v->assign ((size_t) maxBlock, 0.0f);
    os.assign ((size_t) maxBlock * 4, 0.0f);
    gate4.assign ((size_t) maxBlock * 4, 1.0f);

    setParams (params);
    reset();
}

void AmpEngine::reset()
{
    gate.reset();
    comp.reset();
    drive.reset();
    boost.reset();
    amp.reset();
    oversampler.reset();
    gateDelay.reset();
    geq.reset();
    convolver.reset();
    cabLow.reset();
    cabHigh.reset();
    mod.reset();
    delay.reset();
    eq.reset();
    doubler.reset();
    reverb.reset();
    limiter.reset();
    inGainSm.snap (dbToGain (params.inputGainDb));
    outGainSm.snap (dbToGain (params.outputGainDb));
    widthSm.snap (params.width);
    muteSm.snap (params.mute ? 0.0f : 1.0f);
    for (auto& s : slots)
        s.weight = 0.0f;
    cabBoundary();
    convolver.refreshHead();
}

void AmpEngine::setParams (const EngineParams& p) noexcept
{
    params = p;
    if (! isValidPedalOrder (params.pedalOrder))
        params.pedalOrder = defaultPedalOrder();
    gate.setParams (p.gate);
    comp.setParams (p.comp);
    drive.setParams (p.drive);
    boost.setParams (p.boost);
    amp.setParams (p.amp);
    geq.setGains (p.geq);
    mod.setParams (p.mod);
    delay.setParams (p.delay, p.bpm);
    eq.setParams (p.eq);
    doubler.setParams (p.doubler);
    reverb.setParams (p.reverb);
    updateCabFilters();
}

void AmpEngine::updateCabFilters() noexcept
{
    if (std::abs (params.cab.lowCutHz - lastLowCut) > 0.5f)
    {
        if (params.cab.lowCutHz <= 20.5f) cabLow.setBypass();
        else cabLow.setHighPass (sr, params.cab.lowCutHz, 0.7071);
        lastLowCut = params.cab.lowCutHz;
    }
    if (std::abs (params.cab.highCutHz - lastHighCut) > 0.5f)
    {
        if (params.cab.highCutHz >= 19999.0f) cabHigh.setBypass();
        else cabHigh.setLowPass (sr, params.cab.highCutHz, 0.7071);
        lastHighCut = params.cab.highCutHz;
    }
}

//==============================================================================
void AmpEngine::postImpulse (int slot, PreparedIR* ir) noexcept
{
    if (slot < 0 || slot > 1)
    {
        delete ir;
        return;
    }
    // If the audio thread has not picked up the previous one yet, it is still ours to delete.
    delete slots[(size_t) slot].pending.exchange (ir);
}

void AmpEngine::installImpulseNow (int slot, PreparedIR* ir) noexcept
{
    auto& s = slots[(size_t) slot];
    delete s.pending.exchange (nullptr);
    delete s.fading;
    delete s.current;
    s.fading = nullptr;
    s.current = ir;
    s.fade = 1.0f;
}

void AmpEngine::retire (PreparedIR* ir) noexcept
{
    if (ir == nullptr)
        return;
    for (auto& r : retired)
    {
        PreparedIR* expected = nullptr;
        if (r.compare_exchange_strong (expected, ir))
            return;
    }
    // Queue full (garbage collection not running): leak rather than free on the audio thread.
}

void AmpEngine::collectGarbage() noexcept
{
    for (auto& r : retired)
        delete r.exchange (nullptr);
}

void AmpEngine::cabBoundary() noexcept
{
    // Pick up new IRs and crossfade to them (about 40 ms)
    const float fadeStep = (float) kPartition / (float) (0.04 * sr);
    for (auto& s : slots)
    {
        if (auto* incoming = s.pending.exchange (nullptr))
        {
            if (s.current == nullptr)
            {
                s.current = incoming;
                s.fade = 1.0f;
            }
            else
            {
                retire (s.fading);
                s.fading = s.current;
                s.current = incoming;
                s.fade = 0.0f;
            }
        }
        if (s.fade < 1.0f)
        {
            s.fade = std::min (1.0f, s.fade + fadeStep);
            if (s.fade >= 1.0f)
            {
                retire (s.fading);
                s.fading = nullptr;
            }
        }
    }

    // Mic blend
    const auto& c = params.cab;
    const bool bOn = c.micBOn && slots[1].current != nullptr;
    float wA = bOn ? 1.0f - clampf (c.mix, 0.0f, 1.0f) : 1.0f;
    float wB = bOn ? clampf (c.mix, 0.0f, 1.0f) * (c.micBInvert ? -1.0f : 1.0f) : 0.0f;
    if (slots[0].current == nullptr && bOn)
        wB = c.micBInvert ? -1.0f : 1.0f, wA = 0.0f;
    const float smooth = std::exp (-(float) kPartition / (float) (0.02 * sr));
    slots[0].weight = wA + smooth * (slots[0].weight - wA);
    slots[1].weight = wB + smooth * (slots[1].weight - wB);

    for (int i = 0; i < 2; ++i)
    {
        const auto& s = slots[(size_t) i];
        convolver.setSource (i * 2, s.current, s.weight * s.fade);
        convolver.setSource (i * 2 + 1, s.fading, s.weight * (1.0f - s.fade));
    }
}

//==============================================================================
void AmpEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0)
        return;
    float* l = channels[0];
    float* r = numChannels > 1 ? channels[1] : nullptr;
    meters.inPeak = meters.outPeak = 0.0f;
    float gateGr = 0.0f, compGr = 0.0f, limGr = 0.0f, drv = 0.0f;
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);
        // Input: mono sum
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = r != nullptr ? 0.5f * (l[start + i] + r[start + i]) : l[start + i];
        processChunk (l + start, r != nullptr ? r + start : nullptr, n);
        gateGr = std::max (gateGr, gate.getReductionDb());
        compGr = std::max (compGr, comp.getReductionDb());
        limGr = std::max (limGr, limiter.getReductionDb());
        drv = std::max (drv, amp.getDriveLevel());
    }
    meters.gateDb = gateGr;
    meters.compDb = compGr;
    meters.limitDb = limGr;
    meters.ampDrive = drv;
}

void AmpEngine::processChunk (float* outL, float* outR, int n) noexcept
{
    float* x = mono.data();
    const float inTarget = dbToGain (params.inputGainDb);
    for (int i = 0; i < n; ++i)
    {
        x[i] *= inGainSm.next (inTarget);
        meters.inPeak = std::max (meters.inPeak, std::abs (x[i]));
        inputCopy[(size_t) i] = x[i];
    }

    // ---- Pedals + amp, 4x oversampled ---------------------------------------------------------
    gate.computeGains (x, gateGains.data(), n);
    float* o = os.data();
    const int n4 = n * 4;
    oversampler.upsample (x, o, n);
    if (params.gate.enabled)
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 4; ++k)
                gate4[(size_t) (i * 4 + k)] = gateGains[(size_t) i];

    for (int p : params.pedalOrder)
    {
        switch ((PedalId) p)
        {
            case PedalId::gate:
                if (params.gate.enabled)
                    for (int i = 0; i < n4; ++i)
                        o[i] *= gate4[(size_t) i];
                break;
            case PedalId::comp:  comp.process (o, n4); break;
            case PedalId::drive: drive.process (o, n4); break;
            case PedalId::boost: boost.process (o, n4); break;
            case PedalId::numPedals: break;
        }
    }
    if (params.ampOn)
        amp.process (o, n4);
    oversampler.downsample (o, x, n);

    // Tracking gate: also closes on the amp's own hiss
    if (params.gate.enabled && params.gate.track)
        for (int i = 0; i < n; ++i)
            x[i] *= gateDelay.process (gateGains[(size_t) i]);
    else
        for (int i = 0; i < n; ++i)
            gateDelay.process (1.0f);

    // ---- Effects loop: graphic EQ -----------------------------------------------------------------
    if (params.geqOn)
        geq.process (x, n);

    // ---- Cabinet ------------------------------------------------------------------------------------
    float* c = cabOut.data();
    if (params.cab.enabled)
    {
        convolver.process (x, c, n, [this] { cabBoundary(); });
        for (int i = 0; i < n; ++i)
            c[i] = cabHigh.process (cabLow.process (c[i]));
    }
    else
    {
        // keep the convolver's history running so switching back on is seamless
        convolver.process (x, c, n, [this] { cabBoundary(); });
        std::copy (x, x + n, c);
    }

    // ---- Stereo: loop modulation + delay (linear, so after the cab), post EQ, doubler, reverb ---------
    float* L = scratchL.data();
    float* R = scratchR.data();
    std::copy (c, c + n, L);
    std::copy (c, c + n, R);
    if (params.mod.enabled) mod.process (L, R, n);
    if (params.delay.enabled) delay.process (L, R, n);
    if (params.eq.enabled) eq.process (L, R, n);
    if (params.doubler.enabled) doubler.process (L, R, n);
    if (params.reverb.enabled) reverb.process (L, R, n);

    // ---- Output ---------------------------------------------------------------------------------------
    const float outTarget = dbToGain (params.outputGainDb);
    const float widthTarget = clampf (params.width, 0.0f, 2.0f);
    const float muteTarget = params.mute ? 0.0f : 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.next (outTarget) * muteSm.next (muteTarget);
        const float w = widthSm.next (widthTarget);
        const float mid = 0.5f * (L[i] + R[i]), side = 0.5f * (L[i] - R[i]) * w;
        L[i] = (mid + side) * g;
        R[i] = (mid - side) * g;
    }
    limiter.process (L, R, n, params.ceilingDb, params.limiterOn);

    for (int i = 0; i < n; ++i)
    {
        meters.outPeak = std::max (meters.outPeak, std::max (std::abs (L[i]), std::abs (R[i])));
        analysis[(size_t) i] = 0.5f * (L[i] + R[i]);
        if (outR != nullptr)
        {
            outL[i] = L[i];
            outR[i] = R[i];
        }
        else
            outL[i] = 0.5f * (L[i] + R[i]);
    }
}
} // namespace laas
