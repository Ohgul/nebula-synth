#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

NebulaAudioProcessor::NebulaAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, &undoManager, "NebulaState", createParameterLayout()),
      garden (state),
      params (state),
      voiceCtx (params, sampleSource, tables),
      synth (voiceCtx)
{
    synth.addSound (new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (voiceCtx));

    startTimerHz (20);   // ADN: aplica los genes estructurales cuando cambian

    // Cada instancia nueva tiene su propia semilla (dos Nebula iguales no suenan clonadas); se guarda en el proyecto
    voiceCtx.renderSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt();

    state.state.setProperty ("preset", 0, nullptr);
    imitator.onFinished = [this] { triggerAsyncUpdate(); };
    forgeJob.onFinished = [this] { forgePending = true; triggerAsyncUpdate(); };
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

    // Salta el silencio del principio y del final. Antes se quedaba con 4 s: los pads largos perdían todo lo
    // que pasaba después (la resíntesis adaptativa ya analiza el sonido completo). Límite: 60 s.
    int a = 0;
    while (a < n - 1 && std::abs (x[a]) < peak * 0.02f) ++a;
    int b = n - 1;
    while (b > a && std::abs (x[b]) < peak * 0.005f) --b;
    a = juce::jmax (0, a - (int) (s->sampleRate * 0.005));
    b = juce::jmin (n - 1, juce::jmin (b + (int) (s->sampleRate * 0.02), a + (int) (s->sampleRate * 60.0)));
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

    SoundProfile prof;
    auto model = ResynthBuilder::build (*s, regionStart(), regionEnd(), rootOffset(), prof);
    if (model == nullptr) return RecreateResult::Failed;

    setBaseModel (model);
    resynthModel64 = model->toBase64();

    // Mismo volumen que el original para el A/B
    {
        const int n = s->length();
        const int a = (int) (regionStart() * (float) n), b = juce::jmax (a + 1, (int) (regionEnd() * (float) n));
        float pk = 1.0e-6f;
        for (int i = a; i < b; ++i) pk = juce::jmax (pk, std::abs (s->data()[i]));
        voiceCtx.abGain = 1.0f / pk;
    }

    auto set = [this] (const juce::String& id, float v) { if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (GenomeOps::isPerformanceParam (id) || id.startsWith ("smp") || id.startsWith ("fx") || id.startsWith ("macro")) continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    set (ParamID::engine, (float) Engine::Resynth);
    // Un sonido recreado se toca polifónico y sin glide: si antes había un preset Mono/Legato cargado,
    // el modo se heredaba y solo sonaba una nota a la vez.
    set (ParamID::polyMode, (float) PolyMode::Poly);
    set (ParamID::glide, 0.0f);
    set (ParamID::filterType, (float) FilterKind::LowPass);
    set (ParamID::filterChar, 0.0f);
    set (ParamID::cutoff, 20000.0f);
    set (ParamID::resonance, 0.0f);
    set (ParamID::filterEnv, 0.0f);
    set (ParamID::drive, 0.0f);
    set (ParamID::f2On, 0.0f);
    set (ParamID::lfoFilter, 0.0f);
    set (ParamID::lfoPitch, 0.0f);
    set (ParamID::subLevel, 0.0f);
    set (ParamID::noise, 0.0f);
    set (ParamID::ampA, 0.002f);
    set (ParamID::ampD, 0.1f);
    set (ParamID::ampS, 1.0f);
    set (ParamID::ampR, prof.releaseS);
    set (ParamID::ampCurve, 0.0f);
    set (ParamID::velSens, 0.3f);
    for (int src = 0; src < ModSrc::count; ++src)
        for (int d = 0; d < ModDst::count; ++d)
            set (matrixID (src, d), 0.0f);

    state.state.setProperty ("preset", -1, nullptr);
    lastProfile = prof;
    lastProfileSample = s->name;
    recipeOrigin = RecipeOrigin::Recreated;
    garden.germinate();   // el sonido recreado es la nueva semilla
    markReference();
    lastProfile.similarity = similarityOfCurrentPatch();
    lastSimilarity = lastProfile.similarity;
    ++imitationCount;
    return RecreateResult::Resynthesized;
}

float NebulaAudioProcessor::similarityOfCurrentPatch()
{
    lastReport = measureSimilarity();
    return lastReport.overall;
}

// Parecido honesto (ver Similarity.h): el patch actual tocado offline contra la sección del sample,
// con las mismas métricas validadas del banco de pruebas.
SimilarityReport NebulaAudioProcessor::measureSimilarity()
{
    auto s = sampleSource.get();
    if (s == nullptr) return {};
    const double sr = s->sampleRate;
    const int n = s->length();
    const int a0 = juce::jlimit (0, juce::jmax (0, n - 2), (int) (regionStart() * (float) n));
    const int a1 = juce::jlimit (a0 + 2, n, (int) (regionEnd() * (float) n));
    const float* x = s->data();
    float peak = 0.0f;
    for (int i = a0; i < a1; ++i) peak = juce::jmax (peak, std::abs (x[i]));
    if (peak <= 0.0f) return {};
    int onset = a0;
    while (onset < a1 - 1 && std::abs (x[onset]) < peak * 0.003f) ++onset;   // -50 dB, como el análisis
    onset = juce::jmax (a0, onset - (int) (0.005 * sr));
    const int len = juce::jmin (a1 - onset, (int) (8.0 * sr));               // hasta 8 s (costo acotado)
    if (len < (int) (0.05 * sr)) return {};
    juce::AudioBuffer<float> ref (1, len);
    ref.copyFrom (0, 0, x + onset, len);

    GenomeOps ops (*this, 1);
    OfflineSynth offline (getParameters(), ops.parameters(), tables);
    auto m = voiceCtx.models.get();
    offline.setModel (m);
    Genome g;
    for (auto* p : ops.parameters()) g.genes.push_back (p->getValue());
    offline.setGenome (g);

    const bool rs = m != nullptr && juce::roundToInt (state.getRawParameterValue (ParamID::engine)->load()) == Engine::Resynth;
    const float exact = rs ? m->rootNote : (s->hasPitch ? s->rootNote + rootOffset() : 48.0f);
    const int note = juce::jlimit (12, 115, juce::roundToInt (exact));
    // la nota se suelta donde el original empieza a apagarse (si se conoce) o al 85 %
    int hold = (int) (0.85 * len);
    if (rs && m->hasLoop()) hold = juce::jlimit (1, len, (int) (m->releaseStart * sr));
    else if (rs) hold = len;
    juce::AudioBuffer<float> out;
    offline.renderStereo (out, note, hold, len, sr);
    juce::AudioBuffer<float> test (1, len);
    test.copyFrom (0, 0, out, 0, 0, len);
    if (out.getNumChannels() > 1) { test.addFrom (0, 0, out, 1, 0, len); test.applyGain (0.5f); }

    // La tecla tocada no es exactamente la nota del sample (p. ej. E4 -43 ct): en vez de castigar al color por
    // esa transposición, la referencia se lleva a la misma afinación (remuestreo cúbico)
    const double ratio = std::exp2 ((note - exact) / 12.0);
    if (std::abs (ratio - 1.0) > 1.0e-4)
    {
        const int newLen = juce::jmax (2, (int) (len / ratio));
        juce::AudioBuffer<float> shifted (1, newLen);
        const float* src = ref.getReadPointer (0);
        auto at = [&] (int i) { return src[juce::jlimit (0, len - 1, i)]; };
        for (int i = 0; i < newLen; ++i)
        {
            const double pos = i * ratio;
            const int i0 = (int) pos; const float f = (float) (pos - i0);
            const float y0 = at (i0 - 1), y1 = at (i0), y2 = at (i0 + 1), y3 = at (i0 + 2);
            shifted.setSample (0, i, y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0))));
        }
        ref = std::move (shifted);
        test.setSize (1, juce::jmin (test.getNumSamples(), newLen), true);
    }

    const bool pitched = s->hasPitch && ! s->melodic;
    return Similarity::measure (ref, test, sr, pitched, 0.0, exact < 48.0f ? 25.0 : 50.0);
}

