#pragma once
#include <JuceHeader.h>
#include "LookAndFeel.h"

namespace anvil::ui
{

// Drag pad for the formant module. X = formant, Y = pitch. Double-click to centre.
class XYPad : public juce::Component, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    XYPad (juce::RangedAudioParameter& xParam, juce::RangedAudioParameter& yParam) : px (xParam), py (yParam)
    {
        px.addListener (this);
        py.addListener (this);
    }

    ~XYPad() override
    {
        px.removeListener (this);
        py.removeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;
        auto r = getLocalBounds().toFloat();
        g.setColour (well);
        g.fillRoundedRectangle (r, 10.f);
        auto a = area();

        for (int i = 1; i < 8; ++i)
        {
            g.setColour (line.withAlpha (i == 4 ? 0.7f : 0.25f));
            g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) i / 8.f), a.getY(), a.getBottom());
            g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) i / 8.f), a.getX(), a.getRight());
        }

        g.setColour (textDim);
        g.setFont (font (11.f));
        g.drawText ("DEEPER", a.removeFromLeft (60).withTrimmedBottom (4).withTrimmedLeft (4), juce::Justification::bottomLeft);
        a = area();
        g.drawText ("BRIGHTER", a.withTrimmedRight (4).withTrimmedBottom (4), juce::Justification::bottomRight);
        g.drawText ("PITCH UP", a.withTrimmedTop (4), juce::Justification::centredTop);
        g.drawText ("PITCH DOWN", a.withTrimmedBottom (18), juce::Justification::centredBottom);

        auto p = dot();
        g.setColour (accent.withAlpha (0.15f));
        g.fillEllipse (p.x - 22.f, p.y - 22.f, 44.f, 44.f);
        g.setColour (white);
        g.fillEllipse (p.x - 8.f, p.y - 8.f, 16.f, 16.f);

        g.setColour (text);
        g.setFont (font (12.f, true));
        auto info = juce::String ("Formant ") + signedSt (px.convertFrom0to1 (px.getValue()))
                    + "   Pitch " + signedSt (py.convertFrom0to1 (py.getValue()));
        g.drawText (info, getLocalBounds().reduced (12, 8).removeFromBottom (16), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        px.beginChangeGesture();
        py.beginChangeGesture();
        setFrom (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { setFrom (e.position); }
    void mouseUp (const juce::MouseEvent&) override { px.endChangeGesture(); py.endChangeGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        px.setValueNotifyingHost (px.convertTo0to1 (0.f));
        py.setValueNotifyingHost (py.convertTo0to1 (0.f));
    }

private:
    static juce::String signedSt (float v)
    {
        const float r = std::round (v * 10.f) / 10.f;
        return (r > 0 ? "+" : "") + juce::String (r, 1) + " st";
    }

    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (12.f).withTrimmedBottom (18.f); }

    juce::Point<float> dot() const
    {
        auto a = area();
        return { a.getX() + px.getValue() * a.getWidth(), a.getBottom() - py.getValue() * a.getHeight() };
    }

    void setFrom (juce::Point<float> s)
    {
        auto a = area();
        float x = juce::jlimit (0.f, 1.f, (s.x - a.getX()) / a.getWidth());
        float y = juce::jlimit (0.f, 1.f, (a.getBottom() - s.y) / a.getHeight());
        // gentle snap to the centre lines
        if (std::abs (x - 0.5f) < 0.02f) x = 0.5f;
        if (std::abs (y - 0.5f) < 0.02f) y = 0.5f;
        px.setValueNotifyingHost (x);
        py.setValueNotifyingHost (y);
        repaint();
    }

    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override { repaint(); }

    juce::RangedAudioParameter& px;
    juce::RangedAudioParameter& py;
};

} // namespace anvil::ui
