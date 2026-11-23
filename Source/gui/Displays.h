#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/AmpEngine.h"
#include "../dsp/Tuner.h"
#include "Theme.h"

namespace laas
{
class AmpSuiteProcessor;

/** Colour used for each pedal. */
juce::Colour pedalColour (PedalId p);

//==============================================================================
/** Front view of the speaker with the two microphones: drag a mic across the cone, wheel / right rail for distance. */
class SpeakerView : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paintOverChildren (juce::Graphics& g) override { Hardware::drawGlass (g, getLocalBounds().toFloat()); }
    explicit SpeakerView (AmpSuiteProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void tick() { repaint(); }

private:
    struct Geometry
    {
        juce::Point<float> centre;
        float radius = 0.0f;
        juce::Rectangle<float> rail;
    };
    Geometry geometry() const;
    juce::Point<float> micPoint (int mic, const Geometry& g) const;
    float railY (float distCm, const Geometry& g) const;
    int hitMic (juce::Point<float> p, bool& onRail) const;
    bool micActive (int mic) const;
    juce::RangedAudioParameter* param (int mic, int which) const; // 0 pos, 1 dist

    AmpSuiteProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    int dragging = -1, hover = -1;
    bool dragRail = false;
};

//==============================================================================
/** Response view: amp tone stack + graphic EQ, cabinet / mic response, post EQ (draggable) and the output spectrum. */
class ResponseDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paintOverChildren (juce::Graphics& g) override { Hardware::drawGlass (g, getLocalBounds().toFloat()); }
    explicit ResponseDisplay (AmpSuiteProcessor&);

    void pullSpectrum();
    void setSelectedBand (int b) { selected = b; repaint(); }
    std::function<void (int)> onBandSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float freqToX (float f, juce::Rectangle<float> a) const;
    float xToFreq (float x, juce::Rectangle<float> a) const;
    float dbToY (float db, juce::Rectangle<float> a) const;
    float yToDb (float y, juce::Rectangle<float> a) const;
    int hitNode (juce::Point<float> p) const;
    juce::Point<float> nodePosition (int band, juce::Rectangle<float> a) const;
    juce::RangedAudioParameter* bandParam (int band, int which) const;

    AmpSuiteProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> fifo, fftData, window, spectrumDb;
    int fifoPos = 0;

    std::array<float, kResponsePoints> cab {};
    int cabVersion = -1;

    int selected = 2, dragging = -1, hover = -1;
    static constexpr float minDb = -24.0f, maxDb = 18.0f;
};

//==============================================================================
/** Chromatic tuner read-out plus the current amp model's background. */
class TunerPanel : public juce::Component
{
public:
    void paintOverChildren (juce::Graphics& g) override { Hardware::drawGlass (g, getLocalBounds().toFloat()); }
    explicit TunerPanel (AmpSuiteProcessor& p);
    void tick();
    void paint (juce::Graphics&) override;

private:
    AmpSuiteProcessor& processor;
    Tuner tuner;
    double tunerRate = 0.0;
    Tuner::Result shown;
    float shownCents = 0.0f;
    int silentTicks = 1000, analyseCounter = 0;
    std::vector<float> scratch;
};

//==============================================================================
/** Signal-flow strip: every stage as a tile. The pedal tiles can be dragged to change their order. */
class ChainStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Page { pagePedals = 0, pageAmp = 1, pageCab = 2, pageEffects = 3 };

    explicit ChainStrip (AmpSuiteProcessor& p);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override { layoutTiles(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /** Called with (page, sectionKey) when a tile is clicked. */
    std::function<void (int, const juce::String&)> onTileClicked;
    std::function<void()> onOrderChanged;

private:
    struct Tile
    {
        juce::String key, name, enableId;
        juce::Colour colour;
        int page = 0;
        int pedal = -1; // orderable PedalId, -1 = fixed
        int group = 0;  // 0 input, 1 pedals, 2 amp, 3 loop, 4 cab, 5 post, 6 output
        juce::Rectangle<float> bounds;
    };

    void rebuildTiles();
    void layoutTiles();
    juce::String tileValue (const Tile& t) const;
    bool tileEnabled (const Tile& t) const;
    juce::Rectangle<float> powerRect (const Tile& t) const;

    AmpSuiteProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::vector<Tile> tiles;
    PedalOrder order;
    int dragTile = -1, dragTarget = -1, hoverTile = -1;
    float dragX = 0.0f, dragOffset = 0.0f;
    bool dragMoved = false;
};
} // namespace laas
