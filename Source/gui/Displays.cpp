#include "Displays.h"
#include "../PluginProcessor.h"

namespace laas
{
namespace
{
void drawLegend (juce::Graphics& g, juce::Rectangle<int> area, std::initializer_list<std::pair<juce::String, juce::Colour>> items)
{
    g.setFont (uiFont (10.5f, true));
    int x = area.getRight();
    std::vector<std::pair<juce::String, juce::Colour>> list (items);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), it->first) + 26;
        x -= w;
        g.setColour (it->second);
        g.fillRoundedRectangle ((float) x, (float) area.getCentreY() - 2.0f, 12.0f, 4.0f, 2.0f);
        g.setColour (Colours::textDim);
        g.drawText (it->first, x + 16, area.getY(), w - 16, area.getHeight(), juce::Justification::centredLeft);
    }
}

void drawPanelBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (Colours::graphBg);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (Colours::edge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

float param01 (juce::AudioProcessorValueTreeState& s, const char* id) { return s.getRawParameterValue (id)->load(); }
} // namespace

juce::Colour pedalColour (PedalId p)
{
    switch (p)
    {
        case PedalId::gate:  return Colours::accent;
        case PedalId::comp:  return Colours::blue;
        case PedalId::drive: return Colours::detection;
        case PedalId::boost: return Colours::danger;
        case PedalId::numPedals: break;
    }
    return Colours::accent;
}

//==============================================================================
SpeakerView::SpeakerView (AmpSuiteProcessor& p) : processor (p), state (p.getState())
{
    setTooltip ("Drag a mic across the speaker (centre = bright, edge = dark). Mouse wheel or the rail on the right sets the distance");
}

juce::RangedAudioParameter* SpeakerView::param (int mic, int which) const
{
    static const char* ids[2][2] = { { ParamID::micAPos, ParamID::micADist }, { ParamID::micBPos, ParamID::micBDist } };
    return state.getParameter (ids[mic][which]);
}

bool SpeakerView::micActive (int mic) const
{
    return mic == 0 || param01 (state, ParamID::micBOn) > 0.5f;
}

SpeakerView::Geometry SpeakerView::geometry() const
{
    Geometry g;
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    r.removeFromTop (20.0f);
    g.rail = r.removeFromRight (40.0f).reduced (6.0f, 14.0f);
    r.removeFromBottom (22.0f);
    g.radius = std::min (r.getWidth(), r.getHeight()) * 0.5f - 6.0f;
    g.centre = r.getCentre();
    return g;
}

juce::Point<float> SpeakerView::micPoint (int mic, const Geometry& g) const
{
    const auto* p = param (mic, 0);
    const float pos = p->convertFrom0to1 (p->getValue()) * 0.01f;
    const float angle = mic == 0 ? -0.22f : 0.22f;
    return g.centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (pos * g.radius * 0.9f + 4.0f);
}

float SpeakerView::railY (float distCm, const Geometry& g) const
{
    const float t = std::log (juce::jlimit (1.0f, 100.0f, distCm)) / std::log (100.0f);
    return g.rail.getY() + t * g.rail.getHeight();
}

int SpeakerView::hitMic (juce::Point<float> p, bool& onRail) const
{
    const auto g = geometry();
    for (int m = 1; m >= 0; --m)
    {
        if (! micActive (m))
            continue;
        if (micPoint (m, g).getDistanceFrom (p) < 12.0f)
        {
            onRail = false;
            return m;
        }
        const auto* d = param (m, 1);
        const float x = g.rail.getCentreX() + (m == 0 ? -7.0f : 7.0f);
        if (juce::Point<float> (x, railY (d->convertFrom0to1 (d->getValue()), g)).getDistanceFrom (p) < 10.0f)
        {
            onRail = true;
            return m;
        }
    }
    return -1;
}

void SpeakerView::mouseMove (const juce::MouseEvent& e)
{
    bool rail = false;
    const int h = hitMic (e.position, rail);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpeakerView::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitMic (e.position, dragRail);
    if (dragging >= 0)
        param (dragging, dragRail ? 1 : 0)->beginChangeGesture();
}

void SpeakerView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto g = geometry();
    if (dragRail)
    {
        auto* d = param (dragging, 1);
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.y - g.rail.getY()) / g.rail.getHeight());
        d->setValueNotifyingHost (d->convertTo0to1 (std::pow (100.0f, t)));
    }
    else
    {
        auto* p = param (dragging, 0);
        const float r = (e.position.getDistanceFrom (g.centre) - 4.0f) / (g.radius * 0.9f);
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 100.0f, r * 100.0f)));
    }
    repaint();
}

void SpeakerView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        param (dragging, dragRail ? 1 : 0)->endChangeGesture();
    dragging = -1;
}

void SpeakerView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    bool rail = false;
    int m = hitMic (e.position, rail);
    if (m < 0)
        m = 0;
    auto* d = param (m, 1);
    const float v = d->convertFrom0to1 (d->getValue()) * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f);
    d->beginChangeGesture();
    d->setValueNotifyingHost (d->convertTo0to1 (juce::jlimit (1.0f, 100.0f, v)));
    d->endChangeGesture();
}

