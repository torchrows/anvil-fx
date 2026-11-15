#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "ShapeEditor.h"
#include "XYPad.h"
#include "AnvilView.h"

class AnvilEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnvilEditor (AnvilProcessor&);
    ~AnvilEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showModule (int m) { selectModule (m); }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> att;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAttachment> att;
    };

    class ModuleList : public juce::Component
    {
    public:
        explicit ModuleList (AnvilEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        int rowAt (float y) const;
        juce::Rectangle<float> rowBounds (int i) const;
        juce::Rectangle<float> powerBounds (int i) const;
        int hover = -1;
    private:
        AnvilEditor& ed;
    };

    void selectModule (int m);
    void rebuildModulePanel();
    void refreshPresetBox();
    void presetChosen();
    void stepPreset (int dir);
    void showShapeMenu();
    void savePresetDialog();
    void loadSampleDialog();
    void timerCallback() override;
    void setupKnob (Knob& k, const juce::String& paramId, const juce::String& name, bool bipolar);
    bool isOn (int m) const;

    AnvilProcessor& proc;
    anvil::ui::LookAndFeel lnf;

    // top bar
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" };
    Knob inKnob, outKnob, mixKnob;

    // body
    ModuleList moduleList { *this };
    int selected = anvil::mMod;
    juce::Label moduleTitle, moduleBlurb;
    juce::TextButton powerBtn { "Off" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;

    // shape toolbar
    juce::ComboBox rateBox, targetBox, gridBox;
    juce::Label rateLbl, targetLbl, gridLbl, depthLbl;
    juce::Slider depthSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<ComboAttachment> rateAtt, targetAtt;
    std::unique_ptr<SliderAttachment> depthAtt;
    juce::TextButton shapesBtn { "Shapes" };
    anvil::ui::ShapeEditor shapeEditor;
    int shapeVersionSeen = -1;
    std::vector<anvil::ShapePoint> clipboard;

    // module controls
    juce::Component controls;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::unique_ptr<anvil::ui::XYPad> xyPad;
    juce::TextButton loadBtn { "Load sound" }, clearBtn { "Clear" };
    juce::Label sampleName;
    std::unique_ptr<juce::FileChooser> chooser;

    anvil::ui::AnvilView anvilView;

    juce::Array<juce::File> userPresets;
    bool updatingPresetBox = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnvilEditor)
};
