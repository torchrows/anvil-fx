#pragma once
#include <JuceHeader.h>
#include "LookAndFeel.h"
#include "Shape.h"

namespace anvil::ui
{

// Draw and edit a shape.
//  - click empty space: add a point
//  - drag a point: move it (snaps to the grid; hold Alt for free movement)
//  - double-click or right-click a point: delete it
//  - drag the small handle in the middle of a segment up/down: bend the curve
class ShapeEditor : public juce::Component, private juce::Timer
{
public:
    std::function<void (std::vector<ShapePoint>)> onChange;
    std::function<float()> getPhase;      // 0..1 playhead
    std::function<float()> getDepth;      // for dimming when depth is 0

    ShapeEditor() { startTimerHz (30); }

    void setPoints (const std::vector<ShapePoint>& p) { pts = p; repaint(); }
    void setGrid (int divisions) { grid = std::max (1, divisions); repaint(); }
    int getGrid() const { return grid; }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;
        auto r = getLocalBounds().toFloat();
        g.setColour (well);
        g.fillRoundedRectangle (r, 10.f);

        auto a = area();
        // grid
        for (int i = 0; i <= grid; ++i)
        {
            const float x = a.getX() + a.getWidth() * (float) i / (float) grid;
            const bool beat = grid >= 4 && i % (grid / 4) == 0;
            g.setColour (line.withAlpha (beat ? 0.7f : 0.3f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        }
        for (int i = 0; i <= 4; ++i)
        {
            const float y = a.getY() + a.getHeight() * (float) i / 4.f;
            g.setColour (line.withAlpha (i == 2 ? 0.5f : 0.25f));
            g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        }

        const float alpha = (getDepth && getDepth() <= 0.001f) ? 0.45f : 1.f;

        // curve
        juce::Path curve, fill;
        const int steps = std::max (64, (int) a.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / (float) steps;
            const juce::Point<float> p { a.getX() + t * a.getWidth(), a.getBottom() - eval (t) * a.getHeight() };
            if (i == 0) { curve.startNewSubPath (p); fill.startNewSubPath (a.getX(), a.getBottom()); }
            else curve.lineTo (p);
            fill.lineTo (p);
        }
        fill.lineTo (a.getRight(), a.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f * alpha), 0, a.getY(), accent.withAlpha (0.02f), 0, a.getBottom(), false));
        g.fillPath (fill);
        g.setColour (accent.withAlpha (alpha));
        g.strokePath (curve, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // playhead
        if (getPhase)
        {
            const float ph = getPhase();
            const float x = a.getX() + ph * a.getWidth();
            g.setColour (white.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (white);
            g.fillEllipse (x - 4.f, a.getBottom() - eval (ph) * a.getHeight() - 4.f, 8.f, 8.f);
        }

        // curve handles
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            auto h = handlePos ((int) i);
            g.setColour (textDim.withAlpha (hoverHandle == (int) i ? 1.f : 0.6f));
            g.drawEllipse (h.x - 3.5f, h.y - 3.5f, 7.f, 7.f, 1.2f);
        }
        // points
        for (size_t i = 0; i < pts.size(); ++i)
        {
            auto p = toScreen (pts[i]);
            const bool hot = hoverPoint == (int) i || dragPoint == (int) i;
            g.setColour (bg);
            g.fillEllipse (p.x - 6.f, p.y - 6.f, 12.f, 12.f);
            g.setColour (hot ? white : accent);
            g.drawEllipse (p.x - 5.f, p.y - 5.f, 10.f, 10.f, 2.f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int hp = pointAt (e.position), hh = hp < 0 ? handleAt (e.position) : -1;
        if (hp != hoverPoint || hh != hoverHandle) { hoverPoint = hp; hoverHandle = hh; repaint(); }
        setMouseCursor (hp >= 0 ? juce::MouseCursor::DraggingHandCursor
                                : hh >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
    }

    void mouseExit (const juce::MouseEvent&) override { hoverPoint = hoverHandle = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragPoint = pointAt (e.position);
        dragHandle = dragPoint < 0 ? handleAt (e.position) : -1;

        if (e.mods.isPopupMenu())
        {
            if (dragPoint > 0 && dragPoint < (int) pts.size() - 1) { pts.erase (pts.begin() + dragPoint); commit(); }
            dragPoint = -1;
            return;
        }
        if (dragHandle >= 0) { handleStartCurve = pts[(size_t) dragHandle].curve; handleStartY = e.position.y; return; }
        if (dragPoint < 0)
        {
            auto np = fromScreen (e.position, ! e.mods.isAltDown());
            pts.push_back (np);
            std::stable_sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.x < b.x; });
            for (size_t i = 0; i < pts.size(); ++i)
                if (pts[i].x == np.x && pts[i].y == np.y) { dragPoint = (int) i; break; }
            commit();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragHandle >= 0)
        {
            const float dy = (handleStartY - e.position.y) / (area().getHeight() * 0.5f);
            auto& a = pts[(size_t) dragHandle];
            auto& b = pts[(size_t) dragHandle + 1];
            const float dir = b.y >= a.y ? 1.f : -1.f;
            a.curve = juce::jlimit (-1.f, 1.f, handleStartCurve + dy * dir);
            commit();
            return;
        }
        if (dragPoint < 0) return;
        auto np = fromScreen (e.position, ! e.mods.isAltDown());
        auto& p = pts[(size_t) dragPoint];
        const bool first = dragPoint == 0, last = dragPoint == (int) pts.size() - 1;
        const float lo = first ? 0.f : pts[(size_t) dragPoint - 1].x;
        const float hi = last ? 1.f : pts[(size_t) dragPoint + 1].x;
        p.x = first ? 0.f : last ? 1.f : juce::jlimit (lo, hi, np.x);
        p.y = np.y;
        commit();
    }

    void mouseUp (const juce::MouseEvent&) override { dragPoint = dragHandle = -1; repaint(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = pointAt (e.position);
        if (i > 0 && i < (int) pts.size() - 1) { pts.erase (pts.begin() + i); commit(); }
        else if (const int h = handleAt (e.position); h >= 0) { pts[(size_t) h].curve = 0.f; commit(); }
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (14.f, 12.f); }

    juce::Point<float> toScreen (const ShapePoint& p) const
    {
        auto a = area();
        return { a.getX() + p.x * a.getWidth(), a.getBottom() - p.y * a.getHeight() };
    }

    ShapePoint fromScreen (juce::Point<float> s, bool snap) const
    {
        auto a = area();
        float x = juce::jlimit (0.f, 1.f, (s.x - a.getX()) / a.getWidth());
        float y = juce::jlimit (0.f, 1.f, (a.getBottom() - s.y) / a.getHeight());
        if (snap)
        {
            x = std::round (x * (float) grid) / (float) grid;
            const float ys = std::round (y * 8.f) / 8.f;
            if (std::abs (ys - y) < 0.03f) y = ys;
        }
        return { x, y, 0.f };
    }

    float eval (float x) const
    {
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            auto& a = pts[i]; auto& b = pts[i + 1];
            if (x <= b.x)
            {
                const float w = b.x - a.x;
                const float t = w > 1.0e-6f ? (x - a.x) / w : 1.f;
                return a.y + (b.y - a.y) * Shape::curveFn (juce::jlimit (0.f, 1.f, t), a.curve);
            }
        }
        return pts.empty() ? 1.f : pts.back().y;
    }

    juce::Point<float> handlePos (int i) const
    {
        auto& a = pts[(size_t) i]; auto& b = pts[(size_t) i + 1];
        const float mx = (a.x + b.x) * 0.5f;
        auto ar = area();
        return { ar.getX() + mx * ar.getWidth(), ar.getBottom() - eval (mx) * ar.getHeight() };
    }

    int pointAt (juce::Point<float> s) const
    {
        for (int i = (int) pts.size() - 1; i >= 0; --i)
            if (toScreen (pts[(size_t) i]).getDistanceFrom (s) < 9.f) return i;
        return -1;
    }

    int handleAt (juce::Point<float> s) const
    {
        for (int i = 0; i + 1 < (int) pts.size(); ++i)
        {
            if (pts[(size_t) i + 1].x - pts[(size_t) i].x < 0.02f) continue;
            if (handlePos (i).getDistanceFrom (s) < 8.f) return i;
        }
        return -1;
    }

    void commit()
    {
        repaint();
        if (onChange) onChange (pts);
    }

    void timerCallback() override { if (getPhase) repaint(); }

    std::vector<ShapePoint> pts;
    int grid = 16;
    int hoverPoint = -1, hoverHandle = -1, dragPoint = -1, dragHandle = -1;
    float handleStartCurve = 0.f, handleStartY = 0.f;
};

} // namespace anvil::ui
