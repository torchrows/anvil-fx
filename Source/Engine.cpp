#include "Engine.h"
#include "Spectral.h"

namespace anvil
{
using namespace dsp;

namespace
{
    // Shape modulation of one value, in real units, for a knob description.
    inline float modulateReal (const KnobDesc& k, float real, float s, float depth)
    {
        const auto range = knobRange (k);
        float norm = range.convertTo0to1 (juce::jlimit (k.min, k.max, real));
        if (k.bipolar)
            norm = norm + depth * (s - 0.5f);
        else
            norm = norm * (1.f - depth + depth * s);
        return range.convertFrom0to1 (juce::jlimit (0.f, 1.f, norm));
    }

    // ------------------------------------------------------------------ Gate
    class GateModule : public Module
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { env = 0; gate = 1; sm.z = 1; }

        void process (float* L, float* R, int n, const float* k, const int*, const Context& ctx) override
        {
            const float thr = dbToGain (k[0]);
            const bool gateOff = k[0] <= -79.5f;
            const float rel = std::exp (-1.f / (0.001f * k[1] * (float) sr));
            const float att = std::exp (-1.f / (0.001f * 0.5f * (float) sr));
            sm.setTime (sr, k[2]);
            for (int i = 0; i < n; ++i)
            {
                const float x = std::max (std::abs (L[i]), std::abs (R[i]));
                env = x > env ? x : env * rel;
                const float target = (gateOff || env > thr) ? 1.f : 0.f;
                gate = target > gate ? target + att * (gate - target) : target + rel * (gate - target);
                const float s = ctx.shape ? ctx.shape[i] : 1.f;
                const float sg = sm.process (1.f - ctx.shapeDepth * (1.f - s));
                const float g = gate * sg;
                L[i] *= g; R[i] *= g;
            }
        }

    private:
        double sr = 44100;
        float env = 0, gate = 1;
        OnePole sm;
    };

    // ------------------------------------------------------------------ EQ
    class EqModule : public Module
    {
    public:
        void prepare (double s, int) override { sr = s; for (auto& v : last) v = -1e9f; reset(); }
        void reset() override { for (auto& c : f) for (auto& b : c) b.reset(); }

        void process (float* L, float* R, int n, const float* k, const int*, const Context&) override
        {
            bool changed = false;
            for (int i = 0; i < 6; ++i)
                if (std::abs (k[i] - last[i]) > 1.0e-4f) { changed = true; last[i] = k[i]; }

            if (changed)
            {
                for (auto& c : f)
                {
                    c[0].highpass (sr, k[0], 0.7071f);
                    c[1].lowShelf (sr, 110.f, k[1]);
                    c[2].peak (sr, k[2], 0.8f, k[3]);
                    c[3].highShelf (sr, 8000.f, k[4]);
                    c[4].lowpass (sr, k[5], 0.7071f);
                }
                useLowCut = k[0] > 20.5f;
                useHighCut = k[5] < 19900.f;
            }

            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                auto& b = f[c];
                float* x = ch[c];
                for (int i = 0; i < n; ++i)
                {
                    float v = x[i];
                    if (useLowCut) v = b[0].process (v);
                    v = b[1].process (v);
                    v = b[2].process (v);
                    v = b[3].process (v);
                    if (useHighCut) v = b[4].process (v);
                    x[i] = v;
                }
            }
        }

