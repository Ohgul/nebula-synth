#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Lienzo del Jardín: la semilla al centro y los brotes alrededor.
class GardenCanvas : public juce::Component
{
public:
    explicit GardenCanvas (NebulaAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onSelect; // para el auditorio

private:
    static constexpr int seedIndex = -1, none = -2;

    juce::Point<float> sproutPos (int k) const;
    float sproutRadius() const { return seedRadius * 0.55f; }
    int hitTest (juce::Point<float>) const;
    void drawBlob (juce::Graphics&, juce::Point<float> c, float r, const Genome&, bool selected, bool hover, float phase) const;

    NebulaAudioProcessor& processor;
    Garden& garden;
    juce::Point<float> centre;
    float seedRadius = 60.0f, minDist = 130.0f, maxDist = 220.0f;
    int hover = none;
};

// Columna izquierda del Jardín: cargar un sample e imitarlo o tocarlo.
class SamplePanel : public juce::Component
{
public:
    explicit SamplePanel (NebulaAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void update();                      // llamado por el temporizador de GardenView
    void loadFile (const juce::File&);
    void setDragHover (bool h)          { dragHover = h; repaint(); }
    std::function<void()> onSoundChanged;

private:
    void chooseFile();
    void rebuildWaveform();
    juce::Rectangle<int> dropArea() const { return { 0, 30, getWidth(), 92 }; }
    static juce::String noteName (float midi);

    NebulaAudioProcessor& processor;
    juce::TextButton imitateButton { "Imitar con el sinte" }, useButton { "Usar el sample" }, cancelButton { "Cancelar" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<const SampleData> shown;
    juce::Path waveform;
    juce::String message;
    bool dragHover = false, wasImitating = false;
    int seenImitations = 0;
};

class GardenView : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit GardenView (NebulaAudioProcessor&);
    ~GardenView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { samplePanel.setDragHover (true); dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { samplePanel.setDragHover (false); dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void timerCallback() override;
    void audition();
    void refreshButtons();

    NebulaAudioProcessor& processor;
    Garden& garden;
    GardenCanvas canvas;
    SamplePanel samplePanel;
    bool dragHover = false;
    int auditionNoteOn = 60;

    juce::Label mutationLabel;
    juce::Slider mutationSlider;
    juce::TextButton germinateButton { "Germinar desde aqui" }, rerollButton { "Otra tanda" },
                     randomButton { "Semilla al azar" }, backButton { "Atras" };
    juce::ToggleButton lockEnvButton { "Fijar envolvente" }, listenButton { "Escuchar al hacer clic" };

    int lastVersion = -1;
    double noteOffTime = 0.0;
    bool noteIsOn = false;
};
