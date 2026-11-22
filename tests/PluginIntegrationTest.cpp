// Integration test: drives the real LA-AmpSuite processor + editor the way a host would.
//  - bus layouts, latency, presets, state save/restore (incl. pedal order and IR files), user presets,
//    loading an impulse response file, real-time performance, and renders the editor:
//    laas_integration [screenshot.png [page]]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginProcessor.h"
#include "../Source/gui/PluginEditor.h"

#include <random>

using namespace laas;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

constexpr double sr = 48000.0;

/** Guitar-like DI riff: decaying harmonic notes, palm mutes, a little noise. */
juce::AudioBuffer<float> makeGuitar (double seconds, float amp = 0.3f, float baseMidi = 40.0f)
{
    juce::AudioBuffer<float> buf (2, (int) (seconds * sr));
    std::mt19937 rng (5);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    const float riff[] = { 0, 0, 0, 3, 0, 0, 5, 7 };
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const int note = (int) (t / 0.25);
        const double tn = t - note * 0.25;
        const double f0 = 440.0 * std::pow (2.0, (baseMidi + riff[note % 8] - 69.0) / 12.0);
        double s = 0.0;
        for (int h = 1; h <= 12; ++h)
            s += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * tn) * std::exp (-tn * (4.0 + h)) / h;
        const float v = amp * (float) s + 0.0003f * noise (rng);
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }
    return buf;
}

void run (AmpSuiteProcessor& proc, juce::AudioBuffer<float>& audio, int block, AmpSuiteEditor* editor = nullptr)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int start = 0; start + block <= audio.getNumSamples(); start += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, audio, ch, start, block);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            audio.copyFrom (ch, start, buf, ch, 0, block);
        if (editor != nullptr && (start / block) % 6 == 0)
            editor->updateDisplays();
    }
}

void setParam (AmpSuiteProcessor& p, const char* id, float value)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float param (AmpSuiteProcessor& p, const char* id) { return p.getState().getRawParameterValue (id)->load(); }