void SpeakerView::paint (juce::Graphics& g)
{
    drawPanelBackground (g, getLocalBounds().toFloat());
    const auto geo = geometry();
    const bool cabOn = param01 (state, ParamID::cabOn) > 0.5f;
    const int cab = juce::roundToInt (param01 (state, ParamID::cabModel));

    g.setFont (uiFont (11.0f, true));
    g.setColour (Colours::text);
    g.drawText ("CAB / MICS", 10, 3, 120, 18, juce::Justification::centredLeft);
    g.setColour (cabOn ? Colours::accent : Colours::textDim);
    g.drawText (cabOn ? juce::String (cabModelName (cab)).toUpperCase() : juce::String ("CAB OFF (DI)"), 100, 3,
                getWidth() - 110, 18, juce::Justification::centredRight);

    // Baffle + grille
    const auto c = geo.centre;
    const float R = geo.radius;
    auto baffle = juce::Rectangle<float> (R * 2.0f + 20.0f, R * 2.0f + 20.0f).withCentre (c);
    g.setColour (Colours::panel.darker (0.4f));
    g.fillRoundedRectangle (baffle, 6.0f);
    g.setColour (Colours::edge.withAlpha (0.5f));
    for (float y = baffle.getY() + 3.0f; y < baffle.getBottom(); y += 4.0f)
        g.drawHorizontalLine ((int) y, baffle.getX() + 2.0f, baffle.getRight() - 2.0f);

    // Surround, cone, ridges, dust cap
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
    const float coneR = R * 0.9f;
    juce::ColourGradient cone (juce::Colour (0xff2c3038), c.x, c.y, juce::Colour (0xff111317), c.x + coneR, c.y + coneR, true);
    g.setGradientFill (cone);
    g.fillEllipse (juce::Rectangle<float> (coneR * 2.0f, coneR * 2.0f).withCentre (c));
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (float k : { 0.45f, 0.6f, 0.75f })
        g.drawEllipse (juce::Rectangle<float> (coneR * 2.0f * k, coneR * 2.0f * k).withCentre (c), 1.0f);
    const float capR = coneR * 0.3f;
    juce::ColourGradient capGrad (juce::Colour (0xff474c57), c.x - capR * 0.4f, c.y - capR * 0.4f, juce::Colour (0xff1c1f25), c.x + capR, c.y + capR, true);
    g.setGradientFill (capGrad);
    g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
    g.setColour (Colours::edge);
    g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c), 1.0f);
    g.drawEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), 1.0f);

    // Distance rail
    const auto rail = geo.rail;
    g.setColour (Colours::grid);
    g.fillRoundedRectangle (rail.withSizeKeepingCentre (3.0f, rail.getHeight()), 1.5f);
    g.setFont (uiFont (8.5f));
    for (float d : { 1.0f, 3.0f, 10.0f, 30.0f, 100.0f })
    {
        const float y = railY (d, geo);
        g.setColour (Colours::textDim);
        g.drawHorizontalLine ((int) y, rail.getCentreX() - 3.0f, rail.getCentreX() + 3.0f);
    }
    g.drawText ("1cm", (int) rail.getX() - 6, (int) rail.getY() - 13, (int) rail.getWidth() + 12, 10, juce::Justification::centred);
    g.drawText ("1m", (int) rail.getX() - 6, (int) rail.getBottom() + 3, (int) rail.getWidth() + 12, 10, juce::Justification::centred);

    // Microphones
    const char* srcIds[2] = { ParamID::micASource, ParamID::micBSource };
    const char* typeIds[2] = { ParamID::micAType, ParamID::micBType };
    for (int m = 0; m < 2; ++m)
    {
        const bool active = micActive (m) && cabOn;
        const auto colour = (m == 0 ? Colours::accent : Colours::learn).withAlpha (active ? 1.0f : 0.3f);
        const bool userIR = param01 (state, srcIds[m]) > 0.5f && processor.getImpulseName (m).isNotEmpty();
        const auto* dp = param (m, 1);
        const float dist = dp->convertFrom0to1 (dp->getValue());
        const auto pt = micPoint (m, geo);
        const float size = juce::jmap (std::log (dist) / std::log (100.0f), 18.0f, 8.0f);
        const bool hot = m == hover || m == dragging;

        if (! userIR)
        {
            g.setColour (colour.withAlpha (active ? 0.25f : 0.1f));
            g.fillEllipse (juce::Rectangle<float> (size + 8.0f, size + 8.0f).withCentre (pt));
            g.setColour (hot ? colour.brighter (0.4f) : colour);
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (pt));
            g.setColour (Colours::background);
            g.setFont (uiFont (9.0f, true));
            g.drawText (m == 0 ? "A" : "B", juce::Rectangle<float> (size, size).withCentre (pt), juce::Justification::centred);
        }
        const float rx = rail.getCentreX() + (m == 0 ? -7.0f : 7.0f);
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ rx, railY (dist, geo) }));

        // caption
        juce::String text = juce::String (m == 0 ? "A  " : "B  ");
        if (! micActive (m))
            text << "off";
        else if (userIR)
            text << "IR: " << processor.getImpulseName (m);
        else
        {
            const auto* pp = param (m, 0);
            text << micModelName (juce::roundToInt (param01 (state, typeIds[m]))) << "  " << pp->getCurrentValueAsText() << "  "
                 << dp->getCurrentValueAsText();
        }
        g.setFont (uiFont (10.0f, true));
        g.setColour (colour);
        const int y = getHeight() - 30 + m * 13;
        g.drawText (text, 12, y, getWidth() - 60, 13, juce::Justification::centredLeft);
    }
}