juce::File NebulaAudioProcessor::exportMidi()
{
    auto s = sampleSource.get();
    if (s == nullptr) return {};
    const auto notes = Exporter::transcribe (*s, regionStart(), regionEnd());
    if (notes.empty()) return {};
    return Exporter::writeMidi (notes, voiceCtx.bpm, s->name);
}

juce::File NebulaAudioProcessor::exportAudio()
{
    // El sonido actual tocado en C (o en la raíz del sample) con sus efectos
    const double sr = currentSampleRate > 0 ? currentSampleRate : 48000.0;
    GenomeOps ops (*this, 1);
    OfflineSynth offline (getParameters(), ops.parameters(), tables);
    offline.setModel (voiceCtx.models.get());
    Genome g;
    for (auto* p : ops.parameters()) g.genes.push_back (p->getValue());
    offline.setGenome (g);
    double len = 2.0;
    if (auto s = sampleSource.get()) len = juce::jlimit (0.5, 10.0, (regionEnd() - regionStart()) * s->length() / s->sampleRate);
    const double tail = juce::jlimit (0.2, 6.0, (double) state.getRawParameterValue (ParamID::ampR)->load() + (state.getRawParameterValue (ParamID::fxRevOn)->load() > 0.5f ? 2.0 : 0.3));
    juce::AudioBuffer<float> out;
    offline.renderStereo (out, homeKey(), (int) (len * sr), (int) ((len + tail) * sr), sr);
    FxChain fxo;
    fxo.prepare (sr, out.getNumSamples());
    fxo.process (out, params, voiceCtx.bpm);
    const float mag = out.getMagnitude (0, out.getNumSamples());
    if (mag > 0.98f) out.applyGain (0.98f / mag);
    auto s = sampleSource.get();
    return Exporter::writeWav (out, sr, (s != nullptr ? s->name : juce::String ("Nebula")) + " - Nebula.wav");
}

