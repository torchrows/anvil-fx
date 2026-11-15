#pragma once
#include <JuceHeader.h>

// Describes every effect module, its knobs and the parameters built from them.
// The engine, presets and UI all read this one table.

namespace anvil
{

struct KnobDesc
{
    const char* id;        // suffix, full id is "<module>_<id>"
    const char* name;      // shown under the knob
    float min, max, def;
    float skewCentre;      // <= 0 for linear
    const char* unit;      // "dB", "Hz", "ms", "%", "st", ""
    bool bipolar;          // shape moves it both ways around the knob
};

struct ChoiceDesc
{
    const char* id;
    const char* name;
    juce::StringArray options;
    int def;
};

enum ModuleId
{
    mGate = 0, mEq, mDist, mSat, mCrush, mFormant, mVocoder,
    mDoubler, mFlanger, mMod, mDelay, mReverb, mSample,
    numModules
};

struct ModuleDesc
{
    const char* id;
    const char* name;
    const char* blurb;
    std::vector<KnobDesc> knobs;
    std::vector<ChoiceDesc> choices;
    float defaultMix;
};

inline const juce::StringArray& rateNames()
{
    static const juce::StringArray r { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
    return r;
}

inline double rateBeats (int index)
{
    static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return b[juce::jlimit (0, 9, index)];
}

inline const juce::StringArray& delayTimeNames()
{
    static const juce::StringArray r { "1/32", "1/16", "1/16 dot", "1/8T", "1/8", "1/8 dot", "1/4T", "1/4", "1/4 dot", "1/2", "1 bar" };
    return r;
}

inline double delayTimeBeats (int index)
{
    static const double b[] = { 0.125, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return b[juce::jlimit (0, 10, index)];
}

inline const std::vector<ModuleDesc>& modules()
{
    static const std::vector<ModuleDesc> m = {
        { "gate", "Gate", "Trance gate from the shape, plus a noise gate",
          { { "thresh", "Threshold", -80.f, 0.f, -80.f, 0.f, "dB", false },
            { "release", "Release", 5.f, 500.f, 80.f, 80.f, "ms", false },
            { "smooth", "Smooth", 0.f, 20.f, 2.f, 4.f, "ms", false } },
          {}, 1.f },

        { "eq", "EQ", "Shelves, a sweepable mid band and cut filters",
          { { "lowcut", "Low Cut", 20.f, 1000.f, 20.f, 150.f, "Hz", false },
            { "low", "Low", -18.f, 18.f, 0.f, 0.f, "dB", true },
            { "midf", "Mid Freq", 100.f, 8000.f, 1000.f, 1000.f, "Hz", true },
            { "mid", "Mid", -18.f, 18.f, 0.f, 0.f, "dB", true },
            { "high", "High", -18.f, 18.f, 0.f, 0.f, "dB", true },
            { "highcut", "High Cut", 1000.f, 20000.f, 20000.f, 5000.f, "Hz", false } },
          {}, 1.f },

        { "dist", "Distortion", "Drive with four flavours and a tone filter",
          { { "drive", "Drive", 0.f, 36.f, 12.f, 0.f, "dB", false },
            { "tone", "Tone", 500.f, 20000.f, 12000.f, 3000.f, "Hz", false },
            { "out", "Output", -24.f, 6.f, -6.f, 0.f, "dB", false } },
          { { "type", "Type", { "Soft", "Hard", "Fold", "Fuzz" }, 0 } }, 1.f },

        { "sat", "Saturation", "Warm tape or tube colour",
          { { "drive", "Drive", 0.f, 24.f, 6.f, 0.f, "dB", false },
            { "warmth", "Warmth", -1.f, 1.f, 0.3f, 0.f, "", true },
            { "out", "Output", -24.f, 6.f, -3.f, 0.f, "dB", false } },
          { { "type", "Type", { "Tape", "Tube" }, 0 } }, 1.f },

        { "crush", "Bitcrush", "Fewer bits and a lower sample rate",
          { { "bits", "Bits", 1.f, 16.f, 8.f, 0.f, "bits", false },
            { "rate", "Rate", 500.f, 44100.f, 11025.f, 6000.f, "Hz", false },
            { "jitter", "Jitter", 0.f, 1.f, 0.f, 0.f, "%", false } },
          {}, 1.f },

        { "formant", "Formant", "Drag pad: formant shift left and right, pitch up and down",
          { { "shift", "Formant", -12.f, 12.f, 0.f, 0.f, "st", true },
            { "pitch", "Pitch", -12.f, 12.f, 0.f, 0.f, "st", true } },
          {}, 1.f },

        { "vocoder", "Vocoder", "16-band vocoder with a built-in synth or a sidechain carrier",
          { { "note", "Note", 24.f, 72.f, 48.f, 0.f, "note", false },
            { "tone", "Tone", 0.f, 1.f, 0.6f, 0.f, "%", false },
            { "release", "Release", 5.f, 300.f, 40.f, 50.f, "ms", false },
            { "shift", "Band Shift", -1.f, 1.f, 0.f, 0.f, "oct", true } },
          { { "carrier", "Carrier", { "Saw synth", "Square synth", "Noise", "Sidechain" }, 0 },
            { "chord", "Chord", { "Single", "Octaves", "Fifth", "Major", "Minor", "Sus4" }, 0 } }, 1.f },

        { "doubler", "Doubler", "Two detuned copies spread left and right",
          { { "detune", "Detune", 0.f, 40.f, 12.f, 0.f, "ct", false },
            { "delay", "Delay", 5.f, 40.f, 18.f, 0.f, "ms", false },
            { "width", "Width", 0.f, 1.f, 1.f, 0.f, "%", false } },
          {}, 0.5f },

        { "flanger", "Flanger", "Jet-plane sweep with feedback",
          { { "rate", "Rate", 0.02f, 8.f, 0.3f, 0.8f, "Hz", false },
            { "depth", "Depth", 0.f, 1.f, 0.6f, 0.f, "%", false },
            { "manual", "Manual", 0.3f, 10.f, 2.f, 2.f, "ms", true },
            { "feedback", "Feedback", -0.95f, 0.95f, 0.5f, 0.f, "%", true } },
          {}, 0.5f },

        { "mod", "Modulation", "Volume and pan shaping, tremolo and auto-pan",
          { { "volume", "Volume", 0.f, 1.f, 1.f, 0.f, "%", false },
            { "pan", "Pan", -1.f, 1.f, 0.f, 0.f, "pan", true },
            { "width", "Width", 0.f, 2.f, 1.f, 0.f, "%", false } },
          {}, 1.f },

        { "delay", "Delay", "Tempo-synced echoes, darker on every repeat",
          { { "feedback", "Feedback", 0.f, 0.95f, 0.4f, 0.f, "%", false },
            { "tone", "Tone", 500.f, 20000.f, 6000.f, 3000.f, "Hz", false },
            { "drive", "Grit", 0.f, 1.f, 0.f, 0.f, "%", false } },
          { { "time", "Time", delayTimeNames(), 5 },
            { "mode", "Mode", { "Stereo", "Ping-pong" }, 1 } }, 0.3f },

        { "reverb", "Reverb", "Room to hall with pre-delay",
          { { "size", "Size", 0.f, 1.f, 0.6f, 0.f, "%", false },
            { "damp", "Damping", 0.f, 1.f, 0.5f, 0.f, "%", false },
            { "predelay", "Pre-delay", 0.f, 200.f, 20.f, 0.f, "ms", false },
            { "width", "Width", 0.f, 1.f, 1.f, 0.f, "%", false } },
          {}, 0.3f },

        { "sample", "Sample", "Blend a loaded sound (vinyl, noise, texture) under the track",
          { { "level", "Level", -48.f, 6.f, -12.f, 0.f, "dB", false },
            { "pitch", "Pitch", -12.f, 12.f, 0.f, 0.f, "st", true },
            { "duck", "Duck", 0.f, 1.f, 0.f, 0.f, "%", false } },
          { { "route", "Route", { "After FX", "Through FX" }, 0 },
            { "play", "Play", { "Always", "With host" }, 1 } }, 1.f },
    };
    return m;
}

inline juce::String pid (int module, const char* suffix)
{
    return juce::String (modules()[(size_t) module].id) + "_" + suffix;
}

// Shape target list for a module: its knobs, then Mix.
inline juce::StringArray targetNames (int module)
{
    juce::StringArray t;
    if (module == mGate)
        return { "Volume" };
    for (auto& k : modules()[(size_t) module].knobs)
        t.add (k.name);
    t.add ("Mix");
    return t;
}

inline int defaultTarget (int module)
{
    switch (module)
    {
        case mGate:    return 0;  // the gate always shapes volume
        case mEq:      return 2;  // Mid freq sweep
        case mDist:    return 0;
        case mSat:     return 0;
        case mCrush:   return 1;
        case mFormant: return 0;
        case mVocoder: return 4;
        case mDoubler: return 2;
        case mFlanger: return 2;
        case mMod:     return 0;
        case mDelay:   return 3;
        case mReverb:  return 3;
        case mSample:  return 0;
        default:       return 0;
    }
}

inline juce::NormalisableRange<float> knobRange (const KnobDesc& k)
{
    juce::NormalisableRange<float> r (k.min, k.max);
    if (k.skewCentre > 0.f && k.skewCentre > k.min && k.skewCentre < k.max)
        r.setSkewForCentre (k.skewCentre);
    return r;
}


inline juce::String formatValue (float v, const juce::String& unit)
{
    using juce::String;
    if (unit == "dB")   return String (v, 1) + " dB";
    if (unit == "Hz")   return v >= 1000.f ? String (v / 1000.f, v >= 10000.f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
    if (unit == "ms")   return v < 10.f ? String (v, 1) + " ms" : String (juce::roundToInt (v)) + " ms";
    if (unit == "%")    return String (juce::roundToInt (v * 100.f)) + "%";
    if (unit == "st")   { const float r = std::round (v * 10.f) / 10.f; return (r > 0 ? "+" : "") + String (r, 1) + " st"; }
    if (unit == "ct")   return String (v, 1) + " ct";
    if (unit == "oct")  return (v > 0 ? "+" : "") + String (v, 2) + " oct";
    if (unit == "bits") return String (v, 1) + " bits";
    if (unit == "pan")
    {
        const int p = juce::roundToInt (v * 100.f);
        return p == 0 ? String ("C") : (p < 0 ? "L " + String (-p) : "R " + String (p));
    }
    if (unit == "note")
        return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 4);
    return String (v, 2);
}

inline float parseValue (const juce::String& t, const juce::String& unit)
{
    auto s = t.trim().toLowerCase();
    float v = s.retainCharacters ("-+.0123456789").getFloatValue();
    if (unit == "Hz" && s.containsChar ('k')) v *= 1000.f;
    if (unit == "%") v /= 100.f;
    if (unit == "pan")
    {
        if (s.startsWith ("c")) return 0.f;
        v /= 100.f;
        if (s.startsWith ("l")) v = -std::abs (v);
    }
    return v;
}

inline juce::AudioParameterFloatAttributes floatAttrs (const juce::String& unit)
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([unit] (float v, int) { return formatValue (v, unit); })
        .withValueFromStringFunction ([unit] (const juce::String& t) { return parseValue (t, unit); });
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    int v = 1;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "in_gain", v }, "Input", NormalisableRange<float> (-24.f, 24.f), 0.f, floatAttrs ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out_gain", v }, "Output", NormalisableRange<float> (-24.f, 24.f), 0.f, floatAttrs ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master_mix", v }, "Master Mix", NormalisableRange<float> (0.f, 1.f), 1.f, floatAttrs ("%")));

    for (int m = 0; m < numModules; ++m)
    {
        auto& d = modules()[(size_t) m];
        String prefix = String (d.name) + " ";

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid (m, "on"), v }, prefix + "On", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, "mix"), v }, prefix + "Mix", NormalisableRange<float> (0.f, 1.f), d.defaultMix, floatAttrs ("%")));

        for (auto& k : d.knobs)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, k.id), v }, prefix + k.name, knobRange (k), k.def, floatAttrs (k.unit)));

        for (auto& c : d.choices)
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (m, c.id), v }, prefix + c.name, c.options, c.def));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid (m, "shp_depth"), v }, prefix + "Shape Depth", NormalisableRange<float> (0.f, 1.f), 0.f, floatAttrs ("%")));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (m, "shp_rate"), v }, prefix + "Shape Rate", rateNames(), 5));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid (m, "shp_tgt"), v }, prefix + "Shape Target", targetNames (m), defaultTarget (m)));
    }

    return layout;
}

} // namespace anvil
