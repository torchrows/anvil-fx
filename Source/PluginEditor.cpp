#include "PluginEditor.h"

using namespace anvil;
using namespace anvil::ui;

namespace
{
    constexpr int topH = 64;
    constexpr int listW = 196;
    constexpr int anvilW = 230;
    constexpr int rowH = 34;
}

AnvilEditor::AnvilEditor (AnvilProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // ---- top bar
    addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this] { presetChosen(); };
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn }) addAndMakeVisible (*b);
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    saveBtn.onClick = [this] { savePresetDialog(); };

    setupKnob (inKnob, "in_gain", "Input", true);
    setupKnob (outKnob, "out_gain", "Output", true);
    setupKnob (mixKnob, "master_mix", "Mix", false);
    for (auto* k : { &inKnob, &outKnob, &mixKnob })
    {
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    // ---- body
    addAndMakeVisible (moduleList);
    addAndMakeVisible (moduleTitle);
    addAndMakeVisible (moduleBlurb);
    moduleTitle.setFont (font (24.f, true));
    moduleBlurb.setFont (font (13.f));
    moduleBlurb.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (powerBtn);
    powerBtn.setClickingTogglesState (true);
    powerBtn.onStateChange = [this] { powerBtn.setButtonText (powerBtn.getToggleState() ? "On" : "Off"); };

    for (auto* l : { &rateLbl, &targetLbl, &gridLbl, &depthLbl })
    {
        addAndMakeVisible (*l);
        l->setFont (font (11.f, true));
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    rateLbl.setText ("RATE", juce::dontSendNotification);
    targetLbl.setText ("MOVES", juce::dontSendNotification);
    gridLbl.setText ("GRID", juce::dontSendNotification);
    depthLbl.setText ("DEPTH", juce::dontSendNotification);

    for (auto* b : { &rateBox, &targetBox, &gridBox }) addAndMakeVisible (*b);
    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    gridBox.onChange = [this] { static const int g[] = { 4, 8, 16, 32 }; shapeEditor.setGrid (g[juce::jlimit (0, 3, gridBox.getSelectedId() - 1)]); };

    addAndMakeVisible (depthSlider);
    depthSlider.setColour (juce::Slider::trackColourId, colours::accent);
    depthSlider.setColour (juce::Slider::backgroundColourId, colours::well);
    depthSlider.setColour (juce::Slider::thumbColourId, colours::white);
    depthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    depthSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    addAndMakeVisible (shapesBtn);
    shapesBtn.onClick = [this] { showShapeMenu(); };

    addAndMakeVisible (shapeEditor);
    shapeEditor.onChange = [this] (std::vector<ShapePoint> pts)
    {
        proc.setShape (selected, std::move (pts));
        shapeVersionSeen = proc.engine.shapes[selected].version.load();
    };
    shapeEditor.getPhase = [this] { return proc.engine.shapePhase[selected].load(); };
    shapeEditor.getDepth = [this] { return proc.apvts.getRawParameterValue (pid (selected, "shp_depth"))->load(); };

    addAndMakeVisible (controls);
    controls.addChildComponent (loadBtn);
    controls.addChildComponent (clearBtn);
    controls.addChildComponent (sampleName);
    sampleName.setFont (font (13.f));
    sampleName.setColour (juce::Label::textColourId, colours::textDim);
    loadBtn.onClick = [this] { loadSampleDialog(); };
    clearBtn.onClick = [this] { proc.engine.clearSample(); rebuildModulePanel(); };

    addAndMakeVisible (anvilView);
    anvilView.getLevel = [this] { return proc.engine.meterOut.exchange (0.f); };

    proc.onPresetChanged = [safe = juce::Component::SafePointer<AnvilEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (auto* e = safe.getComponent())
            {
                e->refreshPresetBox();
                e->shapeVersionSeen = -1;
                e->rebuildModulePanel();
            }
        });
    };

    refreshPresetBox();
    selectModule (selected);

    setResizable (true, true);
    setResizeLimits (1000, 640, 1700, 1100);
    setSize (1100, 690);
    startTimerHz (15);
}

AnvilEditor::~AnvilEditor()
{
    proc.onPresetChanged = nullptr;
    setLookAndFeel (nullptr);
}

void AnvilEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, bool bipolar)
{
    k.slider.getProperties().set ("bipolar", bipolar);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setFont (font (12.f, true));
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colours::textDim);
    k.att = std::make_unique<SliderAttachment> (proc.apvts, paramId, k.slider);
    if (auto* param = proc.apvts.getParameter (paramId))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

bool AnvilEditor::isOn (int m) const
{
    return proc.apvts.getRawParameterValue (pid (m, "on"))->load() > 0.5f;
}

// ------------------------------------------------------------------ layout

void AnvilEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    // top bar
    auto top = getLocalBounds().removeFromTop (topH).toFloat();
    g.setColour (colours::panel);
    g.fillRect (top);
    g.setColour (colours::line);
    g.drawHorizontalLine (topH - 1, 0.f, (float) getWidth());

    g.setColour (colours::text);
    g.setFont (font (26.f, true));
    g.drawText ("ANVIL", 20, 0, 110, topH, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (font (26.f));
    g.drawText ("FX", 108, 0, 60, topH, juce::Justification::centredLeft);

    // body panels
    auto body = getLocalBounds().withTrimmedTop (topH).reduced (12);
    auto centre = body.withTrimmedLeft (listW + 12).withTrimmedRight (anvilW + 12).toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (centre, 12.f);
    g.fillRoundedRectangle (body.removeFromLeft (listW).toFloat(), 12.f);
}

void AnvilEditor::resized()
{
    auto top = getLocalBounds().removeFromTop (topH).reduced (16, 12);
    top.removeFromLeft (160);

    auto knobsArea = top.removeFromRight (3 * 64);
    for (auto* k : { &inKnob, &outKnob, &mixKnob })
    {
        auto c = knobsArea.removeFromLeft (64);
        k->label.setBounds (c.removeFromRight (30).withTrimmedTop (2));
        k->slider.setBounds (c.withSizeKeepingCentre (46, 46));
    }
    for (auto* k : { &inKnob, &outKnob, &mixKnob })
    {
        auto b = k->slider.getBounds();
        k->label.setBounds (b.getX() - 8, b.getBottom() - 4, b.getWidth() + 16, 14);
        k->slider.setBounds (b.translated (0, -6));
    }

    auto presetArea = top.withSizeKeepingCentre (std::min (460, top.getWidth()), 34);
    prevBtn.setBounds (presetArea.removeFromLeft (34));
    presetArea.removeFromLeft (6);
    saveBtn.setBounds (presetArea.removeFromRight (70));
    presetArea.removeFromRight (6);
    nextBtn.setBounds (presetArea.removeFromRight (34));
    presetArea.removeFromRight (6);
    presetBox.setBounds (presetArea);

    auto body = getLocalBounds().withTrimmedTop (topH).reduced (12);
    moduleList.setBounds (body.removeFromLeft (listW));
    body.removeFromLeft (12);
    anvilView.setBounds (body.removeFromRight (anvilW));
    body.removeFromRight (12);

    auto c = body.reduced (18, 14);
    auto head = c.removeFromTop (54);
    powerBtn.setBounds (head.removeFromRight (64).withSizeKeepingCentre (64, 30));
    head.removeFromRight (10);
    {
        auto tools = head.removeFromRight (190).withSizeKeepingCentre (190, 30);
        shapesBtn.setBounds (tools.removeFromRight (82));
        tools.removeFromRight (6);
        gridBox.setBounds (tools.removeFromRight (66));
        tools.removeFromRight (4);
        gridLbl.setBounds (tools);
    }
    moduleTitle.setBounds (head.removeFromTop (30));
    moduleBlurb.setBounds (head);

    c.removeFromTop (6);
    auto bar = c.removeFromTop (30);
    rateLbl.setBounds (bar.removeFromLeft (40)); bar.removeFromLeft (4);
    rateBox.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (14);
    targetLbl.setBounds (bar.removeFromLeft (48)); bar.removeFromLeft (4);
    targetBox.setBounds (bar.removeFromLeft (130)); bar.removeFromLeft (14);
    depthLbl.setBounds (bar.removeFromLeft (46)); bar.removeFromLeft (4);
    depthSlider.setBounds (bar);

    c.removeFromTop (10);
    controls.setBounds (c.removeFromBottom (selected == mFormant ? 190 : 128));
    c.removeFromBottom (12);
    shapeEditor.setBounds (c);

    rebuildModulePanel();
}

// ------------------------------------------------------------------ module list

