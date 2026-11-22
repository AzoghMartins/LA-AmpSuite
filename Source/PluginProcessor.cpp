#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

namespace laas
{
namespace
{
uint32_t packOrder (const PedalOrder& o) noexcept
{
    uint32_t v = 0;
    for (int i = 0; i < kNumPedals; ++i)
        v |= (uint32_t) (o[(size_t) i] & 7) << (3 * i);
    return v | 0x80000000u;
}

PedalOrder unpackOrder (uint32_t v) noexcept
{
    if ((v & 0x80000000u) == 0)
        return defaultPedalOrder();
    PedalOrder o {};
    for (int i = 0; i < kNumPedals; ++i)
        o[(size_t) i] = (int) ((v >> (3 * i)) & 7);
    return isValidPedalOrder (o) ? o : defaultPedalOrder();
}

juce::String orderString (const PedalOrder& o)
{
    char buf[kNumPedals + 1];
    pedalOrderToString (o, buf);
    return buf;
}
} // namespace

//==============================================================================
/** Rebuilds cabinet impulse responses in the background when mic / cab settings change. */
class AmpSuiteProcessor::ImpulseBuilder : public juce::Thread
{
public:
    explicit ImpulseBuilder (AmpSuiteProcessor& p) : juce::Thread ("LA-AmpSuite IR builder"), owner (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            owner.updateImpulses (false);
            wait (40);
        }
    }

private:
    AmpSuiteProcessor& owner;
};

//==============================================================================
AmpSuiteProcessor::AmpSuiteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LAAmpSuite", createParameterLayout()),
      params (apvts)
{
    formatManager.registerBasicFormats();
    packedOrder.store (packOrder (defaultPedalOrder()));
    cabResponse.fill (-100.0f);
    builder = std::make_unique<ImpulseBuilder> (*this);
    builder->startThread (juce::Thread::Priority::low);
}

AmpSuiteProcessor::~AmpSuiteProcessor()
{
    builder->stopThread (2000);
    builder.reset();
}

bool AmpSuiteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in.size() <= out.size();
}

void AmpSuiteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (buildLock);
    preparedRate = sampleRate;
    preparedBlockSize = std::max (32, samplesPerBlock);
    params.read (engineParams);
    engineParams.pedalOrder = unpackOrder (packedOrder.load());
    engine.setParams (engineParams);
    engine.prepare (preparedRate, preparedBlockSize);
    engineSampleRate.store (preparedRate);
    {
        const juce::ScopedLock il (irLock);
        builtKeys = {};
    }
    updateImpulses (true);
    engine.reset();
    setLatencySamples (engine.getLatency());
}

//==============================================================================
void AmpSuiteProcessor::updateImpulses (bool install)
{
    const juce::ScopedLock sl (buildLock);
    engine.collectGarbage();
    const double sr = preparedRate;
    if (sr <= 0.0)
        return;

    EngineParams p;
    params.read (p);
    const auto& c = p.cab;

    std::array<UserIR, 2> users;
    std::array<juce::String, 2> oldKeys;
    {
        const juce::ScopedLock il (irLock);
        for (int s = 0; s < 2; ++s)
        {
            oldKeys[(size_t) s] = builtKeys[(size_t) s];
            users[(size_t) s].serial = userIRs[(size_t) s].serial;
        }
    }

    const MicSource src[2] = { c.sourceA, c.sourceB };
    const MicPlacement mic[2] = { c.micA, c.micB };
    bool useUser[2];
    for (int s = 0; s < 2; ++s)
        useUser[s] = src[s] == MicSource::userIR && users[(size_t) s].serial > 0;

    // Time-align two built-in mics: the farther one arrives later
    double extra[2] = { 0.0, 0.0 };
    if (! useUser[0] && ! useUser[1] && c.micBOn)
    {
        const double dA = micDelaySamples (sr, mic[0].distanceCm), dB = micDelaySamples (sr, mic[1].distanceCm);
        extra[0] = dA - std::min (dA, dB);
        extra[1] = dB - std::min (dA, dB);
    }

    bool changed = false;
    for (int s = 0; s < 2; ++s)
    {
        juce::String key;
        if (useUser[s])
            key << "U|" << users[(size_t) s].serial << "|" << sr;
        else
            key << "B|" << (int) c.cab << "|" << (int) mic[s].mic << "|" << juce::roundToInt (mic[s].position * 1000.0f) << "|"
                << juce::roundToInt (mic[s].distanceCm * 10.0f) << "|" << juce::roundToInt (extra[s] * 10.0) << "|" << sr;
        if (key == oldKeys[(size_t) s])
            continue;

        std::vector<float> ir;
        if (useUser[s])
        {
            {
                const juce::ScopedLock il (irLock);
                ir = userIRs[(size_t) s].data;
                users[(size_t) s].rate = userIRs[(size_t) s].rate;
            }
            ir = resampleIR (ir, users[(size_t) s].rate, sr);
        }
        else
            ir = buildCabinetIR (sr, c.cab, mic[s], extra[s]);
        conditionIR (ir, sr);
        if (ir.empty())
            ir.assign (1, 1.0f);

        auto* prepared = prepareIR (ir, AmpEngine::kPartition);
        if (install)
            engine.installImpulseNow (s, prepared);
        else
            engine.postImpulse (s, prepared);

        const juce::ScopedLock il (irLock);
        builtKeys[(size_t) s] = key;
        builtIRs[(size_t) s] = std::move (ir);
        changed = true;
    }

    // Blended response for the display
    juce::String rkey;
    {
        const juce::ScopedLock il (irLock);
        rkey << builtKeys[0] << "#" << builtKeys[1] << "#" << (int) c.micBOn << (int) c.micBInvert << "#" << juce::roundToInt (c.mix * 100.0f);
        if (rkey == responseKey && ! changed)
            return;
    }
    std::vector<float> mix;
    {
        const juce::ScopedLock il (irLock);
        const auto& a = builtIRs[0];
        const auto& b = builtIRs[1];
        const float wA = c.micBOn ? 1.0f - c.mix : 1.0f;
        const float wB = c.micBOn ? c.mix * (c.micBInvert ? -1.0f : 1.0f) : 0.0f;
        mix.assign (std::max (a.size(), b.size()), 0.0f);
        for (size_t i = 0; i < a.size(); ++i) mix[i] += wA * a[i];
        for (size_t i = 0; i < b.size(); ++i) mix[i] += wB * b[i];
    }
    const auto resp = irResponse (mix, sr);
    const juce::ScopedLock il (irLock);
    cabResponse = resp;
    responseKey = rkey;
    ++responseVersion;
}

