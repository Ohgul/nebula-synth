#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "SynthVoice.h"
#include "Garden.h"
#include "SampleData.h"
#include "Imitator.h"

class NebulaAudioProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    NebulaAudioProcessor();
    ~NebulaAudioProcessor() override { imitator.stop(); cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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

    void loadPreset (int index);
    int getPresetIndex() const;

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState; // teclado en pantalla
    Garden garden;                         // modo Jardin (variaciones)

    static constexpr int auditionNote = 60;

    // ---- Sample ----
    SampleSource sampleSource;
    juce::String loadSample (const juce::File& file);          // devuelve "" o un mensaje de error
    void setSample (std::shared_ptr<const SampleData> s);
    void useSampleAsInstrument();                                // modo "Usar el sample"
    void startImitation();                                       // modo "Imitar con el sinte"
    void cancelImitation()            { imitator.cancel(); }
    bool isImitating() const          { return imitator.isRunning(); }
    float imitationProgress() const   { return imitator.progress.load(); }
    float imitationSimilarity() const { return imitator.similarity.load(); }

    void applyImitationResult();         // se llama solo al terminar (también usable en pruebas)
    std::atomic<float> lastSimilarity { -1.0f };  // % de parecido de la última imitación (0..1)
    std::atomic<int> imitationCount { 0 };

private:
    void handleAsyncUpdate() override;   // aplica el resultado de la imitación

    static constexpr int numVoices = 12;
    Imitator imitator { *this };

    ParamPointers params;
    juce::Synthesiser synth;
    juce::SmoothedValue<float> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaAudioProcessor)
};
