#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

NebulaAudioProcessor::NebulaAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "NebulaState", createParameterLayout()),
      garden (state),
      params (state)
{
    synth.addSound (new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (params, sampleSource));

    state.state.setProperty ("preset", 0, nullptr);
    imitator.onFinished = [this] { triggerAsyncUpdate(); };
}

//==============================================================================
juce::String NebulaAudioProcessor::loadSample (const juce::File& file)
{
    juce::String error;
    auto s = SampleTools::load (file, error);
    if (s == nullptr)
        return error;
    setSample (s);
    return {};
}

void NebulaAudioProcessor::setSample (std::shared_ptr<const SampleData> s)
{
    state.state.setProperty ("samplePath", s != nullptr ? s->file.getFullPathName() : juce::String(), nullptr);
    sampleSource.set (std::move (s));
}

void NebulaAudioProcessor::useSampleAsInstrument()
{
    if (sampleSource.get() == nullptr) return;
    imitator.cancel();

    // Semilla = el sample tal cual (filtro abierto, sin efectos)
    const auto& ops = garden.operations();
    Genome g = garden.capture();
    const std::pair<const char*, float> faithful[] = {
        { ParamID::engine, 1.0f }, { ParamID::filterType, 0.0f }, { ParamID::cutoff, 20000.0f },
        { ParamID::resonance, 0.0f }, { ParamID::filterEnv, 0.0f }, { ParamID::drive, 0.0f },
        { ParamID::ampA, 0.002f }, { ParamID::ampD, 1.0f }, { ParamID::ampS, 1.0f }, { ParamID::ampR, 0.25f },
        { ParamID::lfoFilter, 0.0f }, { ParamID::lfoPitch, 0.0f }, { ParamID::subLevel, 0.0f }, { ParamID::noise, 0.0f },
        { ParamID::smpStart, 0.0f }, { ParamID::smpTune, 0.0f }, { ParamID::smpReverse, 0.0f },
        { ParamID::smpLoop, 0.0f }, { ParamID::smpTexture, 0.0f }, { ParamID::smpGrain, 0.08f } };
    for (auto& [id, v] : faithful)
        ops.setReal (g, id, v);
    garden.plantGenome (g);
}

void NebulaAudioProcessor::startImitation()
{
    if (auto s = sampleSource.get())
        imitator.start (s, garden.capture());
}

void NebulaAudioProcessor::handleAsyncUpdate() { applyImitationResult(); }

void NebulaAudioProcessor::applyImitationResult()
{
    if (! imitator.finished.load()) return;
    imitator.finished = false;
    garden.plantGenome (imitator.result());
    lastSimilarity = imitator.similarity.load();
    ++imitationCount;
}

void NebulaAudioProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate);

    masterGain.reset (sampleRate, 0.03);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamID::master)->load(), -60.0f));
    keyboardState.reset();
}

bool NebulaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NebulaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Mezcla las notas del teclado en pantalla con el MIDI que llega de Ableton
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamID::master)->load(), -60.0f));

    const int numCh = buffer.getNumChannels();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float g = masterGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = juce::jlimit (-1.5f, 1.5f, d[i] * g); // protección ante picos extremos
        }
    }
}

void NebulaAudioProcessor::loadPreset (int index)
{
    applyPreset (state, index);
    state.state.setProperty ("preset", index, nullptr);
}

int NebulaAudioProcessor::getPresetIndex() const
{
    return (int) state.state.getProperty ("preset", 0);
}

void NebulaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    garden.saveTo (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void NebulaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            garden.loadFrom (state.state);

            // Vuelve a cargar el sample del proyecto (si el archivo sigue ahí)
            const auto path = state.state["samplePath"].toString();
            if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
            {
                juce::String error;
                if (auto s = SampleTools::load (juce::File (path), error))
                    sampleSource.set (s);
            }
        }
}

juce::AudioProcessorEditor* NebulaAudioProcessor::createEditor()
{
    return new NebulaAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NebulaAudioProcessor();
}
