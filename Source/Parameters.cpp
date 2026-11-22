#include "Parameters.h"

namespace laas
{
namespace
{
using APF = juce::AudioParameterFloat;
using APFA = juce::AudioParameterFloatAttributes;

juce::String formatDb (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String formatMs (float v, int) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }
juce::String formatPct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
juce::String formatKnob (float v, int) { return juce::String (v, 1); }
juce::String formatHz (float v, int = 0)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

std::unique_ptr<APF> floatParam (const char* id, const juce::String& name, juce::NormalisableRange<float> range, float def,
                                 std::function<juce::String (float, int)> fmt, const juce::String& label = {})
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  APFA().withLabel (label).withStringFromValueFunction (std::move (fmt)));
}

std::unique_ptr<APF> knobParam (const char* id, const juce::String& name, float def = 5.0f)
{
    return floatParam (id, name, { 0.0f, 10.0f, 0.01f }, def, formatKnob);
}

std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const juce::String& name, const juce::StringArray& items, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
}

juce::StringArray names (int count, const char* (*fn) (int) noexcept)
{
    juce::StringArray s;
    for (int i = 0; i < count; ++i) s.add (fn (i));
    return s;
}

std::unique_ptr<APF> distParam (const char* id, const juce::String& name, float def)
{
    return floatParam (id, name, skewed (1.0f, 100.0f, 10.0f, 0.1f), def,
                       [] (float v, int) { return v < 10.0f ? juce::String (v, 1) + " cm" : juce::String (juce::roundToInt (v)) + " cm"; }, "cm");
}

std::unique_ptr<APF> posParam (const char* id, const juce::String& name, float def)
{
    return floatParam (id, name, { 0.0f, 100.0f, 0.1f }, def, [] (float v, int)
    {
        if (v < 8.0f) return juce::String ("Cap centre");
        if (v < 30.0f) return juce::String ("Cap edge");
        if (v < 75.0f) return juce::String ("Cone ") + juce::String (juce::roundToInt (v)) + "%";
        return juce::String ("Cone edge");
    }, "%");
}
} // namespace

