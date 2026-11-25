#include "PluginEditor.h"

namespace laas
{
namespace
{
const char* eqIds[5][3] = { { ParamID::eq1Freq, ParamID::eq1Gain, ParamID::eq1Q }, { ParamID::eq2Freq, ParamID::eq2Gain, ParamID::eq2Q },
                            { ParamID::eq3Freq, ParamID::eq3Gain, ParamID::eq3Q }, { ParamID::eq4Freq, ParamID::eq4Gain, ParamID::eq4Q },
                            { ParamID::eq5Freq, ParamID::eq5Gain, ParamID::eq5Q } };
const char* eqBandNames[5] = { "LOW", "L-MID", "MID", "H-MID", "HIGH" };

void gridVec (const std::vector<juce::Component*>& items, int columns, juce::Rectangle<int> area)
{
    const int rows = ((int) items.size() + columns - 1) / columns;
    const int cw = area.getWidth() / juce::jmax (1, columns);
    const int rh = area.getHeight() / juce::jmax (1, rows);
    int i = 0;
    for (auto* c : items)
    {
        if (c != nullptr)
            c->setBounds (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * rh, cw, rh);
        ++i;
    }
}

template <typename T>
std::vector<juce::Component*> comps (const std::vector<T*>& v)
{
    return std::vector<juce::Component*> (v.begin(), v.end());
}

constexpr int kMargin = 12, kGap = 8, kSideW = 128, kOutW = 148;
constexpr int kDispY = 112, kDispH = 278;
} // namespace

//==============================================================================
IrLoader::IrLoader (AmpSuiteProcessor& p, int s) : processor (p), slot (s)
{
    loadButton.setTooltip ("Load an impulse response file (WAV, AIFF or FLAC) for this mic slot. Any sample rate; it is resampled");
    clearButton.setTooltip ("Remove the impulse response file and go back to the built-in cabinet");
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load impulse response", processor.getImpulseFile (slot).getParentDirectory(),
                                                       "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File())
                return;
            const auto err = processor.loadImpulseFile (slot, f);
            if (onMessage)
                onMessage (err.isEmpty() ? "Loaded impulse response \"" + f.getFileName() + "\"" : err);
            refresh();
        });
    };
    clearButton.onClick = [this] { processor.clearImpulseFile (slot); refresh(); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    refresh();
}

void IrLoader::refresh()
{
    const auto name = processor.getImpulseName (slot);
    if (name != shownName)
    {
        shownName = name;
        repaint();
    }
    clearButton.setVisible (name.isNotEmpty());
}

void IrLoader::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (22);
    clearButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    loadButton.setBounds (top);
}

void IrLoader::paint (juce::Graphics& g)
{
    g.setFont (uiFont (10.5f));
    g.setColour (shownName.isNotEmpty() ? Colours::text : Colours::textDim);
    g.drawFittedText (shownName.isNotEmpty() ? shownName : juce::String ("no IR file loaded"), getLocalBounds().withTrimmedTop (24),
                      juce::Justification::centred, 1);
}

