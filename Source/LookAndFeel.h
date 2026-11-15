#pragma once
#include <JuceHeader.h>

namespace anvil::ui
{

// Grey theme. One white highlight, no hue.
namespace colours
{
    const juce::Colour bg        { 0xff232428 };
    const juce::Colour panel     { 0xff2d2f34 };
    const juce::Colour panelHi   { 0xff383a40 };
    const juce::Colour well      { 0xff1b1c1f };
    const juce::Colour line      { 0xff46484f };
    const juce::Colour text      { 0xffe6e7ea };
    const juce::Colour textDim   { 0xff9a9ca4 };
    const juce::Colour accent    { 0xffd9dbe0 };
    const juce::Colour white     { 0xffffffff };
    const juce::Colour off       { 0xff5a5c63 };
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, well);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, panelHi);
        setColour (juce::PopupMenu::highlightedTextColourId, white);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::TextButton::buttonColourId, panelHi);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::backgroundColourId, well);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, line);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& s) override
    {
        using namespace colours;
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
        const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float arcR = r - 2.f;
        const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);

        // track
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, start, end, true);
        g.setColour (well);
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc
        const float ang = start + pos * (end - start);
        const float from = bipolar ? (start + end) * 0.5f : start;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0.f, std::min (from, ang), std::max (from, ang), true);
        g.setColour (s.isEnabled() ? accent : off);
        g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // body
        const float br = arcR - 6.f;
        juce::ColourGradient grad (panelHi.brighter (0.15f), c.x, c.y - br, panel.darker (0.2f), c.x, c.y + br, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f);
        g.setColour (line);
        g.drawEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f, 1.f);

        // pointer
        juce::Path p;
        p.addRoundedRectangle (-1.5f, -br + 3.f, 3.f, br * 0.45f, 1.5f);
        g.setColour (white);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace colours;
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (box.hasKeyboardFocus (false) ? accent : line);
        g.drawRoundedRectangle (r, 6.f, 1.f);
        juce::Path arrow;
        const float ax = (float) w - 14.f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
        g.setColour (textDim);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.f); }
    juce::Font getPopupMenuFont() override { return font (14.f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.f, true); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        using namespace colours;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? accent : panelHi;
        if (down) c = c.darker (0.15f); else if (over) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (line);
        g.drawRoundedRectangle (r, 6.f, 1.f);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (12.f));
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::textColourId, colours::text);
        return l;
    }
};

} // namespace anvil::ui
