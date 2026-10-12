#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Widgets.h"
#include "GardenView.h"
#include "Views.h"
#include "SampleEditor.h"
#include "Panels.h"
#include "NeuralUI.h"
#include "DnaPanel.h"
#include "ForgePanel.h"

// Contenido de la ventana (tamaño de diseño fijo; se escala con el zoom)
class NebulaContent : public juce::Component, private juce::Timer
{
    // (la locura se calcula aquí 20 veces por segundo y anima toda la interfaz)
public:
    static constexpr int designW = 1180, designH = 780;

    explicit NebulaContent (NebulaAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (float)> onZoom;
    void showTab (int tab);
    void showWorkspace (int w);          // 0 DESCUBRIR, 1 CREAR, 2 TRANSFORMAR
    void setZoomChoice (float z);
    void mouseDown (const juce::MouseEvent&) override;   // cada clic abre un paso nuevo de "deshacer"

private:
    void timerCallback() override;
    void rebuildPresetMenu();
    void presetChosen();
    void savePreset();

    NebulaAudioProcessor& proc;
    std::array<std::unique_ptr<IconButton>, 4> tabs;
    std::array<std::unique_ptr<IconButton>, 3> workspaces;
    juce::ComboBox presetBox, zoomBox;
    IconButton prevButton { "", Icons::chevronLeft(), "Sonido anterior" }, nextButton { "", Icons::chevronRight(), "Sonido siguiente" },
               saveButton { "", Icons::save(), "Guardar tu sonido como preset" },
               folderButton { "", Icons::folder(), "Abrir la carpeta de tus presets" },
               recipeButton { "", Icons::recipe(), "La receta: como esta hecho este sonido, paso a paso" },
               browseButton { "", Icons::list(), "Explorar sonidos por categoria, buscar y favoritos" },
               undoButton { "", Icons::undo(), "Deshacer" },
               redoButton { "", Icons::redo(), "Rehacer" };

    GardenView garden;
    EditView edit;
    ModView mod;
    FxView fxView;
    MacroStrip macros;
    PresetBrowser browser;
    NeuralBackground neural;
    double lastTick = 0.0;
    int bgCounter = 0;
    SampleEditor sampleEditor;
    DrawPanel drawPanel;
    RecipePanel recipePanel;
    CleanerPanel cleanerPanel;
    DnaPanel dnaPanel;
    ForgePanel forgePanel;
    TransformView transformView;
    juce::String footerText;
    juce::Rectangle<int> footerArea;
    juce::MidiKeyboardComponent keyboard;

    int currentTab = 0, currentWorkspace = 1;
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
