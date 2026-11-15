#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <vector>

// Small allocation-free building blocks used by the effect modules.

namespace anvil::dsp
{

constexpr float pi = 3.14159265358979f;
constexpr float twoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.f, db * 0.05f); }

// RBJ cookbook biquad. Coefficients are recomputed only when asked.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (float nb0, float nb1, float nb2, float a0, float na1, float na2)
    {
        const float inv = 1.f / a0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }

    void lowpass (double sr, float f, float q)
    {
        const float w = twoPi * juce::jlimit (10.f, (float) sr * 0.49f, f) / (float) sr;
        const float c = std::cos (w), a = std::sin (w) / (2.f * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }

    void highpass (double sr, float f, float q)
    {
        const float w = twoPi * juce::jlimit (10.f, (float) sr * 0.49f, f) / (float) sr;
        const float c = std::cos (w), a = std::sin (w) / (2.f * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }

    void bandpass (double sr, float f, float q)
    {
        const float w = twoPi * juce::jlimit (10.f, (float) sr * 0.49f, f) / (float) sr;
        const float c = std::cos (w), a = std::sin (w) / (2.f * q);
        set (a, 0, -a, 1 + a, -2 * c, 1 - a);
    }

    void peak (double sr, float f, float q, float db)
    {
        const float A = std::pow (10.f, db / 40.f);
        const float w = twoPi * juce::jlimit (10.f, (float) sr * 0.49f, f) / (float) sr;
        const float c = std::cos (w), a = std::sin (w) / (2.f * q);
        set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
    }

    void lowShelf (double sr, float f, float db)
    {
        const float A = std::pow (10.f, db / 40.f);
        const float w = twoPi * juce::jlimit (10.f, (float) sr * 0.49f, f) / (float) sr;
        const float c = std::cos (w), s = std::sin (w);
        const float a = s / 2.f * std::sqrt (2.f);
        const float sq = 2.f * std::sqrt (A) * a;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }

    void highShelf (double sr, float f, float db)
    {
        const float A = std::pow (10.f, db / 40.f);
        const float w = twoPi * juce::jlimit (10.f, (float) sr * 0.49f, f) / (float) sr;
        const float c = std::cos (w), s = std::sin (w);
        const float a = s / 2.f * std::sqrt (2.f);
        const float sq = 2.f * std::sqrt (A) * a;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

// One-pole smoother / filter
struct OnePole
{
    float z = 0, a = 0;
    void setTime (double sr, float ms) { a = ms <= 0.f ? 0.f : std::exp (-1.f / (0.001f * ms * (float) sr)); }
    inline float process (float x) noexcept { z = x + a * (z - x); return z; }
};

struct DcBlocker
{
    float x1 = 0, y1 = 0;
    inline float process (float x) noexcept { const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

// Circular delay line with fractional (cubic) reads
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.f);
        mask = size - 1;
        w = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.f); w = 0; }

    inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // delay in samples, >= 1
    inline float read (float d) const noexcept
    {
        d = juce::jlimit (1.f, (float) (mask - 3), d);
        const float rp = (float) w - d;
        int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)];
        const float y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    inline float readInt (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
};

struct Rng
{
    juce::uint32 s = 0x12345678u;
    inline float next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) s * (1.f / 4294967296.f); }
    inline float bipolar() noexcept { return next() * 2.f - 1.f; }
};

// Band-limited saw / square using polyBLEP
struct BlepOsc
{
    double phase = 0, inc = 0;

    static inline float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.f;
    }

    inline float saw() noexcept
    {
        float v = (float) (2.0 * phase - 1.0) - blep (phase, inc);
        phase += inc; if (phase >= 1.0) phase -= 1.0;
        return v;
    }

    inline float square() noexcept
    {
        float v = phase < 0.5 ? 1.f : -1.f;
        v += blep (phase, inc);
        double p2 = phase + 0.5; if (p2 >= 1.0) p2 -= 1.0;
        v -= blep (p2, inc);
        phase += inc; if (phase >= 1.0) phase -= 1.0;
        return v;
    }
};

} // namespace anvil::dsp
