#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analysis.h"
#include "GenomeOps.h"
#include "Presets.h"
#include "SampleData.h"
#include "SynthVoice.h"

// Renderiza patches del sinte "fuera de línea" (sin tocar el audio en vivo).
class OfflineSynth
{
public:
    OfflineSynth (const juce::Array<juce::AudioProcessorParameter*>& all,
                  const std::vector<juce::RangedAudioParameter*>& genomeParams,
                  const WavetableBank& bank)
        : genomeParamList (genomeParams),
          pointers ([this] (const juce::String& id) { return lookup (id); }),
          ctx (pointers, noSample, bank),
          synth (ctx)
    {
        for (auto* p : all)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                ids.add (rp->getParameterID());
                ranged.push_back (rp);
            }
        values.reset (new std::atomic<float>[ranged.size()]);
        for (size_t i = 0; i < ranged.size(); ++i)
            values[i] = ranged[i]->convertFrom0to1 (ranged[i]->getDefaultValue());
        resolved = true;
        pointers = ParamPointers ([this] (const juce::String& id) { return lookup (id); });

        for (auto* gp : genomeParamList)
            genomeIndex.push_back (ids.indexOf (gp->getParameterID()));

        synth.addSound (new SynthSound());
        synth.addVoice (new SynthVoice (ctx));
    }

    void setGenome (const Genome& g)
    {
        for (size_t i = 0; i < genomeParamList.size() && i < g.genes.size(); ++i)
            if (genomeIndex[i] >= 0)
                values[(size_t) genomeIndex[i]] = genomeParamList[i]->convertFrom0to1 (g.genes[i]);
        // Interpretación fija para la búsqueda: una voz, sin glide
        set (ParamID::polyMode, 0.0f); set (ParamID::voiceCount, 1.0f); set (ParamID::glide, 0.0f);
    }

    void render (std::vector<float>& out, int note, int holdSamples, int totalSamples, double sr)
    {
        if (sr != currentRate)
        {
            currentRate = sr;
            synth.setCurrentPlaybackSampleRate (sr);
            if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (0)))
                v->prepare (sr);
        }
        synth.allNotesOff (0, false);
        ctx.lastNote = -1.0f;

        out.assign ((size_t) totalSamples, 0.0f);
        constexpr int block = 512;
        juce::AudioBuffer<float> buf (2, block);
        for (int pos = 0; pos < totalSamples; pos += block)
        {
            const int n = juce::jmin (block, totalSamples - pos);
            buf.clear();
            juce::MidiBuffer midi;
            if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            if (holdSamples >= pos && holdSamples < pos + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), holdSamples - pos);
            ctx.blockTime = pos / sr;
            synth.renderNextBlock (buf, midi, 0, n);
            for (int i = 0; i < n; ++i)
                out[(size_t) (pos + i)] = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
        }
    }

private:
    void set (const char* id, float v) { const int i = ids.indexOf (id); if (i >= 0) values[(size_t) i] = v; }

    std::atomic<float>* lookup (const juce::String& id)
    {
        if (! resolved) return &dummy;   // primera construcción (antes de crear los valores)
        const int i = ids.indexOf (id);
        return i >= 0 ? &values[(size_t) i] : &dummy;
    }

    const std::vector<juce::RangedAudioParameter*>& genomeParamList;
    juce::StringArray ids;
    std::vector<juce::RangedAudioParameter*> ranged;
    std::vector<int> genomeIndex;
    std::unique_ptr<std::atomic<float>[]> values;
    std::atomic<float> dummy { 0.0f };
    bool resolved = false;
    ParamPointers pointers;
    SampleSource noSample;
    VoiceContext ctx;
    NebulaSynth synth;
    double currentRate = 0.0;
};

//==============================================================================
// Busca, por evolución, el patch del sinte que más se parece a un sample.
class Imitator : private juce::Thread
{
public:
    Imitator (const juce::AudioProcessor& proc, const WavetableBank& b)
        : juce::Thread ("Nebula imitacion"), ops (proc), processor (proc), bank (b) {}

    ~Imitator() override { stopThread (4000); }

    // Duración de la búsqueda: generaciones × hijos por generación
    int generations = 40, childrenPerGeneration = 24, elites = 8;

    void start (std::shared_ptr<const SampleData> sample, const Genome& current, float regionStart, float regionEnd, float rootOffset)
    {
        stopThread (4000);
        target = std::move (sample);
        rStart = regionStart; rEnd = regionEnd; rootFix = rootOffset;
        startGenome = current;
        progress = 0.0f;
        similarity = 0.0f;
        finished = false;
        startThread (juce::Thread::Priority::low);
    }

    void cancel()              { signalThreadShouldExit(); }
    void stop()                { stopThread (4000); }
    bool isRunning() const     { return isThreadRunning(); }

    std::atomic<float> progress { 0.0f }, similarity { 0.0f };
    std::atomic<bool> finished { false };
    std::function<void()> onFinished; // se llama desde el hilo de búsqueda