//==============================================================================
MainPanel::MainPanel (AmpSuiteProcessor& p)
    : processor (p),
      state (p.getState()),
      chain (p),
      speaker (p),
      response (p),
      tunerPanel (p),
      inputSection ("Input"),
      outputSection ("Output"),
      gateSection ("Noise Gate", &state, ParamID::gateOn, pedalColour (PedalId::gate)),
      compSection ("Compressor", &state, ParamID::compOn, pedalColour (PedalId::comp)),
      driveSection ("Drive", &state, ParamID::drvOn, pedalColour (PedalId::drive)),
      boostSection ("Boost", &state, ParamID::bstOn, pedalColour (PedalId::boost)),
      ampSection ("Amp Head", &state, ParamID::ampOn, Colours::threshold),
      geqSection ("Graphic EQ (Loop)", &state, ParamID::geqOn, Colours::threshold),
      cabSection ("Cabinet", &state, ParamID::cabOn, Colours::accent),
      micASection ("Mic A", nullptr, {}, Colours::accent),
      micBSection ("Mic B", &state, ParamID::micBOn, Colours::learn),
      eqSection ("Post EQ", &state, ParamID::eqOn, Colours::threshold),
      modSection ("Modulation (Loop)", &state, ParamID::modOn, Colours::blue),
      dlySection ("Delay (Loop)", &state, ParamID::dlyOn, Colours::detection),
      dblSection ("Doubler", &state, ParamID::dblOn, Colours::learn),
      revSection ("Reverb", &state, ParamID::revOn, Colours::accent)
{
    // ---- Header ----------------------------------------------------------------------------
    refreshPresetList();
    presetBox.setTooltip ("Factory and user presets");
    presetBox.onItemChosen = [this] (int id)
    {
        if (id > 0 && id < 1000)
            processor.setCurrentProgram (id - 1);
        else if (id >= 1000 && id - 1000 < numUserPresets)
            processor.loadUserPreset (userPresetFiles[id - 1000]);
    };
    addAndMakeVisible (presetBox);

    saveButton.setTooltip ("Save the current settings as a user preset");
    saveButton.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name for the new preset:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", processor.getCurrentPresetName());
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1)
            {
                const auto name = w->getTextEditorContents ("name");
                showToast (processor.saveUserPreset (name) ? "Saved preset \"" + name + "\"" : juce::String ("Could not save the preset"));
                refreshPresetList();
            }
        }), true);
    };
    addAndMakeVisible (saveButton);

    tunerButton.setClickingTogglesState (true);
    tunerButton.setTooltip ("Mute the output while tuning (the tuner always listens to the input)");
    tunerButton.onClick = [this] { processor.setTunerMute (tunerButton.getToggleState()); };
    addAndMakeVisible (tunerButton);

    // ---- Displays ---------------------------------------------------------------------------
    addAndMakeVisible (chain);
    chain.onTileClicked = [this] (int page, const juce::String& key)
    {
        setPage (page);
        focusSection (key);
    };
    chain.onOrderChanged = [this] { layoutPages(); };
    addAndMakeVisible (speaker);
    addAndMakeVisible (response);
    response.onBandSelected = [this] (int b) { selectEqBand (b); };
    addAndMakeVisible (tunerPanel);
    addAndMakeVisible (meters);

    int tabIndex = 0;
    for (auto* t : { &tabPedals, &tabAmp, &tabCab, &tabFx })
    {
        t->setRadioGroupId (77);
        t->setClickingTogglesState (true);
        const int page = tabIndex++;
        t->onClick = [this, page] { setPage (page); };
        addAndMakeVisible (*t);
    }

    for (auto* s : { &inputSection, &outputSection, &gateSection, &compSection, &driveSection, &boostSection, &ampSection, &geqSection,
                     &cabSection, &micASection, &micBSection, &eqSection, &modSection, &dlySection, &dblSection, &revSection })
        addChildComponent (*s);
    inputSection.setVisible (true);
    outputSection.setVisible (true);

    // ---- Input / output ----------------------------------------------------------------------
    inputKnobs = { knob (inputSection, ParamID::inGain, "Gain", Colours::accent, true) };
    inputKnobs[0]->slider.setTooltip ("Input gain: aim for DI peaks around -12 to -6 dBFS (watch the IN meter)");
    outputKnobs = { knob (outputSection, ParamID::outGain, "Gain", Colours::accent, true),
                    knob (outputSection, ParamID::outWidth, "Width", Colours::accent),
                    knob (outputSection, ParamID::limCeiling, "Ceiling", Colours::danger) };
    limitPill = pill (outputSection, ParamID::limOn, "LIMIT", Colours::danger);

    // ---- Pedals -----------------------------------------------------------------------------------
    const auto cg = pedalColour (PedalId::gate), cc = pedalColour (PedalId::comp), cd = pedalColour (PedalId::drive), cb = pedalColour (PedalId::boost);
    gateKnobs = { knob (gateSection, ParamID::gateThreshold, "Threshold", Colours::threshold), knob (gateSection, ParamID::gateRelease, "Release", cg) };
    gateTrack = pill (gateSection, ParamID::gateTrack, "TRACK AMP", cg);
    gateTrack->setTooltip ("Tracking gate: the gate listens to the clean input but also closes after the amp, removing its hiss");
    compKnobs = { knob (compSection, ParamID::compSustain, "Sustain", cc), knob (compSection, ParamID::compAttack, "Attack", cc),
                  knob (compSection, ParamID::compLevel, "Level", cc, true), knob (compSection, ParamID::compBlend, "Blend", cc) };
    driveType = choice (driveSection, ParamID::drvType);
    driveType->box.setTooltip ("Screamer: mid-hump overdrive, the classic metal boost. Distortion: op-amp hard clipping. "
                               "Fuzz: saturated wall of fuzz. Bass Drive: distorts the highs, keeps the lows clean");
    driveKnobs = { knob (driveSection, ParamID::drvDrive, "Drive", cd), knob (driveSection, ParamID::drvTone, "Tone", cd),
                   knob (driveSection, ParamID::drvLevel, "Level", cd, true), knob (driveSection, ParamID::drvBlend, "Blend", cd) };
    boostKnobs = { knob (boostSection, ParamID::bstGain, "Gain", cb), knob (boostSection, ParamID::bstTight, "Tight", cb),
                   knob (boostSection, ParamID::bstTone, "Tone", cb, true) };
    boostKnobs[1]->slider.setTooltip ("Cuts the low end before the amp so palm mutes stay tight");

    // ---- Amp head -------------------------------------------------------------------------------------
    const auto ca = Colours::threshold;
    ampModel = choice (ampSection, ParamID::ampModel);
    ampKnobs = { knob (ampSection, ParamID::ampGain, "Gain", Colours::danger), knob (ampSection, ParamID::ampBass, "Bass", ca),
                 knob (ampSection, ParamID::ampMid, "Middle", ca), knob (ampSection, ParamID::ampTreble, "Treble", ca),
                 knob (ampSection, ParamID::ampPresence, "Presence", ca), knob (ampSection, ParamID::ampDepth, "Resonance", ca),
                 knob (ampSection, ParamID::ampMaster, "Master", Colours::danger), knob (ampSection, ParamID::ampLevel, "Output", Colours::accent, true) };
    ampKnobs[6]->slider.setTooltip ("Master: how hard the power amp is driven (power tube saturation and sag)");
    ampKnobs[5]->slider.setTooltip ("Resonance / depth: low end bump from the power amp's feedback loop");
    ampKnobs2 = { knob (ampSection, ParamID::ampSag, "Sag", ca), knob (ampSection, ParamID::ampBlend, "Clean Blend", Colours::accent) };
    ampKnobs2[0]->slider.setTooltip ("Power supply sag: compression and bloom when you dig in");
    ampKnobs2[1]->slider.setTooltip ("Blends in the clean low end (below ~180 Hz) under the amp: keeps bass tight and full when distorted");
    ampBright = pill (ampSection, ParamID::ampBright, "BRIGHT", ca);
    ultraLo = pill (ampSection, ParamID::ampUltraLo, "ULTRA LO", ca);
    ultraHi = pill (ampSection, ParamID::ampUltraHi, "ULTRA HI", ca);
    ampMidFreq = choice (ampSection, ParamID::ampMidFreq, "Mid Freq");
    const char* geqIds[] = { ParamID::geq1, ParamID::geq2, ParamID::geq3, ParamID::geq4, ParamID::geq5 };
    const char* geqNames[] = { "80", "240", "750", "2.2k", "6.6k" };
    for (int b = 0; b < kGeqBands; ++b)
    {
        auto f = std::make_unique<Fader> (state, geqIds[b], geqNames[b], ca);
        geqSection.addAndMakeVisible (*f);
        geqFaders.push_back (f.get());
        owned.push_back (std::move (f));
    }

    // ---- Cab ---------------------------------------------------------------------------------------------
    cabModel = choice (cabSection, ParamID::cabModel);
    cabKnobs = { knob (cabSection, ParamID::cabLowCut, "Low Cut", Colours::accent), knob (cabSection, ParamID::cabHighCut, "High Cut", Colours::accent),
                 knob (cabSection, ParamID::micMix, "Mic Blend", Colours::learn) };
    cabKnobs[2]->slider.setTooltip ("Blend between mic A and mic B (when mic B is on)");
    micASource = choice (micASection, ParamID::micASource);
    micAType = choice (micASection, ParamID::micAType);
    micAKnobs = { knob (micASection, ParamID::micAPos, "Position", Colours::accent), knob (micASection, ParamID::micADist, "Distance", Colours::accent) };
    micBSource = choice (micBSection, ParamID::micBSource);
    micBType = choice (micBSection, ParamID::micBType);
    micBKnobs = { knob (micBSection, ParamID::micBPos, "Position", Colours::learn), knob (micBSection, ParamID::micBDist, "Distance", Colours::learn) };
    micBInvert = pill (micBSection, ParamID::micBInvert, juce::String::charToString (0x00d8), Colours::learn);
    micBInvert->setTooltip ("Flip mic B's polarity (try it when the two mics sound thin together)");
    irA = std::make_unique<IrLoader> (processor, 0);
    irB = std::make_unique<IrLoader> (processor, 1);
    irA->onMessage = irB->onMessage = [this] (const juce::String& m) { showToast (m); };
    micASection.addAndMakeVisible (*irA);
    micBSection.addAndMakeVisible (*irB);
    for (int b = 0; b < kEqBands; ++b)
    {
        const bool shelf = b == 0 || b == kEqBands - 1;
        eqKnobs[(size_t) b] = { knob (eqSection, eqIds[b][0], "Freq", Colours::threshold), knob (eqSection, eqIds[b][1], "Gain", Colours::threshold, true),
                                knob (eqSection, eqIds[b][2], shelf ? "Slope" : "Q", Colours::threshold) };
        eqBandButtons[(size_t) b] = std::make_unique<PillButton> (eqBandNames[b], Colours::threshold);
        eqBandButtons[(size_t) b]->setClickingTogglesState (false);
        eqBandButtons[(size_t) b]->onClick = [this, b] { selectEqBand (b); };
        eqSection.addAndMakeVisible (*eqBandButtons[(size_t) b]);
    }

    // ---- Effects ------------------------------------------------------------------------------------------
    modMode = choice (modSection, ParamID::modMode);
    modKnobs = { knob (modSection, ParamID::modRate, "Rate", Colours::blue), knob (modSection, ParamID::modDepth, "Depth", Colours::blue),
                 knob (modSection, ParamID::modFeedback, "Feedback", Colours::blue), knob (modSection, ParamID::modMix, "Mix", Colours::blue) };
    dlyMode = choice (dlySection, ParamID::dlyMode);
    dlySync = choice (dlySection, ParamID::dlySync);
    dlySync->box.setTooltip ("Sync the delay to the host tempo (Free = use the Time knob)");
    const auto cy = Colours::detection;
    dlyKnobs = { knob (dlySection, ParamID::dlyTime, "Time", cy), knob (dlySection, ParamID::dlyFeedback, "Feedback", cy),
                 knob (dlySection, ParamID::dlyHp, "Low Cut", cy), knob (dlySection, ParamID::dlyLp, "High Cut", cy),
                 knob (dlySection, ParamID::dlyDuck, "Duck", cy), knob (dlySection, ParamID::dlyWidth, "Width", cy),
                 knob (dlySection, ParamID::dlyMix, "Level", cy) };
    dlyTimeKnob = dlyKnobs[0];
    dlyKnobs[4]->slider.setTooltip ("Ducking: echoes get quieter while you play and bloom in the gaps");
    dblKnobs = { knob (dblSection, ParamID::dblAmount, "Amount", Colours::learn), knob (dblSection, ParamID::dblDetune, "Detune", Colours::learn),
                 knob (dblSection, ParamID::dblDelay, "Delay", Colours::learn), knob (dblSection, ParamID::dblWidth, "Width", Colours::learn),
                 knob (dblSection, ParamID::dblDrift, "Humanize", Colours::learn) };
    dblKnobs[0]->slider.setTooltip ("Doubler: fakes a second, double-tracked take panned wide (the classic rhythm wall)");
    revType = choice (revSection, ParamID::revType);
    revKnobs = { knob (revSection, ParamID::revDecay, "Decay", Colours::accent), knob (revSection, ParamID::revPredelay, "Pre-Delay", Colours::accent),
                 knob (revSection, ParamID::revSize, "Size", Colours::accent), knob (revSection, ParamID::revDamp, "Damping", Colours::accent),
                 knob (revSection, ParamID::revLowCut, "Low Cut", Colours::accent), knob (revSection, ParamID::revDuck, "Duck", Colours::accent),
                 knob (revSection, ParamID::revWidth, "Width", Colours::accent), knob (revSection, ParamID::revMix, "Level", Colours::accent) };

    sectionByKey = { { "input", &inputSection }, { "output", &outputSection }, { "p0", &gateSection }, { "p1", &compSection },
                     { "p2", &driveSection }, { "p3", &boostSection }, { "amp", &ampSection }, { "geq", &geqSection },
                     { "cab", &cabSection }, { "eq", &eqSection }, { "mod", &modSection }, { "delay", &dlySection },
                     { "doubler", &dblSection }, { "reverb", &revSection } };

    // Dim the controls of switched-off sections
    for (auto* s : { &gateSection, &compSection, &driveSection, &boostSection, &ampSection, &geqSection, &cabSection, &micBSection,
                     &eqSection, &modSection, &dlySection, &dblSection, &revSection })
    {
        s->onActiveChanged = [s]
        {
            for (auto* c : s->getChildren())
                if (dynamic_cast<Knob*> (c) != nullptr || dynamic_cast<Choice*> (c) != nullptr || dynamic_cast<Fader*> (c) != nullptr
                    || dynamic_cast<IrLoader*> (c) != nullptr)
                    c->setAlpha (s->isActive() ? 1.0f : 0.5f);
        };
        s->onActiveChanged();
    }

    selectEqBand (2);
    setPage (0);
    setSize (baseWidth, baseHeight);
}