//==============================================================================
ResponseDisplay::ResponseDisplay (AmpSuiteProcessor& p) : processor (p), state (p.getState())
{
    fifo.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    window.resize ((size_t) fftSize);
    spectrumDb.assign ((size_t) fftSize / 2, -100.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
    cab.fill (0.0f);
    setOpaque (true);
    setTooltip ("Yellow: amp tone stack + graphic EQ. Teal: cabinet and mics. Orange: post EQ (drag the nodes, wheel = Q, double-click = reset)");
}

juce::RangedAudioParameter* ResponseDisplay::bandParam (int band, int which) const
{
    static const char* ids[5][3] = { { ParamID::eq1Freq, ParamID::eq1Gain, ParamID::eq1Q }, { ParamID::eq2Freq, ParamID::eq2Gain, ParamID::eq2Q },
                                     { ParamID::eq3Freq, ParamID::eq3Gain, ParamID::eq3Q }, { ParamID::eq4Freq, ParamID::eq4Gain, ParamID::eq4Q },
                                     { ParamID::eq5Freq, ParamID::eq5Gain, ParamID::eq5Q } };
    return state.getParameter (ids[band][which]);
}

void ResponseDisplay::pullSpectrum()
{
    float tmp[2048];
    int got = 0;
    while ((got = processor.spectrumFifo.pop (tmp, 2048)) > 0)
        for (int i = 0; i < got; ++i)
        {
            fifo[(size_t) fifoPos] = tmp[i];
            fifoPos = (fifoPos + 1) % fftSize;
        }
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = fifo[(size_t) ((fifoPos + i) % fftSize)] * window[(size_t) i];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] / (float) (fftSize / 4), -120.0f);
        auto& s = spectrumDb[(size_t) i];
        s = db > s ? s + (db - s) * 0.5f : s + (db - s) * 0.12f;
    }

    if (processor.getCabResponseVersion() != cabVersion)
    {
        cabVersion = processor.getCabResponseVersion();
        cab = processor.getCabResponse();
        // normalise: 0 dB = average level from 200 Hz to 4 kHz
        double sum = 0.0;
        int n = 0;
        for (int i = 0; i < kResponsePoints; ++i)
            if (responsePointHz (i) >= 200.0f && responsePointHz (i) <= 4000.0f)
            {
                sum += cab[(size_t) i];
                ++n;
            }
        const float off = n > 0 ? (float) (sum / n) : 0.0f;
        for (auto& v : cab) v -= off;
    }
    repaint();
}

juce::Rectangle<float> ResponseDisplay::plotArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (22.0f);
    r.removeFromBottom (18.0f);
    r.removeFromLeft (30.0f);
    r.removeFromRight (6.0f);
    return r;
}

float ResponseDisplay::freqToX (float f, juce::Rectangle<float> a) const
{
    return a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float ResponseDisplay::xToFreq (float x, juce::Rectangle<float> a) const
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float ResponseDisplay::dbToY (float db, juce::Rectangle<float> a) const
{
    return juce::jmap (db, maxDb, minDb, a.getY(), a.getBottom());
}

float ResponseDisplay::yToDb (float y, juce::Rectangle<float> a) const
{
    return juce::jmap (juce::jlimit (a.getY(), a.getBottom(), y), a.getY(), a.getBottom(), maxDb, minDb);
}

juce::Point<float> ResponseDisplay::nodePosition (int band, juce::Rectangle<float> a) const
{
    const auto* f = bandParam (band, 0);
    const auto* gn = bandParam (band, 1);
    return { freqToX (f->convertFrom0to1 (f->getValue()), a), dbToY (gn->convertFrom0to1 (gn->getValue()), a) };
}

int ResponseDisplay::hitNode (juce::Point<float> p) const
{
    const auto a = plotArea();
    for (int b = 0; b < kEqBands; ++b)
        if (nodePosition (b, a).getDistanceFrom (p) < 10.0f)
            return b;
    return -1;
}

void ResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitNode (e.position);
    if (dragging < 0)
        return;
    selected = dragging;
    if (onBandSelected)
        onBandSelected (selected);
    bandParam (dragging, 0)->beginChangeGesture();
    bandParam (dragging, 1)->beginChangeGesture();
    if (auto* on = state.getParameter (ParamID::eqOn); on != nullptr && on->getValue() < 0.5f)
        on->setValueNotifyingHost (1.0f);
}

void ResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto a = plotArea();
    auto* f = bandParam (dragging, 0);
    auto* gn = bandParam (dragging, 1);
    f->setValueNotifyingHost (f->convertTo0to1 (f->getNormalisableRange().snapToLegalValue (xToFreq (e.position.x, a))));
    gn->setValueNotifyingHost (gn->convertTo0to1 (juce::jlimit (-15.0f, 15.0f, yToDb (e.position.y, a))));
    repaint();
}

void ResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        bandParam (dragging, 0)->endChangeGesture();
        bandParam (dragging, 1)->endChangeGesture();
    }
    dragging = -1;
}

void ResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitNode (e.position);
    if (b < 0)
        return;
    auto* gn = bandParam (b, 1);
    gn->beginChangeGesture();
    gn->setValueNotifyingHost (gn->getDefaultValue());
    gn->endChangeGesture();
}

void ResponseDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = hitNode (e.position);
    if (b < 0) b = selected;
    auto* q = bandParam (b, 2);
    const float norm = juce::jlimit (0.0f, 1.0f, q->getValue() + (w.deltaY > 0 ? 0.04f : -0.04f));
    q->beginChangeGesture();
    q->setValueNotifyingHost (norm);
    q->endChangeGesture();
}

void ResponseDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (Colours::background);
    drawPanelBackground (g, bounds);

    const auto a = plotArea();
    const double sr = std::max (8000.0, processor.getEngineSampleRate());

    g.setFont (uiFont (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f, a);
        g.setColour (Colours::grid);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (Colours::textDim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x - 20,
                    (int) a.getBottom() + 3, 40, 12, juce::Justification::centred);
    }
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = dbToY (db, a);
        g.setColour (std::abs (db) < 0.1f ? Colours::edge : Colours::grid);
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        g.setColour (Colours::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), 0, (int) y - 6, 26, 12, juce::Justification::centredRight);
    }

    g.saveState();
    g.reduceClipRegion (a.toNearestInt());

    // Output spectrum
    {
        juce::Path sp;
        sp.startNewSubPath (a.getX(), a.getBottom());
        const int bins = fftSize / 2;
        for (int px = 0; px <= (int) a.getWidth(); px += 2)
        {
            const float f = xToFreq (a.getX() + (float) px, a);
            const float bin = f / (float) sr * (float) fftSize;
            const int b0 = juce::jlimit (1, bins - 2, (int) bin);
            const float db = juce::jmax (spectrumDb[(size_t) b0], spectrumDb[(size_t) b0 + 1]) + 4.5f * std::log2 (f / 1000.0f);
            const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, a.getBottom(), a.getY());
            sp.lineTo (a.getX() + (float) px, y);
        }
        sp.lineTo (a.getRight(), a.getBottom());
        sp.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::input.withAlpha (0.45f), 0.0f, a.getY(), Colours::input.withAlpha (0.05f), 0.0f, a.getBottom(), false));
        g.fillPath (sp);
    }

    // ---- Amp curve: tone stack (relative to noon), presence/resonance, graphic EQ ----------------
    const bool ampOn = param01 (state, ParamID::ampOn) > 0.5f;
    const int model = juce::roundToInt (param01 (state, ParamID::ampModel));
    const auto& spec = ampModelSpec ((AmpModel) model);
    ToneStackSettings ts;
    ts.bass = param01 (state, ParamID::ampBass) * 0.1f;
    ts.mid = param01 (state, ParamID::ampMid) * 0.1f;
    ts.treble = param01 (state, ParamID::ampTreble) * 0.1f;
    ts.midFreq = juce::roundToInt (param01 (state, ParamID::ampMidFreq));
    ts.ultraLo = param01 (state, ParamID::ampUltraLo) > 0.5f;
    ts.ultraHi = param01 (state, ParamID::ampUltraHi) > 0.5f;
    double stackRef = -100.0;
    for (double f = 80.0; f < 6000.0; f *= 1.12)
        stackRef = std::max (stackRef, ToneStack::magnitudeDb (spec.stackType, spec.tmb, {}, f, sr * 4.0));
    Biquad presence, depth;
    presence.setHighShelf (sr * 4.0, spec.presenceHz, param01 (state, ParamID::ampPresence) * 0.1f * spec.presenceMaxDb, 0.7);
    depth.setPeak (sr * 4.0, spec.depthHz, 0.8, param01 (state, ParamID::ampDepth) * 0.1f * spec.depthMaxDb);
    const bool geqOn = param01 (state, ParamID::geqOn) > 0.5f;
    std::array<Biquad, kGeqBands> geq;
    const char* geqIds[] = { ParamID::geq1, ParamID::geq2, ParamID::geq3, ParamID::geq4, ParamID::geq5 };
    for (int b = 0; b < kGeqBands; ++b)
        GraphicEq::design (geq[(size_t) b], sr, b, param01 (state, geqIds[b]));

    // ---- Post EQ and cab filters ---------------------------------------------------------------------
    const bool eqOn = param01 (state, ParamID::eqOn) > 0.5f;
    std::array<Biquad, kEqBands> bands;
    for (int b = 0; b < kEqBands; ++b)
    {
        auto val = [&] (int which) { auto* p = bandParam (b, which); return p->convertFrom0to1 (p->getValue()); };
        EqModule::design (bands[(size_t) b], sr, b, val (0), val (1), val (2));
    }
    const bool cabOn = param01 (state, ParamID::cabOn) > 0.5f;
    const float lowCut = param01 (state, ParamID::cabLowCut), highCut = param01 (state, ParamID::cabHighCut);
    Biquad lc, hc;
    lc.setHighPass (sr, lowCut, 0.7071);
    hc.setLowPass (sr, highCut, 0.7071);

    juce::Path ampPath, cabPath, eqPath;
    for (int px = 0; px <= (int) a.getWidth(); px += 2)
    {
        const float f = xToFreq (a.getX() + (float) px, a);
        const float x = a.getX() + (float) px;

        double adb = ToneStack::magnitudeDb (spec.stackType, spec.tmb, ts, f, sr * 4.0) - stackRef;
        adb += presence.magnitudeDb (sr * 4.0, f) + depth.magnitudeDb (sr * 4.0, f);
        if (geqOn)
            for (auto& b : geq) adb += b.magnitudeDb (sr, f);
        const float ay = dbToY ((float) juce::jlimit (-40.0, 40.0, adb), a);
        if (px == 0) ampPath.startNewSubPath (x, ay); else ampPath.lineTo (x, ay);

        // cabinet response (interpolated on the log grid)
        const float idx = std::log (f / 20.0f) / std::log (1000.0f) * (float) (kResponsePoints - 1);
        const int i0 = juce::jlimit (0, kResponsePoints - 2, (int) idx);
        const float fr = juce::jlimit (0.0f, 1.0f, idx - (float) i0);
        double cdb = cab[(size_t) i0] * (1.0f - fr) + cab[(size_t) i0 + 1] * fr;
        if (lowCut > 20.5f) cdb += lc.magnitudeDb (sr, f);
        if (highCut < 19999.0f) cdb += hc.magnitudeDb (sr, f);
        const float cy = dbToY ((float) juce::jlimit (-60.0, 40.0, cdb), a);
        if (px == 0) cabPath.startNewSubPath (x, cy); else cabPath.lineTo (x, cy);

        double edb = 0.0;
        if (eqOn)
            for (auto& b : bands) edb += b.magnitudeDb (sr, f);
        const float ey = dbToY ((float) juce::jlimit (-40.0, 40.0, edb), a);
        if (px == 0) eqPath.startNewSubPath (x, ey); else eqPath.lineTo (x, ey);
    }

    g.setColour (Colours::accent.withAlpha (cabOn ? 0.9f : 0.25f));
    g.strokePath (cabPath, juce::PathStrokeType (1.6f));
    g.setColour (Colours::detection.withAlpha (ampOn ? 0.9f : 0.25f));
    g.strokePath (ampPath, juce::PathStrokeType (1.6f));
    {
        juce::Path fill (eqPath);
        const float zero = dbToY (0.0f, a);
        fill.lineTo (a.getRight(), zero);
        fill.lineTo (a.getX(), zero);
        fill.closeSubPath();
        g.setColour (Colours::threshold.withAlpha (eqOn ? 0.12f : 0.04f));
        g.fillPath (fill);
        g.setColour (eqOn ? Colours::threshold : Colours::textDim.withAlpha (0.6f));
        g.strokePath (eqPath, juce::PathStrokeType (2.0f));
    }

    static const char* labels[kEqBands] = { "L", "LM", "M", "HM", "H" };
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto pos = nodePosition (b, a);
        const bool sel = b == selected, hov = b == hover || b == dragging;
        const float r = sel || hov ? 8.0f : 6.5f;
        g.setColour (sel ? Colours::threshold : Colours::panelLight);
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos));
        g.setColour (Colours::threshold.withAlpha (eqOn ? 1.0f : 0.4f));
        g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (pos), 1.3f);
        g.setColour (sel ? Colours::background : Colours::text);
        g.setFont (uiFont (8.0f, true));
        g.drawText (labels[b], juce::Rectangle<float> (20, 12).withCentre (pos), juce::Justification::centred);
    }

    g.restoreState();
    g.setFont (uiFont (11.0f, true));
    g.setColour (Colours::text);
    g.drawText ("TONE / SPECTRUM", 10, 3, 140, 18, juce::Justification::centredLeft);
    drawLegend (g, { 0, 3, getWidth() - 10, 18 },
                { { "AMP", Colours::detection }, { "CAB", Colours::accent }, { "POST EQ", Colours::threshold }, { "OUT", Colours::input } });
}

