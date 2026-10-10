#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

NebulaAudioProcessor::NebulaAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "NebulaState", createParameterLayout()),
      garden (state),
      params (state),
      voiceCtx (params, sampleSource, tables),
      synth (voiceCtx)
{
    synth.addSound (new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (voiceCtx));

    state.state.setProperty ("preset", 0, nullptr);
    imitator.onFinished = [this] { triggerAsyncUpdate(); };
}

//==============================================================================
// Sample

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
    state.state.setProperty ("sampleName", s != nullptr ? s->name : juce::String(), nullptr);
    sampleEmbedded64 = (s != nullptr && s->embedded.getSize() > 0) ? s->embedded.toBase64Encoding() : juce::String();
    sampleSource.set (std::move (s));
    setRegion (0.0f, 1.0f);
    setRootOffset (0.0f);
    autoRegion();
}

void NebulaAudioProcessor::setRegion (float start, float end)
{
    start = juce::jlimit (0.0f, 0.995f, start);
    end = juce::jlimit (start + 0.005f, 1.0f, end);
    voiceCtx.regionStart = start;
    voiceCtx.regionEnd = end;
    state.state.setProperty ("regionStart", start, nullptr);
    state.state.setProperty ("regionEnd", end, nullptr);
}

void NebulaAudioProcessor::setRootOffset (float semitones)
{
    voiceCtx.rootOffset = juce::jlimit (-24.0f, 24.0f, semitones);
    state.state.setProperty ("rootOffset", voiceCtx.rootOffset.load(), nullptr);
}

void NebulaAudioProcessor::autoRegion()
{
    auto s = sampleSource.get();
    if (s == nullptr) return;
    const float* x = s->data();
    const int n = s->length();
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (x[i]));
    if (peak <= 0.0f) return;

    // Salta el silencio del principio y del final; en audios largos se queda con 4 s
    int a = 0;
    while (a < n - 1 && std::abs (x[a]) < peak * 0.02f) ++a;
    int b = n - 1;
    while (b > a && std::abs (x[b]) < peak * 0.005f) --b;
    a = juce::jmax (0, a - (int) (s->sampleRate * 0.005));
    b = juce::jmin (n - 1, juce::jmin (b + (int) (s->sampleRate * 0.02), a + (int) (s->sampleRate * 4.0)));
    setRegion ((float) a / (float) n, (float) b / (float) n);
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
        { ParamID::resonance, 0.0f }, { ParamID::filterEnv, 0.0f }, { ParamID::drive, 0.0f }, { ParamID::f2On, 0.0f },
        { ParamID::ampA, 0.002f }, { ParamID::ampD, 1.0f }, { ParamID::ampS, 1.0f }, { ParamID::ampR, 0.25f },
        { ParamID::lfoFilter, 0.0f }, { ParamID::lfoPitch, 0.0f }, { ParamID::subLevel, 0.0f }, { ParamID::noise, 0.0f },
        { ParamID::smpStart, 0.0f }, { ParamID::smpTune, 0.0f }, { ParamID::smpReverse, 0.0f },
        { ParamID::smpLoop, 0.0f }, { ParamID::smpTexture, 0.0f }, { ParamID::smpGrain, 0.08f } };
    for (auto& [id, v] : faithful)
        ops.setReal (g, id, v);
    for (int s = 0; s < ModSrc::count; ++s)
        for (int d = 0; d < ModDst::count; ++d)
            ops.setReal (g, matrixID (s, d), 0.0f);
    garden.plantGenome (g);
}

void NebulaAudioProcessor::startImitation()
{
    if (auto s = sampleSource.get())
        imitator.start (s, garden.capture(), regionStart(), regionEnd(), rootOffset());
}

bool NebulaAudioProcessor::makeWavetableFromSample (int frames, float smoothing)
{
    auto s = sampleSource.get();
    if (s == nullptr) return false;
    const int n = s->length();
    const int a = juce::jlimit (0, n - 2, (int) (regionStart() * (float) n));
    const int b = juce::jlimit (a + 2, n, (int) (regionEnd() * (float) n));
    auto t = WavetableFactory::fromAudio (s->data() + a, b - a, s->sampleRate, frames, smoothing, s->name);
    if (t == nullptr) return false;
    tables.setUser (t);
    tables.hasCustomUser = true;

    // El oscilador 1 pasa a Wavetable con la tabla del usuario
    auto set = [this] (const char* id, float v) { if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (ParamID::engine, 0.0f);
    set (ParamID::osc1Wave, (float) Wave::Table);
    set (ParamID::osc1Table, (float) WavetableBank::userIndex);
    garden.germinate();
    return true;
}

void NebulaAudioProcessor::setUserTableFromDrawing (const std::vector<float>& points)
{
    if (points.size() < 8) return;
    tables.setUser (WavetableFactory::fromDrawing (points));
    tables.hasCustomUser = true;
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

//==============================================================================
void NebulaAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
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
    const int n = buffer.getNumSamples();

    // Reloj para los LFO sincronizados
    voiceCtx.blockTime = freeRunTime;
    bool gotTempo = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) { voiceCtx.bpm = *bpm; gotTempo = true; }
            if (auto ppq = pos->getPpqPosition()) voiceCtx.blockBeats = *ppq;
            else voiceCtx.blockBeats = freeRunTime * voiceCtx.bpm / 60.0;
        }
    if (! gotTempo) voiceCtx.blockBeats = freeRunTime * voiceCtx.bpm / 60.0;
    freeRunTime += n / currentSampleRate;

    // Mezcla las notas del teclado en pantalla con el MIDI que llega de Ableton
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    synth.renderNextBlock (buffer, midi, 0, n);

    // Escuchar la selección del sample
    if (previewing.load())
    {
        if (auto s = sampleSource.get())
        {
            const double a = regionStart() * (s->length() - 1), b = regionEnd() * (s->length() - 1);
            const double step = s->sampleRate / currentSampleRate;
            for (int i = 0; i < n && previewing.load(); ++i)
            {
                const double idx = a + previewPos;
                if (idx >= b) { previewing = false; break; }
                const int i0 = (int) idx;
                const float t = (float) (idx - i0);
                const float v = 0.5f * (s->data()[i0] + (s->data()[juce::jmin (s->length() - 1, i0 + 1)] - s->data()[i0]) * t);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.addSample (ch, i, v);
                previewPos += step;
            }
            previewProgressValue = (float) (previewPos / juce::jmax (1.0, b - a));
        }
        else previewing = false;
    }

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamID::master)->load(), -60.0f));

    const int numCh = buffer.getNumChannels();
    float peak = 0.0f;
    int w = scopeWrite.load();
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = juce::jlimit (-1.5f, 1.5f, d[i] * g);   // protección ante picos extremos
            mono += d[i];
        }
        mono /= (float) juce::jmax (1, numCh);
        scope[(size_t) w] = mono;
        w = (w + 1) & (scopeSize - 1);
        peak = juce::jmax (peak, std::abs (mono));
    }
    scopeWrite = w;
    outputLevel = juce::jmax (peak, outputLevel.load() * 0.9f);
}

