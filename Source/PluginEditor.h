#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Widgets.h"
#include "GardenView.h"
#include "Views.h"
#include "SampleEditor.h"

// Contenido de la ventana (tamaño de diseño fijo; se escala con el zoom)
class NebulaContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int designW = 1180, designH = 780;

    explicit NebulaContent (NebulaAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (float)> onZoom;
    void showTab (int tab);
    void setZoomChoice (float z);

private:
    void timerCallback() override;
    void rebuildPresetMenu();
    void presetChosen();
    void savePreset();

    NebulaAudioProcessor& proc;
    std::array<std::unique_ptr<IconButton>, 3> tabs;
    juce::ComboBox presetBox, zoomBox;
    IconButton prevButton { "", Icons::back(), "Preset anterior" }, nextButton { "", Icons::play(), "Preset siguiente" },
               saveButton { "", Icons::save(), "Guardar tu sonido como preset" },
               folderButton { "", Icons::folder(), "Abrir la carpeta de tus presets" };

    GardenView garden;
    EditView edit;
    ModView mod;
    SampleEditor sampleEditor;
    DrawPanel drawPanel;
    juce::MidiKeyboardComponent keyboard;

    int currentTab = 0;
    juce::StringArray userPresets;
    std::unique_ptr<juce::AlertWindow> saveDialog;
};

class NebulaAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit NebulaAudioProcessorEditor (NebulaAudioProcessor&);
    ~NebulaAudioProcessorEditor() override;
    void resized() override;

private:
    void setZoom (float z);

    NebulaAudioProcessor& processor;
    NebulaLookAndFeel lnf;
    NebulaContent content;
    juce::TooltipWindow tooltips { this, 600 };
    float zoom = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaAudioProcessorEditor)
};