//==============================================================================
TunerPanel::TunerPanel (AmpSuiteProcessor& p) : processor (p)
{
    scratch.resize (4096);
}

void TunerPanel::tick()
{
    const double sr = processor.getEngineSampleRate();
    if (std::abs (sr - tunerRate) > 0.5)
    {
        tuner.prepare (sr);
        tunerRate = sr;
    }
    int got = 0;
    while ((got = processor.tunerFifo.pop (scratch.data(), (int) scratch.size())) > 0)
        tuner.push (scratch.data(), got);

    if (++analyseCounter >= 3)
    {
        analyseCounter = 0;
        const auto r = tuner.analyse();
        if (r.valid)
        {
            if (! shown.valid || r.note != shown.note)
                shownCents = r.cents;
            shown = r;
            silentTicks = 0;
        }
        else if (++silentTicks > 8)
            shown.valid = false;
        shownCents += (shown.cents - shownCents) * 0.5f;
        repaint();
    }
}

void TunerPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (10, 8);
    const bool muted = processor.isTunerMuted();

    auto top = r.removeFromTop (16);
    g.setFont (uiFont (11.0f, true));
    g.setColour (Colours::textDim);
    g.drawText ("TUNER", top, juce::Justification::centredLeft);
    if (muted)
    {
        g.setColour (Colours::danger);
        g.drawText ("OUTPUT MUTED", top, juce::Justification::centredRight);
    }

    auto row = r.removeFromTop (58);
    const bool active = shown.valid;
    const float absC = std::abs (shownCents);
    const auto noteColour = ! active ? Colours::textDim.withAlpha (0.4f)
                                     : (absC < 3.0f ? Colours::accent : (absC < 15.0f ? Colours::detection : Colours::danger));
    g.setFont (uiFont (46.0f, true));
    g.setColour (noteColour);
    g.drawText (active ? juce::MidiMessage::getMidiNoteName (shown.note, true, true, 4) : juce::String ("--"), row.removeFromLeft (120),
                juce::Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (Colours::text);
    if (active)
        g.drawText (juce::String (shown.hz, 1) + " Hz    " + (shownCents >= 0 ? "+" : "") + juce::String (juce::roundToInt (shownCents)) + " ct",
                    row, juce::Justification::centredLeft);
    else
        g.drawText ("play a single note", row, juce::Justification::centredLeft);

    // Cents meter with a strobe-like needle
    auto meter = r.removeFromTop (22).toFloat().reduced (0.0f, 5.0f);
    g.setColour (Colours::graphBg);
    g.fillRoundedRectangle (meter, 4.0f);
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = meter.getCentreX() + (float) c / 50.0f * meter.getWidth() * 0.5f;
        g.setColour (c == 0 ? Colours::accent : Colours::edge);
        g.drawVerticalLine ((int) x, meter.getY() + (c == 0 ? -3.0f : 2.0f), meter.getBottom() + (c == 0 ? 3.0f : -2.0f));
    }
    if (active)
    {
        const float x = meter.getCentreX() + juce::jlimit (-50.0f, 50.0f, shownCents) / 50.0f * meter.getWidth() * 0.5f;
        g.setColour (noteColour);
        g.fillRoundedRectangle (juce::Rectangle<float> (5.0f, meter.getHeight() + 8.0f).withCentre ({ x, meter.getCentreY() }), 2.0f);
    }

    // Amp model information
    r.removeFromTop (10);
    g.setColour (Colours::edge);
    g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
    r.removeFromTop (8);
    auto& st = processor.getState();
    const auto& spec = ampModelSpec ((AmpModel) juce::roundToInt (st.getRawParameterValue (ParamID::ampModel)->load()));
    g.setFont (uiFont (11.0f, true));
    g.setColour (Colours::textDim);
    g.drawText (spec.bass ? "BASS AMP" : "GUITAR AMP", r.removeFromTop (16), juce::Justification::centredLeft);
    g.setFont (uiFont (22.0f, true));
    g.setColour (Colours::threshold);
    g.drawText (juce::String (spec.name).toUpperCase(), r.removeFromTop (28), juce::Justification::centredLeft);
    g.setFont (uiFont (11.0f));
    g.setColour (Colours::text);
    g.drawText (juce::String ("Voiced after the ") + spec.inspiredBy, r.removeFromTop (16), juce::Justification::centredLeft);
    r.removeFromTop (4);
    g.setColour (Colours::textDim);
    g.drawFittedText (spec.description, r.removeFromTop (48), juce::Justification::topLeft, 3);
}

