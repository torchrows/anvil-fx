// Offline checks: every module and preset processes audio without blowing up,
// levels stay sane, the formant shifter is transparent at zero, and state
// survives a save/load round trip.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace anvil;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s\n", msg); } } while (0)

struct Stats { double rmsIn = 0, rmsOut = 0, peak = 0; bool finite = true; };

static void setParam (AnvilProcessor& p, const juce::String& id, float real)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    if (prm) prm->setValueNotifyingHost (prm->convertTo0to1 (real));
}

static void fillTest (juce::AudioBuffer<float>& b, double sr, juce::int64 start, int kind)
{
    juce::Random r (42 + (int) start);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = (double) (start + i) / sr;
        float v;
        if (kind == 0)   // vocal-ish: buzzy tone with vowel-like filtering approximation
        {
            const double f0 = 140.0;
            double s = 0;
            for (int h = 1; h < 30; ++h)
            {
                const double fh = f0 * h;
                const double env = std::exp (-std::pow ((fh - 700.0) / 300.0, 2.0)) + 0.6 * std::exp (-std::pow ((fh - 1200.0) / 400.0, 2.0)) + 0.05;
                s += env * std::sin (2.0 * juce::MathConstants<double>::pi * fh * t) / h;
            }
            v = (float) (0.5 * s);
        }
        else v = (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.05 * r.nextFloat());
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
}

static Stats run (AnvilProcessor& p, double seconds, int kind = 0, bool keepLast = false, juce::AudioBuffer<float>* capture = nullptr)
{
    const double sr = p.getSampleRate();
    const int block = 512;
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Stats st;
    double sumIn = 0, sumOut = 0;
    juce::int64 n = 0;
    if (capture) capture->setSize (4, total);
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = std::min (block, total - pos);
        juce::AudioBuffer<float> b (buf.getArrayOfWritePointers(), 2, len);
        fillTest (b, sr, pos, kind);
        if (capture) { capture->copyFrom (0, pos, b, 0, 0, len); }
        for (int i = 0; i < len; ++i) sumIn += b.getSample (0, i) * b.getSample (0, i);
        p.processBlock (b, midi);
        if (capture) { capture->copyFrom (2, pos, b, 0, 0, len); capture->copyFrom (3, pos, b, 1, 0, len); }
        const bool count = ! keepLast || pos > total / 2;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                const float v = b.getSample (ch, i);
                if (! std::isfinite (v)) st.finite = false;
                if (count && ch == 0) { sumOut += v * v; ++n; }
                st.peak = std::max (st.peak, (double) std::abs (v));
            }
    }
    st.rmsIn = std::sqrt (sumIn / total);
    st.rmsOut = std::sqrt (sumOut / std::max<juce::int64> (1, n));
    return st;
}

