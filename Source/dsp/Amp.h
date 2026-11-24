#pragma once

#include <array>

#include "DspUtils.h"
#include "ToneStack.h"

namespace laas
{
/** One triode gain stage: coupling high-pass, cathode shelf, gain, tube curve, Miller low-pass. */
struct StageSpec
{
    float gain = 10.0f;      // small-signal voltage gain (linear)
    float couplingHz = 20.0f; // coupling cap / grid resistor high-pass
    float cathodeHz = 0.0f;   // partially bypassed cathode: low shelf corner (0 = fully bypassed)
    float cathodeDb = 0.0f;   // low shelf gain below cathodeHz
    float millerHz = 12000.0f; // Miller capacitance / treble bleed low-pass
    float bias = 0.0f;        // operating point: negative = colder (more asymmetric clipping)
    float trebleHz = 0.0f;    // treble-peaking network in front of the stage (e.g. 470k mixer resistor
    float trebleDb = 0.0f;    //   bypassed by a small cap): high shelf at the stage input (0 = none)
};

enum class AmpModel
{
    plexi,      // Marshall 1959 Super Lead (jumpered)
    brit800,    // Marshall JCM800 2203
    american51, // Peavey 5150 / 6505 lead channel
    recto,      // Mesa/Boogie Dual Rectifier, modern red channel
    markLead,   // Mesa/Boogie Mark IIC+ lead
    svt,        // Ampeg SVT Classic (bass)
    flipTop,    // Ampeg B-15 Portaflex (bass)
    bass400,    // Mesa/Boogie Bass 400+ (bass)
    numModels
};

struct AmpModelSpec
{
    const char* name;
    const char* shortName;
    const char* inspiredBy;
    const char* description;
    bool bass;

    int numStages;
    std::array<StageSpec, 5> stages;
    int gainPotBefore;     // gain knob sits in front of this stage
    float gainMinDb, gainMaxDb;
    float brightHz, brightDb; // bright cap on the gain pot (strongest at low gain)
    int stackAfter;        // tone stack after this stage index (numStages - 1 = after the preamp)
    ToneStackType stackType;
    TmbComponents tmb;
    float stackMakeupDb;
    float followerDrive;   // cathode follower compression before the stack (0 = none)

    float powerDrive;      // how hard the master pushes the power tubes
    float powerAsym;
    float presenceHz, presenceMaxDb;
    float depthHz, depthMaxDb;
    float sag;             // supply sag (tube rectifier = more)
    float xfmrLowHz, xfmrHighHz;
    float outputTrimDb;    // level calibration so every model sits at a similar loudness

    // Power tube character: how abruptly the output stage goes from clean to clipped. EL34 pentodes
    // break up early and gradually (the Marshall crunch), 6L6 beam tetrodes stay cleaner and then
    // clip harder, 6550s have the most headroom and the hardest knee. 2 = the generic soft curve.
    float powerKnee = 2.0f;
    const char* powerTubeName = "";
};

const AmpModelSpec& ampModelSpec (AmpModel m) noexcept;
const char* ampModelName (int m) noexcept;

struct AmpParams
{
    AmpModel model = AmpModel::brit800;
    float gain = 5.0f;      // 0..10
    float bass = 5.0f, mid = 5.0f, treble = 5.0f;
    float presence = 5.0f, depth = 5.0f; // 0..10
    float master = 5.0f;    // 0..10
    float levelDb = 0.0f;
    bool bright = false;
    float sag = 0.5f;       // 0..1 scales the model's sag
    float blend = 0.0f;     // 0..1 clean low-end blend (mostly for bass)
    int midFreq = 2;        // svt mid frequency selector
    bool ultraLo = false, ultraHi = false;
};

/** Triode stage processor (runs at the oversampled rate). */
class TriodeStage
{
public:
    void prepare (double osRate, const StageSpec& s);
    void reset() noexcept;
    inline float process (float x) noexcept;

private:
    StageSpec spec;
    OnePole coupling, cathode, miller, treble;
    float gridEnv = 0.0f, gridAtt = 0.0f, gridRel = 0.0f, restOut = 0.0f;
};

/**
    Amp head: preamp triode cascade with the model's gain pot / bright cap / tone stack placement,
    cathode follower, presence and resonance (depth), push-pull power amp with sag, output
    transformer. Runs at 4x the host rate (the caller oversamples).
*/
class AmpModule
{
public:
    void prepare (double osRate, int maxOsBlock);
    void reset() noexcept;
    void setParams (const AmpParams& p) noexcept;
    void process (float* x, int n) noexcept;
    float getDriveLevel() const noexcept { return driveMeter; }

private:
    void configureModel() noexcept;
    void updateFilters() noexcept;

    double osSr = 192000.0;
    AmpParams params;
    AmpModel configured = AmpModel::numModels;
    const AmpModelSpec* spec = nullptr;

    std::array<TriodeStage, 5> stages;
    ToneStack stack;
    OnePole bright;
    Biquad presence, depth, xfmrHp, xfmrLp, blendLp1, blendLp2, inputHp;
    Smoothed gainSm, masterSm, levelSm, blendSm;
    EnvelopeFollower sagEnv;
    float lastBright = -1.0f, lastPresence = -1.0f, lastDepth = -1.0f, lastGain = -100.0f;
    bool lastBrightOn = false;
    float driveMeter = 0.0f, stackMakeup = 1.0f;
};
} // namespace laas
