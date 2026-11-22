#pragma once

#include <array>

#include "DspUtils.h"

namespace laas
{
/** The re-orderable pedals in front of the amp. */
enum class PedalId { gate, comp, drive, boost, numPedals };
constexpr int kNumPedals = (int) PedalId::numPedals;
using PedalOrder = std::array<int, kNumPedals>;

inline PedalOrder defaultPedalOrder() noexcept { return { 0, 1, 2, 3 }; }
const char* pedalShortName (PedalId p) noexcept;
/** Order as a string of digits, e.g. "0123". */
void pedalOrderToString (const PedalOrder& o, char* out) noexcept;
PedalOrder pedalOrderFromString (const char* s) noexcept;
bool isValidPedalOrder (const PedalOrder& o) noexcept;

//==============================================================================
struct GateParams
{
    bool enabled = false;
    float thresholdDb = -55.0f;
    float releaseMs = 80.0f;
    bool track = true; // also gate the amp's output (tracking gate)
};

/** Noise gate keyed from the clean input (like a "tracking" gate). Computes a gain per input sample. */
class NoiseGate
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const GateParams& p) noexcept { params = p; }
    /** Writes the gate gain for each sample of the clean input into gains. */
    void computeGains (const float* key, float* gains, int n) noexcept;
    float getReductionDb() const noexcept { return grDb; }

private:
    double sr = 48000.0;
    GateParams params;
    Biquad keyHp, keyLp;
    EnvelopeFollower env;
    float gain = 1.0f, attCoeff = 0.0f, grDb = 0.0f;
    int hold = 0;
    bool open = false;
};

//==============================================================================
struct CompPedalParams
{
    bool enabled = false;
    float sustain = 5.0f; // 0..10 (amount of compression)
    float attackMs = 10.0f;
    float levelDb = 0.0f;
    float blend = 1.0f;   // 0..1 parallel mix
};

/** Pedal compressor (feed-forward, soft knee, auto makeup) with a blend for parallel compression. */
class CompPedal
{
public:
    void prepare (double rate);
    void reset() noexcept;
    void setParams (const CompPedalParams& p) noexcept { params = p; }
    void process (float* x, int n) noexcept;
    float getReductionDb() const noexcept { return grDb; }

private:
    double sr = 48000.0;
    CompPedalParams params;
    float envDb = -100.0f, rmsState = 0.0f, rmsCoeff = 0.0f, grDb = 0.0f;
};

//==============================================================================
enum class DriveType { screamer, distortion, fuzz, bassDrive, numTypes };
const char* driveTypeName (int t) noexcept;

struct DrivePedalParams
{
    bool enabled = false;
    DriveType type = DriveType::screamer;
    float drive = 3.0f;   // 0..10
    float tone = 5.0f;    // 0..10
    float levelDb = 0.0f;
    float blend = 1.0f;   // 0..1 (wet amount, dry is mixed for less than 1)
};

/** Overdrive / distortion / fuzz / bass drive pedal (runs at the oversampled rate). */
class DrivePedal
{
public:
    void prepare (double osRate);
    void reset() noexcept;
    void setParams (const DrivePedalParams& p) noexcept;
    void process (float* x, int n) noexcept;

private:
    void design() noexcept;

    double sr = 192000.0;
    DrivePedalParams params, designed;
    bool needsDesign = true;
    OnePole clipHp, preHp, toneLp, postLp, fuzzHp2, dcBlock;
    Biquad bassLow1, bassLow2, bassHigh1, bassHigh2, muffLp, muffHp, midPeak;
    Smoothed driveSm, levelSm;
};

//==============================================================================
struct BoostParams
{
    bool enabled = false;
    float gainDb = 6.0f;   // 0..24
    float tightHz = 120.0f; // high-pass in front of the amp
    float tone = 5.0f;     // 0..10 (dark..bright)
};

/** Clean boost with a "tight" high-pass and a tilt tone: the classic way to tighten a high-gain amp. */
class BoostPedal
{
public:
    void prepare (double osRate);
    void reset() noexcept;
    void setParams (const BoostParams& p) noexcept;
    void process (float* x, int n) noexcept;

private:
    double sr = 192000.0;
    BoostParams params;
    float lastTight = -1.0f, lastTone = -1.0f;
    Biquad hp;
    OnePole tilt;
    Smoothed gainSm;
};
} // namespace laas
