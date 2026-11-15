#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace anvil;

AnvilProcessor::AnvilProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "ANVIL", createLayout())
{
    engine.attach (apvts);
    resetToDefaults();
}

bool AnvilProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void AnvilProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (32, samplesPerBlock);
    stereoBuf.setSize (2, maxBlock);
    scBuf.setSize (2, maxBlock);
    engine.prepare (sampleRate, maxBlock);
    setLatencySamples (engine.getLatency());
}

void AnvilProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int chans = mainOut.getNumChannels();
    if (chans == 0 || n == 0) return;

    bool playing = false;
    double bpm = 120.0, ppq = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
        }
    }

    const bool hasSc = getBusCount (true) > 1 && getBus (true, 1)->isEnabled();
    juce::AudioBuffer<float> sc;
    if (hasSc) sc = getBusBuffer (buffer, true, 1);

    const double beatsPerSample = bpm / 60.0 / getSampleRate();

    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = std::min (maxBlock, n - pos);
        float* L = stereoBuf.getWritePointer (0);
        float* R = stereoBuf.getWritePointer (1);
        std::copy (mainIn.getReadPointer (0) + pos, mainIn.getReadPointer (0) + pos + len, L);
        const float* srcR = mainIn.getNumChannels() > 1 ? mainIn.getReadPointer (1) : mainIn.getReadPointer (0);
        std::copy (srcR + pos, srcR + pos + len, R);

        const float* scL = nullptr;
        const float* scR = nullptr;
        if (hasSc && sc.getNumChannels() > 0)
        {
            scL = sc.getReadPointer (0) + pos;
            scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) + pos : scL;
        }

        engine.process (L, R, len, scL, scR, playing, bpm, ppq + pos * beatsPerSample);

        if (chans == 1)
        {
            float* o = mainOut.getWritePointer (0) + pos;
            for (int i = 0; i < len; ++i) o[i] = 0.5f * (L[i] + R[i]);
        }
        else
        {
            std::copy (L, L + len, mainOut.getWritePointer (0) + pos);
            std::copy (R, R + len, mainOut.getWritePointer (1) + pos);
        }
    }

    if (engine.getLatency() != getLatencySamples())
        triggerAsyncUpdate();
}

void AnvilProcessor::handleAsyncUpdate()
{
    setLatencySamples (engine.getLatency());
}

// ---------------------------------------------------------------- state

juce::ValueTree AnvilProcessor::buildState()
{
    auto state = apvts.copyState();
    juce::ValueTree shapesTree ("SHAPES");
    for (int m = 0; m < numModules; ++m)
        shapesTree.appendChild (engine.shapes[m].toTree (modules()[(size_t) m].id), nullptr);
    state.removeChild (state.getChildWithName ("SHAPES"), nullptr);
    state.appendChild (shapesTree, nullptr);
    state.setProperty ("samplePath", engine.getSamplePath(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    return state;
}

void AnvilProcessor::restoreState (const juce::ValueTree& tree)
{
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    auto copy = tree.createCopy();
    auto shapesTree = copy.getChildWithName ("SHAPES");
    for (int m = 0; m < numModules; ++m)
    {
        auto t = shapesTree.getChildWithName (modules()[(size_t) m].id);
        if (t.isValid()) engine.shapes[m].fromTree (t);
        else engine.shapes[m].setPoints (shapes::flat());
    }
    const juce::String path = copy.getProperty ("samplePath");
    presetName = copy.getProperty ("presetName", "Custom");
    apvts.replaceState (copy);

    if (path.isNotEmpty() && juce::File (path).existsAsFile()) engine.loadSample (juce::File (path));
    else if (path.isEmpty()) engine.clearSample();

    if (onPresetChanged) onPresetChanged();
}

void AnvilProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = buildState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AnvilProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreState (juce::ValueTree::fromXml (*xml));
}

// ---------------------------------------------------------------- presets

const juce::String AnvilProcessor::getProgramName (int index)
{
    auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? p[(size_t) index].name : juce::String();
}

void AnvilProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& s : engine.shapes)
        s.setPoints (shapes::flat());
    engine.shapes[mGate].setPoints (shapes::gate (16, 0xFFFFu, 0.75f));
    engine.shapes[mMod].setPoints (shapes::pump());
    for (int m : { (int) mEq, (int) mDist, (int) mCrush, (int) mFlanger, (int) mFormant })
        engine.shapes[m].setPoints (shapes::sine());
}

void AnvilProcessor::applyFactoryPreset (int index)
{
    auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
    auto& p = list[(size_t) index];

    resetToDefaults();
    for (auto& [id, v] : p.values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    for (auto& [m, pts] : p.shapes)
        engine.shapes[m].setPoints (pts);

    currentFactory = index;
    presetName = p.name;
    if (onPresetChanged) onPresetChanged();
}

juce::File AnvilProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Anvil FX").getChildFile ("Presets");
}

bool AnvilProcessor::saveUserPreset (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = userPresetFolder();
    folder.createDirectory();
    presetName = name.trim();
    auto state = buildState();
    auto xml = state.createXml();
    if (! xml) return false;
    const bool ok = xml->writeTo (folder.getChildFile (clean + ".anvilfx"));
    if (onPresetChanged) onPresetChanged();
    return ok;
}

bool AnvilProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml) return false;
    restoreState (juce::ValueTree::fromXml (*xml));
    presetName = file.getFileNameWithoutExtension();
    if (onPresetChanged) onPresetChanged();
    return true;
}

juce::Array<juce::File> AnvilProcessor::getUserPresets() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.anvilfx");
    files.sort();
    return files;
}

juce::AudioProcessorEditor* AnvilProcessor::createEditor()
{
    return new AnvilEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnvilProcessor();
}