Knob* MainPanel::knob (Section& s, const char* id, const juce::String& name, juce::Colour c, bool bipolar)
{
    auto k = std::make_unique<Knob> (state, id, name, c, bipolar);
    auto* raw = k.get();
    s.addAndMakeVisible (*raw);
    owned.push_back (std::move (k));
    return raw;
}

Choice* MainPanel::choice (Section& s, const char* id, const juce::String& caption)
{
    auto c = std::make_unique<Choice> (state, id, caption);
    auto* raw = c.get();
    s.addAndMakeVisible (*raw);
    owned.push_back (std::move (c));
    return raw;
}

ParamPill* MainPanel::pill (Section& s, const char* id, const juce::String& text, juce::Colour c)
{
    auto p = std::make_unique<ParamPill> (state, id, text, c);
    auto* raw = p.get();
    s.addAndMakeVisible (*raw);
    owned.push_back (std::move (p));
    return raw;
}

void MainPanel::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    juce::String lastCategory;
    const auto& presets = getFactoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].category != lastCategory)
        {
            presetBox.addSectionHeading (presets[(size_t) i].category);
            lastCategory = presets[(size_t) i].category;
        }
        presetBox.addItem (presets[(size_t) i].name, i + 1);
    }

    numUserPresets = 0;
    auto files = AmpSuiteProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.laas");
    files.sort();
    if (! files.isEmpty())
    {
        presetBox.addSectionHeading ("User presets");
        for (auto& f : files)
        {
            if (numUserPresets >= 256) break;
            userPresetFiles[numUserPresets] = f;
            presetBox.addItem (f.getFileNameWithoutExtension(), 1000 + numUserPresets);
            ++numUserPresets;
        }
    }
    presetBox.setText (processor.getCurrentPresetName(), juce::dontSendNotification);
}