    private:
        double sr = 44100;
        Biquad f[2][5];
        float last[6] {};
        bool useLowCut = false, useHighCut = false;
    };

    // ------------------------------------------------------------------ Distortion
    class DistModule : public Module
    {
    public:
        void prepare (double s, int maxBlock) override
        {
            sr = s;
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
            os->initProcessing ((size_t) maxBlock);
            reset();
        }

        void reset() override
        {
            if (os) os->reset();
            for (auto& t : tone) t.reset();
            for (auto& d : dc) d.reset();
            lastTone = -1;
        }

        static inline float shapeSample (float x, int type) noexcept
        {
            switch (type)
            {
                case 1: return juce::jlimit (-1.f, 1.f, x);
                case 2: return std::sin (juce::jlimit (-24.f, 24.f, x) * pi * 0.5f);
                case 3: { const float b = x + 0.25f;
                          return b > 0.f ? 1.f - std::exp (-b) : -0.75f * (1.f - std::exp (1.6f * b)); }
                default: return std::tanh (x);
            }
        }

        void process (float* L, float* R, int n, const float* k, const int* c, const Context&) override
        {
            const float g = dbToGain (k[0]);
            const int type = c[0];
            const float outG = dbToGain (k[2]);

            float* chans[2] = { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            auto up = os->processSamplesUp (block);
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    d[i] = shapeSample (d[i] * g, type);
            }
            os->processSamplesDown (block);

            if (std::abs (k[1] - lastTone) > 0.5f)
            {
                for (auto& t : tone) t.lowpass (sr, k[1], 0.7071f);
                lastTone = k[1];
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = chans[ch];
                for (int i = 0; i < n; ++i)
                {
                    float v = x[i];
                    if (type == 3) v = dc[ch].process (v);
                    if (k[1] < 19900.f) v = tone[ch].process (v);
                    x[i] = v * outG;
                }
            }
        }

    private:
        double sr = 44100;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        Biquad tone[2];
        DcBlocker dc[2];
        float lastTone = -1;
    };

    // ------------------------------------------------------------------ Saturation
    class SatModule : public Module
    {
    public:
        void prepare (double s, int) override { sr = s; lastW = -9; reset(); }
        void reset() override { for (auto& b : lo) b.reset(); for (auto& b : hi) b.reset(); for (auto& d : dc) d.reset(); }

        void process (float* L, float* R, int n, const float* k, const int* c, const Context&) override
        {
            const float g = dbToGain (k[0]);
            const float norm = 1.f / std::tanh (std::max (1.f, g));
            const float outG = dbToGain (k[2]);
            const bool tube = c[0] == 1;
            if (std::abs (k[1] - lastW) > 1.0e-3f)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    lo[ch].lowShelf (sr, 180.f, 5.f * k[1]);
                    hi[ch].highShelf (sr, 7000.f, -3.5f * k[1]);
                }
                lastW = k[1];
            }
            float* chans[2] = { L, R };
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = chans[ch];
                for (int i = 0; i < n; ++i)
                {
                    float v = x[i] * g;
                    if (tube)
                        v = dc[ch].process (v >= 0.f ? std::tanh (v) : 1.15f * std::tanh (0.8f * v));
                    else
                        v = std::tanh (v + 0.05f * v * v * v);
                    v *= norm;
                    v = hi[ch].process (lo[ch].process (v));
                    x[i] = v * outG;
                }
            }
        }

    private:
        double sr = 44100;
        Biquad lo[2], hi[2];
        DcBlocker dc[2];
        float lastW = -9;
    };

    // ------------------------------------------------------------------ Bitcrush
    class CrushModule : public Module
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { phase = 1; hold[0] = hold[1] = 0; }

        void process (float* L, float* R, int n, const float* k, const int*, const Context&) override
        {
            const float steps = std::pow (2.f, k[0] - 1.f);
            const float inc = juce::jlimit (0.f, 1.f, k[1] / (float) sr);
            const float jit = k[2];
            for (int i = 0; i < n; ++i)
            {
                phase += inc;
                if (phase >= 1.f)
                {
                    phase -= 1.f;
                    if (jit > 0.f) phase -= jit * 0.5f * rng.next();
                    hold[0] = L[i]; hold[1] = R[i];
                }
                L[i] = std::round (hold[0] * steps) / steps;
                R[i] = std::round (hold[1] * steps) / steps;
            }
        }

    private:
        double sr = 44100;
        float phase = 1, hold[2] {};
        Rng rng;
    };

    // ------------------------------------------------------------------ Formant / pitch (owns its mix)
    class FormantModule : public Module
    {
    public:
        void prepare (double s, int) override
        {
            for (auto& p : shifter) p.prepare (s);
            for (auto& d : dryDelay) d.prepare (SpectralShifter::totalLatency + 8);
        }
        void reset() override { for (auto& p : shifter) p.reset(); for (auto& d : dryDelay) d.reset(); }
        int latency() const override { return SpectralShifter::totalLatency; }

        void process (float* L, float* R, int n, const float* k, const int*, const Context&) override
        {
            const float fr = std::pow (2.f, k[0] / 12.f);
            const float pr = std::pow (2.f, k[1] / 12.f);
            float* chans[2] = { L, R };
            for (int ch = 0; ch < 2; ++ch)
            {
                shifter[ch].setRatios (pr, fr);
                float* x = chans[ch];
                for (int i = 0; i < n; ++i)
                    x[i] = shifter[ch].processSample (x[i]);
            }
        }

        // dry signal delayed to line up with the shifted output
        void delayDry (float* L, float* R, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                dryDelay[0].push (L[i]); L[i] = dryDelay[0].readInt (SpectralShifter::totalLatency);
                dryDelay[1].push (R[i]); R[i] = dryDelay[1].readInt (SpectralShifter::totalLatency);
            }
        }

    private:
        SpectralShifter shifter[2];
        DelayLine dryDelay[2];
    };

    // ------------------------------------------------------------------ Vocoder
    class VocoderModule : public Module
    {
    public:
        static constexpr int bands = 16;

        void prepare (double s, int) override
        {
            sr = s;
            for (int b = 0; b < bands; ++b)
                centre[b] = 150.f * std::pow (8000.f / 150.f, (float) b / (float) (bands - 1));
            for (int b = 0; b < bands; ++b)
            {
                modF[b][0].bandpass (sr, centre[b], 4.f);
                modF[b][1].bandpass (sr, centre[b], 4.f);
            }
            lastShift = -9;
            reset();
        }

        void reset() override
        {
            for (auto& b : modF) for (auto& f : b) f.reset();
            for (auto& b : carF) for (auto& f : b) f.reset();
            for (auto& e : env) e = 0;
            for (auto& o : osc) o.phase = 0;
        }

        void process (float* L, float* R, int n, const float* k, const int* c, const Context& ctx) override
        {
            if (std::abs (k[3] - lastShift) > 1.0e-3f)
            {
                const float m = std::pow (2.f, k[3]);
                for (int b = 0; b < bands; ++b)
                {
                    carF[b][0].bandpass (sr, centre[b] * m, 4.f);
                    carF[b][1].bandpass (sr, centre[b] * m, 4.f);
                }
                lastShift = k[3];
            }

            static const int chords[6][4] = { { 0, 99, 99, 99 }, { 0, 12, -12, 99 }, { 0, 7, 99, 99 },
                                              { 0, 4, 7, 99 }, { 0, 3, 7, 99 }, { 0, 5, 7, 99 } };
            const int chord = juce::jlimit (0, 5, c[1]);
            int voices = 0;
            for (int v = 0; v < 4; ++v)
            {
                if (chords[chord][v] == 99) continue;
                const float note = k[0] + (float) chords[chord][v];
                osc[voices++].inc = 440.0 * std::pow (2.0, (note - 69.0) / 12.0) / sr;
            }
            const float voiceNorm = 1.f / std::sqrt ((float) voices);
            const int carrier = (c[0] == 3 && ctx.scL == nullptr) ? 0 : c[0];

            const float tone = k[1];
            const float rel = std::exp (-1.f / (0.001f * k[2] * (float) sr));
            const float att = std::exp (-1.f / (0.001f * 1.5f * (float) sr));
            float gain[bands];
            for (int b = 0; b < bands; ++b)
            {
                const float pos = (float) b / (float) (bands - 1) * 2.f - 1.f;
                gain[b] = std::max (0.1f, 1.f + (tone - 0.5f) * 1.6f * pos);
            }

            for (int i = 0; i < n; ++i)
            {
                const float mod = 0.5f * (L[i] + R[i]);

                float car = 0.f;
                if (carrier == 2) car = rng.bipolar() * 1.1f;
                else if (carrier == 3) car = 0.5f * (ctx.scL[i] + (ctx.scR ? ctx.scR[i] : ctx.scL[i]));
                else
                {
                    for (int v = 0; v < voices; ++v)
                        car += carrier == 1 ? osc[v].square() : osc[v].saw();
                    car *= voiceNorm * 0.5f;
                }
                const float sib = rng.bipolar() * 0.35f * tone;

                float outL = 0.f, outR = 0.f;
                for (int b = 0; b < bands; ++b)
                {
                    const float m = modF[b][1].process (modF[b][0].process (mod));
                    const float a = std::abs (m);
                    env[b] = a > env[b] ? a + att * (env[b] - a) : a + rel * (env[b] - a);
                    float cin = car;
                    if (b >= bands - 4) cin += sib;
                    const float cb = carF[b][1].process (carF[b][0].process (cin));
                    const float y = cb * env[b] * gain[b];
                    if (b & 1) { outL += y * 0.8f; outR += y; } else { outL += y; outR += y * 0.8f; }
                }
                L[i] = outL * makeup;
                R[i] = outR * makeup;
            }
        }

    private:
        double sr = 44100;
        float centre[bands] {};
        Biquad modF[bands][2], carF[bands][2];
        float env[bands] {};
        BlepOsc osc[4];
        Rng rng;
        float lastShift = -9;
        static constexpr float makeup = 18.f;
    };

    // ------------------------------------------------------------------ Doubler
    class DoublerModule : public Module
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (0.1 * sr));
            reset();
        }
        void reset() override { for (auto& d : dl) d.reset(); ph[0] = 0; ph[1] = 0.37; }

        void process (float* L, float* R, int n, const float* k, const int*, const Context&) override
        {
            static const double rates[2] = { 0.23, 0.31 };
            const double ratio = std::pow (2.0, k[0] / 1200.0) - 1.0;
            const float base = k[1] * 0.001f * (float) sr;
            float* chans[2] = { L, R };
            for (int ch = 0; ch < 2; ++ch)
            {
                const double inc = rates[ch] / sr;
                const float depth = (float) (ratio / (twoPi * rates[ch]) * sr);
                const float b = ch == 0 ? base : base * 1.17f;
                float* x = chans[ch];
                for (int i = 0; i < n; ++i)
                {
                    dl[ch].push (x[i]);
                    x[i] = dl[ch].read (b + depth * (1.f + (float) std::sin (twoPi * ph[ch])));
                    ph[ch] += inc; if (ph[ch] >= 1.0) ph[ch] -= 1.0;
                }
            }
            const float w = k[2];
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * w;
                L[i] = m + s; R[i] = m - s;
            }
        }

    private:
        double sr = 44100;
        DelayLine dl[2];
        double ph[2] {};
    };

    // ------------------------------------------------------------------ Flanger
    class FlangerModule : public Module
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (0.03 * sr));
            reset();
        }
        void reset() override { for (auto& d : dl) d.reset(); fb[0] = fb[1] = 0; }

        void process (float* L, float* R, int n, const float* k, const int*, const Context&) override
        {
            const double inc = k[0] / sr;
            const float depth = k[1] * 0.95f;
            const float man = k[2] * 0.001f * (float) sr;
            const float feedback = k[3];
            float* chans[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double p = phase + (ch == 1 ? 0.25 : 0.0);
                    const float lfo = (float) std::sin (twoPi * p);
                    const float d = std::max (2.f, man * (1.f + depth * lfo));
                    const float y = dl[ch].read (d);
                    dl[ch].push (std::tanh (chans[ch][i] + feedback * y));
                    chans[ch][i] = y;
                }
                phase += inc; if (phase >= 1.0) phase -= 1.0;
            }
        }

    private:
        double sr = 44100, phase = 0;
        DelayLine dl[2];
        float fb[2] {};
    };

    // ------------------------------------------------------------------ Modulation (volume / pan / width, per sample)
    class ModModule : public Module
    {
    public:
        void prepare (double, int) override {}
        void reset() override {}

        void process (float* L, float* R, int n, const float* k, const int*, const Context& ctx) override
        {
            auto& d = modules()[(size_t) mMod];
            const int t = ctx.shapeTarget;
            for (int i = 0; i < n; ++i)
            {
                float vol = k[0], pan = k[1], width = k[2];
                if (ctx.shape && t >= 0 && t < 3 && ctx.shapeDepth > 0.f)
                {
                    const float s = ctx.shape[i];
                    if (t == 0) vol = modulateReal (d.knobs[0], vol, s, ctx.shapeDepth);
                    else if (t == 1) pan = modulateReal (d.knobs[1], pan, s, ctx.shapeDepth);
                    else width = modulateReal (d.knobs[2], width, s, ctx.shapeDepth);
                }
                const float m = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]) * width;
                float l = m + sd, r = m - sd;
                const float ang = (pan + 1.f) * pi * 0.25f;
                const float gl = std::min (1.f, std::cos (ang) * 1.41421356f);
                const float gr = std::min (1.f, std::sin (ang) * 1.41421356f);
                L[i] = l * gl * vol;
                R[i] = r * gr * vol;
            }
        }
    };

    // ------------------------------------------------------------------ Delay
    class DelayModule : public Module
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (8.5 * sr));
            timeSm.setTime (sr, 60.f);
            timeSm.z = -1;
            lastTone = -1;
            reset();
        }
        void reset() override { for (auto& d : dl) d.reset(); for (auto& t : tone) t.reset(); timeSm.z = -1; }

        void process (float* L, float* R, int n, const float* k, const int* c, const Context& ctx) override
        {
            const double beats = delayTimeBeats (c[0]);
            const float target = (float) juce::jlimit (1.0, 8.0 * sr, beats * 60.0 / std::max (20.0, ctx.bpm) * sr);
            if (timeSm.z < 0) timeSm.z = target;
            if (std::abs (k[1] - lastTone) > 0.5f) { for (auto& t : tone) t.lowpass (sr, k[1], 0.6f); lastTone = k[1]; }
            const float fb = k[0], grit = k[2];
            const bool ping = c[1] == 1;
            for (int i = 0; i < n; ++i)
            {
                const float t = timeSm.process (target);
                const float yl = dl[0].read (t), yr = dl[1].read (t);
                float fl = tone[0].process (yl) * fb, fr = tone[1].process (yr) * fb;
                if (grit > 0.f)
                {
                    const float dr = 1.f + 4.f * grit;
                    fl += grit * (std::tanh (fl * dr) / dr * 1.5f - fl);
                    fr += grit * (std::tanh (fr * dr) / dr * 1.5f - fr);
                }
                if (ping)
                {
                    dl[0].push (0.5f * (L[i] + R[i]) + fr);
                    dl[1].push (fl);
                }
                else
                {
                    dl[0].push (L[i] + fl);
                    dl[1].push (R[i] + fr);
                }
                L[i] = yl; R[i] = yr;
            }
        }

    private:
        double sr = 44100;
        DelayLine dl[2];
        Biquad tone[2];
        OnePole timeSm;
        float lastTone = -1;
    };

    // ------------------------------------------------------------------ Reverb
    class ReverbModule : public Module
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            verb.setSampleRate (s);
            for (auto& d : pre) d.prepare ((int) (0.25 * sr));
            reset();
        }
        void reset() override { verb.reset(); for (auto& d : pre) d.reset(); }

        void process (float* L, float* R, int n, const float* k, const int*, const Context&) override
        {
            juce::Reverb::Parameters p;
            p.roomSize = k[0];
            p.damping = k[1];
            p.width = k[3];
            p.wetLevel = 0.45f;
            p.dryLevel = 0.f;
            if (p.roomSize != last.roomSize || p.damping != last.damping || p.width != last.width)
            {
                verb.setParameters (p);
                last = p;
            }
            const int pd = (int) (k[2] * 0.001f * (float) sr);
            for (int i = 0; i < n; ++i)
            {
                pre[0].push (L[i]); pre[1].push (R[i]);
                L[i] = pd > 0 ? pre[0].readInt (pd) : L[i];
                R[i] = pd > 0 ? pre[1].readInt (pd) : R[i];
            }
            verb.processStereo (L, R, n);
        }

    private:
        double sr = 44100;
        juce::Reverb verb;
        juce::Reverb::Parameters last { -1, -1, -1, -1, -1, -1 };
        DelayLine pre[2];
    };
}

