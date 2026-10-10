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

// Copia del sample con el análisis profundo de tono (nota con cents o tonalidad de la frase)
std::shared_ptr<const SampleData> NebulaAudioProcessor::withDeepAnalysis (std::shared_ptr<const SampleData> s)
{
    if (s == nullptr || s->length() < 64) return s;
    auto copy = std::make_shared<SampleData> (*s);
    const auto prof = SoundProfiler::analyse (*copy, 0.0f, 1.0f, 0.0f, nullptr, 8);
    if (prof.valid && prof.pitched)
    {
        copy->rootNote = prof.note;
        copy->hasPitch = true;
        copy->melodic = prof.melodic;
        copy->keyTonic = prof.keyTonic;
        copy->keyMinor = prof.keyMinor;
    }
    return copy;
}

void NebulaAudioProcessor::setSample (std::shared_ptr<const SampleData> s, bool keepHistory)
{
    s = withDeepAnalysis (std::move (s));
    const bool hasFile = s != nullptr && s->file.existsAsFile();
    state.state.setProperty ("samplePath", hasFile ? s->file.getFullPathName() : juce::String(), nullptr);
    state.state.setProperty ("sampleName", s != nullptr ? s->name : juce::String(), nullptr);
    sampleEmbedded64 = (s != nullptr && s->embedded.getSize() > 0) ? s->embedded.toBase64Encoding() : juce::String();
    if (! keepHistory)
    {
        cleaner.cancel();
        history.clear();
        if (s != nullptr) history.push_back ({ "Original", s });
        historyIndex = (int) history.size() - 1;
    }
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
    {
        // Primero se analiza: el análisis da un patch "clásico" de partida y la receta
        lastProfile = SoundProfiler::analyse (*s, regionStart(), regionEnd(), rootOffset());
        lastProfileSample = s->name;
        recipeOrigin = RecipeOrigin::Imitated;
        std::vector<std::pair<juce::String, float>> seeds;
        if (lastProfile.valid && lastProfile.pitched) seeds = Recipe::classicGuess (lastProfile);
        imitator.start (s, garden.capture(), regionStart(), regionEnd(), rootOffset(), seeds);
    }
}

//==============================================================================
// Recrear: resíntesis del sample con los osciladores de Nebula
NebulaAudioProcessor::RecreateResult NebulaAudioProcessor::recreateFromSample()
{
    auto s = sampleSource.get();
    if (s == nullptr) return RecreateResult::Failed;
    imitator.cancel();

    ResynthData rd;
    const auto prof = SoundProfiler::analyse (*s, regionStart(), regionEnd(), rootOffset(), &rd, 64);
    if (! prof.valid) return RecreateResult::Failed;

    // Sin nota clara o inarmónico (campana, metal): la tabla no lo captura bien → búsqueda con osciladores
    if (! prof.pitched || prof.inharmonic || rd.cycles.size() < 2)
    {
        startImitation();
        lastProfile = prof;
        recipeOrigin = RecipeOrigin::Imitated;
        return RecreateResult::Imitating;
    }

    applyResynthesis (prof, rd);
    lastProfile = prof;
    lastProfileSample = s->name;
    recipeOrigin = RecipeOrigin::Recreated;
    lastProfile.similarity = similarityOfCurrentPatch();
    lastSimilarity = lastProfile.similarity;
    ++imitationCount;
    return RecreateResult::Resynthesized;
}

