#include "Presets.h"

namespace anvil
{
namespace
{
    struct B
    {
        Preset p;
        B (const char* name, const char* cat) { p.name = name; p.category = cat; }
        B& on (int m) { p.values.push_back ({ pid (m, "on"), 1.f }); return *this; }
        B& set (int m, const char* id, float v) { p.values.push_back ({ pid (m, id), v }); return *this; }
        B& mix (int m, float v) { return set (m, "mix", v); }
        // choice index for shape rate: 0 1/32, 1 1/16T, 2 1/16, 3 1/8T, 4 1/8, 5 1/4, 6 1/2, 7 1 bar, 8 2 bars, 9 4 bars
        B& shape (int m, std::vector<ShapePoint> s, int rate, float depth, int target = -1)
        {
            p.shapes.push_back ({ m, std::move (s) });
            set (m, "shp_rate", (float) rate);
            set (m, "shp_depth", depth);
            if (target >= 0) set (m, "shp_tgt", (float) target);
            return *this;
        }
        B& global (const char* id, float v) { p.values.push_back ({ id, v }); return *this; }
        operator Preset() const { return p; }
    };

    // throw shape: closed for most of the bar, open on the last beat
    std::vector<ShapePoint> lastBeat() { return { { 0, 0, 0 }, { .74f, 0, 0 }, { .75f, 1, 0 }, { .98f, 1, 0 }, { 1, 0, 0 } }; }
    std::vector<ShapePoint> swell()    { return { { 0, 0, .6f }, { 1, 1, 0 } }; }
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = {
        B ("Init (clean)", "Basics"),

        // ---- Rhythm
        B ("Sidechain Pump 1/4", "Rhythm").on (mMod).shape (mMod, shapes::pump(), 5, 0.9f, 0),
        B ("Sidechain Pump 1/8", "Rhythm").on (mMod).shape (mMod, shapes::pump(), 4, 0.75f, 0),
        B ("Trance Gate 1/16", "Rhythm").on (mGate).set (mGate, "smooth", 2.f)
            .shape (mGate, shapes::gate (16, 0b1011011010110111u, 0.7f), 7, 1.f),
        B ("Half-time Chop", "Rhythm").on (mGate).set (mGate, "smooth", 3.f)
            .shape (mGate, shapes::gate (8, 0b10011001u, 0.85f), 8, 1.f),
        B ("Stutter Crush 1/16", "Rhythm").on (mGate).on (mCrush)
            .set (mCrush, "bits", 6.f).set (mCrush, "rate", 8000.f).mix (mCrush, 0.6f)
            .shape (mGate, shapes::gate (16, 0b0111011101110111u, 0.6f), 7, 1.f),
        B ("Auto-Pan 1/8", "Rhythm").on (mMod).shape (mMod, shapes::sine(), 6, 1.f, 1),
        B ("Tremolo 1/16", "Rhythm").on (mMod).shape (mMod, shapes::sine(), 2, 0.6f, 0),

        // ---- Vocals
        B ("Vocal Doubler Wide", "Vocals").on (mEq).set (mEq, "lowcut", 110.f)
            .on (mDoubler).set (mDoubler, "detune", 10.f).set (mDoubler, "delay", 16.f).mix (mDoubler, 0.45f),
        B ("Phone / Radio Vocal", "Vocals").on (mEq).set (mEq, "lowcut", 400.f).set (mEq, "highcut", 3200.f)
            .set (mEq, "midf", 1500.f).set (mEq, "mid", 6.f)
            .on (mDist).set (mDist, "drive", 10.f).set (mDist, "tone", 5000.f).mix (mDist, 0.35f),
        B ("Vocal Throw (last beat)", "Vocals").on (mDelay).set (mDelay, "time", 8.f).set (mDelay, "mode", 1.f)
            .set (mDelay, "feedback", 0.5f).set (mDelay, "tone", 3500.f).mix (mDelay, 0.45f)
            .shape (mDelay, lastBeat(), 7, 1.f, 3)
            .on (mReverb).set (mReverb, "size", 0.75f).mix (mReverb, 0.18f),
        B ("Robot Vocoder", "Vocals").on (mVocoder).set (mVocoder, "carrier", 0.f).set (mVocoder, "chord", 4.f)
            .set (mVocoder, "note", 48.f).set (mVocoder, "tone", 0.6f).mix (mVocoder, 1.f),
        B ("Choir Vocoder", "Vocals").on (mVocoder).set (mVocoder, "chord", 3.f).set (mVocoder, "note", 55.f)
            .set (mVocoder, "release", 80.f).mix (mVocoder, 0.85f)
            .on (mDoubler).mix (mDoubler, 0.4f)
            .on (mReverb).set (mReverb, "size", 0.85f).mix (mReverb, 0.35f),
        B ("Whisper Vocoder", "Vocals").on (mVocoder).set (mVocoder, "carrier", 2.f).set (mVocoder, "tone", 0.8f).mix (mVocoder, 0.8f),
        B ("Deep Voice", "Vocals").on (mFormant).set (mFormant, "shift", -4.f).set (mFormant, "pitch", -5.f),
        B ("Helium Voice", "Vocals").on (mFormant).set (mFormant, "shift", 5.f).set (mFormant, "pitch", 5.f),
        B ("Brighter Voice (formant only)", "Vocals").on (mFormant).set (mFormant, "shift", 3.f),
        B ("Darker Voice (formant only)", "Vocals").on (mFormant).set (mFormant, "shift", -3.f),
        B ("Formant Wobble 1/4", "Vocals").on (mFormant).set (mFormant, "shift", 0.f)
            .shape (mFormant, shapes::sine(), 5, 0.6f, 0),

        // ---- Drums & 808
        B ("808 Grit", "Drums & 808").on (mSat).set (mSat, "drive", 9.f).set (mSat, "warmth", 0.4f).set (mSat, "out", -7.f)
            .on (mDist).set (mDist, "drive", 8.f).set (mDist, "tone", 6000.f).mix (mDist, 0.3f)
            .on (mEq).set (mEq, "low", 2.f),
        B ("Drum Bus Crunch", "Drums & 808").on (mSat).set (mSat, "drive", 6.f)
            .on (mCrush).set (mCrush, "bits", 10.f).set (mCrush, "rate", 22050.f).mix (mCrush, 0.25f)
            .on (mEq).set (mEq, "high", 2.f),
        B ("Hi-hat Shuffle Pan", "Drums & 808").on (mMod).shape (mMod, shapes::triangle(), 5, 0.7f, 1),
        B ("Fold Destroyer", "Drums & 808").on (mDist).set (mDist, "type", 2.f).set (mDist, "drive", 18.f).mix (mDist, 0.5f)
            .shape (mDist, shapes::rampDown(), 5, 0.6f, 0),

        // ---- Lo-fi
        B ("Lo-fi Tape", "Lo-fi").on (mSat).set (mSat, "drive", 8.f).set (mSat, "warmth", 0.6f).set (mSat, "out", -7.f)
            .on (mCrush).set (mCrush, "bits", 12.f).set (mCrush, "rate", 16000.f).mix (mCrush, 0.5f)
            .on (mEq).set (mEq, "lowcut", 60.f).set (mEq, "highcut", 7000.f)
            .on (mFlanger).set (mFlanger, "rate", 0.4f).set (mFlanger, "depth", 0.15f).set (mFlanger, "manual", 4.f)
            .set (mFlanger, "feedback", 0.f).mix (mFlanger, 0.25f),
        B ("Bitcrush Sweep 1 bar", "Lo-fi").on (mCrush).set (mCrush, "rate", 30000.f).set (mCrush, "bits", 10.f)
            .shape (mCrush, shapes::rampDown(), 7, 0.9f, 1),
        B ("Old Radio", "Lo-fi").on (mEq).set (mEq, "lowcut", 500.f).set (mEq, "highcut", 2500.f)
            .on (mSat).set (mSat, "type", 1.f).set (mSat, "drive", 14.f)
            .on (mCrush).set (mCrush, "bits", 9.f).set (mCrush, "rate", 9000.f).mix (mCrush, 0.4f),

        // ---- Space & FX
        B ("Dreamy Space Verb", "Space & FX").on (mReverb).set (mReverb, "size", 0.92f).set (mReverb, "damp", 0.3f)
            .set (mReverb, "predelay", 40.f).mix (mReverb, 0.45f)
            .on (mDelay).set (mDelay, "time", 8.f).set (mDelay, "feedback", 0.5f).set (mDelay, "tone", 4000.f).mix (mDelay, 0.25f),
        B ("Ping-Pong 1/8", "Space & FX").on (mDelay).set (mDelay, "time", 4.f).set (mDelay, "mode", 1.f)
            .set (mDelay, "feedback", 0.45f).mix (mDelay, 0.3f),
        B ("Dub Echo", "Space & FX").on (mDelay).set (mDelay, "time", 8.f).set (mDelay, "feedback", 0.75f)
            .set (mDelay, "tone", 2000.f).set (mDelay, "drive", 0.6f).mix (mDelay, 0.35f),
        B ("Jet Flanger", "Space & FX").on (mFlanger).set (mFlanger, "rate", 0.15f).set (mFlanger, "depth", 0.8f)
            .set (mFlanger, "feedback", 0.8f).mix (mFlanger, 0.5f),
        B ("Wah Wobble 1/8", "Space & FX").on (mEq).set (mEq, "mid", 12.f).set (mEq, "midf", 1200.f)
            .shape (mEq, shapes::sine(), 4, 1.f, 2),
        B ("Riser Build 4 bars", "Space & FX").on (mEq).set (mEq, "lowcut", 1000.f)
            .shape (mEq, swell(), 9, 1.f, 0)
            .on (mReverb).set (mReverb, "size", 0.8f).mix (mReverb, 0.6f)
            .shape (mReverb, swell(), 9, 1.f, 4),

        // ---- Mix
        B ("Warm Master Glue", "Mix").on (mSat).set (mSat, "drive", 3.f).set (mSat, "warmth", 0.2f).mix (mSat, 0.6f)
            .on (mEq).set (mEq, "lowcut", 25.f).set (mEq, "low", 1.f).set (mEq, "high", 1.5f),
        B ("Stereo Widener", "Mix").on (mMod).set (mMod, "width", 1.4f)
            .on (mEq).set (mEq, "lowcut", 30.f),
    };
    return list;
}

} // namespace anvil