void AmpSuiteProcessor::rebuildImpulsesNow()
{
    updateImpulses (false);
}

std::array<float, kResponsePoints> AmpSuiteProcessor::getCabResponse() const
{
    const juce::ScopedLock il (irLock);
    return cabResponse;
}

juce::String AmpSuiteProcessor::loadImpulseFile (int slot, const juce::File& file)
{
    if (slot < 0 || slot > 1)
        return "Invalid slot";
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return "Could not read " + file.getFileName();
    const auto maxLen = (juce::int64) (reader->sampleRate * 2.0);
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    if (len <= 0)
        return file.getFileName() + " is empty";
    juce::AudioBuffer<float> buf ((int) std::max (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    std::vector<float> data ((size_t) len);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < len; ++i)
            data[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();

    {
        const juce::ScopedLock il (irLock);
        auto& u = userIRs[(size_t) slot];
        u.file = file;
        u.name = file.getFileNameWithoutExtension();
        u.data = std::move (data);
        u.rate = reader->sampleRate;
        u.serial = ++irSerial;
    }
    if (auto* prm = apvts.getParameter (slot == 0 ? ParamID::micASource : ParamID::micBSource))
        prm->setValueNotifyingHost (prm->convertTo0to1 (1.0f));
    if (slot == 1)
        if (auto* prm = apvts.getParameter (ParamID::micBOn))
            prm->setValueNotifyingHost (1.0f);
    return {};
}

void AmpSuiteProcessor::clearImpulseFile (int slot)
{
    {
        const juce::ScopedLock il (irLock);
        userIRs[(size_t) slot] = {};
    }
    if (auto* prm = apvts.getParameter (slot == 0 ? ParamID::micASource : ParamID::micBSource))
        prm->setValueNotifyingHost (0.0f);
}

juce::String AmpSuiteProcessor::getImpulseName (int slot) const
{
    const juce::ScopedLock il (irLock);
    return userIRs[(size_t) slot].name;
}

juce::File AmpSuiteProcessor::getImpulseFile (int slot) const
{
    const juce::ScopedLock il (irLock);
    return userIRs[(size_t) slot].file;
}

//==============================================================================
void AmpSuiteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples); // mono in, stereo out
    else
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);

    params.read (engineParams);
    engineParams.pedalOrder = unpackOrder (packedOrder.load (std::memory_order_relaxed));
    engineParams.mute = tunerMute.load (std::memory_order_relaxed);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                engineParams.bpm = *bpm;
    engine.setParams (engineParams);

    const int channels = std::min (2, numOut);
    float inPeak = 0.0f, outPeak = 0.0f, gate = 0.0f, comp = 0.0f, lim = 0.0f, drive = 0.0f;
    for (int start = 0; start < numSamples; start += preparedBlockSize)
    {
        const int n = std::min (preparedBlockSize, numSamples - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (channels > 1 ? 1 : 0, start) };
        engine.process (ptrs, channels, n);
        spectrumFifo.push (engine.getAnalysisBuffer(), n);
        tunerFifo.push (engine.getInputBuffer(), n);
        const auto& m = engine.getMeters();
        inPeak = std::max (inPeak, m.inPeak);
        outPeak = std::max (outPeak, m.outPeak);
        gate = std::max (gate, m.gateDb);
        comp = std::max (comp, m.compDb);
        lim = std::max (lim, m.limitDb);
        drive = std::max (drive, m.ampDrive);
    }

    meters.inPeak.store (std::max (meters.inPeak.load (std::memory_order_relaxed), inPeak), std::memory_order_relaxed);
    meters.outPeak.store (std::max (meters.outPeak.load (std::memory_order_relaxed), outPeak), std::memory_order_relaxed);
    meters.gate.store (gate, std::memory_order_relaxed);
    meters.comp.store (comp, std::memory_order_relaxed);
    meters.limit.store (lim, std::memory_order_relaxed);
    meters.drive.store (drive, std::memory_order_relaxed);
}