juce::File NebulaAudioProcessor::exportWavetable()
{
    if (auto m = voiceCtx.models.get(); m != nullptr && m->harmonic != nullptr
        && juce::roundToInt (state.getRawParameterValue (ParamID::engine)->load()) == Engine::Resynth)
        return Exporter::writeWavetable (*m->harmonic);
    if (auto t = tables.get (WavetableBank::userIndex); t != nullptr && tables.hasCustomUser)
        return Exporter::writeWavetable (*t);
    return {};
}

void NebulaAudioProcessor::markReference()
{
    reference = garden.capture();
}

float NebulaAudioProcessor::computeCraziness()
{
    const Genome now = garden.capture();
    const float dist = reference.isValid() ? juce::jmin (1.0f, GenomeOps::distance (reference, now) * 1.8f) : 0.0f;
    auto v = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
    float mod = 0.0f;
    for (int s = 0; s < ModSrc::count; ++s)
        for (int d = 0; d < ModDst::count; ++d) mod += std::abs (state.getRawParameterValue (matrixID (s, d))->load());
    const float c = 0.5f * dist + 0.55f * v (ParamID::macroChaos) + 0.12f * v (ParamID::macroMove)
                  + 0.12f * juce::jmin (1.0f, mod * 0.4f) + 0.08f * v (ParamID::fmAmount) + 0.06f * v (ParamID::drive);
    const float out = juce::jlimit (0.0f, 1.0f, c);
    craziness = craziness.load() + (out - craziness.load()) * 0.15f;
    return craziness.load();
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
            auto m = voiceCtx.models.get();
            int step = 1;
            if (m != nullptr && m->harmonic != nullptr)
                t << step++ << ". Midio sus armonicos en " << m->frames() << " momentos (hasta " << juce::jmin (1023, (int) (18000.0f / lastProfile.f0))
                  << ") y los guardo como una tabla que cambia en el tiempo: sintesis aditiva.\n";
            if (m != nullptr && ! m->partials.empty())
                t << step++ << ". Encontro " << (int) m->partials.size() << " parciales sueltos (frecuencias propias, como en una campana o un golpe) y siguio el volumen de cada uno.\n";
            if (m != nullptr && ! m->noise.empty())
                t << step++ << ". Separo el ruido (lo que no es nota) en 24 bandas, cada una con su volumen en el tiempo.\n";
            if (m != nullptr && ! m->pitchCurve.empty())
            {
                bool moves = false; for (auto v : m->pitchCurve) if (std::abs (v) > 5.0f) moves = true;
                if (moves) t << step++ << ". Copio la curva de tono del original (vibrato, glides o la melodia).\n";
            }
            t << step++ << ". Todo queda afinado a la tecla que toques: en C suena C.\n";
            if (m != nullptr && m->width > 0.05f) t << step++ << ". Conserva el ancho estereo del original (" << Recipe::pct (m->width) << ").\n";
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

void NebulaAudioProcessor::handleAsyncUpdate()
{
    applyImitationResult();
    if (forgePending.exchange (false)) collectForgeResult();
}

void NebulaAudioProcessor::applyImitationResult()
{
    if (! imitator.finished.load()) return;
    imitator.finished = false;
    garden.plantGenome (imitator.result());
    // el parecido que se muestra se mide igual que en RECREAR (el del optimizador es sólo su guía interna)
    lastSimilarity = similarityOfCurrentPatch();
    markReference();
    ++imitationCount;
}

//==============================================================================
void NebulaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlockExpected)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate);

    maxBlock = juce::jmax (16, samplesPerBlockExpected);
    os2 = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os4 = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os2->initProcessing ((size_t) maxBlock);
    os4->initProcessing ((size_t) maxBlock);
    scaledMidi.ensureSize (4096);
    currentFactor = 1;
    fx.prepare (sampleRate, maxBlock);
    masterGain.reset (sampleRate, 0.03);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamID::master)->load(), -60.0f));
    keyboardState.reset();
    restartRandomSequence();
}