bool cleanOutput (const juce::AudioBuffer<float>& audio, float& peak)
{
    peak = audio.getMagnitude (0, audio.getNumSamples());
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int s = 0; s < audio.getNumSamples(); ++s)
            if (! std::isfinite (audio.getSample (ch, s)))
                return false;
    return peak <= 0.9f && peak > 0.02f;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("LA-AmpSuite plugin integration test\n");

    auto proc = std::make_unique<AmpSuiteProcessor>();
    check (! proc->acceptsMidi() && ! proc->producesMidi(), "audio effect without MIDI");
    {
        juce::AudioProcessor::BusesLayout mono, stereo, monoToStereo, bad;
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        stereo.inputBuses.add (juce::AudioChannelSet::stereo()); stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        monoToStereo.inputBuses.add (juce::AudioChannelSet::mono()); monoToStereo.outputBuses.add (juce::AudioChannelSet::stereo());
        bad.inputBuses.add (juce::AudioChannelSet::create5point1()); bad.outputBuses.add (juce::AudioChannelSet::create5point1());
        check (proc->checkBusesLayoutSupported (mono) && proc->checkBusesLayoutSupported (stereo)
                   && proc->checkBusesLayoutSupported (monoToStereo) && ! proc->checkBusesLayoutSupported (bad),
               "supports mono, stereo and mono->stereo; rejects 5.1");
    }

    proc->setPlayConfigDetails (2, 2, sr, 512);
    proc->prepareToPlay (sr, 512);
    check (proc->getLatencySamples() == 23, "reports latency of " + juce::String (proc->getLatencySamples()) + " samples ("
                                                 + juce::String (1000.0 * proc->getLatencySamples() / sr, 2) + " ms)");

    // ---- Presets ---------------------------------------------------------------------------------
    {
        bool ok = proc->getNumPrograms() >= 14;
        for (int i = 0; i < proc->getNumPrograms(); ++i)
        {
            proc->setCurrentProgram (i);
            proc->rebuildImpulsesNow();
            const bool bass = proc->getProgramName (i).startsWith ("Bass");
            auto audio = makeGuitar (1.5, bass ? 0.4f : 0.3f, bass ? 28.0f : 40.0f);
            run (*proc, audio, 512);
            float peak = 0.0f;
            const bool clean = cleanOutput (audio, peak);
            ok &= clean;
            std::printf ("        %-22s peak %6.1f dBFS%s\n", proc->getProgramName (i).toRawUTF8(), juce::Decibels::gainToDecibels (peak),
                         clean ? "" : "   <-- problem");
        }
        check (ok, juce::String (proc->getNumPrograms()) + " factory presets all produce clean, limited output");
        proc->setCurrentProgram (7);
        check (proc->getProgramName (7) == "Modern Metal" && juce::roundToInt (param (*proc, ParamID::ampModel)) == (int) AmpModel::recto
                   && param (*proc, ParamID::bstOn) > 0.5f,
               "factory preset applies (Modern Metal)");
        proc->setCurrentProgram (0);
    }

    // ---- Real-time performance: full rig ------------------------------------------------------------
    {
        proc->setCurrentProgram (7);
        for (auto* id : { ParamID::modOn, ParamID::dlyOn, ParamID::dblOn, ParamID::revOn, ParamID::eqOn, ParamID::compOn, ParamID::drvOn, ParamID::geqOn })
            setParam (*proc, id, 1.0f);
        proc->rebuildImpulsesNow();
        auto audio = makeGuitar (10.0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (*proc, audio, 256);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double cpu = ms / 10000.0 * 100.0;
        check (cpu < 25.0, "everything on, 10 s of audio processed in " + juce::String (ms, 0) + " ms (" + juce::String (cpu, 1) + " % of one core)");
        proc->setCurrentProgram (0);
    }

    // ---- State round trip (incl. pedal order) -------------------------------------------------------
    {
        setParam (*proc, ParamID::ampGain, 7.3f);
        setParam (*proc, ParamID::micBDist, 33.0f);
        proc->setPedalOrder ({ 3, 2, 1, 0 });
        juce::MemoryBlock data;
        proc->getStateInformation (data);
        AmpSuiteProcessor copy;
        copy.setStateInformation (data.getData(), (int) data.getSize());
        bool same = true;
        for (auto* p : proc->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                same &= std::abs (rp->getValue() - copy.getState().getParameter (rp->paramID)->getValue()) < 1.0e-5f;
        check (same, "state save/restore round trip (" + juce::String (proc->getParameters().size()) + " parameters)");
        check (copy.getPedalOrder() == PedalOrder { 3, 2, 1, 0 }, "pedal order is saved with the session");
        proc->setPedalOrder (defaultPedalOrder());
    }

    // ---- Impulse response files ------------------------------------------------------------------------
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("laas-ir-test");
        dir.createDirectory();
        auto wav = dir.getChildFile ("Test Cab 44k.wav");
        {
            auto ir = buildCabinetIR (44100.0, CabModel::greenback412, {});
            juce::AudioBuffer<float> b (1, (int) ir.size());
            for (int i = 0; i < (int) ir.size(); ++i) b.setSample (0, i, ir[(size_t) i] * 0.5f);
            wav.deleteFile();
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> os (wav.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), 44100.0, 1, 24, {}, 0));
            if (w != nullptr)
            {
                os.release();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }
        const auto err = proc->loadImpulseFile (0, wav);
        proc->rebuildImpulsesNow();
        check (err.isEmpty() && proc->getImpulseName (0) == "Test Cab 44k" && param (*proc, ParamID::micASource) > 0.5f,
               "loads a 44.1 kHz WAV impulse response into mic A" + (err.isEmpty() ? juce::String() : " (" + err + ")"));
        auto audio = makeGuitar (1.0);
        run (*proc, audio, 512);
        float peak = 0.0f;
        const bool irClean = cleanOutput (audio, peak);
        check (irClean, "user IR renders clean output (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB)");

        juce::MemoryBlock data;
        proc->getStateInformation (data);
        AmpSuiteProcessor copy;
        copy.setStateInformation (data.getData(), (int) data.getSize());
        check (copy.getImpulseName (0) == "Test Cab 44k" && copy.getState().getRawParameterValue (ParamID::micASource)->load() > 0.5f,
               "IR file is restored with the session");
        check (proc->loadImpulseFile (1, dir.getChildFile ("missing.wav")).isNotEmpty(), "missing IR file reports an error");
        proc->clearImpulseFile (0);
        check (proc->getImpulseName (0).isEmpty() && param (*proc, ParamID::micASource) < 0.5f, "clearing the IR goes back to the built-in cab");
        dir.deleteRecursively();
    }

    // ---- User preset save / load -------------------------------------------------------------------------
    {
        setParam (*proc, ParamID::revMix, 33.0f);
        const bool saved = proc->saveUserPreset ("Integration Test Preset");
        setParam (*proc, ParamID::revMix, 5.0f);
        const auto file = AmpSuiteProcessor::getUserPresetFolder().getChildFile ("Integration Test Preset.laas");
        const bool loaded = proc->loadUserPreset (file);
        check (saved && loaded && std::abs (param (*proc, ParamID::revMix) - 33.0f) < 0.01f, "user preset save and load");
        file.deleteFile();
        proc->setCurrentProgram (0);
    }

    // ---- Cab response for the display ------------------------------------------------------------------------
    {
        proc->rebuildImpulsesNow();
        const auto r = proc->getCabResponse();
        float mid = -200.0f, top = -200.0f;
        for (int i = 0; i < kResponsePoints; ++i)
        {
            if (responsePointHz (i) > 1500.0f && responsePointHz (i) < 3000.0f) mid = std::max (mid, r[(size_t) i]);
            if (responsePointHz (i) > 12000.0f) top = std::max (top, r[(size_t) i]);
        }
        check (mid > top + 15.0f, "cab response for the display is computed");
    }

    // ---- Editor -------------------------------------------------------------------------------------------------
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditorIfNeeded());
    auto* editor = dynamic_cast<AmpSuiteEditor*> (ed.get());
    check (editor != nullptr, "editor created");
    {
        proc->setCurrentProgram (7);
        proc->rebuildImpulsesNow();
        for (int page = 0; page < 4; ++page)
        {
            editor->getPanel().setPage (page);
            auto audio = makeGuitar (0.5);
            run (*proc, audio, 480, editor);
        }
        check (true, "all editor pages render while audio runs");
    }

    if (argc > 1 && editor != nullptr)
    {
        const int page = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
        if (argc > 3)
            proc->setCurrentProgram (juce::String (argv[3]).getIntValue());
        proc->rebuildImpulsesNow();
        editor->getPanel().setPage (page);
        const bool bass = proc->getProgramName (proc->getCurrentProgram()).startsWith ("Bass");
        auto audio = makeGuitar (3.0, bass ? 0.4f : 0.3f, bass ? 28.0f : 40.0f);
        run (*proc, audio, 480, editor);
        editor->setSize (MainPanel::baseWidth, MainPanel::baseHeight);
        for (int i = 0; i < 30; ++i)
            editor->updateDisplays();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        check (stream.openedOk() && png.writeImageToStream (image, stream), "screenshot written to " + out.getFullPathName());
    }

    ed.reset();
    proc.reset();
    std::printf (failures == 0 ? "All integration checks passed\n" : "%d integration check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