void NebulaAudioProcessor::applyResynthesis (const SoundProfile& prof, ResynthData& rd)
{
    auto s = sampleSource.get();
    auto table = Wavetable::fromCycles (rd.cycles, s != nullptr ? s->name : juce::String ("Recreado"));
    tables.setUser (table);
    tables.hasCustomUser = true;

    auto set = [this] (const juce::String& id, float v) { if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };

    // Todo a su valor de fábrica (menos lo de interpretación y lo del sample)
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (GenomeOps::isPerformanceParam (id) || id.startsWith ("smp")) continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }

    // Oscilador 1 = la tabla del sonido; la Env 3 la recorre como pasó en el original
    set (ParamID::engine, 0.0f);
    set (ParamID::osc1Wave, (float) Wave::Table);
    set (ParamID::osc1Table, (float) WavetableBank::userIndex);
    set (ParamID::osc1Shape, 0.0f);
    set (ParamID::osc1Octave, 0.0f);
    set (ParamID::oscMix, 0.0f);
    set (ParamID::subLevel, 0.0f);
    set (ParamID::fmAmount, 0.0f);
    set (ParamID::noise, 0.0f);
    set (ParamID::noiseColor, rd.noiseColor);
    set (ParamID::uniVoices, 1.0f);
    set (ParamID::uniDetune, 0.0f);

    // Filtros abiertos y limpios: no colorean
    set (ParamID::filterType, (float) FilterKind::LowPass);
    set (ParamID::filterSlope, 0.0f);
    set (ParamID::filterChar, 0.0f);
    set (ParamID::cutoff, 20000.0f);
    set (ParamID::resonance, 0.0f);
    set (ParamID::filterEnv, 0.0f);
    set (ParamID::drive, 0.0f);
    set (ParamID::f2On, 0.0f);
    set (ParamID::lfoFilter, 0.0f);

    // Volumen: la tabla ya trae la forma del volumen; la Env 1 sólo abre y suelta
    set (ParamID::ampA, 0.002f);
    set (ParamID::ampD, 0.1f);
    set (ParamID::ampS, 1.0f);
    set (ParamID::ampR, prof.releaseS);
    set (ParamID::ampCurve, 0.0f);

    // Env 3 recorre la tabla
    set (ParamID::env3A, prof.scanS);
    set (ParamID::env3D, 0.1f);
    set (ParamID::env3S, 1.0f);
    set (ParamID::env3R, 8.0f);
    set (ParamID::env3Curve, rd.scanCurve);
    set (ParamID::env3Mode, 0.0f);
    for (int src = 0; src < ModSrc::count; ++src)
        for (int d = 0; d < ModDst::count; ++d)
            set (matrixID (src, d), 0.0f);
    set (matrixID (ModSrc::Env3, ModDst::Shape1), 1.0f);

    // Ruido con su propia envolvente (Env 2)
    if (rd.noiseAmount > 0.001f)
    {
        set (ParamID::fltA, rd.nA);
        set (ParamID::fltD, rd.nD);
        set (ParamID::fltS, rd.nS);
        set (ParamID::fltR, rd.nR);
        set (ParamID::fltCurve, 0.0f);
        set (ParamID::fltMode, 0.0f);
        set (matrixID (ModSrc::Env2, ModDst::Noise), rd.noiseAmount);
    }

    // Vibrato
    if (prof.vibratoHz > 0.0f)
    {
        set (ParamID::lfoOn, 1.0f);
        set (ParamID::lfoShape, (float) LfoShape::Sine);
        set (ParamID::lfoSync, 0.0f);
        set (ParamID::lfoRate, prof.vibratoHz);
        set (ParamID::lfoPitch, prof.vibratoCents);
        set (ParamID::lfoRetrig, 0.0f);
        set (ParamID::lfoFade, 0.15f);
    }
    set (ParamID::modTime, 1.0f);
    set (ParamID::modAmount, 1.0f);

    state.state.setProperty ("preset", -1, nullptr);
    garden.germinate();   // el sonido recreado es la nueva semilla
}

float NebulaAudioProcessor::similarityOfCurrentPatch()
{
    auto s = sampleSource.get();
    if (s == nullptr) return -1.0f;
    GenomeOps ops (*this, 1);
    OfflineSynth offline (getParameters(), ops.parameters(), tables);
    Genome g;
    for (auto* p : ops.parameters()) g.genes.push_back (p->getValue());
    return ImitationTools::similarity (offline, g, *s, regionStart(), regionEnd(), rootOffset());
}