void NebulaAudioProcessor::reset()
{
    restartRandomSequence();
}

void NebulaAudioProcessor::restartRandomSequence()
{
    if (! voiceCtx.seededRandom.load()) return;
    voiceCtx.noteOnCount = 0;
    voiceCtx.lastNote = -1.0f;
    freeRunTime = 0.0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i))) v->resetState();
    fx.reset();
    if (os2 != nullptr) os2->reset();
    if (os4 != nullptr) os4->reset();
    currentFactor = 1;
    synth.setCurrentPlaybackSampleRate (currentSampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i))) v->prepare (currentSampleRate);
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamID::master)->load(), -60.0f));
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

    // Calidad: el sinte corre a 2x o 4x y se filtra de vuelta (sin aliasing). Al exportar, mínimo 2x.
    int q = params.i (P::quality);
    if (isNonRealtime()) q = juce::jmax (q, 1);
    const int factor = (q == 2 ? 4 : q == 1 ? 2 : 1);
    if (n > maxBlock) { /* bloque más grande de lo esperado: sin oversampling */ }
    const int useFactor = n > maxBlock ? 1 : factor;
    if (useFactor != currentFactor)
    {
        currentFactor = useFactor;
        // El oversampling retrasa la salida unas muestras: se avisa a Ableton para que lo compense
        setLatencySamples (useFactor == 1 ? 0 : juce::roundToInt ((useFactor == 2 ? os2 : os4)->getLatencyInSamples()));
        synth.setCurrentPlaybackSampleRate (currentSampleRate * useFactor);
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i))) v->prepare (currentSampleRate * useFactor);
    }
    if (useFactor == 1)
        synth.renderNextBlock (buffer, midi, 0, n);
    else
    {
        auto& os = useFactor == 2 ? *os2 : *os4;
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = os.processSamplesUp (block);
        up.clear();
        float* chans[2] { up.getChannelPointer (0), up.getNumChannels() > 1 ? up.getChannelPointer (1) : up.getChannelPointer (0) };
        juce::AudioBuffer<float> upBuf (chans, (int) juce::jmin ((size_t) 2, up.getNumChannels()), (int) up.getNumSamples());
        scaledMidi.clear();
        for (const auto meta : midi) scaledMidi.addEvent (meta.getMessage(), meta.samplePosition * useFactor);
        synth.renderNextBlock (upBuf, scaledMidi, 0, upBuf.getNumSamples());
        os.processSamplesDown (block);
    }

    // Protección: si algo produjo un valor no finito (NaN/inf), ese bloque sale en silencio y los efectos
    // y el filtro de continua vuelven a cero (si no, el NaN quedaría circulando en la reverb o el delay)
    auto finite = [&] (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer (ch);
            float acc = 0.0f;
            for (int i = 0; i < n; ++i) acc += d[i] * 0.0f;   // NaN o inf * 0 = NaN
            if (acc != 0.0f) return false;
        }
        return true;
    };
    if (! finite (buffer)) { buffer.clear(); ++nonFiniteBlocks; }

    fx.process (buffer, params, voiceCtx.bpm);
    if (! finite (buffer)) { buffer.clear(); fx.reset(); ++nonFiniteBlocks; }

    // Quita cualquier corriente continua (DC) de la salida
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        float& x1 = dcX[ch]; float& y1 = dcY[ch];
        for (int i = 0; i < n; ++i) { const float y = d[i] - x1 + 0.9995f * y1; x1 = d[i]; y1 = y; d[i] = y; }
    }

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
    markReference();
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
    if (resynthModel64.isNotEmpty() && juce::roundToInt (state.getRawParameterValue (ParamID::engine)->load()) == Engine::Resynth)
        xml.setAttribute ("resynthModel", resynthModel64);

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
    if (xml->hasAttribute ("resynthModel"))
        if (auto m = ResynthModel::fromBase64 (xml->getStringAttribute ("resynthModel")))
        {
            setBaseModel (m);
            resynthModel64 = xml->getStringAttribute ("resynthModel");
        }
    state.state.setProperty ("preset", -1, nullptr);
    markReference();
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
    copy.setProperty ("version", 3, nullptr);
    copy.setProperty ("randomMode", voiceCtx.seededRandom.load() ? "seeded" : "legacy", nullptr);
    copy.setProperty ("renderSeed", (juce::int64) voiceCtx.renderSeed.load(), nullptr);
    if (resynthModel64.isNotEmpty()) copy.setProperty ("resynthModel", resynthModel64, nullptr);
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
    const auto model64 = tree["resynthModel"].toString();
    // Azar: los proyectos guardados antes de esta versión no tienen "randomMode" y conservan el azar libre
    const bool seeded = tree["randomMode"].toString() == "seeded";
    const auto seed = tree.hasProperty ("renderSeed") ? (juce::uint32) (juce::int64) tree["renderSeed"] : voiceCtx.renderSeed.load();
    tree.removeProperty ("randomMode", nullptr);
    tree.removeProperty ("renderSeed", nullptr);
    setRandomMode (seeded, seed);
    tree.removeProperty ("resynthModel", nullptr);
    tree.removeProperty ("sampleEmbedded", nullptr);
    tree.removeProperty ("userTable", nullptr);
    state.replaceState (tree);
    garden.loadFrom (state.state);

    // Modelo de resíntesis
    if (model64.isNotEmpty())
        if (auto m = ResynthModel::fromBase64 (model64)) { setBaseModel (m); resynthModel64 = model64; }

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
    if (auto smp = sampleSource.get())
    {
        const int n = smp->length();
        const int a = (int) (voiceCtx.regionStart.load() * (float) n), b = juce::jmax (a + 1, (int) (voiceCtx.regionEnd.load() * (float) n));
        float pk = 1.0e-6f;
        for (int i = a; i < b && i < n; ++i) pk = juce::jmax (pk, std::abs (smp->data()[i]));
        voiceCtx.abGain = 1.0f / pk;
    }
    markReference();
    undoManager.clearUndoHistory();
}