//==============================================================================
ChainStrip::ChainStrip (AmpSuiteProcessor& p) : processor (p), state (p.getState())
{
    order = processor.getPedalOrder();
    rebuildTiles();
    setTooltip ("Click a stage to edit it, click its dot to switch it on/off, drag the pedals to change their order");
}

void ChainStrip::rebuildTiles()
{
    tiles.clear();
    tiles.push_back ({ "input", "INPUT", {}, Colours::textDim, pagePedals, -1, 0, {} });
    for (int pd : order)
        tiles.push_back ({ juce::String ("p") + juce::String (pd), pedalShortName ((PedalId) pd), pedalEnableID ((PedalId) pd),
                           pedalColour ((PedalId) pd), pagePedals, pd, 1, {} });
    tiles.push_back ({ "amp", "AMP", ParamID::ampOn, Colours::threshold, pageAmp, -1, 2, {} });
    tiles.push_back ({ "geq", "GEQ", ParamID::geqOn, Colours::threshold, pageAmp, -1, 3, {} });
    tiles.push_back ({ "mod", "MOD", ParamID::modOn, Colours::blue, pageEffects, -1, 3, {} });
    tiles.push_back ({ "delay", "DELAY", ParamID::dlyOn, Colours::detection, pageEffects, -1, 3, {} });
    tiles.push_back ({ "cab", "CAB", ParamID::cabOn, Colours::accent, pageCab, -1, 4, {} });
    tiles.push_back ({ "eq", "EQ", ParamID::eqOn, Colours::threshold, pageCab, -1, 5, {} });
    tiles.push_back ({ "doubler", "DOUBLE", ParamID::dblOn, Colours::learn, pageEffects, -1, 5, {} });
    tiles.push_back ({ "reverb", "REVERB", ParamID::revOn, Colours::accent, pageEffects, -1, 5, {} });
    tiles.push_back ({ "output", "OUTPUT", {}, Colours::textDim, pagePedals, -1, 6, {} });
    layoutTiles();
}