// ====================================================================== Engine

Engine::Engine()
{
    mods[mGate] = std::make_unique<GateModule>();
    mods[mEq] = std::make_unique<EqModule>();
    mods[mDist] = std::make_unique<DistModule>();
    mods[mSat] = std::make_unique<SatModule>();
    mods[mCrush] = std::make_unique<CrushModule>();
    mods[mFormant] = std::make_unique<FormantModule>();
    mods[mVocoder] = std::make_unique<VocoderModule>();
    mods[mDoubler] = std::make_unique<DoublerModule>();
    mods[mFlanger] = std::make_unique<FlangerModule>();
    mods[mMod] = std::make_unique<ModModule>();
    mods[mDelay] = std::make_unique<DelayModule>();
    mods[mReverb] = std::make_unique<ReverbModule>();
    // mSample is rendered by the engine itself
}

Engine::~Engine() = default;

void Engine::attach (juce::AudioProcessorValueTreeState& s)
{
    inGain = s.getRawParameterValue ("in_gain");
    outGain = s.getRawParameterValue ("out_gain");
    masterMix = s.getRawParameterValue ("master_mix");

    for (int m = 0; m < numModules; ++m)
    {
        auto& d = modules()[(size_t) m];
        auto& p = mp[m];
        p.on = s.getRawParameterValue (pid (m, "on"));
        p.mix = s.getRawParameterValue (pid (m, "mix"));
        for (auto& k : d.knobs)
            p.knobs.push_back (s.getParameter (pid (m, k.id)));
        for (auto& c : d.choices)
            p.choices.push_back (s.getRawParameterValue (pid (m, c.id)));
        p.depth = s.getRawParameterValue (pid (m, "shp_depth"));
        p.rate = s.getRawParameterValue (pid (m, "shp_rate"));
        p.target = s.getRawParameterValue (pid (m, "shp_tgt"));
    }
}