void MainPanel::showToast (const juce::String& text)
{
    toastText = text;
    toastTicks = 60 * 6;
    repaint (0, 0, baseWidth, 46);
}

void MainPanel::selectEqBand (int band)
{
    eqBand = juce::jlimit (0, kEqBands - 1, band);
    for (int b = 0; b < kEqBands; ++b)
    {
        eqBandButtons[(size_t) b]->setToggleState (b == eqBand, juce::dontSendNotification);
        for (auto* k : eqKnobs[(size_t) b])
            k->setVisible (b == eqBand);
    }
    response.setSelectedBand (eqBand);
}

void MainPanel::setPage (int page)
{
    currentPage = juce::jlimit (0, 3, page);
    tabPedals.setToggleState (currentPage == 0, juce::dontSendNotification);
    tabAmp.setToggleState (currentPage == 1, juce::dontSendNotification);
    tabCab.setToggleState (currentPage == 2, juce::dontSendNotification);
    tabFx.setToggleState (currentPage == 3, juce::dontSendNotification);
    for (auto* s : { &gateSection, &compSection, &driveSection, &boostSection })
        s->setVisible (currentPage == 0);
    for (auto* s : { &ampSection, &geqSection })
        s->setVisible (currentPage == 1);
    for (auto* s : { &cabSection, &micASection, &micBSection, &eqSection })
        s->setVisible (currentPage == 2);
    for (auto* s : { &modSection, &dlySection, &dblSection, &revSection })
        s->setVisible (currentPage == 3);
}