juce::AudioProcessorEditor* NebulaAudioProcessor::createEditor()
{
    return new NebulaAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NebulaAudioProcessor();
}

//==============================================================================
// ADN

void NebulaAudioProcessor::setBaseModel (std::shared_ptr<const ResynthModel> m)
{
    std::atomic_store (&baseModel, std::shared_ptr<const ResynthModel> (m));
    appliedDna = Dna::readStructural (params);
    voiceCtx.models.set (Dna::makeVariant (m, appliedDna));
}

void NebulaAudioProcessor::applyDna()
{
    const auto now = Dna::readStructural (params);
    auto base = baseResynthModel();
    if (base == nullptr) { appliedDna = now; return; }
    if (now != appliedDna)
    {
        appliedDna = now;
        voiceCtx.models.set (Dna::makeVariant (base, now));
    }
}

void NebulaAudioProcessor::timerCallback()
{
    applyDna();
}

juce::int64 NebulaAudioProcessor::nextDnaSeed()
{
    const juce::int64 s = (juce::int64) state.state.getProperty ("dnaSeed", 1);
    state.state.setProperty ("dnaSeed", s + 1, nullptr);
    return s;
}

bool NebulaAudioProcessor::isGeneLocked (const juce::String& id) const
{
    return juce::StringArray::fromTokens (state.state["dnaLocks"].toString(), ",", "").contains (id);
}