juce::String NebulaAudioProcessor::recipeText()
{
    juce::String t;
    t << "# Receta del sonido\n";
    if (recipeOrigin != RecipeOrigin::None && lastProfile.valid)
    {
        if (recipeOrigin == RecipeOrigin::Recreated)
            t << "Nebula analizo \"" << lastProfileSample << "\" y lo reconstruyo con sus propios osciladores (resintesis)"
              << (lastProfile.similarity >= 0.0f ? ": parecido " + juce::String (juce::roundToInt (lastProfile.similarity * 100.0f)) + " %." : juce::String (".")) << "\n";
        else if (recipeOrigin == RecipeOrigin::Imitated)
            t << "Nebula analizo \"" << lastProfileSample << "\" y busco el patch de osciladores clasicos que mas se parece"
              << (lastSimilarity.load() >= 0.0f ? ": parecido " + juce::String (juce::roundToInt (lastSimilarity.load() * 100.0f)) + " %." : juce::String (".")) << "\n";
        t << "\n" << Recipe::describeProfile (lastProfile, lastProfileSample) << "\n";
        if (recipeOrigin == RecipeOrigin::Recreated)
        {
            t << "## Como lo recreo Nebula\n";
            t << "1. Corto el sonido en 64 momentos y en cada uno midio sus armonicos (hasta " << juce::jmin (1023, (int) (18000.0f / lastProfile.f0))
              << "). Cada momento se volvio un cuadro de la tabla de ondas \"Usuario\": es sintesis aditiva guardada como wavetable.\n";
            t << "2. Como cada cuadro es un ciclo exacto, la tabla suena en la nota que toques, sin la desafinacion del original.\n";
            t << "3. La Env 3 recorre la tabla en " << Recipe::sec (lastProfile.scanS) << " (matriz Env 3 -> Forma 1 = 100 %), asi el timbre y el volumen cambian igual que en el original.\n";
            if (lastProfile.noiseRatio > 0.03f && ! lastProfile.inharmonic)
                t << "4. La parte de ruido (lo que no es nota) va aparte: ruido " << Recipe::brightnessWord (lastProfile.noiseBrightnessHz)
                  << " con su propia envolvente (matriz Env 2 -> Ruido).\n";
            if (lastProfile.vibratoHz > 0.0f) t << "5. El vibrato se separo y lo hace el LFO 1.\n";
            t << "\n";
        }
        t << Recipe::describeClassic (lastProfile) << "\n";
    }
    else
        t << "Este es el sonido que esta sonando ahora. Si cargas un audio y usas Recrear, aqui veras tambien el analisis del original.\n\n";

    auto v = [this] (const juce::String& id) { auto* p = state.getRawParameterValue (id); return p != nullptr ? p->load() : 0.0f; };
    auto user = tables.get (WavetableBank::userIndex);
    t << Recipe::describePatch (v, tables.hasCustomUser, user != nullptr ? user->name : juce::String());
    return t;
}