void Engine::prepare (double sampleRate, int mb)
{
    sr = sampleRate;
    maxBlock = std::max (mb, subBlock);
    for (auto& m : mods)
        if (m) m->prepare (sr, subBlock);
    dryBuf.setSize (2, maxBlock);
    wetBuf.setSize (2, subBlock);
    smpBuf.setSize (2, maxBlock);
    shapeBuf.assign ((size_t) maxBlock, 1.f);
    dryDelayL.prepare (8192);
    dryDelayR.prepare (8192);
    reset();
}

void Engine::reset()
{
    for (auto& m : mods)
        if (m) m->reset();
    for (int m = 0; m < numModules; ++m)
    {
        onGain[m] = mp[m].on ? (mp[m].on->load() > 0.5f ? 1.f : 0.f) : 0.f;
        mixPrev[m] = mp[m].mix ? mp[m].mix->load() : 1.f;
        shapeSmooth[m] = 1.f;
    }
    dryDelayL.reset();
    dryDelayR.reset();
    freePpq = 0.0;
    samplePos = 0.0;
    duckEnv = 0.f;
}

int Engine::getLatency() const
{
    return (mp[mFormant].on && mp[mFormant].on->load() > 0.5f) ? mods[mFormant]->latency() : 0;
}

bool Engine::loadSample (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const auto maxLen = (juce::int64) (reader->sampleRate * 120.0);   // cap at two minutes
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    juce::AudioBuffer<float> src (2, len);
    reader->read (&src, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        src.copyFrom (1, 0, src, 0, 0, len);

    auto data = std::make_shared<SampleData>();
    const double ratio = reader->sampleRate / sr;
    const int outLen = (int) ((double) len / ratio);
    data->audio.setSize (2, std::max (1, outLen));
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, src.getReadPointer (ch), data->audio.getWritePointer (ch), outLen);
    }
    data->name = f.getFileNameWithoutExtension();

    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        pendingSample = data;   // freed on this thread once the audio thread swaps it out
        std::swap (sample, pendingSample);
    }
    pendingSample.reset();
    samplePath = f.getFullPathName();
    return true;
}