void NebulaAudioProcessor::copyScope (float* dest, int count) const
{
    const int w = scopeWrite.load();
    for (int i = 0; i < count; ++i)
        dest[i] = scope[(size_t) ((w - count + i + scopeSize * 2) & (scopeSize - 1))];
}

//==============================================================================
// Presets

void NebulaAudioProcessor::loadPreset (int index)
{
    applyPreset (state, index);
    state.state.setProperty ("preset", index, nullptr);
}

int NebulaAudioProcessor::getPresetIndex() const
{
    return (int) state.state.getProperty ("preset", 0);
}

juce::File NebulaAudioProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Nebula").getChildFile ("Presets");
}

juce::StringArray NebulaAudioProcessor::userPresetNames() const
{
    juce::StringArray names;
    for (auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.nebula"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool NebulaAudioProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    juce::XmlElement xml ("NebulaPreset");
    xml.setAttribute ("version", 2);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ParamID::master)
            {
                auto* e = xml.createNewChildElement ("P");
                e->setAttribute ("id", rp->getParameterID());
                e->setAttribute ("v", rp->convertFrom0to1 (rp->getValue()));
            }
    if (tables.hasCustomUser)
        if (auto t = tables.get (WavetableBank::userIndex))
            xml.setAttribute ("userTable", t->toBase64());

    auto folder = userPresetFolder();
    folder.createDirectory();
    return xml.writeTo (folder.getChildFile (name + ".nebula"));
}

bool NebulaAudioProcessor::loadUserPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (userPresetFolder().getChildFile (name + ".nebula"));
    if (xml == nullptr || ! xml->hasTagName ("NebulaPreset")) return false;
    for (auto* e : xml->getChildWithTagNameIterator ("P"))
        if (auto* rp = state.getParameter (e->getStringAttribute ("id")))
            if (rp->getParameterID() != ParamID::master)
                rp->setValueNotifyingHost (rp->convertTo0to1 ((float) e->getDoubleAttribute ("v")));
    if (xml->hasAttribute ("userTable"))
        if (auto t = Wavetable::fromBase64 (xml->getStringAttribute ("userTable"), name))
        {
            tables.setUser (t);
            tables.hasCustomUser = true;
        }
    state.state.setProperty ("preset", -1, nullptr);
    return true;
}

//==============================================================================
// Estado (lo guarda Ableton con el proyecto)

void NebulaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    garden.saveTo (copy);
    copy.setProperty ("sampleEmbedded", sampleEmbedded64, nullptr);
    if (tables.hasCustomUser)
        if (auto t = tables.get (WavetableBank::userIndex))
            copy.setProperty ("userTable", t->toBase64(), nullptr);
    copy.setProperty ("version", 2, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void NebulaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto embedded = tree["sampleEmbedded"].toString();
    const auto userTable = tree["userTable"].toString();
    tree.removeProperty ("sampleEmbedded", nullptr);
    tree.removeProperty ("userTable", nullptr);
    state.replaceState (tree);
    garden.loadFrom (state.state);

    // Tabla de onda del usuario
    if (userTable.isNotEmpty())
        if (auto t = Wavetable::fromBase64 (userTable, "Usuario"))
        {
            tables.setUser (t);
            tables.hasCustomUser = true;
        }

    // Sample: primero el archivo original; si ya no existe, la copia guardada en el proyecto
    std::shared_ptr<const SampleData> s;
    const auto path = state.state["samplePath"].toString();
    if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
    {
        juce::String error;
        s = SampleTools::load (juce::File (path), error);
    }
    if (s == nullptr && embedded.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (embedded))
            s = SampleTools::decodeFlac (mb, state.state["sampleName"].toString(), juce::File());
    }
    if (s != nullptr)
    {
        sampleEmbedded64 = s->embedded.getSize() > 0 ? s->embedded.toBase64Encoding() : embedded;
        sampleSource.set (s);
    }

    const float rs = (float) state.state.getProperty ("regionStart", 0.0f);
    const float re = (float) state.state.getProperty ("regionEnd", 1.0f);
    voiceCtx.regionStart = rs;
    voiceCtx.regionEnd = juce::jmax (rs + 0.005f, re);
    voiceCtx.rootOffset = (float) state.state.getProperty ("rootOffset", 0.0f);
}

juce::AudioProcessorEditor* NebulaAudioProcessor::createEditor()
{
    return new NebulaAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NebulaAudioProcessor();
}