void NebulaAudioProcessor::setGeneLocked (const juce::String& id, bool locked)
{
    auto list = juce::StringArray::fromTokens (state.state["dnaLocks"].toString(), ",", "");
    list.removeString (id);
    if (locked) list.add (id);
    list.removeEmptyStrings();
    state.state.setProperty ("dnaLocks", list.joinIntoString (","), nullptr);
}

void NebulaAudioProcessor::resetGene (const juce::String& id)
{
    if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->getDefaultValue());
    applyDna();
}

void NebulaAudioProcessor::mutateDna (float amount, juce::int64 seed)
{
    juce::Random r (seed);
    amount = juce::jlimit (0.0f, 1.0f, amount);
    for (auto& g : Dna::genes())
    {
        auto* p = state.getParameter (g.id);
        if (p == nullptr) continue;
        const float v = p->getValue();
        const float u = r.nextFloat() * 2.0f - 1.0f;   // se consume siempre: los candados no cambian el resto
        if (isGeneLocked (g.id) || juce::String (g.id) == ParamID::dnaHold) continue;
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v + u * amount * 0.5f));
    }
    applyDna();
}

juce::String NebulaAudioProcessor::dnaToJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("nebulaDna", 1);
    for (auto& g : Dna::genes())
        if (auto* p = state.getParameter (g.id)) o->setProperty (g.id, p->convertFrom0to1 (p->getValue()));
    return juce::JSON::toString (juce::var (o));
}

bool NebulaAudioProcessor::dnaFromJson (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject() || ! v.hasProperty ("nebulaDna")) return false;
    for (auto& g : Dna::genes())
        if (v.hasProperty (g.id))
            if (auto* p = state.getParameter (g.id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) (double) v[g.id]));
    applyDna();
    return true;
}

void NebulaAudioProcessor::storeDnaSlot (int slot)
{
    state.state.setProperty (slot == 0 ? "dnaSlotA" : "dnaSlotB", dnaToJson(), nullptr);
}

bool NebulaAudioProcessor::recallDnaSlot (int slot)
{
    return dnaFromJson (state.state[slot == 0 ? "dnaSlotA" : "dnaSlotB"].toString());
}

bool NebulaAudioProcessor::hasDnaSlot (int slot) const
{
    return state.state[slot == 0 ? "dnaSlotA" : "dnaSlotB"].toString().isNotEmpty();
}

//==============================================================================
// FORGE