    Genome result() const { const juce::ScopedLock sl (resultLock); return best; }
    float resultNote() const { return renderNote; }

private:
    void run() override
    {
        constexpr double sr = 44100.0;
        OfflineSynth offline (processor.getParameters(), ops.parameters(), bank);
        SoundAnalyser analyser;

        // ---- Preparar el objetivo: la región elegida, desde el ataque, hasta 2.5 s ----
        const int fullLen = target->length();
        const int a0 = juce::jlimit (0, fullLen - 2, (int) (rStart * (float) fullLen));
        const int a1 = juce::jlimit (a0 + 2, fullLen, (int) (rEnd * (float) fullLen));
        const float* x = target->data() + a0;
        const int n = a1 - a0;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (x[i]));
        int onset = 0;
        while (onset < n - 1 && std::abs (x[onset]) < peak * 0.03f) ++onset;
        int end = n - 1;
        while (end > onset && std::abs (x[end]) < peak * 0.003f) --end;

        const double lengthS = juce::jlimit (0.15, 2.5, (end - onset) / target->sampleRate);
        const SoundFeatures want = analyser.analyse (x + onset, n - onset, target->sampleRate, lengthS);

        renderNote = target->hasPitch ? (float) juce::roundToInt (target->rootNote + rootFix) : 48.0f;
        const int note = juce::jlimit (12, 115, (int) renderNote);
        const int total = (int) (lengthS * sr) + 1024;
        const int hold = (int) (lengthS * 0.85 * sr);

        std::vector<float> audio;
        auto evaluate = [&] (Genome& g) -> float
        {
            ops.setReal (g, ParamID::engine, 0.0f); // siempre con el motor de sinte
            offline.setGenome (g);
            offline.render (audio, note, hold, total, sr);
            float pk = 0.0f;
            for (auto v : audio) pk = juce::jmax (pk, std::abs (v));
            if (pk < 1.0e-5f || ! std::isfinite (pk)) return 1000.0f;
            const auto got = analyser.analyse (audio.data(), (int) audio.size(), sr, lengthS);
            return SoundAnalyser::distance (want, got);
        };

        // ---- Población inicial: sonido actual, presets y variantes al azar ----
        std::vector<std::pair<float, Genome>> pop;
        auto add = [&] (Genome g) { ops.sanitize (g); const float d = evaluate (g); pop.push_back ({ d, std::move (g) }); };

        add (startGenome);
        const auto& presets = factoryPresets();
        for (int p = 0; p < (int) presets.size(); ++p)
            add (presetGenome (p));
        while ((int) pop.size() < elites + childrenPerGeneration)
        {
            Genome g = presetGenome (ops.random().nextInt ((int) presets.size()));
            g = ops.mutate (g, 1.0f, false);
            add (ops.mutate (g, 0.7f, false));
            if (threadShouldExit()) return;
        }

        // ---- Evolución ----
        for (int gen = 0; gen < generations; ++gen)
        {
            if (threadShouldExit()) return;

            std::sort (pop.begin(), pop.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            pop.resize ((size_t) elites);
            publish (pop.front());

            const float t = (float) gen / (float) generations;
            const float amt = 0.08f + 0.6f * (1.0f - t) * (1.0f - t);

            for (int c = 0; c < childrenPerGeneration; ++c)
            {
                // Torneo: los mejores tienen más chance de ser padres
                const int a = juce::jmin (ops.random().nextInt (elites), ops.random().nextInt (elites));
                Genome child = pop[(size_t) a].second;
                if (ops.random().nextFloat() < 0.4f)
                    child = ops.crossover (child, pop[(size_t) ops.random().nextInt (elites)].second);
                child = ops.mutate (child, amt * (0.5f + ops.random().nextFloat()), false);
                add (child);
                if (threadShouldExit()) return;
            }
            progress = (float) (gen + 1) / (float) generations;
        }

        std::sort (pop.begin(), pop.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        publish (pop.front());
        progress = 1.0f;
        finished = true;
        if (onFinished) onFinished();
    }

    void publish (const std::pair<float, Genome>& p)
    {
        const juce::ScopedLock sl (resultLock);
        best = p.second;
        similarity = SoundAnalyser::similarity (p.first);
    }

    Genome presetGenome (int index)
    {
        Genome g;
        for (auto* p : ops.parameters()) g.genes.push_back (p->getDefaultValue());
        for (const auto& [id, value] : factoryPresets()[(size_t) index].values)
            ops.setReal (g, id, value);
        // Conserva los ajustes de sample/motor del sonido actual
        for (size_t i = 0; i < ops.parameters().size(); ++i)
            if (ops.parameters()[i]->getParameterID().startsWith ("smp") && i < startGenome.genes.size())
                g.genes[i] = startGenome.genes[i];
        return g;
    }

    GenomeOps ops;
    const juce::AudioProcessor& processor;
    const WavetableBank& bank;
    float rStart = 0.0f, rEnd = 1.0f, rootFix = 0.0f;
    std::shared_ptr<const SampleData> target;
    Genome startGenome, best;
    float renderNote = 48.0f;
    juce::CriticalSection resultLock;
};
