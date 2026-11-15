#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Shape.h"
#include "Dsp.h"

namespace anvil
{

// Per-block timing and side inputs handed to every module.
struct Context
{
    double sampleRate = 44100.0;
    double bpm = 120.0;
    double ppq = 0.0;           // position at the start of this sub-block
    bool playing = false;
    const float* scL = nullptr; // sidechain for this sub-block, or null
    const float* scR = nullptr;
    const float* shape = nullptr; // per-sample shape value (0..1) for this module
    float shapeDepth = 0.f;
    int shapeTarget = -1;
};

class Module
{
public:
    virtual ~Module() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    // knobs: real-unit values (already shape-modulated for the target knob)
    // choices: choice indices
    virtual void process (float* L, float* R, int n, const float* knobs, const int* choices, const Context& ctx) = 0;
    virtual int latency() const { return 0; }
};

// Loaded sound for the sample layer. Swapped in from the message thread.
struct SampleData
{
    juce::AudioBuffer<float> audio;   // at the engine sample rate, stereo
    juce::String name;
};

class Engine
{
public:
    static constexpr int subBlock = 32;

    Engine();
    ~Engine();

    void attach (juce::AudioProcessorValueTreeState& apvts);
    void prepare (double sampleRate, int maxBlock);
    void reset();

    // L/R in place. scL/scR may be null.
    void process (float* L, float* R, int n, const float* scL, const float* scR,
                  bool playing, double bpm, double ppqStart);

    int getLatency() const;

    Shape shapes[numModules];

    // sample layer
    bool loadSample (const juce::File& f);
    void clearSample();
    juce::String getSampleName() const;
    juce::String getSamplePath() const { return samplePath; }

    // level meters for the UI (peak since last read)
    std::atomic<float> meterIn { 0.f }, meterOut { 0.f };
    std::atomic<float> shapePhase[numModules] {};

private:
    struct ModuleParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* mix = nullptr;
        std::vector<juce::RangedAudioParameter*> knobs;
        std::vector<std::atomic<float>*> choices;
        std::atomic<float>* depth = nullptr;
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* target = nullptr;
    };

    std::unique_ptr<Module> mods[numModules];
    ModuleParams mp[numModules];
    float onGain[numModules] {};
    float mixPrev[numModules] {};
    float shapeSmooth[numModules] {};

    std::atomic<float>* inGain = nullptr;
    std::atomic<float>* outGain = nullptr;
    std::atomic<float>* masterMix = nullptr;

    double sr = 44100.0;
    double freePpq = 0.0;
    int maxBlock = 512;

    juce::AudioBuffer<float> dryBuf, wetBuf, smpBuf;
    std::vector<float> shapeBuf;
    dsp::DelayLine dryDelayL, dryDelayR;
    float masterMixPrev = 1.f, inPrev = 1.f, outPrev = 1.f;

    // sample playback
    std::shared_ptr<SampleData> sample, pendingSample;
    juce::SpinLock sampleLock;
    juce::String samplePath;
    double samplePos = 0.0;
    float duckEnv = 0.f;
    void renderSample (float* L, float* R, int n, const float* knobs, const int* choices, bool playing, double ppqStart, double bpm);
};

} // namespace anvil