juce::AudioBuffer<float> NebulaAudioProcessor::forgeSection (bool& ok) const
{
    ok = false;
    auto s = sampleSource.get();
    if (s == nullptr) return {};
    const int n = s->length();
    const int a = juce::jlimit (0, juce::jmax (0, n - 2), (int) (regionStart() * (float) n));
    const int b = juce::jlimit (a + 2, n, (int) (regionEnd() * (float) n));
    juce::AudioBuffer<float> buf (1, b - a);
    buf.copyFrom (0, 0, s->data() + a, b - a);
    ok = true;
    return buf;
}

bool NebulaAudioProcessor::startForge (juce::int64 seed, int count)
{
    bool ok = false;
    auto section = forgeSection (ok);
    if (! ok) return false;
    Forge::SearchSettings st;
    st.seed = seed; st.candidates = juce::jlimit (8, 16, count);
    st.dataset = Forge::builtInDataset();   // vecinos de la base de patches nativos como puntos de partida
    forgeJob.start (section, sampleSource.get()->sampleRate, sampleSource.get()->name, st);
    return true;
}

bool NebulaAudioProcessor::forgeMutate (int index, juce::int64 seed)
{
    bool ok = false;
    auto section = forgeSection (ok);
    const auto list = forgeCandidates();
    if (! ok || index < 0 || index >= (int) list.size()) return false;
    Forge::SearchSettings st;
    st.seed = seed; st.candidates = 8;
    forgeJob.start (section, sampleSource.get()->sampleRate, sampleSource.get()->name, st, list[(size_t) index], std::nullopt, forgeLocks());
    return true;
}

bool NebulaAudioProcessor::forgeCrossover (int a, int b, juce::int64 seed)
{
    bool ok = false;
    auto section = forgeSection (ok);
    const auto list = forgeCandidates();
    if (! ok || a == b || a < 0 || b < 0 || a >= (int) list.size() || b >= (int) list.size()) return false;
    Forge::SearchSettings st;
    st.seed = seed; st.candidates = 8;
    forgeJob.start (section, sampleSource.get()->sampleRate, sampleSource.get()->name, st, list[(size_t) a], list[(size_t) b], forgeLocks());
    return true;
}

Forge::ModuleLocks NebulaAudioProcessor::forgeLocks() const
{
    return Forge::ModuleLocks::fromString (state.state["forgeLocks"].toString());
}

void NebulaAudioProcessor::setForgeLock (Forge::Module m, bool locked)
{
    auto l = forgeLocks();
    l.set (m, locked);
    state.state.setProperty ("forgeLocks", l.toString(), nullptr);
}

static Forge::PolarityEdit readPolarity (const juce::ValueTree& st)
{
    const auto xml = st["forgePolarity"].toString();
    if (xml.isEmpty()) return {};
    if (auto x = juce::parseXML (xml)) return Forge::PolarityEdit::fromValueTree (juce::ValueTree::fromXml (*x));
    return {};
}

void NebulaAudioProcessor::setRecipePolarity (float amount)
{
    auto e = readPolarity (state.state);
    const auto now = currentSynthPatch();
    // la receta que suena ahora pasa a ser la base (también si se tocaron perillas después del último movimiento)
    if (! e.active || ! now.sameSound (e.result(), 1.0e-4f)) e.begin (now);
    e.locks = forgeLocks();
    e.amount = juce::jlimit (-1.0f, 1.0f, amount);
    const auto edit = e.toValueTree().toXmlString();
    auto patch = e.result();
    patch.name = e.base.name;
    applySynthPatch (patch);                          // (applySynthPatch no toca la base guardada)
    state.state.setProperty ("forgePolarity", edit, nullptr);
}

float NebulaAudioProcessor::recipePolarity() const { return readPolarity (state.state).amount; }
bool NebulaAudioProcessor::recipePolarityActive() const { return readPolarity (state.state).active; }

void NebulaAudioProcessor::revertRecipePolarity()
{
    auto e = readPolarity (state.state);
    if (! e.active) return;
    const auto base = e.revert();
    applySynthPatch (base);
    state.state.removeProperty ("forgePolarity", nullptr);
}

