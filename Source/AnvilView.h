#pragma once
#include <JuceHeader.h>
#include "LookAndFeel.h"
#if ANVIL_HAS_ASSETS
 #include "BinaryData.h"
#endif

namespace anvil::ui
{

// Anvil, the character, reacting to the output level over a small starfield.
// Frames come from Assets/anvil/*.png (rendered from Blender), played as a loop
// that speeds up and bounces with the music.
class AnvilView : public juce::Component, private juce::Timer
{
public:
    std::function<float()> getLevel;   // 0..1+, peak since last call

    AnvilView()
    {
       #if ANVIL_HAS_ASSETS
        juce::StringArray names;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            names.add (BinaryData::namedResourceList[i]);
        names.sort (true);
        for (auto& n : names)
        {
            if (! n.startsWith ("anvil_")) continue;
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (n.toRawUTF8(), size))
                frames.add (juce::ImageCache::getFromMemory (data, size));
        }
       #endif
        juce::Random r (7);
        for (int i = 0; i < 70; ++i)
            stars.push_back ({ r.nextFloat(), r.nextFloat(), 0.4f + r.nextFloat() * 1.2f, r.nextFloat() * 6.28f });
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;
        auto r = getLocalBounds().toFloat();
        g.setColour (well);
        g.fillRoundedRectangle (r, 12.f);

        // stars
        for (auto& s : stars)
        {
            const float a = 0.25f + 0.35f * (0.5f + 0.5f * std::sin (s.phase + t * 1.3f));
            g.setColour (juce::Colours::white.withAlpha (a));
            const float x = r.getX() + s.x * r.getWidth(), y = r.getY() + s.y * r.getHeight();
            g.fillEllipse (x, y, s.size, s.size);
        }

        auto stage = r.reduced (14.f).withTrimmedBottom (34.f);
        const float bounce = 1.f + 0.07f * level;
        const float bob = std::sin (t * 2.2f) * 3.f - level * 6.f;

        // glow
        const auto c = stage.getCentre().translated (0.f, bob);
        const float gr = stage.getWidth() * (0.42f + 0.08f * level);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.10f + 0.25f * level), c.x, c.y,
                                                 accent.withAlpha (0.f), c.x + gr, c.y, true));
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.f, gr * 2.f);

        if (! frames.isEmpty())
        {
            auto& img = frames.getReference (frameIndex % frames.size());
            auto box = stage.withSizeKeepingCentre (stage.getWidth() * bounce, stage.getHeight() * bounce).translated (0.f, bob);
            g.setOpacity (1.f);
            g.drawImageWithin (img, (int) box.getX(), (int) box.getY(), (int) box.getWidth(), (int) box.getHeight(),
                               juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        }
        else
        {
            // placeholder until the renders are in
            const float rr = stage.getWidth() * 0.28f * bounce;
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (line.withAlpha (0.25f * (float) i));
                const float ring = rr + (float) i * 10.f + std::fmod (t * 12.f, 10.f);
                g.drawEllipse (c.x - ring, c.y - ring, ring * 2.f, ring * 2.f, 1.f);
            }
            g.setColour (panelHi);
            g.fillEllipse (c.x - rr, c.y - rr, rr * 2.f, rr * 2.f);
            g.setColour (text);
            g.setFont (font (15.f, true));
            g.drawText ("ANVIL", juce::Rectangle<float> (c.x - rr, c.y - 10.f, rr * 2.f, 20.f), juce::Justification::centred);
        }

        // output meter
        auto m = r.reduced (16.f).removeFromBottom (10.f);
        g.setColour (panel);
        g.fillRoundedRectangle (m, 4.f);
        g.setColour (accent);
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * juce::jlimit (0.f, 1.f, meter)), 4.f);
        g.setColour (textDim);
        g.setFont (font (11.f));
        g.drawText ("OUT", r.reduced (16.f).removeFromBottom (28.f).removeFromTop (14.f), juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        const float raw = getLevel ? getLevel() : 0.f;
        const float db = juce::Decibels::gainToDecibels (raw, -60.f);
        const float target = juce::jlimit (0.f, 1.f, (db + 36.f) / 36.f);
        meter = target > meter ? target : meter * 0.92f;
        level = target > level ? level + (target - level) * 0.6f : level * 0.88f;
        t += 1.f / 30.f;
        frameClock += (1.f + 2.5f * level) * (12.f / 30.f);
        frameIndex = (int) frameClock;
        repaint();
    }

    struct Star { float x, y, size, phase; };
    std::vector<Star> stars;
    juce::Array<juce::Image> frames;
    float level = 0.f, meter = 0.f, t = 0.f, frameClock = 0.f;
    int frameIndex = 0;
};

} // namespace anvil::ui
