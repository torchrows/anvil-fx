#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Engine.h"
#include "Presets.h"

class AnvilProcessor : public juce::AudioProcessor,
                       private juce::AsyncUpdater
{
public:
    AnvilProcessor();
    ~AnvilProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Anvil FX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    // Programs = factory presets (hosts list them too)
    int getNumPrograms() override { return (int) anvil::factoryPresets().size(); }
    int getCurrentProgram() override { return currentFactory; }
    void setCurrentProgram (int index) override { applyFactoryPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- presets
    void applyFactoryPreset (int index);
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::Array<juce::File> getUserPresets() const;
    static juce::File userPresetFolder();
    juce::String getPresetName() const { return presetName; }

    // ---- shapes (message thread)
    void setShape (int module, std::vector<anvil::ShapePoint> pts) { engine.shapes[module].setPoints (std::move (pts)); }

    juce::AudioProcessorValueTreeState apvts;
    anvil::Engine engine;
    std::function<void()> onPresetChanged;   // UI refresh hook

private:
    void handleAsyncUpdate() override;
    juce::ValueTree buildState();
    void restoreState (const juce::ValueTree& tree);
    void resetToDefaults();

    juce::AudioBuffer<float> stereoBuf, scBuf;
    int currentFactory = 0;
    juce::String presetName { "Init (clean)" };
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnvilProcessor)
};