void Engine::clearSample()
{
    std::shared_ptr<SampleData> old;
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        std::swap (old, sample);
    }
    samplePath.clear();
}

juce::String Engine::getSampleName() const
{
    auto s = sample;
    return s ? s->name : juce::String();
}

void Engine::renderSample (float* L, float* R, int n, const float* k, const int* c, bool playing, double ppqStart, double bpm)
{
    std::fill (L, L + n, 0.f);
    std::fill (R, R + n, 0.f);

    const juce::SpinLock::ScopedTryLockType sl (sampleLock);
    if (! sl.isLocked() || sample == nullptr)
        return;

    auto& a = sample->audio;
    const int len = a.getNumSamples();
    if (len < 2) return;

    const bool withHost = c[1] == 1;
    if (withHost && ! playing) return;

    const double rate = std::pow (2.0, k[1] / 12.0);
    if (withHost)
        samplePos = std::fmod (std::max (0.0, ppqStart) * 60.0 / bpm * sr * rate, (double) len);

    const float g = dbToGain (k[0]);
    auto* sl0 = a.getReadPointer (0);
    auto* sl1 = a.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const int i0 = (int) samplePos;
        const int i1 = (i0 + 1) % len;
        const float f = (float) (samplePos - i0);
        L[i] = (sl0[i0] + (sl0[i1] - sl0[i0]) * f) * g;
        R[i] = (sl1[i0] + (sl1[i1] - sl1[i0]) * f) * g;
        samplePos += rate;
        if (samplePos >= len) samplePos -= len;
    }
}