void MainPanel::focusSection (const juce::String& key)
{
    highlightKey = key;
    highlightAmount = 1.0f;
}

void MainPanel::updateModelControls()
{
    const int m = juce::roundToInt (state.getRawParameterValue (ParamID::ampModel)->load());
    if (m == shownModel)
        return;
    shownModel = m;
    const bool svt = (AmpModel) m == AmpModel::svt;
    for (juce::Component* c : { (juce::Component*) ultraLo, (juce::Component*) ultraHi, (juce::Component*) ampMidFreq })
    {
        c->setEnabled (svt);
        c->setAlpha (svt ? 1.0f : 0.35f);
    }
    const auto& spec = ampModelSpec ((AmpModel) m);
    ampModel->box.setTooltip (juce::String (spec.inspiredBy) + " (" + spec.powerTubeName + "): " + spec.description);
}

//==============================================================================
void MainPanel::resized()
{
    presetBox.setBounds (300, 11, 250, 26);
    saveButton.setBounds (556, 11, 56, 26);
    tunerButton.setBounds (622, 11, 76, 26);

    chain.setBounds (kMargin, 54, baseWidth - 2 * kMargin, 50);
    speaker.setBounds (kMargin, kDispY, 330, kDispH);
    response.setBounds (350, kDispY, 600, kDispH);
    tunerPanel.setBounds (958, kDispY, 396, kDispH);
    meters.setBounds (1362, kDispY + 4, 66, kDispH - 8);

    const int tabsY = kDispY + kDispH + kGap;
    const int pageX = kMargin + kSideW + kGap;
    tabPedals.setBounds (pageX, tabsY, 110, 24);
    tabAmp.setBounds (pageX + 116, tabsY, 90, 24);
    tabCab.setBounds (pageX + 212, tabsY, 120, 24);
    tabFx.setBounds (pageX + 338, tabsY, 100, 24);

    inputSection.setBounds (kMargin, tabsY, kSideW, baseHeight - tabsY - kMargin);
    outputSection.setBounds (baseWidth - kMargin - kOutW, tabsY, kOutW, baseHeight - tabsY - kMargin);
    {
        auto c = inputSection.getContentArea();
        inputKnobs[0]->setBounds (c.removeFromTop (c.getHeight() / 2));
    }
    {
        limitPill->setBounds (outputSection.getWidth() - 58, 5, 50, 19);
        auto c = outputSection.getContentArea().withTrimmedTop (6);
        gridVec (comps (outputKnobs), 1, c);
    }
    layoutPages();
}

