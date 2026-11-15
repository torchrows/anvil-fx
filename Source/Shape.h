#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace anvil
{

// A drawable shape: points from x = 0 to x = 1, each with a curve on the
// segment that follows it. The UI edits the points; the audio thread reads a
// baked lookup table that is swapped in without locks.
struct ShapePoint
{
    float x = 0.f, y = 0.f;
    float curve = 0.f;   // -1..1, bends the segment to the next point
};

class Shape
{
public:
    static constexpr int tableSize = 1024;
    static constexpr int maxPoints = 64;

    Shape() { setPoints ({ { 0.f, 1.f, 0.f }, { 1.f, 1.f, 0.f } }); }

    // ---- message thread ----
    void setPoints (std::vector<ShapePoint> p)
    {
        sanitise (p);
        points = std::move (p);
        bake();
    }

    const std::vector<ShapePoint>& getPoints() const { return points; }

    static float curveFn (float t, float c)
    {
        if (std::abs (c) < 1.0e-3f) return t;
        const float e = std::pow (6.f, -c);   // c>0 eases out, c<0 eases in
        return std::pow (t, e);
    }

    float evalPoints (float x) const
    {
        if (points.empty()) return 1.f;
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            auto& a = points[i];
            auto& b = points[i + 1];
            if (x <= b.x)
            {
                const float w = b.x - a.x;
                const float t = w > 1.0e-6f ? (x - a.x) / w : 1.f;
                return a.y + (b.y - a.y) * curveFn (juce::jlimit (0.f, 1.f, t), a.curve);
            }
        }
        return points.back().y;
    }

    juce::ValueTree toTree (const juce::Identifier& type) const
    {
        juce::ValueTree t (type);
        for (auto& p : points)
        {
            juce::ValueTree pt ("pt");
            pt.setProperty ("x", p.x, nullptr);
            pt.setProperty ("y", p.y, nullptr);
            pt.setProperty ("c", p.curve, nullptr);
            t.appendChild (pt, nullptr);
        }
        return t;
    }

    void fromTree (const juce::ValueTree& t)
    {
        std::vector<ShapePoint> p;
        for (auto c : t)
            p.push_back ({ (float) c.getProperty ("x"), (float) c.getProperty ("y"), (float) c.getProperty ("c") });
        if (p.size() >= 2)
            setPoints (std::move (p));
    }

    // ---- audio thread ----
    float lookup (double phase) const noexcept
    {
        auto& t = tables[(size_t) active.load (std::memory_order_acquire)];
        const double p = (phase - std::floor (phase)) * tableSize;
        const int i0 = (int) p;
        const float f = (float) (p - i0);
        const int a = i0 % tableSize;
        const int b = (i0 + 1) % tableSize;
        return t[(size_t) a] + (t[(size_t) b] - t[(size_t) a]) * f;
    }

    std::atomic<int> version { 0 };   // bumped on every change, the UI watches it

private:
    static void sanitise (std::vector<ShapePoint>& p)
    {
        for (auto& pt : p)
        {
            pt.x = juce::jlimit (0.f, 1.f, pt.x);
            pt.y = juce::jlimit (0.f, 1.f, pt.y);
            pt.curve = juce::jlimit (-1.f, 1.f, pt.curve);
        }
        std::stable_sort (p.begin(), p.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        if (p.size() > (size_t) maxPoints) p.resize ((size_t) maxPoints);
        if (p.empty()) p.push_back ({ 0.f, 1.f, 0.f });
        if (p.front().x > 0.f) p.insert (p.begin(), { 0.f, p.front().y, 0.f });
        p.front().x = 0.f;
        if (p.back().x < 1.f) p.push_back ({ 1.f, p.front().y, 0.f });
        p.back().x = 1.f;
    }

    void bake()
    {
        const int next = 1 - active.load();
        auto& t = tables[(size_t) next];
        for (int i = 0; i < tableSize; ++i)
            t[(size_t) i] = evalPoints ((float) i / (float) tableSize);
        active.store (next, std::memory_order_release);
        ++version;
    }

    std::vector<ShapePoint> points;
    std::array<std::array<float, tableSize>, 2> tables {};
    std::atomic<int> active { 0 };
};

// Ready-made shapes for the shape menu and the presets.
namespace shapes
{
    inline std::vector<ShapePoint> flat()      { return { { 0, 1, 0 }, { 1, 1, 0 } }; }
    inline std::vector<ShapePoint> sine()      { return { { 0, .5f, .55f }, { .25f, 1, -.55f }, { .5f, .5f, .55f }, { .75f, 0, -.55f }, { 1, .5f, 0 } }; }
    inline std::vector<ShapePoint> triangle()  { return { { 0, 0, 0 }, { .5f, 1, 0 }, { 1, 0, 0 } }; }
    inline std::vector<ShapePoint> rampUp()    { return { { 0, 0, 0 }, { 1, 1, 0 } }; }
    inline std::vector<ShapePoint> rampDown()  { return { { 0, 1, 0 }, { 1, 0, 0 } }; }
    inline std::vector<ShapePoint> square()    { return { { 0, 1, 0 }, { .5f, 1, 0 }, { .501f, 0, 0 }, { 1, 0, 0 } }; }

    // Sidechain pump: duck at the start of each cycle, recover with a curve.
    inline std::vector<ShapePoint> pump()      { return { { 0, 0, .7f }, { .45f, 1, 0 }, { 1, 1, 0 } }; }

    // On/off gate with `steps` steps; pattern bits say which are open.
    inline std::vector<ShapePoint> gate (int steps, juce::uint32 pattern, float duty = .8f)
    {
        std::vector<ShapePoint> p;
        const float w = 1.f / (float) steps;
        for (int i = 0; i < steps; ++i)
        {
            const bool open = ((pattern >> i) & 1u) != 0;
            const float x0 = (float) i * w;
            p.push_back ({ x0, open ? 1.f : 0.f, 0 });
            p.push_back ({ x0 + w * duty, open ? 1.f : 0.f, 0 });
            p.push_back ({ x0 + w * duty + .001f, 0.f, 0 });
        }
        p.push_back ({ 1.f, 0.f, 0 });
        return p;
    }

    inline std::vector<ShapePoint> stairs (int steps, bool up)
    {
        std::vector<ShapePoint> p;
        for (int i = 0; i < steps; ++i)
        {
            const float y = up ? (float) i / (float) (steps - 1) : 1.f - (float) i / (float) (steps - 1);
            p.push_back ({ (float) i / (float) steps, y, 0 });
            p.push_back ({ (float) (i + 1) / (float) steps - .001f, y, 0 });
        }
        return p;
    }
}

} // namespace anvil
