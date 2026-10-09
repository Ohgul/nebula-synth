#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GardenView.h"

namespace NebulaColours
{
    inline const juce::Colour background { 0xff12141c };
    inline const juce::Colour panel      { 0xff1b1e2b };
    inline const juce::Colour panelEdge  { 0xff2a2e40 };
    inline const juce::Colour track      { 0xff2e3247 };
    inline const juce::Colour accent     { 0xff8b7cf6 }; // violeta
    inline const juce::Colour accent2    { 0xff4fd1c5 }; // turquesa
    inline const juce::Colour text       { 0xffe6e8f2 };
    inline const juce::Colour textDim    { 0xff8d92ab };
}

class NebulaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NebulaLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getLabelFont (juce::Label&) override       { return juce::FontOptions (12.0f); }
};

class KnobCell : public juce::Component
{
public:
    KnobCell (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& name);
    void resized() override;
private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class ChoiceCell : public juce::Component
{
public:
    ChoiceCell (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& name);
    void resized() override;
private:
    juce::Label label;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class NebulaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit NebulaAudioProcessorEditor (NebulaAudioProcessor&);
    ~NebulaAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        std::vector<juce::Component*> cells;
        juce::Rectangle<int> area;
    };

    juce::Component* knob (const char* id, const char* name);
    juce::Component* choice (const char* id, const char* name);
    void layoutRow (std::vector<Section*> row, juce::Rectangle<int> area);
    void stepPreset (int delta);
    void showTab (bool garden);
    void updateVisibility();
    bool samplerEngine() const { return processor.state.getRawParameterValue (ParamID::engine)->load() > 0.5f; }
    Section& topSection() { return samplerEngine() ? sampleSection : oscSection; }
    void timerCallback() override;

    NebulaAudioProcessor& processor;
    NebulaLookAndFeel lnf;

    std::vector<std::unique_ptr<juce::Component>> cells;
    Section oscSection, sampleSection, filterSection, ampSection, fltEnvSection, lfoSection, masterSection;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton gardenTab { "JARDIN" }, editTab { "EDITAR" };
    std::unique_ptr<GardenView> gardenView;
    bool showingGarden = true;
    bool lastEngineSampler = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaAudioProcessorEditor)
};
