#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "SampleData.h"
#include "Wavetable.h"
#include "SynthVoice.h"
#include "Garden.h"
#include "Imitator.h"
#include "SoundProfile.h"
#include "Recipe.h"
#include "Cleaner.h"
#include "ResynthBuilder.h"
#include "FxChain.h"
#include "Exporter.h"
#include "Dna.h"
#include "Similarity.h"
#include "Forge/ForgeJob.h"

class NebulaAudioProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    NebulaAudioProcessor();
    ~NebulaAudioProcessor() override { stopTimer(); forgeJob.stop(); imitator.stop(); cleaner.cancel(); cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    // Azar reproducible (ver VoiceContext). Sin interfaz: lo guarda el proyecto.
    // (cambia el modo y la semilla; el reinicio completo del estado lo hacen prepareToPlay() y reset())
    void setRandomMode (bool seeded, juce::uint32 seed) { voiceCtx.seededRandom = seeded; voiceCtx.renderSeed = seed; voiceCtx.noteOnCount = 0; }
    bool isSeededRandom() const { return voiceCtx.seededRandom.load(); }
    juce::uint32 renderSeed() const { return voiceCtx.renderSeed.load(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nebula"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Los presets se manejan desde la interfaz del plugin, no como "programas" del host
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Presets ----
    void loadPreset (int index);
    int getPresetIndex() const;
    static juce::File userPresetFolder();
    juce::StringArray userPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);

    juce::UndoManager undoManager;         // deshacer / rehacer
    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState; // teclado en pantalla
    WavetableBank tables;
    Garden garden;                         // modo Jardín (variaciones)

    static constexpr int auditionNote = 60;
    // Todo sample se afina a C: la tecla C más cercana a su nota suena en C
    int homeKey() const
    {
        auto s = sampleSource.get();
        if (s == nullptr || ! s->hasPitch) return 60;
        return juce::jlimit (24, 96, 12 * juce::roundToInt ((s->rootNote + rootOffset()) / 12.0f));
    }

    // ---- v3: resíntesis, A/B, locura ----
    std::shared_ptr<const ResynthModel> resynthModel() const { return voiceCtx.models.get(); }

    // ---- ADN ----
    // El modelo base (lo que sale de RECREAR o del proyecto) no se modifica; los genes estructurales generan una
    // variante. applyDna() la reconstruye ya (un temporizador lo hace solo al mover los genes).
    std::shared_ptr<const ResynthModel> baseResynthModel() const { return std::atomic_load (&baseModel); }
    void applyDna();
    // Mutación reproducible: misma semilla + mismo ADN = mismo resultado. Los genes con candado no cambian.
    void mutateDna (float amount, juce::int64 seed);
    juce::int64 nextDnaSeed();                       // semilla siguiente (se guarda con el proyecto)
    bool isGeneLocked (const juce::String& id) const;
    void setGeneLocked (const juce::String& id, bool locked);
    void resetGene (const juce::String& id);
    juce::String dnaToJson() const;                  // copiar
    bool dnaFromJson (const juce::String& json);     // pegar
    void storeDnaSlot (int slot);                    // A/B: guarda el ADN actual en la ranura 0 (A) o 1 (B)
    bool recallDnaSlot (int slot);                   // A/B: carga la ranura
    bool hasDnaSlot (int slot) const;
    void setAB (bool original) { voiceCtx.abOriginal = original; }
    bool isAB() const { return voiceCtx.abOriginal.load(); }
    float computeCraziness();
    // Exportar (para arrastrar a Ableton). Devuelven el archivo o un File vacío.
    juce::File exportMidi();
    juce::File exportAudio();
    juce::File exportWavetable();            // 0..1: qué tan lejos y qué tan "loco" está el sonido
    void markReference();                // el sonido actual pasa a ser la referencia (locura 0)
    std::atomic<float> craziness { 0.0f };

    // ---- Sample ----
    SampleSource sampleSource;
    juce::String loadSample (const juce::File& file);          // devuelve "" o un mensaje de error
    void setSample (std::shared_ptr<const SampleData> s, bool keepHistory = false);
    void useSampleAsInstrument();                                // "Usar el sample"
    void startImitation();                                       // "Imitar con el sinte"
    bool makeWavetableFromSample (int frames, float smoothing);  // "Hacer onda"
    void setUserTableFromDrawing (const std::vector<float>& points);
    void cancelImitation()            { imitator.cancel(); }
    bool isImitating() const          { return imitator.isRunning(); }
    float imitationProgress() const   { return imitator.progress.load(); }
    float imitationSimilarity() const { return imitator.similarity.load(); }

    // Sección elegida del sample (0..1) y corrección de nota raíz (semitonos)
    void setRegion (float start, float end);
    float regionStart() const { return voiceCtx.regionStart.load(); }
    float regionEnd() const   { return voiceCtx.regionEnd.load(); }
    void setRootOffset (float semitones);
    float rootOffset() const  { return voiceCtx.rootOffset.load(); }
    void autoRegion();        // sugiere una sección (salta silencios)

    // ---- Recrear (resíntesis): el sinte reconstruye el sonido del sample ----
    enum class RecreateResult { Failed, Resynthesized, Imitating };
    RecreateResult recreateFromSample();
    SoundProfile lastProfile;                 // último análisis del sample (para la receta)
    juce::String lastProfileSample;           // nombre del audio analizado
    enum class RecipeOrigin { None, Recreated, Imitated, WaveOnly };
    RecipeOrigin recipeOrigin = RecipeOrigin::None;
    juce::String recipeText();                // receta completa del sonido actual
    float similarityOfCurrentPatch();         // parecido del patch actual con la sección del sample (0..1, honesto)
    SimilarityReport measureSimilarity();     // el mismo parecido, con el detalle por aspecto
    SimilarityReport lastReport;              // último parecido medido (color, envolvente, tono)

    // Arrastrar un audio al oscilador: se vuelve la tabla "Usuario"
    bool makeWavetableFromFile (const juce::File& file, juce::String& error);

    // ---- Limpiar: versiones separadas del audio + historial ----
    Cleaner::Job cleaner;
    struct Version { juce::String label; std::shared_ptr<const SampleData> sample; };
    std::vector<Version> history;            // Original -> lo que vas rescatando
    int historyIndex = -1;
    void startCleaning();
    bool rescueCleaned (int kind);           // "Rescatar este"
    void restoreVersion (int index);         // volver a una versión del historial

    // Escuchar la selección (o, si se pasa, otro audio completo)
    void startPreview (std::shared_ptr<const SampleData> other = nullptr)
    {
        previewOther.set (std::move (other));
        previewPos = 0.0; previewing = true;
    }
    void stopPreview()   { previewing = false; }
    bool isPreviewing() const { return previewing.load(); }
    float previewProgress() const { return previewProgressValue.load(); }

    // ---- FORGE: síntesis inversa (recetas del motor Synth que no dependen del sample) ----
    bool startForge (juce::int64 seed, int count = 8);          // analiza la sección del sample y busca
    bool forgeMutate (int candidateIndex, juce::int64 seed);    // nueva generación: descendientes de un candidato
    void cancelForge()                  { forgeJob.cancel(); }
    bool isForging() const              { return forgeJob.isRunning(); }
    float forgeProgress() const         { return forgeJob.currentProgress(); }
    void collectForgeResult();                                   // al terminar (también usable en pruebas)
    std::vector<Forge::PatchCandidate> forgeCandidates() const  { const juce::ScopedLock sl (forgeLock); return forgeList; }
    Forge::ReferenceFeatures forgeReference() const             { const juce::ScopedLock sl (forgeLock); return forgeFeatures; }
    std::vector<std::vector<Forge::PatchCandidate>> forgeGenerations() const { const juce::ScopedLock sl (forgeLock); return forgeHistory; }
    void auditionForgeCandidate (int index);                    // escuchar sin cambiar el sonido
    void auditionForgeReference();                              // la referencia, afinada a su tecla
    void applySynthPatch (const Forge::SynthPatch&);            // el patch pasa a ser el sonido (motor Synth)
    Forge::SynthPatch currentSynthPatch() const;                // la receta que suena ahora
    std::atomic<int> forgeRuns { 0 };

    void applyImitationResult();         // se llama solo al terminar (también usable en pruebas)
    std::atomic<float> lastSimilarity { -1.0f };
    std::atomic<int> imitationCount { 0 };

    // ---- Osciloscopio (últimas muestras de salida, para la interfaz) ----
    static constexpr int scopeSize = 2048;
    void copyScope (float* dest, int n) const;
    std::atomic<float> outputLevel { 0.0f };
    int activeVoices() const { return synth.activeVoiceCount(); }

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void setBaseModel (std::shared_ptr<const ResynthModel> m);
    std::shared_ptr<const ResynthModel> baseModel;
    Dna::Structural appliedDna;

    static constexpr int numVoices = 16;

    ParamPointers params;
    VoiceContext voiceCtx;
    NebulaSynth synth;
    Imitator imitator { *this, tables };
    juce::SmoothedValue<float> masterGain;

    double currentSampleRate = 44100.0, freeRunTime = 0.0;
    // En modo reproducible, reset() deja el procesador como recién preparado: así una misma secuencia
    // MIDI desde un reset da exactamente el mismo audio. En legacy no hace nada (como antes).
    void restartRandomSequence();
    juce::String sampleEmbedded64;

    std::atomic<bool> previewing { false };
    FxChain fx;
    std::unique_ptr<juce::dsp::Oversampling<float>> os2, os4;
    int currentFactor = 1, maxBlock = 512;
    float dcX[2] {}, dcY[2] {};
    Forge::Job forgeJob;
    juce::CriticalSection forgeLock;
    std::vector<Forge::PatchCandidate> forgeList;
    std::vector<std::vector<Forge::PatchCandidate>> forgeHistory;   // generaciones anteriores
    Forge::ReferenceFeatures forgeFeatures;
    juce::AudioBuffer<float> forgeSection (bool& ok) const;
    std::atomic<bool> forgePending { false };
public:
    std::atomic<int> nonFiniteBlocks { 0 };   // bloques silenciados por valores no finitos (para pruebas y diagnóstico)
private:
    juce::MidiBuffer scaledMidi;
    Genome reference;
    juce::String resynthModel64;
    SampleSource previewOther;
    static std::shared_ptr<const SampleData> withDeepAnalysis (std::shared_ptr<const SampleData> s);
    void applyResynthesis (const SoundProfile&, ResynthData&);
    std::atomic<float> previewProgressValue { 0.0f };
    double previewPos = 0.0;

    std::array<float, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaAudioProcessor)
};