juce::Rectangle<float> AnvilEditor::ModuleList::rowBounds (int i) const
{
    return { 8.f, 12.f + (float) i * (float) rowH, (float) getWidth() - 16.f, (float) rowH - 4.f };
}

juce::Rectangle<float> AnvilEditor::ModuleList::powerBounds (int i) const
{
    auto r = rowBounds (i);
    return r.removeFromRight (30.f).withSizeKeepingCentre (14.f, 14.f);
}

int AnvilEditor::ModuleList::rowAt (float y) const
{
    const int i = (int) std::floor ((y - 12.f) / (float) rowH);
    return juce::isPositiveAndBelow (i, (int) numModules) ? i : -1;
}

void AnvilEditor::ModuleList::paint (juce::Graphics& g)
{
    for (int i = 0; i < numModules; ++i)
    {
        auto r = rowBounds (i);
        const bool sel = ed.selected == i;
        const bool on = ed.isOn (i);
        if (sel || hover == i)
        {
            g.setColour (sel ? colours::panelHi : colours::panelHi.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 8.f);
        }
        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (font (14.f, sel));
        g.drawText (modules()[(size_t) i].name, r.withTrimmedLeft (12.f), juce::Justification::centredLeft);

        auto pb = powerBounds (i);
        if (on)
        {
            g.setColour (colours::white.withAlpha (0.2f));
            g.fillEllipse (pb.expanded (3.f));
            g.setColour (colours::white);
            g.fillEllipse (pb);
        }
        else
        {
            g.setColour (colours::off);
            g.drawEllipse (pb.reduced (1.f), 1.5f);
        }
    }
}

void AnvilEditor::ModuleList::mouseDown (const juce::MouseEvent& e)
{
    const int i = rowAt (e.position.y);
    if (i < 0) return;
    if (powerBounds (i).expanded (8.f).contains (e.position))
    {
        if (auto* p = ed.proc.apvts.getParameter (pid (i, "on")))
            p->setValueNotifyingHost (ed.isOn (i) ? 0.f : 1.f);
    }
    ed.selectModule (i);
    repaint();
}

void AnvilEditor::ModuleList::mouseMove (const juce::MouseEvent& e)
{
    const int i = rowAt (e.position.y);
    if (i != hover) { hover = i; repaint(); }
}

// ------------------------------------------------------------------ module panel

void AnvilEditor::selectModule (int m)
{
    selected = m;
    auto& d = modules()[(size_t) m];
    moduleTitle.setText (d.name, juce::dontSendNotification);
    moduleBlurb.setText (d.blurb, juce::dontSendNotification);

    powerAtt.reset();
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, pid (m, "on"), powerBtn);
    powerBtn.setButtonText (powerBtn.getToggleState() ? "On" : "Off");

    rateAtt.reset(); targetAtt.reset(); depthAtt.reset();
    rateBox.clear (juce::dontSendNotification);
    rateBox.addItemList (rateNames(), 1);
    targetBox.clear (juce::dontSendNotification);
    targetBox.addItemList (targetNames (m), 1);
    rateAtt = std::make_unique<ComboAttachment> (proc.apvts, pid (m, "shp_rate"), rateBox);
    targetAtt = std::make_unique<ComboAttachment> (proc.apvts, pid (m, "shp_tgt"), targetBox);
    depthAtt = std::make_unique<SliderAttachment> (proc.apvts, pid (m, "shp_depth"), depthSlider);
    targetBox.setEnabled (m != mGate);

    shapeVersionSeen = -1;
    resized();
    moduleList.repaint();
}