void ChainStrip::layoutTiles()
{
    const float gap = 12.0f;
    const float w = ((float) getWidth() - gap * (float) (tiles.size() - 1)) / (float) tiles.size();
    float x = 0.0f;
    for (auto& t : tiles)
    {
        t.bounds = { x, 0.0f, w, (float) getHeight() - 10.0f };
        x += w + gap;
    }
}

juce::Rectangle<float> ChainStrip::powerRect (const Tile& t) const
{
    return { t.bounds.getX() + 7.0f, t.bounds.getY() + 8.0f, 11.0f, 11.0f };
}

bool ChainStrip::tileEnabled (const Tile& t) const
{
    if (t.enableId.isEmpty())
        return true;
    return state.getRawParameterValue (t.enableId)->load() > 0.5f;
}

juce::String ChainStrip::tileValue (const Tile& t) const
{
    auto db = [] (float v) { return (v > 0.05f ? "-" : "") + juce::String (std::abs (v), 1) + " dB"; };
    auto& m = processor.meters;
    auto value = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
    if (t.key == "input") return juce::String (value (ParamID::inGain), 1) + " dB";
    if (t.key == "output") return "LIM " + db (m.limit.load());
    if (t.key == "amp") return ampModelSpec ((AmpModel) juce::roundToInt (value (ParamID::ampModel))).shortName + juce::String ("  ")
                               + juce::String (value (ParamID::ampGain), 1);
    if (t.key == "cab")
    {
        const bool irA = value (ParamID::micASource) > 0.5f && processor.getImpulseName (0).isNotEmpty();
        return irA ? juce::String ("IR FILE") : juce::String (cabShortName (juce::roundToInt (value (ParamID::cabModel))));
    }
    if (t.key == "mod") return juce::StringArray { "CHORUS", "FLANGER", "PHASER" }[juce::roundToInt (value (ParamID::modMode))];
    if (t.key == "delay") return juce::String (juce::roundToInt (value (ParamID::dlyTime))) + " ms";
    if (t.key == "reverb") return juce::StringArray { "PLATE", "ROOM", "HALL", "CHAMBER" }[juce::roundToInt (value (ParamID::revType))];
    if (t.pedal >= 0)
    {
        switch ((PedalId) t.pedal)
        {
            case PedalId::gate:  return db (m.gate.load());
            case PedalId::comp:  return db (m.comp.load());
            case PedalId::drive: return juce::String (driveTypeName (juce::roundToInt (value (ParamID::drvType)))).toUpperCase();
            case PedalId::boost: return "+" + juce::String (value (ParamID::bstGain), 1) + " dB";
            case PedalId::numPedals: break;
        }
    }
    return {};
}