void MainPanel::layoutPedals (juce::Rectangle<int> area)
{
    shownOrder = processor.getPedalOrder();
    Section* sections[kNumPedals] = { &gateSection, &compSection, &driveSection, &boostSection };
    const int w = (area.getWidth() - kGap * (kNumPedals - 1)) / kNumPedals;
    int x = area.getX();
    for (int i = 0; i < kNumPedals; ++i)
    {
        const int pd = shownOrder[(size_t) i];
        const int width = i == kNumPedals - 1 ? area.getRight() - x : w;
        sections[pd]->setBounds (x, area.getY(), width, area.getHeight());
        x += width + kGap;
    }
    {
        auto c = gateSection.getContentArea();
        auto bottom = c.removeFromBottom (c.getHeight() / 2);
        gridVec (comps (gateKnobs), 2, c);
        gateTrack->setBounds (bottom.withSizeKeepingCentre (std::min (130, bottom.getWidth() - 16), 24));
    }
    gridVec (comps (compKnobs), 2, compSection.getContentArea());
    {
        auto c = driveSection.getContentArea();
        driveType->setBounds (c.removeFromTop (26));
        c.removeFromTop (4);
        gridVec (comps (driveKnobs), 2, c);
    }
    gridVec (comps (boostKnobs), 2, boostSection.getContentArea());
}