void AnvilEditor::rebuildModulePanel()
{
    const int m = selected;
    auto& d = modules()[(size_t) m];

    for (auto& k : knobs) controls.removeChildComponent (&k->slider), controls.removeChildComponent (&k->label);
    for (auto& c : choices) controls.removeChildComponent (&c->box), controls.removeChildComponent (&c->label);
    knobs.clear();
    choices.clear();
    if (xyPad) { controls.removeChildComponent (xyPad.get()); xyPad.reset(); }

    auto area = controls.getLocalBounds();
    const bool isSample = m == mSample;
    loadBtn.setVisible (isSample);
    clearBtn.setVisible (isSample);
    sampleName.setVisible (isSample);

    if (isSample)
    {
        auto col = area.removeFromLeft (150).reduced (0, 8);
        loadBtn.setBounds (col.removeFromTop (32));
        col.removeFromTop (6);
        clearBtn.setBounds (col.removeFromTop (28).removeFromLeft (80));
        col.removeFromTop (4);
        sampleName.setBounds (col);
        auto nm = proc.engine.getSampleName();
        sampleName.setText (nm.isEmpty() ? "No sound loaded" : nm, juce::dontSendNotification);
        area.removeFromLeft (10);
    }

    if (m == mFormant)
    {
        auto* px = proc.apvts.getParameter (pid (m, "shift"));
        auto* py = proc.apvts.getParameter (pid (m, "pitch"));
        xyPad = std::make_unique<XYPad> (*px, *py);
        controls.addAndMakeVisible (*xyPad);
        xyPad->setBounds (area.removeFromLeft (std::min (420, area.getWidth() - 110)));
        area.removeFromLeft (16);
    }
    else
    {
        for (auto& k : d.knobs)
        {
            auto kn = std::make_unique<Knob>();
            setupKnob (*kn, pid (m, k.id), k.name, k.bipolar);
            controls.addAndMakeVisible (kn->slider);
            controls.addAndMakeVisible (kn->label);
            knobs.push_back (std::move (kn));
        }
    }

    {
        auto kn = std::make_unique<Knob>();
        setupKnob (*kn, pid (m, "mix"), m == mGate ? "Output" : "Mix", false);
        if (m == mGate) kn->label.setText ("Amount", juce::dontSendNotification);
        controls.addAndMakeVisible (kn->slider);
        controls.addAndMakeVisible (kn->label);
        knobs.push_back (std::move (kn));
    }

    for (auto& c : d.choices)
    {
        auto ch = std::make_unique<Choice>();
        ch->box.addItemList (c.options, 1);
        ch->att = std::make_unique<ComboAttachment> (proc.apvts, pid (m, c.id), ch->box);
        ch->label.setText (juce::String (c.name).toUpperCase(), juce::dontSendNotification);
        ch->label.setFont (font (11.f, true));
        ch->label.setColour (juce::Label::textColourId, colours::textDim);
        controls.addAndMakeVisible (ch->box);
        controls.addAndMakeVisible (ch->label);
        choices.push_back (std::move (ch));
    }

    // lay out: knobs left to right, choices stacked at the right
    auto choiceCol = choices.empty() ? juce::Rectangle<int>() : area.removeFromRight (150);
    const int nKnobs = (int) knobs.size();
    const int kw = m == mFormant ? 88 : juce::jlimit (60, 88, (area.getWidth() - 6 * nKnobs) / std::max (1, nKnobs));
    if (m == mFormant)
    {
        auto col = area.removeFromLeft (kw).withSizeKeepingCentre (kw, 118);
        auto& k = *knobs.back();
        k.label.setBounds (col.removeFromBottom (18));
        k.slider.setBounds (col);
    }
    else
    {
        for (auto& k : knobs)
        {
            auto col = area.removeFromLeft (kw);
            k->label.setBounds (col.removeFromBottom (18));
            k->slider.setBounds (col.reduced (4, 0));
            area.removeFromLeft (6);
        }
    }
    for (auto& c : choices)
    {
        c->label.setBounds (choiceCol.removeFromTop (16));
        c->box.setBounds (choiceCol.removeFromTop (30));
        choiceCol.removeFromTop (10);
    }

    // shape
    shapeEditor.setPoints (proc.engine.shapes[m].getPoints());
    shapeVersionSeen = proc.engine.shapes[m].version.load();
}

// ------------------------------------------------------------------ presets

void AnvilEditor::refreshPresetBox()
{
    updatingPresetBox = true;
    presetBox.clear (juce::dontSendNotification);
    auto& f = factoryPresets();
    juce::String cat;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f[i].category != cat)
        {
            cat = f[i].category;
            presetBox.addSectionHeading (cat);
        }
        presetBox.addItem (f[i].name, (int) i + 1);
    }
    userPresets = proc.getUserPresets();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("Your presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 1000 + i);
    }

    const auto name = proc.getPresetName();
    int found = 0;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == name) { found = presetBox.getItemId (i); break; }
    if (found) presetBox.setSelectedId (found, juce::dontSendNotification);
    else presetBox.setText (name, juce::dontSendNotification);
    updatingPresetBox = false;
}