//==============================================================================
PedalOrder AmpSuiteProcessor::getPedalOrder() const noexcept
{
    return unpackOrder (packedOrder.load());
}

void AmpSuiteProcessor::setPedalOrder (const PedalOrder& order)
{
    packedOrder.store (packOrder (order));
    apvts.state.setProperty ("order", orderString (order), nullptr);
}

void AmpSuiteProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void AmpSuiteProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;
    currentProgram = index;
    currentPresetName = presets[(size_t) index].name;
    resetToDefaults();
    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    setPedalOrder (presets[(size_t) index].order.isEmpty() ? defaultPedalOrder()
                                                          : pedalOrderFromString (presets[(size_t) index].order.toRawUTF8()));
}

const juce::String AmpSuiteProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? presets[(size_t) index].name : juce::String();
}

void AmpSuiteProcessor::storeExtraState (juce::ValueTree& state) const
{
    state.setProperty ("order", orderString (getPedalOrder()), nullptr);
    state.setProperty ("irA", getImpulseFile (0).getFullPathName(), nullptr);
    state.setProperty ("irB", getImpulseFile (1).getFullPathName(), nullptr);
}

void AmpSuiteProcessor::restoreExtraState (const juce::ValueTree& tree)
{
    setPedalOrder (pedalOrderFromString (tree.getProperty ("order").toString().toRawUTF8()));
    const char* keys[2] = { "irA", "irB" };
    for (int s = 0; s < 2; ++s)
    {
        const auto path = tree.getProperty (keys[s]).toString();
        const juce::File f = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
        if (f.existsAsFile())
        {
            if (f != getImpulseFile (s))
            {
                // keep the source choice stored in the state
                const float source = apvts.getRawParameterValue (s == 0 ? ParamID::micASource : ParamID::micBSource)->load();
                const float bOn = apvts.getRawParameterValue (ParamID::micBOn)->load();
                loadImpulseFile (s, f);
                if (auto* prm = apvts.getParameter (s == 0 ? ParamID::micASource : ParamID::micBSource))
                    prm->setValueNotifyingHost (prm->convertTo0to1 (source));
                if (auto* prm = apvts.getParameter (ParamID::micBOn))
                    prm->setValueNotifyingHost (bOn);
            }
        }
        else
        {
            const juce::ScopedLock il (irLock);
            userIRs[(size_t) s] = {};
        }
    }
}

juce::File AmpSuiteProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("LA Audio").getChildFile ("LA-AmpSuite").getChildFile ("Presets");
}

bool AmpSuiteProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;
    auto state = apvts.copyState();
    storeExtraState (state);
    state.setProperty ("presetName", name.trim(), nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".laas")))
        return false;
    currentPresetName = name.trim();
    return true;
}

bool AmpSuiteProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    restoreExtraState (tree);
    currentPresetName = tree.getProperty ("presetName", file.getFileNameWithoutExtension()).toString();
    currentProgram = -1;
    return true;
}

void AmpSuiteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    storeExtraState (state);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpSuiteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    currentProgram = (int) tree.getProperty ("program", 0);
    currentPresetName = tree.getProperty ("presetName", "Default").toString();
    apvts.replaceState (tree);
    restoreExtraState (tree);
}

juce::AudioProcessorEditor* AmpSuiteProcessor::createEditor()
{
    return new AmpSuiteEditor (*this);
}
} // namespace laas

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new laas::AmpSuiteProcessor();
}