void MainPanel::layoutPages()
{
    const int tabsY = kDispY + kDispH + kGap;
    const int botY = tabsY + 24 + kGap;
    const auto area = juce::Rectangle<int> (kMargin + kSideW + kGap, botY, baseWidth - 2 * kMargin - kSideW - kOutW - 2 * kGap,
                                            baseHeight - botY - kMargin);

    layoutPedals (area);

    // ---- Amp page ----
    {
        const int ampW = 790;
        ampSection.setBounds (area.withWidth (ampW));
        geqSection.setBounds (area.withTrimmedLeft (ampW + kGap));
        auto c = ampSection.getContentArea();
        auto top = c.removeFromTop (26);
        ampModel->setBounds (top.removeFromLeft (230));
        top.removeFromLeft (10);
        ampBright->setBounds (top.removeFromLeft (80));
        c.removeFromTop (6);
        auto row1 = c.removeFromTop (c.getHeight() / 2);
        gridVec (comps (ampKnobs), 8, row1);
        const int cw = c.getWidth() / 8;
        ampKnobs2[0]->setBounds (c.getX(), c.getY(), cw, c.getHeight());
        ampKnobs2[1]->setBounds (c.getX() + cw, c.getY(), cw, c.getHeight());
        auto svtArea = juce::Rectangle<int> (c.getX() + cw * 3, c.getY() + 14, cw * 4, c.getHeight() - 28);
        auto svtTop = svtArea.removeFromTop (svtArea.getHeight() / 2);
        ampMidFreq->setBounds (svtTop.withSizeKeepingCentre (std::min (200, svtTop.getWidth()), 44));
        auto pills = svtArea.withSizeKeepingCentre (220, 24);
        ultraLo->setBounds (pills.removeFromLeft (105));
        pills.removeFromLeft (10);
        ultraHi->setBounds (pills);

        auto g = geqSection.getContentArea().reduced (6, 4);
        gridVec (comps (geqFaders), kGeqBands, g);
    }

    // ---- Cab page ----
    {
        const int cabW = 250, micW = 270;
        cabSection.setBounds (area.withWidth (cabW));
        micASection.setBounds (area.withTrimmedLeft (cabW + kGap).withWidth (micW));
        micBSection.setBounds (area.withTrimmedLeft (cabW + micW + 2 * kGap).withWidth (micW));
        eqSection.setBounds (area.withTrimmedLeft (cabW + 2 * micW + 3 * kGap));
        {
            auto c = cabSection.getContentArea();
            cabModel->setBounds (c.removeFromTop (26));
            c.removeFromTop (4);
            gridVec ({ cabKnobs[0], cabKnobs[1], cabKnobs[2] }, 2, c);
        }
        auto layoutMic = [] (Section& s, Choice* src, Choice* type, std::vector<Knob*>& k, IrLoader& ir)
        {
            auto c = s.getContentArea();
            auto top = c.removeFromTop (26);
            src->setBounds (top.removeFromLeft (top.getWidth() / 2 - 3));
            top.removeFromLeft (6);
            type->setBounds (top);
            c.removeFromTop (4);
            auto irArea = c.removeFromBottom (48);
            gridVec (comps (k), 2, c);
            ir.setBounds (irArea.reduced (10, 2));
        };
        layoutMic (micASection, micASource, micAType, micAKnobs, *irA);
        layoutMic (micBSection, micBSource, micBType, micBKnobs, *irB);
        micBInvert->setBounds (micBSection.getWidth() - 62, 5, 30, 19);
        {
            auto c = eqSection.getContentArea();
            auto row = c.removeFromTop (22);
            const int bw = row.getWidth() / kEqBands;
            for (int b = 0; b < kEqBands; ++b)
                eqBandButtons[(size_t) b]->setBounds (row.removeFromLeft (bw).reduced (1, 0));
            c.removeFromTop (8);
            const auto knobRow = c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 120));
            for (auto& band : eqKnobs)
                gridVec ({ band[0], band[1], band[2] }, 3, knobRow);
        }
    }

    // ---- Effects page ----
    {
        const float cols[] = { 2.0f, 4.0f, 3.0f, 4.0f };
        const float pad = 14.0f;
        const float cw = ((float) area.getWidth() - 3 * kGap - 4 * pad) / 13.0f;
        int x = area.getX();
        Section* fx[] = { &modSection, &dlySection, &dblSection, &revSection };
        for (int i = 0; i < 4; ++i)
        {
            const int w = i == 3 ? area.getRight() - x : juce::roundToInt (cols[i] * cw + pad);
            fx[i]->setBounds (x, area.getY(), w, area.getHeight());
            x += w + kGap;
        }
        {
            auto c = modSection.getContentArea();
            modMode->setBounds (c.removeFromTop (26));
            c.removeFromTop (4);
            gridVec (comps (modKnobs), 2, c);
        }
        {
            auto c = dlySection.getContentArea();
            auto top = c.removeFromTop (26);
            dlyMode->setBounds (top.removeFromLeft (top.getWidth() / 2 - 3));
            top.removeFromLeft (6);
            dlySync->setBounds (top);
            c.removeFromTop (4);
            gridVec (comps (dlyKnobs), 4, c);
        }
        {
            auto c = dblSection.getContentArea();
            c.removeFromTop (30);
            gridVec (comps (dblKnobs), 3, c);
        }
        {
            auto c = revSection.getContentArea();
            revType->setBounds (c.removeFromTop (26).withTrimmedRight (c.getWidth() / 2));
            c.removeFromTop (4);
            gridVec (comps (revKnobs), 4, c);
        }
    }
}