void AnvilEditor::presetChosen()
{
    if (updatingPresetBox) return;
    const int id = presetBox.getSelectedId();
    if (id <= 0) return;
    if (id >= 1000) proc.loadUserPreset (userPresets[id - 1000]);
    else proc.applyFactoryPreset (id - 1);
}

void AnvilEditor::stepPreset (int dir)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemId (i) > 0) ids.add (presetBox.getItemId (i));
    if (ids.isEmpty()) return;
    int idx = ids.indexOf (presetBox.getSelectedId());
    idx = idx < 0 ? 0 : (idx + dir + ids.size()) % ids.size();
    presetBox.setSelectedId (ids[idx], juce::sendNotificationSync);
}

void AnvilEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name your preset. It's saved to Documents/Anvil FX/Presets.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getPresetName(), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
        {
            const auto name = w->getTextEditorContents ("name");
            if (proc.saveUserPreset (name))
                refreshPresetBox();
        }
    }), true);
}

void AnvilEditor::loadSampleDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
        {
            if (proc.engine.loadSample (f))
            {
                if (auto* p = proc.apvts.getParameter (pid (mSample, "on")))
                    p->setValueNotifyingHost (1.f);
            }
            else
                sampleName.setText ("Couldn't read that file", juce::dontSendNotification);
        }
        rebuildModulePanel();
    });
}

// ------------------------------------------------------------------ shape menu

void AnvilEditor::showShapeMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Shapes");
    m.addItem (1, "Flat");
    m.addItem (2, "Sine");
    m.addItem (3, "Triangle");
    m.addItem (4, "Ramp up");
    m.addItem (5, "Ramp down");
    m.addItem (6, "Square");
    m.addItem (7, "Sidechain pump");
    m.addItem (8, "Gate 1/16 (all steps)");
    m.addItem (9, "Gate 1/8 offbeats");
    m.addItem (10, "Stairs up");
    m.addItem (11, "Stairs down");
    m.addSectionHeader ("Edit");
    m.addItem (20, "Invert");
    m.addItem (21, "Reverse");
    m.addItem (22, "Copy");
    m.addItem (23, "Paste", ! clipboard.empty());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapesBtn), [this] (int r)
    {
        if (r == 0) return;
        auto pts = proc.engine.shapes[selected].getPoints();
        switch (r)
        {
            case 1: pts = shapes::flat(); break;
            case 2: pts = shapes::sine(); break;
            case 3: pts = shapes::triangle(); break;
            case 4: pts = shapes::rampUp(); break;
            case 5: pts = shapes::rampDown(); break;
            case 6: pts = shapes::square(); break;
            case 7: pts = shapes::pump(); break;
            case 8: pts = shapes::gate (16, 0xFFFFu, 0.75f); break;
            case 9: pts = shapes::gate (8, 0b10101010u, 0.8f); break;
            case 10: pts = shapes::stairs (8, true); break;
            case 11: pts = shapes::stairs (8, false); break;
            case 20: for (auto& p : pts) { p.y = 1.f - p.y; p.curve = -p.curve; } break;
            case 21:
            {
                std::vector<ShapePoint> rev;
                for (int i = (int) pts.size() - 1; i >= 0; --i)
                {
                    const float c = i > 0 ? -pts[(size_t) i - 1].curve : 0.f;
                    rev.push_back ({ 1.f - pts[(size_t) i].x, pts[(size_t) i].y, c });
                }
                pts = rev;
                break;
            }
            case 22: clipboard = pts; return;
            case 23: pts = clipboard; break;
            default: return;
        }
        proc.setShape (selected, pts);
        shapeEditor.setPoints (proc.engine.shapes[selected].getPoints());
        // picking a shape with depth at 0 would do nothing; give it some
        if (auto* d = proc.apvts.getParameter (pid (selected, "shp_depth")))
            if (d->getValue() < 0.01f && r < 20) d->setValueNotifyingHost (selected == mGate ? 1.f : 0.6f);
    });
}

// ------------------------------------------------------------------ timer

void AnvilEditor::timerCallback()
{
    moduleList.repaint();
    const int v = proc.engine.shapes[selected].version.load();
    if (v != shapeVersionSeen)
    {
        shapeEditor.setPoints (proc.engine.shapes[selected].getPoints());
        shapeVersionSeen = v;
    }
}