bool NebulaAudioProcessor::makeWavetableFromFile (const juce::File& file, juce::String& error)
{
    auto s = SampleTools::load (file, error);
    if (s == nullptr) return false;
    s = withDeepAnalysis (s);
    ResynthData rd;
    std::shared_ptr<Wavetable> t;
    const auto prof = SoundProfiler::analyse (*s, 0.0f, 1.0f, 0.0f, &rd, 32);
    if (prof.valid && prof.pitched && ! prof.inharmonic && rd.cycles.size() >= 2)
        t = Wavetable::fromCycles (rd.cycles, s->name);
    else
    {
        // Sin nota clara: se recorta por ventanas, como "Hacer onda"
        const int n = juce::jmin (s->length(), (int) (s->sampleRate * 4.0));
        t = WavetableFactory::fromAudio (s->data(), n, s->sampleRate, 32, 0.3f, s->name);
    }
    if (t == nullptr) { error = "No pude sacar una onda de ese audio."; return false; }
    tables.setUser (t);
    tables.hasCustomUser = true;
    auto set = [this] (const char* id, float v) { if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (ParamID::engine, 0.0f);
    set (ParamID::osc1Wave, (float) Wave::Table);
    set (ParamID::osc1Table, (float) WavetableBank::userIndex);
    recipeOrigin = RecipeOrigin::WaveOnly;
    return true;
}

//==============================================================================
// Limpiar
void NebulaAudioProcessor::startCleaning()
{
    if (auto s = sampleSource.get())
        cleaner.start (s);
}

bool NebulaAudioProcessor::rescueCleaned (int kind)
{
    auto r = cleaner.result (kind);
    if (r == nullptr) return false;
    // Lo que había después de la versión actual se descarta (como "deshacer" y luego hacer otra cosa)
    if (historyIndex >= 0 && historyIndex + 1 < (int) history.size())
        history.erase (history.begin() + historyIndex + 1, history.end());
    history.push_back ({ Cleaner::names()[kind], r });
    historyIndex = (int) history.size() - 1;
    setSample (r, true);
    cleaner.start (sampleSource.get());   // nuevas opciones a partir de lo rescatado
    return true;
}

void NebulaAudioProcessor::restoreVersion (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) history.size())) return;
    historyIndex = index;
    setSample (history[(size_t) index].sample, true);
    cleaner.start (sampleSource.get());
}

bool NebulaAudioProcessor::makeWavetableFromSample (int frames, float smoothing)
{
    auto s = sampleSource.get();
    if (s == nullptr) return false;
    const int n = s->length();
    const int a = juce::jlimit (0, n - 2, (int) (regionStart() * (float) n));
    const int b = juce::jlimit (a + 2, n, (int) (regionEnd() * (float) n));
    std::shared_ptr<Wavetable> t;
    {
        // Con nota clara: cuadros por análisis armónico (más limpios); si no, por ventanas
        ResynthData rd;
        const auto prof = SoundProfiler::analyse (*s, regionStart(), regionEnd(), rootOffset(), &rd, frames);
        if (prof.valid && prof.pitched && ! prof.inharmonic && rd.cycles.size() >= 2)
        {
            if (smoothing > 0.0f && rd.cycles.size() > 2)
            {
                auto copy = rd.cycles;
                for (size_t f = 1; f + 1 < rd.cycles.size(); ++f)
                    for (size_t i = 0; i < rd.cycles[f].size(); ++i)
                        rd.cycles[f][i] = copy[f][i] * (1.0f - smoothing) + 0.5f * smoothing * (copy[f - 1][i] + copy[f + 1][i]);
            }
            t = Wavetable::fromCycles (rd.cycles, s->name);
        }
        else
            t = WavetableFactory::fromAudio (s->data() + a, b - a, s->sampleRate, frames, smoothing, s->name);
    }
    if (t == nullptr) return false;
    tables.setUser (t);
    tables.hasCustomUser = true;

    // El oscilador 1 pasa a Wavetable con la tabla del usuario
    auto set = [this] (const char* id, float v) { if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (ParamID::engine, 0.0f);
    set (ParamID::osc1Wave, (float) Wave::Table);
    set (ParamID::osc1Table, (float) WavetableBank::userIndex);
    recipeOrigin = RecipeOrigin::WaveOnly;
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
        auto other = previewOther.get();
        if (auto s = other != nullptr ? other : sampleSource.get())
        {
            const double a = other != nullptr ? 0.0 : regionStart() * (s->length() - 1);
            const double b = other != nullptr ? (double) (s->length() - 1) : regionEnd() * (s->length() - 1);
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
        s = withDeepAnalysis (s);
        sampleEmbedded64 = s->embedded.getSize() > 0 ? s->embedded.toBase64Encoding() : embedded;
        sampleSource.set (s);
        history.clear();
        history.push_back ({ "Original", s });
        historyIndex = 0;
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
