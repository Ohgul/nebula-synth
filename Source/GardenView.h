#pragma once
#include "Widgets.h"
#include "SampleEditor.h"

// Lienzo del Jardín: la semilla al centro y los brotes alrededor, animados.
class GardenCanvas : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GardenCanvas (NebulaAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void animate();                    // se llama ~30 veces por segundo
    std::function<void()> onSelect;    // para escuchar el sonido elegido

private:
    static constexpr int seedIndex = -1, none = -2, profileSize = 96;

    // Lo que se dibuja de cada "organismo"; se interpola para que se transforme suavemente
    struct Blob
    {
        std::array<float, profileSize> prof {};
        juce::Colour colour;
        float core = 0.2f, halo = 1.3f, rot = 0.0f, noise = 0.0f, lfo = 0.0f;
        bool init = false;
    };

    Blob target (const Genome&) const;
    static void approach (Blob& cur, const Blob& tgt, float k);
    void drawBlob (juce::Graphics&, juce::Point<float> c, float r, const Blob&, bool selected, bool hover, float phase, float glowBoost) const;

    juce::Point<float> sproutPos (int k) const;
    float sproutRadius() const { return seedRadius * 0.55f; }
    int hitTest (juce::Point<float>) const;

    NebulaAudioProcessor& processor;
    Garden& garden;
    juce::Point<float> centre;
    float seedRadius = 60.0f, minDist = 130.0f, maxDist = 220.0f;
    int hover = none;

    Blob seedBlob;
    std::array<Blob, Garden::numSprouts> sproutBlobs;
    int lastGeneration = -1, lastSelected = -1;
    double growStart = 0.0;
    juce::Point<float> seedFrom;
    std::array<float, 512> scopeBuf {};
    float level = 0.0f;
};

// Columna izquierda del Jardín: cargar un sample e imitarlo, tocarlo o hacer una onda.
class SamplePanel : public juce::Component
{
public:
    explicit SamplePanel (NebulaAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void update();                      // llamado por el temporizador de GardenView
    void loadFile (const juce::File&);
    void chooseFile();
    void setDragHover (bool h)          { dragHover = h; repaint(); }
    std::function<void()> onSoundChanged, onOpenEditor, onOpenCleaner, onOpenRecipe;
    std::function<int()> framesForWave;

private:
    juce::Rectangle<int> dropArea() const { return { 0, 28, getWidth(), 80 }; }

    NebulaAudioProcessor& processor;
    RegionView region;
    IconButton loadButton    { "", Icons::folder(), "Cargar otro audio" },
               recreateButton { "Recrear sonido", Icons::wand(), "Resintesis: Nebula analiza la seccion y la reconstruye con sus osciladores (nota exacta, timbre, ruido y envolventes)" },
               imitateButton { "Imitar con osciladores", Icons::sliders(), "Busca el patch de ondas clasicas (sierra, pulso, filtro...) que mas se parece" },
               useButton     { "Usar el sample", Icons::waveform(), "Toca el audio como instrumento; los brotes lo transforman" },
               waveButton    { "Hacer onda", Icons::cycle(), "Convierte la seccion en una tabla de ondas para el oscilador 1" },
               editButton    { "Seccion", Icons::scissors(), "Elige que parte del audio se usa (zoom, inicio y fin)" },
               cleanButton   { "Limpiar", Icons::broom(), "Separa la parte del audio que quieres rescatar (nota, golpes, sin ruido...)" },
               recipeButton  { "Ver la receta", Icons::recipe(), "Como es este sonido y como esta hecho, paso a paso" },
               cancelButton  { "Cancelar", Icons::stop(), "Detener la busqueda" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<const SampleData> shown;
    juce::String message;
    bool dragHover = false;
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

    std::function<void()> onOpenSampleEditor, onOpenCleaner, onOpenRecipe;
    std::function<int()> framesForWave;

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

    juce::Slider mutationSlider;
    IconButton germinateButton { "Germinar", Icons::sprout(), "Crea 8 variaciones a partir del sonido que suena ahora" },
               rerollButton    { "Otra tanda", Icons::dice(), "Nuevas variaciones de la misma semilla" },
               randomButton    { "Al azar", Icons::sparkle(), "Empieza desde un sonido aleatorio" },
               backButton      { "Atras", Icons::back(), "Regresa a la generacion anterior" },
               lockEnvButton   { "Fijar env", Icons::lock(), "Las mutaciones no cambian la envolvente de volumen" },
               lockFilterButton { "Fijar filtro", Icons::filterShape (0), "Las mutaciones no cambian los filtros (el sonido conserva su caracter)" },
               listenButton    { "Escuchar", Icons::speaker(), "Suena cada vez que eliges un brote" };

    int lastVersion = -1;
    double noteOffTime = 0.0;
    bool noteIsOn = false;
};