void Engine::process (float* L, float* R, int n, const float* scL, const float* scR,
                      bool playing, double bpm, double ppqStart)
{
    if (n <= 0) return;
    if (bpm <= 0.0) bpm = 120.0;
    if (! playing) ppqStart = freePpq;
    const double beatsPerSample = bpm / 60.0 / sr;

    // input gain
    {
        const float target = dbToGain (inGain->load());
        float g = inPrev; const float step = (target - g) / (float) n;
        float pk = 0.f;
        for (int i = 0; i < n; ++i) { g += step; L[i] *= g; R[i] *= g; pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i]))); }
        inPrev = target;
        meterIn.store (std::max (meterIn.load(), pk));
    }

    // dry copy for the master mix, delayed by the plug-in latency
    const int lat = getLatency();
    float* dL = dryBuf.getWritePointer (0);
    float* dR = dryBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        dryDelayL.push (L[i]); dryDelayR.push (R[i]);
        dL[i] = lat > 0 ? dryDelayL.readInt (lat) : L[i];
        dR[i] = lat > 0 ? dryDelayR.readInt (lat) : R[i];
    }

    // sample layer
    const bool smpOn = mp[mSample].on->load() > 0.5f || onGain[mSample] > 0.f;
    float smpRoute = 0;
    if (smpOn)
    {
        auto& d = modules()[(size_t) mSample];
        float k[3]; int c[2];
        for (size_t i = 0; i < d.knobs.size(); ++i)
            k[i] = mp[mSample].knobs[i]->convertFrom0to1 (mp[mSample].knobs[i]->getValue());
        for (size_t i = 0; i < d.choices.size(); ++i)
            c[i] = (int) mp[mSample].choices[i]->load();
        smpRoute = (float) c[0];

        // shape on level (or mix) at block rate
        const double beats = rateBeats ((int) mp[mSample].rate->load());
        const float s = shapes[mSample].lookup ((ppqStart + 0.5 * n * beatsPerSample) / beats);
        shapePhase[mSample].store ((float) std::fmod (ppqStart / beats, 1.0));
        const float depth = mp[mSample].depth->load();
        const int tgt = (int) mp[mSample].target->load();
        float mixVal = mp[mSample].mix->load();
        if (depth > 0.f)
        {
            if (tgt < 3) k[tgt] = modulateReal (d.knobs[(size_t) tgt], k[tgt], s, depth);
            else mixVal *= (1.f - depth + depth * s);
        }

        float* sL = smpBuf.getWritePointer (0);
        float* sR = smpBuf.getWritePointer (1);
        renderSample (sL, sR, n, k, c, playing, ppqStart, bpm);

        const float onT = mp[mSample].on->load() > 0.5f ? 1.f : 0.f;
        const float step = (float) n / (0.01f * (float) sr);
        const float g0 = onGain[mSample] * mixPrev[mSample];
        onGain[mSample] = onT > onGain[mSample] ? std::min (onT, onGain[mSample] + step) : std::max (onT, onGain[mSample] - step);
        const float g1 = onGain[mSample] * mixVal;
        mixPrev[mSample] = mixVal;
        const float duckAmt = k[2];
        const float duckRel = std::exp (-1.f / (0.15f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const float g = g0 + (g1 - g0) * (float) i / (float) n;
            const float in = std::max (std::abs (L[i]), std::abs (R[i]));
            duckEnv = in > duckEnv ? in : duckEnv * duckRel;
            const float duck = 1.f - duckAmt * juce::jlimit (0.f, 1.f, duckEnv * 3.f);
            sL[i] *= g * duck; sR[i] *= g * duck;
        }
        if (smpRoute > 0.5f)
            for (int i = 0; i < n; ++i) { L[i] += sL[i]; R[i] += sR[i]; }
    }

    // effect chain
    for (int m = 0; m < numModules; ++m)
    {
        if (m == mSample || ! mods[m]) continue;
        auto& p = mp[m];
        const float onT = p.on->load() > 0.5f ? 1.f : 0.f;
        if (onT == 0.f && onGain[m] == 0.f) continue;

        auto& d = modules()[(size_t) m];
        const int nk = (int) d.knobs.size();
        const double beats = rateBeats ((int) p.rate->load());
        const float depth = p.depth->load();
        const int tgt = (int) p.target->load();
        const bool perSample = (m == mGate) || (m == mMod && tgt < 3);

        // per-sample shape values for this block
        float* sh = shapeBuf.data();
        for (int i = 0; i < n; ++i)
            sh[i] = shapes[m].lookup ((ppqStart + i * beatsPerSample) / beats);
        shapePhase[m].store ((float) std::fmod (std::max (0.0, ppqStart) / beats, 1.0));

        int choices[4] {};
        for (size_t c = 0; c < d.choices.size(); ++c)
            choices[c] = (int) p.choices[c]->load();

        float knobs[8];
        auto* formant = (m == mFormant) ? static_cast<FormantModule*> (mods[m].get()) : nullptr;

        for (int pos = 0; pos < n; pos += subBlock)
        {
            const int len = std::min (subBlock, n - pos);
            const float s = sh[pos + len / 2];

            for (int i = 0; i < nk; ++i)
            {
                float real = p.knobs[(size_t) i]->convertFrom0to1 (p.knobs[(size_t) i]->getValue());
                if (! perSample && depth > 0.f && tgt == i)
                    real = modulateReal (d.knobs[(size_t) i], real, s, depth);
                knobs[i] = real;
            }

            float mix = p.mix->load();
            if (m != mGate && depth > 0.f && tgt == nk)
                mix *= (1.f - depth + depth * s);

            // bypass fade
            const float step = (float) len / (0.01f * (float) sr);
            const float on0 = onGain[m];
            onGain[m] = onT > onGain[m] ? std::min (onT, onGain[m] + step) : std::max (onT, onGain[m] - step);
            const float w0 = mixPrev[m] * on0;
            const float w1 = mix * onGain[m];
            mixPrev[m] = mix;

            float* wl = wetBuf.getWritePointer (0);
            float* wr = wetBuf.getWritePointer (1);
            std::copy (L + pos, L + pos + len, wl);
            std::copy (R + pos, R + pos + len, wr);

            Context ctx;
            ctx.sampleRate = sr;
            ctx.bpm = bpm;
            ctx.ppq = ppqStart + pos * beatsPerSample;
            ctx.playing = playing;
            ctx.scL = scL ? scL + pos : nullptr;
            ctx.scR = scR ? scR + pos : nullptr;
            ctx.shape = sh + pos;
            ctx.shapeDepth = depth;
            ctx.shapeTarget = tgt;

            mods[m]->process (wl, wr, len, knobs, choices, ctx);

            if (formant)
                formant->delayDry (L + pos, R + pos, len);

            for (int i = 0; i < len; ++i)
            {
                const float w = w0 + (w1 - w0) * (float) (i + 1) / (float) len;
                L[pos + i] += (wl[i] - L[pos + i]) * w;
                R[pos + i] += (wr[i] - R[pos + i]) * w;
            }
        }

        if (onGain[m] == 0.f)
            mods[m]->reset();
    }

    // sample after the chain
    if (smpOn && smpRoute < 0.5f)
    {
        const float* sL = smpBuf.getReadPointer (0);
        const float* sR = smpBuf.getReadPointer (1);
        for (int i = 0; i < n; ++i) { L[i] += sL[i]; R[i] += sR[i]; }
    }

    // master mix and output gain
    {
        const float mixT = masterMix->load();
        const float outT = dbToGain (outGain->load());
        float pk = 0.f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) / (float) n;
            const float mx = masterMixPrev + (mixT - masterMixPrev) * t;
            const float og = outPrev + (outT - outPrev) * t;
            float l = dL[i] + (L[i] - dL[i]) * mx;
            float r = dR[i] + (R[i] - dR[i]) * mx;
            l *= og; r *= og;
            if (! std::isfinite (l)) l = 0.f;
            if (! std::isfinite (r)) r = 0.f;
            L[i] = l; R[i] = r;
            pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
        }
        masterMixPrev = mixT;
        outPrev = outT;
        meterOut.store (std::max (meterOut.load(), pk));
    }

    if (! playing) freePpq += n * beatsPerSample;
    else freePpq = ppqStart + n * beatsPerSample;
}

} // namespace anvil