const char* pedalEnableID (PedalId p) noexcept
{
    switch (p)
    {
        case PedalId::gate:  return ParamID::gateOn;
        case PedalId::comp:  return ParamID::compOn;
        case PedalId::drive: return ParamID::drvOn;
        case PedalId::boost: return ParamID::bstOn;
        case PedalId::numPedals: break;
    }
    return ParamID::gateOn;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto group = [] (const char* id, const char* name) { return std::make_unique<juce::AudioProcessorParameterGroup> (id, name, "|"); };

    // ---- Input -------------------------------------------------------------------------------------
    auto input = group ("input", "Input");
    input->addChild (floatParam (ParamID::inGain, "Input Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, formatDb, "dB"));
    layout.add (std::move (input));

    // ---- Pedals ------------------------------------------------------------------------------------
    auto gate = group ("gate", "Noise Gate");
    gate->addChild (boolParam (ParamID::gateOn, "Gate", false));
    gate->addChild (floatParam (ParamID::gateThreshold, "Gate Threshold", { -90.0f, -10.0f, 0.1f }, -60.0f, formatDb, "dB"));
    gate->addChild (floatParam (ParamID::gateRelease, "Gate Release", skewed (5.0f, 1000.0f, 80.0f, 0.1f), 80.0f, formatMs, "ms"));
    gate->addChild (boolParam (ParamID::gateTrack, "Gate Tracks Amp", true));
    layout.add (std::move (gate));

    auto comp = group ("comp", "Compressor");
    comp->addChild (boolParam (ParamID::compOn, "Compressor", false));
    comp->addChild (knobParam (ParamID::compSustain, "Comp Sustain", 5.0f));
    comp->addChild (floatParam (ParamID::compAttack, "Comp Attack", skewed (0.1f, 100.0f, 10.0f, 0.01f), 10.0f, formatMs, "ms"));
    comp->addChild (floatParam (ParamID::compLevel, "Comp Level", { -12.0f, 12.0f, 0.1f }, 0.0f, formatDb, "dB"));
    comp->addChild (floatParam (ParamID::compBlend, "Comp Blend", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    layout.add (std::move (comp));

    auto drv = group ("drive", "Drive");
    drv->addChild (boolParam (ParamID::drvOn, "Drive", false));
    drv->addChild (choiceParam (ParamID::drvType, "Drive Type", names ((int) DriveType::numTypes, driveTypeName), 0));
    drv->addChild (knobParam (ParamID::drvDrive, "Drive Amount", 3.0f));
    drv->addChild (knobParam (ParamID::drvTone, "Drive Tone", 5.0f));
    drv->addChild (floatParam (ParamID::drvLevel, "Drive Level", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb, "dB"));
    drv->addChild (floatParam (ParamID::drvBlend, "Drive Blend", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    layout.add (std::move (drv));

    auto bst = group ("boost", "Boost");
    bst->addChild (boolParam (ParamID::bstOn, "Boost", false));
    bst->addChild (floatParam (ParamID::bstGain, "Boost Gain", { 0.0f, 24.0f, 0.1f }, 8.0f, formatDb, "dB"));
    bst->addChild (floatParam (ParamID::bstTight, "Boost Tight", skewed (20.0f, 800.0f, 150.0f, 1.0f), 120.0f,
                               [] (float v, int) { return v <= 20.5f ? juce::String ("Off") : formatHz (v); }, "Hz"));
    bst->addChild (knobParam (ParamID::bstTone, "Boost Tone", 5.0f));
    layout.add (std::move (bst));

    // ---- Amp head -------------------------------------------------------------------------------------
    auto amp = group ("amp", "Amp");
    amp->addChild (boolParam (ParamID::ampOn, "Amp", true));
    amp->addChild (choiceParam (ParamID::ampModel, "Amp Model", names ((int) AmpModel::numModels, ampModelName), (int) AmpModel::brit800));
    amp->addChild (knobParam (ParamID::ampGain, "Amp Gain", 5.0f));
    amp->addChild (knobParam (ParamID::ampBass, "Amp Bass", 5.0f));
    amp->addChild (knobParam (ParamID::ampMid, "Amp Middle", 5.0f));
    amp->addChild (knobParam (ParamID::ampTreble, "Amp Treble", 5.0f));
    amp->addChild (knobParam (ParamID::ampPresence, "Amp Presence", 5.0f));
    amp->addChild (knobParam (ParamID::ampDepth, "Amp Resonance", 5.0f));
    amp->addChild (knobParam (ParamID::ampMaster, "Amp Master", 5.0f));
    amp->addChild (floatParam (ParamID::ampLevel, "Amp Output", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb, "dB"));
    amp->addChild (boolParam (ParamID::ampBright, "Amp Bright", false));
    amp->addChild (floatParam (ParamID::ampSag, "Amp Sag", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    amp->addChild (floatParam (ParamID::ampBlend, "Clean Low Blend", { 0.0f, 100.0f, 1.0f }, 0.0f, formatPct, "%"));
    amp->addChild (choiceParam (ParamID::ampMidFreq, "Mid Frequency", { "220 Hz", "450 Hz", "800 Hz", "1.6 kHz", "3 kHz" }, 2));
    amp->addChild (boolParam (ParamID::ampUltraLo, "Ultra Lo", false));
    amp->addChild (boolParam (ParamID::ampUltraHi, "Ultra Hi", false));
    layout.add (std::move (amp));

    auto geq = group ("geq", "Graphic EQ");
    geq->addChild (boolParam (ParamID::geqOn, "Graphic EQ", false));
    const char* geqIds[] = { ParamID::geq1, ParamID::geq2, ParamID::geq3, ParamID::geq4, ParamID::geq5 };
    const char* geqNames[] = { "GEQ 80 Hz", "GEQ 240 Hz", "GEQ 750 Hz", "GEQ 2.2 kHz", "GEQ 6.6 kHz" };
    for (int b = 0; b < kGeqBands; ++b)
        geq->addChild (floatParam (geqIds[b], geqNames[b], { -12.0f, 12.0f, 0.1f }, 0.0f, formatDb, "dB"));
    layout.add (std::move (geq));

    // ---- Effects loop -------------------------------------------------------------------------------------
    auto mod = group ("mod", "Modulation");
    mod->addChild (boolParam (ParamID::modOn, "Modulation", false));
    mod->addChild (choiceParam (ParamID::modMode, "Modulation Type", { "Chorus", "Flanger", "Phaser" }, 0));
    mod->addChild (floatParam (ParamID::modRate, "Mod Rate", skewed (0.02f, 8.0f, 0.8f, 0.01f), 0.6f,
                               [] (float v, int) { return juce::String (v, 2) + " Hz"; }, "Hz"));
    mod->addChild (floatParam (ParamID::modDepth, "Mod Depth", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    mod->addChild (floatParam (ParamID::modFeedback, "Mod Feedback", { 0.0f, 90.0f, 1.0f }, 20.0f, formatPct, "%"));
    mod->addChild (floatParam (ParamID::modMix, "Mod Mix", { 0.0f, 100.0f, 1.0f }, 35.0f, formatPct, "%"));
    layout.add (std::move (mod));

    auto dly = group ("delay", "Delay");
    dly->addChild (boolParam (ParamID::dlyOn, "Delay", false));
    dly->addChild (choiceParam (ParamID::dlyMode, "Delay Type", { "Digital", "Tape", "Ping-Pong", "Slapback" }, 0));
    dly->addChild (floatParam (ParamID::dlyTime, "Delay Time", skewed (10.0f, 2000.0f, 300.0f, 1.0f), 380.0f, formatMs, "ms"));
    juce::StringArray syncs;
    for (int i = 0; i < numDelaySyncOptions(); ++i) syncs.add (delaySyncName (i));
    dly->addChild (choiceParam (ParamID::dlySync, "Delay Sync", syncs, 0));
    dly->addChild (floatParam (ParamID::dlyFeedback, "Delay Feedback", { 0.0f, 95.0f, 1.0f }, 30.0f, formatPct, "%"));
    dly->addChild (floatParam (ParamID::dlyHp, "Delay Low Cut", skewed (20.0f, 2000.0f, 250.0f, 1.0f), 200.0f, formatHz, "Hz"));
    dly->addChild (floatParam (ParamID::dlyLp, "Delay High Cut", skewed (1000.0f, 20000.0f, 6000.0f, 1.0f), 5000.0f, formatHz, "Hz"));
    dly->addChild (floatParam (ParamID::dlyDuck, "Delay Ducking", { 0.0f, 100.0f, 1.0f }, 40.0f, formatPct, "%"));
    dly->addChild (floatParam (ParamID::dlyWidth, "Delay Width", { 0.0f, 100.0f, 1.0f }, 80.0f, formatPct, "%"));
    dly->addChild (floatParam (ParamID::dlyMix, "Delay Level", { 0.0f, 100.0f, 1.0f }, 20.0f, formatPct, "%"));
    layout.add (std::move (dly));

    // ---- Cabinet --------------------------------------------------------------------------------------------
    auto cab = group ("cab", "Cabinet");
    cab->addChild (boolParam (ParamID::cabOn, "Cabinet", true));
    cab->addChild (choiceParam (ParamID::cabModel, "Cabinet Model", names ((int) CabModel::numCabs, cabModelName), (int) CabModel::v30x412));
    cab->addChild (floatParam (ParamID::cabLowCut, "Cab Low Cut", skewed (20.0f, 400.0f, 80.0f, 1.0f), 20.0f,
                               [] (float v, int) { return v <= 20.5f ? juce::String ("Off") : formatHz (v); }, "Hz"));
    cab->addChild (floatParam (ParamID::cabHighCut, "Cab High Cut", skewed (2000.0f, 20000.0f, 8000.0f, 1.0f), 20000.0f,
                               [] (float v, int) { return v >= 19999.0f ? juce::String ("Off") : formatHz (v); }, "Hz"));
    const juce::StringArray sources { "Built-in", "IR File" };
    const auto micNames = names ((int) MicModel::numMics, micModelName);
    cab->addChild (choiceParam (ParamID::micASource, "Mic A Source", sources, 0));
    cab->addChild (choiceParam (ParamID::micAType, "Mic A Model", micNames, (int) MicModel::dyn57));
    cab->addChild (posParam (ParamID::micAPos, "Mic A Position", 25.0f));
    cab->addChild (distParam (ParamID::micADist, "Mic A Distance", 2.0f));
    cab->addChild (boolParam (ParamID::micBOn, "Mic B", false));
    cab->addChild (choiceParam (ParamID::micBSource, "Mic B Source", sources, 0));
    cab->addChild (choiceParam (ParamID::micBType, "Mic B Model", micNames, (int) MicModel::ribbon121));
    cab->addChild (posParam (ParamID::micBPos, "Mic B Position", 50.0f));
    cab->addChild (distParam (ParamID::micBDist, "Mic B Distance", 8.0f));
    cab->addChild (boolParam (ParamID::micBInvert, "Mic B Polarity", false));
    cab->addChild (floatParam (ParamID::micMix, "Mic Blend", { 0.0f, 100.0f, 1.0f }, 40.0f, [] (float v, int)
    {
        const int b = juce::roundToInt (v);
        return b == 0 ? juce::String ("A") : (b == 100 ? juce::String ("B") : juce::String (100 - b) + " / " + juce::String (b));
    }, "%"));
    layout.add (std::move (cab));

    // ---- Post --------------------------------------------------------------------------------------------------
    auto eq = group ("eq", "Post EQ");
    eq->addChild (boolParam (ParamID::eqOn, "Post EQ", false));
    const char* eqIds[5][3] = { { ParamID::eq1Freq, ParamID::eq1Gain, ParamID::eq1Q }, { ParamID::eq2Freq, ParamID::eq2Gain, ParamID::eq2Q },
                                { ParamID::eq3Freq, ParamID::eq3Gain, ParamID::eq3Q }, { ParamID::eq4Freq, ParamID::eq4Gain, ParamID::eq4Q },
                                { ParamID::eq5Freq, ParamID::eq5Gain, ParamID::eq5Q } };
    const char* bandNames[] = { "Low", "Low-Mid", "Mid", "High-Mid", "High" };
    const float defFreq[] = { 100.0f, 400.0f, 1500.0f, 3500.0f, 9000.0f };
    const float minFreq[] = { 30.0f, 80.0f, 200.0f, 800.0f, 2000.0f };
    const float maxFreq[] = { 600.0f, 2000.0f, 6000.0f, 12000.0f, 18000.0f };
    for (int b = 0; b < kEqBands; ++b)
    {
        const bool shelf = b == 0 || b == kEqBands - 1;
        eq->addChild (floatParam (eqIds[b][0], juce::String ("EQ ") + bandNames[b] + " Freq",
                                  skewed (minFreq[b], maxFreq[b], defFreq[b], 1.0f), defFreq[b], formatHz, "Hz"));
        eq->addChild (floatParam (eqIds[b][1], juce::String ("EQ ") + bandNames[b] + " Gain", { -15.0f, 15.0f, 0.1f }, 0.0f, formatDb, "dB"));
        eq->addChild (floatParam (eqIds[b][2], juce::String ("EQ ") + bandNames[b] + (shelf ? " Slope" : " Q"),
                                  shelf ? juce::NormalisableRange<float> (0.3f, 1.0f, 0.01f) : skewed (0.2f, 8.0f, 1.0f, 0.01f),
                                  shelf ? 0.7f : 1.0f, [] (float v, int) { return juce::String (v, 2); }));
    }
    layout.add (std::move (eq));

    auto dbl = group ("doubler", "Doubler");
    dbl->addChild (boolParam (ParamID::dblOn, "Doubler", false));
    dbl->addChild (floatParam (ParamID::dblAmount, "Doubler Amount", { 0.0f, 100.0f, 1.0f }, 60.0f, formatPct, "%"));
    dbl->addChild (floatParam (ParamID::dblDetune, "Doubler Detune", { 0.0f, 40.0f, 0.1f }, 7.0f,
                               [] (float v, int) { return juce::String (v, 1) + " ct"; }, "ct"));
    dbl->addChild (floatParam (ParamID::dblDelay, "Doubler Delay", { 0.0f, 60.0f, 0.1f }, 16.0f, formatMs, "ms"));
    dbl->addChild (floatParam (ParamID::dblWidth, "Doubler Width", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    dbl->addChild (floatParam (ParamID::dblDrift, "Doubler Humanize", { 0.0f, 100.0f, 1.0f }, 60.0f, formatPct, "%"));
    layout.add (std::move (dbl));

    auto rev = group ("reverb", "Reverb");
    rev->addChild (boolParam (ParamID::revOn, "Reverb", false));
    rev->addChild (choiceParam (ParamID::revType, "Reverb Type", { "Plate", "Room", "Hall", "Chamber" }, 1));
    rev->addChild (floatParam (ParamID::revDecay, "Reverb Decay", skewed (0.2f, 10.0f, 1.8f, 0.01f), 1.2f,
                               [] (float v, int) { return juce::String (v, 2) + " s"; }, "s"));
    rev->addChild (floatParam (ParamID::revPredelay, "Reverb Pre-Delay", { 0.0f, 200.0f, 0.1f }, 15.0f, formatMs, "ms"));
    rev->addChild (floatParam (ParamID::revSize, "Reverb Size", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revDamp, "Reverb Damping", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revLowCut, "Reverb Low Cut", skewed (20.0f, 1000.0f, 250.0f, 1.0f), 300.0f, formatHz, "Hz"));
    rev->addChild (floatParam (ParamID::revDuck, "Reverb Ducking", { 0.0f, 100.0f, 1.0f }, 20.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revWidth, "Reverb Width", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revMix, "Reverb Level", { 0.0f, 100.0f, 1.0f }, 15.0f, formatPct, "%"));
    layout.add (std::move (rev));

    auto out = group ("output", "Output");
    out->addChild (floatParam (ParamID::outGain, "Output Gain", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb, "dB"));
    out->addChild (floatParam (ParamID::outWidth, "Stereo Width", { 0.0f, 200.0f, 1.0f }, 100.0f, formatPct, "%"));
    out->addChild (boolParam (ParamID::limOn, "Limiter", true));
    out->addChild (floatParam (ParamID::limCeiling, "Limiter Ceiling", { -12.0f, 0.0f, 0.1f }, -1.0f, formatDb, "dB"));
    layout.add (std::move (out));

    return layout;
}

//==============================================================================
ParameterCache::ParameterCache (juce::AudioProcessorValueTreeState& s)
{
    const char* ids[] = {
#define LAAS_ID_STRING(name, id) id,
        LAAS_PARAMETERS (LAAS_ID_STRING)
#undef LAAS_ID_STRING
    };
    for (size_t i = 0; i < values.size(); ++i)
    {
        values[i] = s.getRawParameterValue (ids[i]);
        jassert (values[i] != nullptr);
    }
}

void ParameterCache::read (EngineParams& p) const noexcept
{
    auto f = [this] (P id) { return get (id); };
    auto b = [this] (P id) { return get (id) > 0.5f; };
    auto i = [this] (P id) { return (int) std::lround (get (id)); };

    p.inputGainDb = f (P::inGain);
    p.gate = { b (P::gateOn), f (P::gateThreshold), f (P::gateRelease), b (P::gateTrack) };
    p.comp = { b (P::compOn), f (P::compSustain), f (P::compAttack), f (P::compLevel), f (P::compBlend) * 0.01f };
    p.drive = { b (P::drvOn), (DriveType) i (P::drvType), f (P::drvDrive), f (P::drvTone), f (P::drvLevel), f (P::drvBlend) * 0.01f };
    p.boost = { b (P::bstOn), f (P::bstGain), f (P::bstTight), f (P::bstTone) };

    p.ampOn = b (P::ampOn);
    auto& a = p.amp;
    a.model = (AmpModel) juce::jlimit (0, (int) AmpModel::numModels - 1, i (P::ampModel));
    a.gain = f (P::ampGain);
    a.bass = f (P::ampBass);
    a.mid = f (P::ampMid);
    a.treble = f (P::ampTreble);
    a.presence = f (P::ampPresence);
    a.depth = f (P::ampDepth);
    a.master = f (P::ampMaster);
    a.levelDb = f (P::ampLevel);
    a.bright = b (P::ampBright);
    a.sag = f (P::ampSag) * 0.01f;
    a.blend = f (P::ampBlend) * 0.01f;
    a.midFreq = i (P::ampMidFreq);
    a.ultraLo = b (P::ampUltraLo);
    a.ultraHi = b (P::ampUltraHi);

    p.geqOn = b (P::geqOn);
    p.geq = { f (P::geq1), f (P::geq2), f (P::geq3), f (P::geq4), f (P::geq5) };
    p.mod = { b (P::modOn), (ModMode) i (P::modMode), f (P::modRate), f (P::modDepth) * 0.01f, f (P::modFeedback) * 0.01f, f (P::modMix) * 0.01f };
    p.delay = { b (P::dlyOn), (DelayMode) i (P::dlyMode), f (P::dlyTime), i (P::dlySync), f (P::dlyFeedback) * 0.01f, f (P::dlyHp),
                f (P::dlyLp), f (P::dlyDuck) * 0.01f, f (P::dlyWidth) * 0.01f, f (P::dlyMix) * 0.01f };

    auto& c = p.cab;
    c.enabled = b (P::cabOn);
    c.cab = (CabModel) juce::jlimit (0, (int) CabModel::numCabs - 1, i (P::cabModel));
    c.lowCutHz = f (P::cabLowCut);
    c.highCutHz = f (P::cabHighCut);
    c.sourceA = (MicSource) i (P::micASource);
    c.micA = { (MicModel) i (P::micAType), f (P::micAPos) * 0.01f, f (P::micADist) };
    c.micBOn = b (P::micBOn);
    c.sourceB = (MicSource) i (P::micBSource);
    c.micB = { (MicModel) i (P::micBType), f (P::micBPos) * 0.01f, f (P::micBDist) };
    c.micBInvert = b (P::micBInvert);
    c.mix = f (P::micMix) * 0.01f;

    p.eq.enabled = b (P::eqOn);
    const P eqp[5][3] = { { P::eq1Freq, P::eq1Gain, P::eq1Q }, { P::eq2Freq, P::eq2Gain, P::eq2Q }, { P::eq3Freq, P::eq3Gain, P::eq3Q },
                          { P::eq4Freq, P::eq4Gain, P::eq4Q }, { P::eq5Freq, P::eq5Gain, P::eq5Q } };
    for (size_t k = 0; k < 5; ++k)
    {
        p.eq.freq[k] = f (eqp[k][0]);
        p.eq.gain[k] = f (eqp[k][1]);
        p.eq.q[k] = f (eqp[k][2]);
    }
    p.doubler = { b (P::dblOn), f (P::dblAmount) * 0.01f, f (P::dblDetune), f (P::dblDelay), f (P::dblWidth) * 0.01f, f (P::dblDrift) * 0.01f };
    p.reverb = { b (P::revOn), (ReverbType) i (P::revType), f (P::revDecay), f (P::revPredelay), f (P::revSize) * 0.01f,
                 f (P::revDamp) * 0.01f, f (P::revLowCut), f (P::revDuck) * 0.01f, f (P::revWidth) * 0.01f, f (P::revMix) * 0.01f };

    p.outputGainDb = f (P::outGain);
    p.width = f (P::outWidth) * 0.01f;
    p.limiterOn = b (P::limOn);
    p.ceilingDb = f (P::limCeiling);
}

//==============================================================================
const std::vector<FactoryPreset>& getFactoryPresets()
{
    using namespace ParamID;
    const float plexi = 0, b800 = 1, a51 = 2, recto = 3, mark = 4, svt = 5, b15 = 6, b400 = 7;
    const float v30 = 0, gb = 1, t75 = 2, open212 = 3, c810 = 4, c115 = 5, c410 = 6;
    const float m57 = 0, m421 = 1, m121 = 2, m87 = 3, m7b = 4;
    const float screamer = 0, dist = 1, fuzz = 2, bassDrive = 3;
    juce::ignoreUnused (b800, gb, m7b, dist);

    static const std::vector<FactoryPreset> presets {
        { "Default", "Guitar", {} },
        { "Classic Rock Crunch", "Guitar",
          { { ampModel, plexi }, { ampGain, 6.5f }, { ampBass, 4.5f }, { ampMid, 7.0f }, { ampTreble, 6.0f }, { ampPresence, 5.0f },
            { ampMaster, 7.5f }, { ampBright, 1 }, { cabModel, gb }, { micAType, m57 }, { micAPos, 35 }, { micADist, 3 },
            { micBOn, 1 }, { micBType, m121 }, { micBPos, 50 }, { micBDist, 12 }, { micMix, 35 }, { revOn, 1 }, { revType, 1 }, { revMix, 12 } } },
        { "Edge of Breakup", "Guitar",
          { { ampModel, plexi }, { ampGain, 3.5f }, { ampBass, 5.0f }, { ampMid, 6.0f }, { ampTreble, 6.5f }, { ampMaster, 5.5f },
            { ampBright, 1 }, { cabModel, open212 }, { micAType, m421 }, { micAPos, 30 }, { micADist, 4 }, { revOn, 1 }, { revType, 0 },
            { revMix, 16 }, { compOn, 1 }, { compSustain, 3.0f }, { compBlend, 60 } } },
        { "Punk Rock Wall", "Guitar",
          { { ampModel, b800 }, { ampGain, 7.5f }, { ampBass, 6.0f }, { ampMid, 7.0f }, { ampTreble, 7.0f }, { ampPresence, 6.0f },
            { ampMaster, 6.0f }, { ampBright, 1 }, { cabModel, t75 }, { micAType, m57 }, { micAPos, 22 }, { micADist, 2 },
            { micBOn, 1 }, { micBType, m421 }, { micBPos, 60 }, { micBDist, 4 }, { micMix, 30 }, { gateOn, 1 }, { gateThreshold, -62 },
            { dblOn, 1 }, { dblAmount, 70 }, { dblWidth, 100 } } },
        { "Pop-Punk Rhythm", "Guitar",
          { { ampModel, b800 }, { ampGain, 6.5f }, { ampBass, 5.5f }, { ampMid, 6.0f }, { ampTreble, 6.5f }, { ampPresence, 6.0f },
            { drvOn, 1 }, { drvType, screamer }, { drvDrive, 1.0f }, { drvTone, 6.0f }, { drvLevel, 6.0f }, { cabModel, v30 },
            { micAType, m57 }, { micAPos, 25 }, { micADist, 2 }, { gateOn, 1 }, { gateThreshold, -58 }, { dblOn, 1 }, { dblAmount, 60 } } },
        { "Hard Rock Lead", "Guitar",
          { { ampModel, b800 }, { ampGain, 8.0f }, { ampMid, 7.5f }, { ampTreble, 6.0f }, { ampBright, 1 }, { drvOn, 1 },
            { drvType, screamer }, { drvDrive, 3.0f }, { drvLevel, 4.0f }, { cabModel, gb }, { micAType, m57 }, { micAPos, 30 },
            { dlyOn, 1 }, { dlyTime, 420 }, { dlyFeedback, 32 }, { dlyMix, 22 }, { revOn, 1 }, { revType, 0 }, { revMix, 16 } } },
        { "Thrash Rhythm", "Guitar",
          { { ampModel, a51 }, { ampGain, 7.0f }, { ampBass, 5.5f }, { ampMid, 6.0f }, { ampTreble, 6.5f }, { ampPresence, 6.0f },
            { ampDepth, 6.0f }, { drvOn, 1 }, { drvType, screamer }, { drvDrive, 0.0f }, { drvTone, 6.0f }, { drvLevel, 8.0f },
            { gateOn, 1 }, { gateThreshold, -55 }, { cabModel, v30 }, { micAType, m57 }, { micAPos, 20 }, { micADist, 1.5f },
            { cabLowCut, 70 } } },
        { "Modern Metal", "Guitar",
          { { ampModel, recto }, { ampGain, 7.5f }, { ampBass, 6.0f }, { ampMid, 3.5f }, { ampTreble, 6.0f }, { ampPresence, 6.5f },
            { ampDepth, 6.0f }, { bstOn, 1 }, { bstGain, 6.0f }, { bstTight, 150 }, { bstTone, 6.0f }, { gateOn, 1 }, { gateThreshold, -55 },
            { cabModel, v30 }, { micAType, m57 }, { micAPos, 25 }, { micADist, 2 }, { micBOn, 1 }, { micBType, m121 }, { micBPos, 40 },
            { micBDist, 5 }, { micMix, 40 }, { cabLowCut, 80 }, { cabHighCut, 9000 } } },
        { "Djent Tight", "Guitar",
          { { ampModel, recto }, { ampGain, 6.5f }, { ampBass, 5.0f }, { ampMid, 5.0f }, { ampTreble, 6.5f }, { ampPresence, 7.0f },
            { bstOn, 1 }, { bstGain, 9.0f }, { bstTight, 300 }, { gateOn, 1 }, { gateThreshold, -45 }, { gateRelease, 40 },
            { geqOn, 1 }, { geq1, -3.0f }, { geq2, -2.0f }, { geq3, 2.0f }, { geq4, 2.0f }, { cabModel, v30 }, { micAType, m57 },
            { micAPos, 15 }, { micADist, 1 }, { cabLowCut, 100 }, { cabHighCut, 8500 } } },
        { "Mark Thrash V", "Guitar",
          { { ampModel, mark }, { ampGain, 6.0f }, { ampBass, 2.5f }, { ampMid, 4.5f }, { ampTreble, 7.5f }, { ampPresence, 6.0f },
            { geqOn, 1 }, { geq1, 4.0f }, { geq2, 1.0f }, { geq3, -5.0f }, { geq4, 1.0f }, { geq5, 4.0f }, { gateOn, 1 },
            { gateThreshold, -55 }, { cabModel, v30 }, { micAType, m57 }, { micAPos, 25 }, { micADist, 2 } } },
        { "Mark Singing Lead", "Guitar",
          { { ampModel, mark }, { ampGain, 8.0f }, { ampBass, 3.0f }, { ampMid, 6.0f }, { ampTreble, 7.0f }, { geqOn, 1 },
            { geq1, 3.0f }, { geq3, -2.0f }, { geq4, 1.0f }, { geq5, 2.0f }, { cabModel, v30 }, { micAType, m421 }, { micAPos, 30 },
            { dlyOn, 1 }, { dlyMode, 2 }, { dlyTime, 450 }, { dlyMix, 20 }, { revOn, 1 }, { revType, 2 }, { revDecay, 2.4f }, { revMix, 15 } } },
        { "Doom Fuzz", "Guitar",
          { { ampModel, plexi }, { ampGain, 5.0f }, { ampBass, 7.0f }, { ampMid, 5.0f }, { ampTreble, 5.0f }, { ampMaster, 8.0f },
            { drvOn, 1 }, { drvType, fuzz }, { drvDrive, 7.0f }, { drvTone, 4.0f }, { drvLevel, 0.0f }, { cabModel, gb },
            { micAType, m121 }, { micAPos, 45 }, { micADist, 6 }, { micBOn, 1 }, { micBType, m57 }, { micBPos, 20 }, { micMix, 40 },
            { revOn, 1 }, { revType, 3 }, { revMix, 14 } } },
        { "Bass: SVT Rock", "Bass",
          { { ampModel, svt }, { ampGain, 5.0f }, { ampBass, 6.0f }, { ampMid, 6.0f }, { ampMidFreq, 2 }, { ampTreble, 5.5f },
            { cabModel, c810 }, { micAType, m421 }, { micAPos, 30 }, { micADist, 3 }, { micBOn, 1 }, { micBType, m87 }, { micBPos, 50 },
            { micBDist, 30 }, { micMix, 30 }, { compOn, 1 }, { compSustain, 2.5f }, { compAttack, 20.0f } } },
        { "Bass: Punk Grind", "Bass",
          { { ampModel, svt }, { ampGain, 8.0f }, { ampBass, 5.5f }, { ampMid, 7.0f }, { ampMidFreq, 3 }, { ampTreble, 6.0f },
            { ampUltraHi, 1 }, { cabModel, c810 }, { micAType, m57 }, { micAPos, 20 }, { micADist, 2 } } },
        { "Bass: Metal Drive", "Bass",
          { { ampModel, b400 }, { ampGain, 4.0f }, { drvOn, 1 }, { drvType, bassDrive }, { drvDrive, 6.0f }, { drvTone, 6.0f },
            { compOn, 1 }, { compSustain, 5.0f }, { ampBlend, 30 }, { cabModel, c410 }, { micAType, m421 }, { micAPos, 30 },
            { geqOn, 1 }, { geq1, 2.0f }, { geq2, -2.0f }, { geq3, 1.0f }, { geq4, 3.0f }, { geq5, 2.0f },
            { gateOn, 1 }, { gateThreshold, -58 } },
          "1023" },
        { "Bass: Vintage B-15", "Bass",
          { { ampModel, b15 }, { ampGain, 4.5f }, { ampBass, 6.0f }, { ampTreble, 4.5f }, { cabModel, c115 }, { micAType, m87 },
            { micAPos, 40 }, { micADist, 15 }, { revOn, 1 }, { revType, 1 }, { revMix, 8 } } },
    };
    return presets;
}
} // namespace laas