void ChainStrip::tick()
{
    const auto o = processor.getPedalOrder();
    if (o != order && dragTile < 0)
    {
        order = o;
        rebuildTiles();
    }
    repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    if (tiles.empty())
        return;
    if (tiles.front().bounds.getWidth() <= 0.0f)
        layoutTiles();

    // Connectors
    for (size_t i = 0; i + 1 < tiles.size(); ++i)
    {
        const auto& a = tiles[i].bounds;
        const float x = a.getRight() + 6.0f, y = a.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (x - 3.0f, y - 4.0f);
        arrow.lineTo (x + 1.5f, y);
        arrow.lineTo (x - 3.0f, y + 4.0f);
        g.setColour (Colours::textDim.withAlpha (0.6f));
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }

    // Group brackets: pedals / amp / loop / cab / post
    {
        static const char* groupNames[] = { "", "PEDALS (drag to reorder)", "HEAD", "FX LOOP", "SPEAKER", "POST FX", "" };
        for (int grp = 1; grp <= 5; ++grp)
        {
            float x0 = 1.0e9f, x1 = -1.0e9f;
            for (auto& t : tiles)
                if (t.group == grp) { x0 = std::min (x0, t.bounds.getX()); x1 = std::max (x1, t.bounds.getRight()); }
            if (x1 < x0)
                continue;
            const float y = (float) getHeight() - 4.0f;
            g.setColour (Colours::edge);
            g.drawHorizontalLine ((int) y, x0 + 2.0f, x1 - 2.0f);
            g.drawVerticalLine ((int) x0 + 2, y - 3.0f, y);
            g.drawVerticalLine ((int) x1 - 2, y - 3.0f, y);
            g.setFont (uiFont (8.5f, true));
            const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), groupNames[grp]) + 10;
            const auto label = juce::Rectangle<float> ((float) tw, 10.0f).withCentre ({ (x0 + x1) * 0.5f, y });
            g.setColour (Colours::background);
            g.fillRect (label);
            g.setColour (Colours::textDim);
            g.drawText (groupNames[grp], label, juce::Justification::centred);
        }
    }

    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& t = tiles[i];
        auto b = t.bounds;
        if ((int) i == dragTile)
            b = b.withX (dragX - dragOffset);
        const bool on = tileEnabled (t);
        const bool hov = (int) i == hoverTile;

        g.setColour ((int) i == dragTile ? Colours::panelLight.brighter (0.15f) : (hov ? Colours::panelLight.brighter (0.06f) : Colours::panelLight));
        g.fillRoundedRectangle (b.reduced (0.5f, 1.5f), 5.0f);
        g.setColour (on ? t.colour.withAlpha (0.8f) : Colours::edge);
        g.drawRoundedRectangle (b.reduced (0.5f, 1.5f), 5.0f, on ? 1.2f : 1.0f);
        if (on)
        {
            g.setColour (t.colour);
            g.fillRoundedRectangle (b.getX() + 6.0f, b.getBottom() - 5.0f, b.getWidth() - 12.0f, 2.0f, 1.0f);
        }
        if (t.enableId.isNotEmpty())
        {
            auto pr = powerRect (t).withX (b.getX() + 7.0f);
            g.setColour (on ? t.colour : Colours::graphBg);
            g.fillEllipse (pr);
            g.setColour (on ? t.colour.brighter (0.3f) : Colours::textDim);
            g.drawEllipse (pr, 1.0f);
        }
        g.setFont (uiFont (11.0f, true));
        g.setColour (on ? Colours::text : Colours::textDim);
        g.drawText (t.name, b.withTrimmedLeft (t.enableId.isNotEmpty() ? 20.0f : 6.0f).withHeight (24.0f).translated (0.0f, 1.0f),
                    t.enableId.isNotEmpty() ? juce::Justification::centredLeft : juce::Justification::centred);
        g.setFont (uiFont (9.5f));
        g.setColour (on ? t.colour.withAlpha (0.9f) : Colours::textDim.withAlpha (0.6f));
        g.drawFittedText (on ? tileValue (t) : juce::String ("off"), b.withTrimmedTop (20.0f).withTrimmedBottom (5.0f).reduced (4.0f, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1);
    }

    if (dragTile >= 0 && dragTarget >= 0)
    {
        const auto& t = tiles[(size_t) dragTarget];
        const float x = dragTarget > dragTile ? t.bounds.getRight() + 6.0f : t.bounds.getX() - 6.0f;
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (x - 1.5f, 2.0f, 3.0f, t.bounds.getHeight() - 4.0f, 1.5f);
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].bounds.contains (e.position)) h = (int) i;
    if (h != hoverTile)
    {
        hoverTile = h;
        setMouseCursor (h >= 0 && tiles[(size_t) h].pedal >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    dragTile = -1;
    dragMoved = false;
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& t = tiles[i];
        if (! t.bounds.contains (e.position))
            continue;
        if (t.enableId.isNotEmpty() && powerRect (t).expanded (4.0f).contains (e.position))
        {
            if (auto* p = state.getParameter (t.enableId))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                p->endChangeGesture();
            }
            return;
        }
        dragTile = (int) i;
        dragOffset = e.position.x - t.bounds.getX();
        dragX = e.position.x;
        return;
    }
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTile < 0 || tiles[(size_t) dragTile].pedal < 0)
        return;
    if (! dragMoved && std::abs (e.getDistanceFromDragStartX()) < 5)
        return;
    dragMoved = true;
    dragX = e.position.x;
    dragTarget = -1;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].pedal >= 0 && e.position.x >= tiles[i].bounds.getX() - 6.0f && e.position.x <= tiles[i].bounds.getRight() + 6.0f)
            dragTarget = (int) i;
    if (dragTarget < 0)
    {
        int first = -1, last = -1;
        for (size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].pedal >= 0) { if (first < 0) first = (int) i; last = (int) i; }
        dragTarget = e.position.x < tiles[(size_t) first].bounds.getX() ? first : last;
    }
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragTile >= 0 && ! dragMoved)
    {
        if (onTileClicked)
            onTileClicked (tiles[(size_t) dragTile].page, tiles[(size_t) dragTile].key);
    }
    else if (dragTile >= 0 && dragTarget >= 0 && dragTarget != dragTile)
    {
        int first = -1;
        for (size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].pedal >= 0) { first = (int) i; break; }
        const int from = dragTile - first, to = dragTarget - first;
        std::vector<int> o (order.begin(), order.end());
        const int moved = o[(size_t) from];
        o.erase (o.begin() + from);
        o.insert (o.begin() + to, moved);
        for (int i = 0; i < kNumPedals; ++i) order[(size_t) i] = o[(size_t) i];
        processor.setPedalOrder (order);
        rebuildTiles();
        if (onOrderChanged)
            onOrderChanged();
    }
    dragTile = dragTarget = -1;
    dragMoved = false;
    repaint();
}
} // namespace laas