void NebulaAudioProcessor::collectForgeResult()
{
    if (! forgeJob.finished.exchange (false)) return;
    auto r = forgeJob.result();
    if (r.cancelled && r.candidates.empty()) return;
    const juce::ScopedLock sl (forgeLock);
    if (! forgeList.empty()) forgeHistory.push_back (forgeList);   // se conserva la generación anterior
    forgeList = std::move (r.candidates);
    forgeFeatures = std::move (r.features);
    forgeDiagnostics = r.generation > 0
        ? "generacion " + juce::String (r.generation) + ": " + juce::String ((int) forgeList.size()) + " hijos ("
            + (forgeList.empty() ? juce::String() : forgeList.front().source) + ") medidos contra la referencia en " + juce::String (r.seconds, 1) + " s"
        : r.diagnostics.summary();
    ++forgeRuns;
}

void NebulaAudioProcessor::auditionForgeCandidate (int index)
{
    const auto list = forgeCandidates();
    if (index < 0 || index >= (int) list.size()) return;
    const auto ref = forgeReference();
    Forge::PatchRenderer r;
    const double sr = currentSampleRate > 0 ? currentSampleRate : 48000.0;
    const double len = juce::jlimit (0.5, 6.0, ref.lengthS > 0 ? ref.lengthS : 2.0);
    auto audio = r.render (list[(size_t) index].patch, ref.note, ref.releaseStartS > 0 ? ref.releaseStartS : len, len + 0.3, sr);
    juce::AudioBuffer<float> mono (1, audio.getNumSamples());
    mono.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
    startPreview (SampleTools::fromBuffer (mono, sr, list[(size_t) index].id));
}

void NebulaAudioProcessor::auditionForgeReference()
{
    const auto ref = forgeReference();
    if (ref.tuned.getNumSamples() == 0) return;
    juce::AudioBuffer<float> mono (1, ref.tuned.getNumSamples());
    mono.copyFrom (0, 0, ref.tuned, 0, 0, ref.tuned.getNumSamples());
    startPreview (SampleTools::fromBuffer (mono, ref.sampleRate, "referencia"));
}

void NebulaAudioProcessor::applySynthPatch (const Forge::SynthPatch& p)
{
    auto set = [this] (const juce::String& id, float v) { if (auto* prm = state.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    for (auto& info : Forge::synthParams()) set (info.id, p.get (info.id));
    // lo que no es receta queda neutro: motor Synth, sin efectos ni macros ni Cosmos/Abismo
    set (ParamID::engine, (float) Engine::Synth);
    set (ParamID::transform, 0.0f);
    for (auto* id : { ParamID::fxEqOn, ParamID::fxCompOn, ParamID::fxChorusOn, ParamID::fxDelayOn, ParamID::fxRevOn }) set (id, 0.0f);
    set (ParamID::fxWidth, 1.0f);
    for (auto* id : { ParamID::macroBright, ParamID::macroBody, ParamID::macroMove, ParamID::macroSpace, ParamID::macroChaos }) set (id, 0.0f);
    state.state.setProperty ("forgePatch", p.toXmlString(), nullptr);
    state.state.removeProperty ("forgePolarity", nullptr);   // otra receta: Cosmos/Abismo empieza desde ella
    state.state.setProperty ("preset", -1, nullptr);
    markReference();
}

Forge::SynthPatch NebulaAudioProcessor::currentSynthPatch() const
{
    auto p = Forge::SynthPatch::init();
    const auto stored = state.state["forgePatch"].toString();
    if (stored.isNotEmpty()) { auto meta = Forge::SynthPatch::fromXmlString (stored); p.name = meta.name; p.origin = meta.origin; p.reference = meta.reference; p.architecture = meta.architecture; p.seed = meta.seed; }
    for (auto& info : Forge::synthParams())
        if (auto* v = state.getRawParameterValue (info.id)) p.values[info.id] = v->load();
    return p;
}