static double db (double x) { return 20.0 * std::log10 (std::max (1e-9, x)); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;

    {
        std::printf ("== Each module alone (input tone, mix 100%%)\n");
        for (int m = 0; m < numModules; ++m)
        {
            if (m == mSample) continue;
            AnvilProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);
            setParam (p, pid (m, "on"), 1.f);
            setParam (p, pid (m, "mix"), 1.f);
            auto st = run (p, 2.0, 0, true);
            std::printf ("  %-11s in %6.1f dB  out %6.1f dB  peak %5.2f\n", modules()[(size_t) m].name, db (st.rmsIn), db (st.rmsOut), st.peak);
            CHECK (st.finite, "non-finite output");
            CHECK (st.peak < 4.0, "peak way too hot");
            CHECK (st.rmsOut > 1e-4 || m == mGate, "output silent");
        }
    }

    {
        std::printf ("== Each module with its shape at full depth\n");
        for (int m = 0; m < numModules; ++m)
        {
            if (m == mSample) continue;
            AnvilProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);
            setParam (p, pid (m, "on"), 1.f);
            setParam (p, pid (m, "shp_depth"), 1.f);
            setParam (p, pid (m, "shp_rate"), 2.f);
            p.engine.shapes[m].setPoints (shapes::sine());
            const int targets = targetNames (m).size();
            for (int t = 0; t < targets; ++t)
            {
                setParam (p, pid (m, "shp_tgt"), (float) t);
                auto st = run (p, 0.5);
                CHECK (st.finite, "non-finite with shape");
                CHECK (st.peak < 4.0, "shape pushed peak too hot");
            }
        }
        std::printf ("  done\n");
    }

    {
        std::printf ("== Formant shifter at zero is transparent (after latency)\n");
        AnvilProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        setParam (p, pid (mFormant, "on"), 1.f);
        juce::AudioBuffer<float> cap;
        run (p, 1.5, 0, false, &cap);
        const int lat = p.engine.getLatency();
        std::printf ("  latency %d samples\n", lat);
        double num = 0, den1 = 0, den2 = 0;
        for (int i = 24000; i < cap.getNumSamples(); ++i)
        {
            const double a = cap.getSample (0, i - lat), b = cap.getSample (2, i);
            num += a * b; den1 += a * a; den2 += b * b;
        }
        const double corr = num / std::sqrt (den1 * den2);
        int bestLag = 0; double bestC = -2;
        for (int lag = 0; lag < 4096; ++lag)
        {
            double nn = 0, d1 = 0, d2 = 0;
            for (int i = 30000; i < 50000; ++i) { const double a = cap.getSample (0, i - lag), b = cap.getSample (2, i); nn += a * b; d1 += a * a; d2 += b * b; }
            const double c = nn / std::sqrt (d1 * d2);
            if (c > bestC) { bestC = c; bestLag = lag; }
        }
        std::printf ("  best lag %d corr %.3f\n", bestLag, bestC);
        const double gain = std::sqrt (den2 / den1);
        std::printf ("  correlation %.3f  gain %.2f dB\n", corr, db (gain));
        CHECK (corr > 0.9, "formant at zero is not transparent");
        CHECK (std::abs (db (gain)) < 1.5, "formant at zero changes level");
    }

    {
        std::printf ("== Formant / pitch move the spectrum the right way\n");
        auto centroid = [&] (float shift, float pitch)
        {
            AnvilProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);
            setParam (p, pid (mFormant, "on"), 1.f);
            setParam (p, pid (mFormant, "shift"), shift);
            setParam (p, pid (mFormant, "pitch"), pitch);
            juce::AudioBuffer<float> cap;
            run (p, 1.5, 0, false, &cap);
            juce::dsp::FFT fft (13);
            std::vector<float> d (2 * 8192, 0.f);
            for (int i = 0; i < 8192; ++i)
                d[(size_t) i] = cap.getSample (2, 40000 + i) * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / 8192.f));
            fft.performFrequencyOnlyForwardTransform (d.data());
            double num = 0, den = 0, best = 0; int bestK = 0;
            for (int k = 1; k < 4096; ++k)
            {
                const double f = k * sr / 8192.0;
                num += f * d[(size_t) k]; den += d[(size_t) k];
                if (k < 400 && d[(size_t) k] > best) { best = d[(size_t) k]; bestK = k; }
            }
            return std::pair<double, double> { num / den, bestK * sr / 8192.0 };
        };
        auto base = centroid (0, 0);
        auto fUp = centroid (6, 0);
        auto pUp = centroid (0, 7);
        std::printf ("  base: centroid %.0f Hz, strongest low peak %.0f Hz\n", base.first, base.second);
        std::printf ("  formant +6: centroid %.0f Hz, low peak %.0f Hz\n", fUp.first, fUp.second);
        std::printf ("  pitch +7:   centroid %.0f Hz, low peak %.0f Hz\n", pUp.first, pUp.second);
        CHECK (fUp.first > base.first * 1.15, "formant up did not brighten");
    }

    {
        std::printf ("== All factory presets\n");
        AnvilProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            auto st = run (p, 1.0, i % 2);
            std::printf ("  %-30s out %6.1f dB  peak %5.2f\n", p.getProgramName (i).toRawUTF8(), db (st.rmsOut), st.peak);
            CHECK (st.finite, "preset produced non-finite output");
            CHECK (st.peak < 4.0, "preset too hot");
        }
    }

    {
        std::printf ("== State round trip\n");
        AnvilProcessor a;
        a.applyFactoryPreset (4);
        a.engine.shapes[mDelay].setPoints (shapes::triangle());
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        AnvilProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters())
        {
            auto* ra = dynamic_cast<juce::RangedAudioParameter*> (prm);
            auto* rb = b.apvts.getParameter (ra->getParameterID());
            if (std::abs (ra->getValue() - rb->getValue()) > 1e-5f) same = false;
        }
        CHECK (same, "parameters differ after reload");
        CHECK (b.engine.shapes[mDelay].getPoints().size() == shapes::triangle().size(), "shape lost after reload");
        CHECK (b.getPresetName() == a.getPresetName(), "preset name lost");
        std::printf ("  %s\n", same ? "ok" : "mismatch");
    }

    {
        std::printf ("== Odd block sizes and mono\n");
        AnvilProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 256);
        p.prepareToPlay (sr, 256);
        p.applyFactoryPreset (12);
        juce::AudioBuffer<float> b (2, 1000);
        juce::MidiBuffer midi;
        fillTest (b, sr, 0, 0);
        p.processBlock (b, midi);   // larger than prepared size
        bool ok = true;
        for (int i = 0; i < 1000; ++i) ok &= std::isfinite (b.getSample (0, i));
        CHECK (ok, "block larger than prepared size broke");
        std::printf ("  %s\n", ok ? "ok" : "broken");
    }

    {
        std::printf ("== Editor builds\n");
        AnvilProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr, "no editor");
        if (ed)
        {
            for (int m = 0; m < numModules; ++m)
            {
                // switch modules through the list to exercise every control panel
                ed->setSize (1100 + m, 690);
            }
            auto* ae = dynamic_cast<AnvilEditor*> (ed.get());
            for (int m : { (int) mMod, (int) mFormant, (int) mVocoder, (int) mSample })
            {
                ae->showModule (m);
                juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
                auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("editor_" + juce::String (modules()[(size_t) m].id) + ".png");
                f.deleteFile();
                juce::FileOutputStream os (f);
                juce::PNGImageFormat png;
                png.writeImageToStream (img, os);
            }
            auto f = juce::File::getCurrentWorkingDirectory();
            std::printf ("  wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
