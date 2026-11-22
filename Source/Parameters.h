#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/AmpEngine.h"

namespace laas
{
// Every parameter: X (enum name, id string)
#define LAAS_PARAMETERS(X)                                                                                         \
    X (inGain, "inGain")                                                                                            \
    X (gateOn, "gateOn") X (gateThreshold, "gateThreshold") X (gateRelease, "gateRelease") X (gateTrack, "gateTrack") \
    X (compOn, "compOn") X (compSustain, "compSustain") X (compAttack, "compAttack") X (compLevel, "compLevel")      \
    X (compBlend, "compBlend")                                                                                      \
    X (drvOn, "drvOn") X (drvType, "drvType") X (drvDrive, "drvDrive") X (drvTone, "drvTone") X (drvLevel, "drvLevel") \
    X (drvBlend, "drvBlend")                                                                                        \
    X (bstOn, "bstOn") X (bstGain, "bstGain") X (bstTight, "bstTight") X (bstTone, "bstTone")                       \
    X (ampOn, "ampOn") X (ampModel, "ampModel") X (ampGain, "ampGain") X (ampBass, "ampBass") X (ampMid, "ampMid")  \
    X (ampTreble, "ampTreble") X (ampPresence, "ampPresence") X (ampDepth, "ampDepth") X (ampMaster, "ampMaster")   \
    X (ampLevel, "ampLevel") X (ampBright, "ampBright") X (ampSag, "ampSag") X (ampBlend, "ampBlend")               \
    X (ampMidFreq, "ampMidFreq") X (ampUltraLo, "ampUltraLo") X (ampUltraHi, "ampUltraHi")                          \
    X (geqOn, "geqOn") X (geq1, "geq1") X (geq2, "geq2") X (geq3, "geq3") X (geq4, "geq4") X (geq5, "geq5")         \
    X (modOn, "modOn") X (modMode, "modMode") X (modRate, "modRate") X (modDepth, "modDepth")                       \
    X (modFeedback, "modFeedback") X (modMix, "modMix")                                                             \
    X (dlyOn, "dlyOn") X (dlyMode, "dlyMode") X (dlyTime, "dlyTime") X (dlySync, "dlySync")                         \
    X (dlyFeedback, "dlyFeedback") X (dlyHp, "dlyHp") X (dlyLp, "dlyLp") X (dlyDuck, "dlyDuck")                     \
    X (dlyWidth, "dlyWidth") X (dlyMix, "dlyMix")                                                                   \
    X (cabOn, "cabOn") X (cabModel, "cabModel") X (cabLowCut, "cabLowCut") X (cabHighCut, "cabHighCut")             \
    X (micASource, "micASource") X (micAType, "micAType") X (micAPos, "micAPos") X (micADist, "micADist")           \
    X (micBOn, "micBOn") X (micBSource, "micBSource") X (micBType, "micBType") X (micBPos, "micBPos")               \
    X (micBDist, "micBDist") X (micBInvert, "micBInvert") X (micMix, "micMix")                                      \
    X (eqOn, "eqOn")                                                                                                \
    X (eq1Freq, "eq1Freq") X (eq1Gain, "eq1Gain") X (eq1Q, "eq1Q")                                                  \
    X (eq2Freq, "eq2Freq") X (eq2Gain, "eq2Gain") X (eq2Q, "eq2Q")                                                  \
    X (eq3Freq, "eq3Freq") X (eq3Gain, "eq3Gain") X (eq3Q, "eq3Q")                                                  \
    X (eq4Freq, "eq4Freq") X (eq4Gain, "eq4Gain") X (eq4Q, "eq4Q")                                                  \
    X (eq5Freq, "eq5Freq") X (eq5Gain, "eq5Gain") X (eq5Q, "eq5Q")                                                  \
    X (dblOn, "dblOn") X (dblAmount, "dblAmount") X (dblDetune, "dblDetune") X (dblDelay, "dblDelay")               \
    X (dblWidth, "dblWidth") X (dblDrift, "dblDrift")                                                               \
    X (revOn, "revOn") X (revType, "revType") X (revDecay, "revDecay") X (revPredelay, "revPredelay")               \
    X (revSize, "revSize") X (revDamp, "revDamp") X (revLowCut, "revLowCut") X (revDuck, "revDuck")                 \
    X (revWidth, "revWidth") X (revMix, "revMix")                                                                   \
    X (outGain, "outGain") X (outWidth, "outWidth") X (limOn, "limOn") X (limCeiling, "limCeiling")

namespace ParamID
{
#define LAAS_DECLARE_ID(name, id) inline constexpr auto name = id;
LAAS_PARAMETERS (LAAS_DECLARE_ID)
#undef LAAS_DECLARE_ID
} // namespace ParamID

enum class P : int
{
#define LAAS_DECLARE_ENUM(name, id) name,
    LAAS_PARAMETERS (LAAS_DECLARE_ENUM)
#undef LAAS_DECLARE_ENUM
    count
};

/** IDs of the enable switch of each pedal (indexed by PedalId). */
const char* pedalEnableID (PedalId p) noexcept;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw parameter pointers so the audio thread can build EngineParams cheaply. */
class ParameterCache
{
public:
    explicit ParameterCache (juce::AudioProcessorValueTreeState& state);
    /** Fills everything except the pedal order, tempo and mute. */
    void read (EngineParams& p) const noexcept;
    float get (P p) const noexcept { return values[(size_t) p]->load (std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>*, (size_t) P::count> values {};
};

struct FactoryPreset
{
    FactoryPreset (juce::String n, juce::String c, std::vector<std::pair<const char*, float>> v, juce::String o = {})
        : name (std::move (n)), category (std::move (c)), values (std::move (v)), order (std::move (o)) {}

    juce::String name, category;
    std::vector<std::pair<const char*, float>> values;
    juce::String order; // empty = default pedal order
};

const std::vector<FactoryPreset>& getFactoryPresets();
} // namespace laas
