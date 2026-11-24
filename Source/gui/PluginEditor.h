#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "Displays.h"
#include "Theme.h"
#include "Widgets.h"

namespace laas
{
/** Load / clear buttons and the file name of a user impulse response for one mic slot. */
class IrLoader : public juce::Component
{
public:
    IrLoader (AmpSuiteProcessor& p, int slot);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void (const juce::String&)> onMessage;

private:
    AmpSuiteProcessor& processor;
    int slot;
    PillButton loadButton { "LOAD IR" }, clearButton { "X", Colours::danger };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownName;
};

/** All UI content, laid out at a fixed base size and scaled by the editor. */
class MainPanel : public juce::Component
{
public:
    static constexpr int baseWidth = 1440, baseHeight = 780;

    explicit MainPanel (AmpSuiteProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    void setPage (int page);
    int getPage() const { return currentPage; }

private:
    Knob* knob (Section& s, const char* id, const juce::String& name, juce::Colour c, bool bipolar = false);
    Choice* choice (Section& s, const char* id, const juce::String& caption = {});
    ParamPill* pill (Section& s, const char* id, const juce::String& text, juce::Colour c);
    void layoutPages();
    void layoutPedals (juce::Rectangle<int> area);
    void selectEqBand (int band);
    void refreshPresetList();
    void showToast (const juce::String& text);
    void focusSection (const juce::String& key);
    void updateModelControls();

    AmpSuiteProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<juce::Component>> owned;

    // Header
    MenuBox presetBox;
    PillButton saveButton { "SAVE" }, tunerButton { "TUNER", Colours::danger };
    juce::File userPresetFiles[256];
    int numUserPresets = 0;

    // Displays
    ChainStrip chain;
    SpeakerView speaker;
    ResponseDisplay response;
    TunerPanel tunerPanel;
    LevelMeters meters;

    // Pages
    PillButton tabPedals { "PEDALS" }, tabAmp { "AMP" }, tabCab { "CAB & MICS" }, tabFx { "EFFECTS" };
    int currentPage = 0;

    Section inputSection, outputSection;
    Section gateSection, compSection, driveSection, boostSection;
    Section ampSection, geqSection;
    Section cabSection, micASection, micBSection, eqSection;
    Section modSection, dlySection, dblSection, revSection;
    std::map<juce::String, Section*> sectionByKey;

    std::vector<Knob*> inputKnobs, outputKnobs, gateKnobs, compKnobs, driveKnobs, boostKnobs, ampKnobs, ampKnobs2;
    std::vector<Knob*> cabKnobs, micAKnobs, micBKnobs, modKnobs, dlyKnobs, dblKnobs, revKnobs;
    std::vector<Fader*> geqFaders;
    std::array<std::array<Knob*, 3>, kEqBands> eqKnobs {};
    std::array<std::unique_ptr<PillButton>, kEqBands> eqBandButtons;
    int eqBand = 2;
    Choice *driveType = nullptr, *ampModel = nullptr, *ampMidFreq = nullptr, *cabModel = nullptr;
    Choice *micASource = nullptr, *micAType = nullptr, *micBSource = nullptr, *micBType = nullptr;
    Choice *modMode = nullptr, *dlyMode = nullptr, *dlySync = nullptr, *revType = nullptr;
    ParamPill *gateTrack = nullptr, *ampBright = nullptr, *ultraLo = nullptr, *ultraHi = nullptr, *limitPill = nullptr, *micBInvert = nullptr;
    std::unique_ptr<IrLoader> irA, irB;
    Knob* dlyTimeKnob = nullptr;
    PedalOrder shownOrder {};
    int shownModel = -1;

    int toastTicks = 0;
    juce::String infoText, toastText;
    juce::String highlightKey;
    float highlightAmount = 0.0f;
};

class AmpSuiteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmpSuiteEditor (AmpSuiteProcessor&);
    ~AmpSuiteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls new data from the audio thread and refreshes the displays (called at 60 Hz). */
    void updateDisplays() { panel.tick(); }
    MainPanel& getPanel() { return panel; }

private:
    void timerCallback() override { updateDisplays(); }

    AmpSuiteLookAndFeel lookAndFeel;
    MainPanel panel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSuiteEditor)
};
} // namespace laas