void MainPanel::paint (juce::Graphics& g)
{
    Hardware::drawChassis (g, { 0, 0, baseWidth, baseHeight }, 46);

    // Logo: a clipped waveform
    {
        juce::Path wave;
        const float x0 = 14.0f, y0 = 23.0f;
        wave.startNewSubPath (x0, y0);
        wave.lineTo (x0 + 3.0f, y0 - 8.0f);
        wave.lineTo (x0 + 9.0f, y0 - 8.0f);
        wave.lineTo (x0 + 13.0f, y0 + 8.0f);
        wave.lineTo (x0 + 19.0f, y0 + 8.0f);
        wave.lineTo (x0 + 22.0f, y0);
        wave.lineTo (x0 + 26.0f, y0);
        g.setColour (Colours::signature);
        g.strokePath (wave, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Colours::danger);
        g.fillEllipse (x0 + 24.0f, y0 - 10.0f, 6.0f, 6.0f);

        g.setFont (uiFont (21.0f, true));
        g.setColour (Colours::text);
        g.drawText ("LA", 48, 8, 30, 30, juce::Justification::centredLeft);
        g.setColour (Colours::signature);
        g.drawText ("AMPSUITE", 76, 8, 120, 30, juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (uiFont (10.5f));
        g.drawText ("AMP  /  CAB  /  FX", 186, 15, 100, 16, juce::Justification::centredLeft);
    }

    g.setFont (uiFont (11.5f));
    if (toastTicks > 0)
    {
        g.setColour (Colours::learn);
        g.drawFittedText (toastText, baseWidth - 640, 6, 628, 34, juce::Justification::centredRight, 2);
    }
    else
    {
        g.setColour (Colours::textDim);
        g.drawText (infoText, baseWidth - 640, 10, 628, 26, juce::Justification::centredRight);
    }

    g.setFont (uiFont (10.5f));
    g.setColour (Colours::textDim);
    const juce::String hints[] = { "Pedals run in the order of the chain strip - drag the pedal tiles to reorder",
                                   "Knobs go 0-10 like on the amp. The graphic EQ sits in the effects loop",
                                   "Drag the mics on the speaker. Load your own IRs per mic slot",
                                   "Modulation and delay are in the loop; doubler and reverb come after the cab" };
    g.drawText (hints[currentPage], tabFx.getRight() + 16, tabFx.getY(), 600, 24, juce::Justification::centredLeft);
}

void MainPanel::tick()
{
    response.pullSpectrum();
    tunerPanel.tick();
    chain.tick();
    speaker.tick();
    irA->refresh();
    irB->refresh();
    updateModelControls();

    const float inPeak = processor.meters.inPeak.exchange (0.0f);
    const float outPeak = processor.meters.outPeak.exchange (0.0f);
    meters.setLevels (inPeak, outPeak, processor.meters.comp.load() + processor.meters.limit.load());

    if (processor.getPedalOrder() != shownOrder)
        layoutPages();

    tunerButton.setToggleState (processor.isTunerMuted(), juce::dontSendNotification);

    const bool synced = state.getRawParameterValue (ParamID::dlySync)->load() > 0.5f;
    dlyTimeKnob->setEnabled (! synced);
    dlyTimeKnob->setAlpha (synced ? 0.4f : (dlySection.isActive() ? 1.0f : 0.5f));

    if (highlightAmount > 0.0f)
    {
        highlightAmount = juce::jmax (0.0f, highlightAmount - 0.02f);
        if (auto it = sectionByKey.find (highlightKey); it != sectionByKey.end())
            it->second->setHighlight (highlightAmount);
    }

    if (toastTicks > 0 && --toastTicks == 0)
        repaint (0, 0, baseWidth, 46);

    const double sr = processor.getEngineSampleRate();
    juce::String info;
    info << "Latency " << juce::String (1000.0 * processor.getLatencySamples() / juce::jmax (1.0, sr), 2) << " ms    "
         << juce::String (sr / 1000.0, 1) << " kHz    4x oversampled amp";
    if (info != infoText)
    {
        infoText = info;
        repaint (0, 0, baseWidth, 46);
    }

    const auto name = processor.getCurrentPresetName();
    if (presetBox.getText() != name && ! presetBox.isPopupActive())
        presetBox.setText (name, juce::dontSendNotification);
}

//==============================================================================
AmpSuiteEditor::AmpSuiteEditor (AmpSuiteProcessor& p)
    : AudioProcessorEditor (p), panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (MainPanel::baseWidth * 5 / 10, MainPanel::baseHeight * 5 / 10, MainPanel::baseWidth * 2, MainPanel::baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainPanel::baseWidth / MainPanel::baseHeight);
    setSize (MainPanel::baseWidth * 9 / 10, MainPanel::baseHeight * 9 / 10);

    startTimerHz (60);
}

AmpSuiteEditor::~AmpSuiteEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AmpSuiteEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void AmpSuiteEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainPanel::baseWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setBounds (0, 0, MainPanel::baseWidth, MainPanel::baseHeight);
}
} // namespace laas
