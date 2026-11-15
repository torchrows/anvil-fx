#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>

namespace anvil
{

// Phase-vocoder pitch shifter with separate formant control.
// The spectral envelope is found with cepstral smoothing, the fine structure
// is pitch-shifted, then the envelope is re-applied, moved by the formant ratio.
// So Pitch changes the note and keeps the voice character, and Formant changes
// the character (deeper / chipmunk) and keeps the note.
class SpectralShifter
{
public:
    static constexpr int order = 11;
    static constexpr int size = 1 << order;   // 2048
    static constexpr int osamp = 4;
    static constexpr int hop = size / osamp;
    static constexpr int latency = size - hop;     // FIFO offset
    static constexpr int totalLatency = size;      // measured input-to-output delay
    static constexpr int half = size / 2;
    static constexpr int lifter = 36;          // cepstral coefficients kept for the envelope

    SpectralShifter() : fft (order) {}

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        inFifo.assign (size, 0.f);
        outFifo.assign (size, 0.f);
        work.assign (2 * size, 0.f);
        cep.assign (2 * size, 0.f);
        accum.assign (2 * size, 0.f);
        lastPhase.assign (half + 1, 0.f);
        sumPhase.assign (half + 1, 0.f);
        anaMag.assign (half + 1, 0.f);
        anaFreq.assign (half + 1, 0.f);
        synMag.assign (half + 1, 0.f);
        synFreq.assign (half + 1, 0.f);
        env.assign (half + 1, 1.f);
        window.resize (size);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * juce::MathConstants<float>::pi * (float) i / (float) size);
        reset();
    }

    void reset()
    {
        std::fill (inFifo.begin(), inFifo.end(), 0.f);
        std::fill (outFifo.begin(), outFifo.end(), 0.f);
        std::fill (accum.begin(), accum.end(), 0.f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.f);
        std::fill (sumPhase.begin(), sumPhase.end(), 0.f);
        rover = latency;
    }

    void setRatios (float pitch, float formant)
    {
        pitchRatio = juce::jlimit (0.25f, 4.f, pitch);
        formantRatio = juce::jlimit (0.25f, 4.f, formant);
    }

    inline float processSample (float x) noexcept
    {
        inFifo[(size_t) rover] = x;
        const float y = outFifo[(size_t) (rover - latency)];
        if (++rover >= size)
        {
            rover = latency;
            processFrame();
        }
        return y;
    }

private:
    void processFrame()
    {
        const float pi = juce::MathConstants<float>::pi;
        const float twoPi = 2.f * pi;
        const float freqPerBin = (float) sr / (float) size;
        const float expct = twoPi * (float) hop / (float) size;

        // ---- analysis
        for (int i = 0; i < size; ++i)
            work[(size_t) i] = inFifo[(size_t) i] * window[(size_t) i];
        std::fill (work.begin() + size, work.end(), 0.f);
        fft.performRealOnlyForwardTransform (work.data(), true);

        for (int k = 0; k <= half; ++k)
        {
            const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
            const float mag = 2.f * std::sqrt (re * re + im * im);
            const float ph = std::atan2 (im, re);
            float d = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            d -= (float) k * expct;
            d -= twoPi * std::round (d / twoPi);
            d = (float) osamp * d / twoPi;
            anaMag[(size_t) k] = mag;
            anaFreq[(size_t) k] = ((float) k + d) * freqPerBin;
        }

        // ---- spectral envelope (cepstrum)
        for (int k = 0; k <= half; ++k)
        {
            cep[(size_t) (2 * k)] = std::log (anaMag[(size_t) k] + 1.0e-6f);
            cep[(size_t) (2 * k + 1)] = 0.f;
        }
        fft.performRealOnlyInverseTransform (cep.data());
        for (int i = lifter; i <= size - lifter; ++i)
            cep[(size_t) i] = 0.f;
        std::fill (cep.begin() + size, cep.end(), 0.f);
        fft.performRealOnlyForwardTransform (cep.data(), true);
        for (int k = 0; k <= half; ++k)
            env[(size_t) k] = std::exp (cep[(size_t) (2 * k)]);

        // ---- shift the fine structure, then re-apply the moved envelope
        std::fill (synMag.begin(), synMag.end(), 0.f);
        std::fill (synFreq.begin(), synFreq.end(), 0.f);
        for (int k = 0; k <= half; ++k)
        {
            const int idx = (int) std::lround ((float) k * pitchRatio);
            if (idx < 0 || idx > half) continue;
            const float exc = anaMag[(size_t) k] / std::max (env[(size_t) k], 1.0e-6f);
            if (exc > synMag[(size_t) idx])
            {
                synMag[(size_t) idx] = exc;
                synFreq[(size_t) idx] = anaFreq[(size_t) k] * pitchRatio;
            }
        }
        for (int k = 0; k <= half; ++k)
        {
            const float src = (float) k / formantRatio;
            float e;
            if (src >= (float) half) e = env[(size_t) half] * 0.1f;
            else
            {
                const int i0 = (int) src;
                const float f = src - (float) i0;
                e = env[(size_t) i0] + (env[(size_t) (i0 + 1)] - env[(size_t) i0]) * f;
            }
            synMag[(size_t) k] *= e;
        }

        // ---- synthesis
        for (int k = 0; k <= half; ++k)
        {
            float d = synFreq[(size_t) k] / freqPerBin - (float) k;
            d = twoPi * d / (float) osamp;
            d += (float) k * expct;
            float sp = sumPhase[(size_t) k] + d;
            sp -= twoPi * std::floor (sp / twoPi);
            sumPhase[(size_t) k] = sp;
            const float m = synMag[(size_t) k] * 0.5f;
            work[(size_t) (2 * k)] = m * std::cos (sp);
            work[(size_t) (2 * k + 1)] = m * std::sin (sp);
        }
        fft.performRealOnlyInverseTransform (work.data());

        const float norm = outputGain();
        for (int i = 0; i < size; ++i)
            accum[(size_t) i] += window[(size_t) i] * work[(size_t) i] * norm;
        for (int i = 0; i < hop; ++i)
            outFifo[(size_t) i] = accum[(size_t) i];
        std::memmove (accum.data(), accum.data() + hop, sizeof (float) * (size_t) size);
        std::memmove (inFifo.data(), inFifo.data() + hop, sizeof (float) * (size_t) latency);
    }

    // Hann^2 overlap-add at osamp 4 sums to 1.5
    static constexpr float outputGain() { return 1.f / 1.5f; }

    juce::dsp::FFT fft;
    double sr = 44100;
    std::vector<float> inFifo, outFifo, work, cep, accum, window;
    std::vector<float> lastPhase, sumPhase, anaMag, anaFreq, synMag, synFreq, env;
    int rover = latency;
    float pitchRatio = 1.f, formantRatio = 1.f;
};

} // namespace anvil
