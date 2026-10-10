#pragma once
#include "Graphs.h"
#include "SampleEditor.h"

// ===========================================================================
// Pestaña EDITAR: osciladores (o sample), mezcla, unísono, filtros, voces
// ===========================================================================
class EditView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit EditView (NebulaAudioProcessor&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Arrastrar un audio a un oscilador: se vuelve su onda (tabla Usuario)
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override { dropOsc = 0; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    std::function<void()> onOpenSampleEditor, onOpenDraw, onOpenCleaner;

private:
    void timerCallback() override;
    bool samplerEngine() const { return getParamValue (proc.state, ParamID::engine) > 0.5f; }
    void updateVisibility();
    NebKnob& knob (const char* id, const char* name, const char* tip);

    NebulaAudioProcessor& proc;
    std::vector<std::unique_ptr<NebKnob>> knobs;

    // Motor
    IconChoice engine;

    // Osciladores
    IconChoice wave1, wave2;
    WavePreview preview1, preview2;
    ParamCombo table1, table2;
    IconButton draw1 { "", Icons::pencil(), "Dibujar tu propia onda" },
               audio1 { "", Icons::folder(), "Cargar un audio y convertirlo en la onda (tambien puedes arrastrarlo aqui)" },
               audio2 { "", Icons::folder(), "Cargar un audio y convertirlo en la onda (tambien puedes arrastrarlo aqui)" };
    void loadAudioAsWave (const juce::File& f, int osc);
    int oscAt (int x, int y) const;
    int dropOsc = 0;
    juce::String dropMessage;
    double dropMessageTime = 0.0;
    std::unique_ptr<juce::FileChooser> chooser;
    NebKnob *shape1, *oct1, *shape2, *semi2, *fine2;

    // Sample
    RegionView regionMini;
    IconButton editRegion { "Elegir seccion", Icons::scissors(), "Elige que parte del sample se usa" };
    IconToggle reverse, loop;
    IconChoice playMode;
    ParamCombo beats;
    NebKnob *smpStart, *smpTune, *smpTexture, *smpGrain, *smpGrainAmt;
    IconChoice grainType;
    IconButton cleanButton { "Limpiar", Icons::broom(), "Limpiar el audio: separa la parte que quieres rescatar" };

    // Mezcla y unísono
    NebKnob *mix, *sub, *fm, *noise, *noiseCol, *uniV, *uniD, *uniW;

    // Filtros
    FilterGraph filterGraph;
    IconChoice f1Type, f1Slope, f1Char, f2Type, f2Slope, f2Char, routing;
    IconToggle f2On;
    NebKnob *cut1, *res1, *env1, *drive, *cut2, *res2, *env2;

    // Envolvente de amplitud
    EnvGraph ampGraph;
    NebKnob *aA, *aD, *aS, *aR, *aC;

    // Voces y salida
    IconChoice polyMode;
    NebKnob *voices, *glide, *bend, *vel, *volume;

    juce::Rectangle<int> rOsc1, rOsc2, rSample, rMix, rUni, rFilter, rAmp, rVoice, rEngine;
    bool lastSampler = false;
    int layoutSig = -1;
};

// ===========================================================================
// Pestaña MODULACION: envolventes, LFOs y matriz
// ===========================================================================
class ModView : public juce::Component, private juce::Timer
{
public:
    explicit ModView (NebulaAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showEnv (int index);
    NebKnob& knob (const char* id, const char* name, const char* tip);

    NebulaAudioProcessor& proc;
    std::vector<std::unique_ptr<NebKnob>> knobs;

    // Envolventes
    std::array<std::unique_ptr<IconButton>, 3> envTabs;
    EnvGraph envGraph;
    std::array<std::array<NebKnob*, 5>, 3> envKnobs {};
    IconToggle env2Loop, env3Loop;
    int currentEnv = 1;

    // LFOs
    IconChoice lfo1Shape, lfo2Shape;
    LfoGraph lfo1Graph, lfo2Graph;
    ParamCombo lfo1Sync, lfo2Sync;
    IconToggle lfo1Retrig, lfo2Retrig, lfo1Power, lfo2Power;
    void timerCallback() override;
    NebKnob *l1Rate, *l1Fade, *l1Pitch, *l1Filter, *l2Rate, *l2Fade;

    // Matriz
    MatrixGrid matrix;
    NebKnob *modTime, *modAmount;

    juce::Rectangle<int> rEnv, rLfo1, rLfo2, rMatrix;
};
