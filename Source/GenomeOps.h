#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include "Parameters.h"

// Un "genoma" es el valor normalizado (0..1) de cada parámetro de sonido.
struct Genome
{
    std::vector<float> genes;
    bool isValid() const { return ! genes.empty(); }
};

// Operaciones sobre genomas: mutar, cruzar, medir distancia.
// Cada hilo usa su propia instancia (tiene su propio generador aleatorio).
class GenomeOps
{
public:
    // Parámetros de "interpretación" que no forman parte del sonido (no se mutan ni se guardan en los brotes)
    static bool isPerformanceParam (const juce::String& id)
    {
        return id == ParamID::master || id == ParamID::polyMode || id == ParamID::voiceCount
            || id == ParamID::glide || id == ParamID::bendRange || id == ParamID::velSens
            || id == ParamID::smpPlayMode || id == ParamID::smpBeats
            || id == ParamID::lfoOn || id == ParamID::lfo2On || id == ParamID::quality || id == ParamID::mpeOn;
    }

    explicit GenomeOps (const juce::AudioProcessor& proc, juce::int64 seed = juce::Time::currentTimeMillis())
        : rng (seed)
    {
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isPerformanceParam (rp->getParameterID()))
                {
                    index[rp->getParameterID().toStdString()] = (int) params.size();
                    params.push_back (rp);
                }
    }

    const std::vector<juce::RangedAudioParameter*>& parameters() const { return params; }
    int size() const { return (int) params.size(); }
    juce::Random& random() { return rng; }

    int indexOf (const juce::String& id) const
    {
        auto it = index.find (id.toStdString());
        return it == index.end() ? -1 : it->second;
    }

    float real (const Genome& g, const juce::String& id) const
    {
        const int i = indexOf (id);
        if (i < 0 || i >= (int) g.genes.size()) return 0.0f;
        return params[(size_t) i]->convertFrom0to1 (g.genes[(size_t) i]);
    }

    void setReal (Genome& g, const juce::String& id, float realValue) const
    {
        const int i = indexOf (id);
        if (i >= 0 && i < (int) g.genes.size())
            g.genes[(size_t) i] = params[(size_t) i]->convertTo0to1 (realValue);
    }

    int engineOf (const Genome& g) const { return juce::roundToInt (real (g, ParamID::engine)); }
    bool isSamplerGenome (const Genome& g) const { return engineOf (g) == Engine::Sample; }

    //==========================================================================
    Genome mutate (const Genome& parent, float amt, bool lockEnv, bool lockFilter = false)
    {
        Genome g = parent;
        const int engine = engineOf (g);
        const bool sampler = engine == Engine::Sample;

        for (size_t i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const auto id = p->getParameterID();
            float& v = g.genes[i];

            if (id == ParamID::engine) continue;
            if (lockEnv && isEnvelopeGene (id)) continue;
            if (lockFilter && isFilterGene (id)) continue;
            if (engine != Engine::Synth && isOscillatorGene (id)) continue;   // sin efecto fuera del motor Sinte
            if (! sampler && isSampleGene (id)) continue;
            if (engine != Engine::Resynth && id.startsWith ("rs")) continue;
            // Decisiones del usuario: encendidos, macros, efectos y la curva de tono no mutan
            if (id.endsWith ("On") || id.startsWith ("macro") || id.startsWith ("fx") || id == ParamID::rsPitch) continue;
            // El ADN y Cosmos/Abismo tienen su propia mutación (con semilla y candados)
            if (id.startsWith ("dna") || id == ParamID::transform || id == ParamID::identity || id == ParamID::transformOut) continue;

            auto chance = [&] (float x) { return rng.nextFloat() < amt * x; };
            auto setIdx = [&] (float realValue) { v = p->convertTo0to1 (realValue); };

            if (id == ParamID::osc1Wave || id == ParamID::osc2Wave)
            {
                // Más peso a las formas con más carácter
                if (chance (0.3f)) setIdx ((float) weighted ({ 1.0f, 1.0f, 3.0f, 2.0f, 1.6f, 1.0f, 2.0f }));
            }
            else if (id == ParamID::osc1Table || id == ParamID::osc2Table)
            {
                if (chance (0.2f)) setIdx ((float) rng.nextInt (4));
            }
            else if (id == ParamID::osc2Semi || id == ParamID::smpTune)
            {
                if (chance (id == ParamID::smpTune ? 0.3f : 0.4f))
                {
                    static const int synthIntervals[]  { -12, -12, -5, 0, 0, 0, 5, 7, 7, 12, 12, 19, 24 };
                    static const int sampleIntervals[] { -12, -12, -7, -5, 0, 0, 0, 0, 5, 7, 12 };
                    setIdx (id == ParamID::smpTune ? (float) sampleIntervals[rng.nextInt (juce::numElementsInArray (sampleIntervals))]
                                                   : (float) synthIntervals[rng.nextInt (juce::numElementsInArray (synthIntervals))]);
                }
            }
            else if (id == ParamID::osc1Octave)
            {
                if (chance (0.25f)) setIdx (juce::jlimit (-2.0f, 1.0f, p->convertFrom0to1 (v) + (rng.nextBool() ? 1.0f : -1.0f)));
            }
            else if (id == ParamID::uniVoices)
            {
                if (chance (0.2f)) setIdx (juce::jlimit (1.0f, 5.0f, p->convertFrom0to1 (v) + (float) (rng.nextInt (5) - 2)));
            }
            else if (id == ParamID::filterType || id == ParamID::f2Type)
            {
                if (chance (0.15f)) setIdx ((float) weighted ({ 4.0f, 1.5f, 1.5f, 0.6f }));
            }
            else if (id == ParamID::f2On || id == ParamID::fltMode || id == ParamID::env3Mode
                     || id == ParamID::lfoRetrig || id == ParamID::lfo2Retrig || id == ParamID::smpReverse || id == ParamID::smpLoop)
            {
                if (chance (0.12f)) v = v > 0.5f ? 0.0f : 1.0f;
            }
            else if (id == ParamID::lfoSync || id == ParamID::lfo2Sync)
            {
                if (chance (0.15f)) setIdx (rng.nextFloat() < 0.5f ? 0.0f : (float) (3 + rng.nextInt (5)));
            }
            else if (id.startsWith ("m") && id.containsChar ('_'))
            {
                // Matriz: pocas conexiones y moderadas
                if (std::abs (v - 0.5f) < 0.01f)
                {
                    if (chance (0.05f))
                        setIdx ((rng.nextBool() ? 1.0f : -1.0f) * (0.15f + 0.5f * rng.nextFloat()));
                }
                else if (chance (0.25f)) setIdx (0.0f);
                else if (chance (0.5f))  v = juce::jlimit (0.0f, 1.0f, v + gauss() * 0.1f);
            }
            else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                if (chance (0.15f)) setIdx ((float) rng.nextInt (choice->choices.size()));
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
            {
                if (chance (0.1f)) v = v > 0.5f ? 0.0f : 1.0f;
            }
            else
            {
                // FM, ruido, textura, LFO al filtro/tono... son "especias": cambian menos
                const bool spice = id == ParamID::fmAmount || id == ParamID::noise || id == ParamID::smpTexture
                                || id == ParamID::lfoFilter || id == ParamID::lfoPitch || id == ParamID::f2Env;
                const float prob = spice ? 0.4f : 0.3f + 0.7f;
                if (rng.nextFloat() < (spice ? amt * prob : (0.3f + 0.7f * amt)))
                {
                    v += gauss() * amt * 0.3f;
                    if (spice && v < 0.04f) v = 0.0f;
                    v = juce::jlimit (0.0f, 1.0f, v);
                }
            }
        }

        sanitize (g);
        return g;
    }

    // Mezcla gen por gen de dos padres
    Genome crossover (const Genome& a, const Genome& b)
    {
        Genome c = a;
        for (size_t i = 0; i < c.genes.size() && i < b.genes.size(); ++i)
            if (rng.nextBool()) c.genes[i] = b.genes[i];
        return c;
    }

    // Evita que una mutación quede muda, inservible o demasiado pesada
    void sanitize (Genome& g)
    {
        if (real (g, ParamID::ampS) < 0.05f && real (g, ParamID::ampD) < 0.08f)
            setReal (g, ParamID::ampD, 0.25f);
        // ataque de 1 s como máximo: con más, una nota corta casi no se oye y el brote parece mudo
        // (la curva cuenta: con curva muy negativa el ataque arranca lentísimo y tarda mucho más en oírse)
        {
            const float slow = 1.0f + 3.0f * juce::jmax (0.0f, -real (g, ParamID::ampCurve));
            if (real (g, ParamID::ampA) * slow > 1.0f)
                setReal (g, ParamID::ampA, 1.0f / slow);
        }

        // Filtro 1
        {
            const int ft = (int) real (g, ParamID::filterType);
            const float cut = real (g, ParamID::cutoff);
            const float env = real (g, ParamID::filterEnv);
            if (ft == 0)
            {
                const float fs = real (g, ParamID::fltS);
                const float sustainCut = cut * std::pow (2.0f, env * 6.0f * (env < 0.0f ? juce::jmax (fs, 0.4f) : fs));
                const float peakCut = cut * std::pow (2.0f, juce::jmax (0.0f, env) * 6.0f);
                if (sustainCut < 300.0f && peakCut < 900.0f)
                    setReal (g, ParamID::cutoff, juce::jmin (20000.0f, cut * 400.0f / juce::jmax (1.0f, sustainCut)));
                // envolvente negativa: en su punto más cerrado el filtro no debe bajar de ~150 Hz (si no, el
                // golpe del volumen ocurre con el filtro cerrado y el brote queda mudo)
                const float c2 = real (g, ParamID::cutoff);
                if (env < 0.0f && c2 * std::pow (2.0f, env * 6.0f) < 150.0f)
                    setReal (g, ParamID::filterEnv, juce::jmin (0.0f, -std::log2 (juce::jmax (150.0f, c2) / 150.0f) / 6.0f));
            }
            if (ft == 2)
            {
                if (cut > 1500.0f) setReal (g, ParamID::cutoff, 400.0f + 1100.0f * rng.nextFloat());
                if (env > 0.2f)    setReal (g, ParamID::filterEnv, 0.2f * rng.nextFloat());
            }
            if (ft == 1)
            {
                if (cut < 200.0f || cut > 3000.0f) setReal (g, ParamID::cutoff, 500.0f + 1500.0f * rng.nextFloat());
                if (std::abs (env) > 0.35f)        setReal (g, ParamID::filterEnv, (rng.nextFloat() - 0.3f) * 0.5f);
            }
        }

        // Filtro 2 (si está encendido)
        if (real (g, ParamID::f2On) > 0.5f)
        {
            const int ft = (int) real (g, ParamID::f2Type);
            const float cut = real (g, ParamID::f2Cutoff);
            if (ft == 2 && cut > 2500.0f) setReal (g, ParamID::f2Cutoff, 150.0f + 600.0f * rng.nextFloat());
            if (ft == 0 && cut < 400.0f)  setReal (g, ParamID::f2Cutoff, 1500.0f + 4500.0f * rng.nextFloat());
            if (ft == 1 && (cut < 300.0f || cut > 3000.0f)) setReal (g, ParamID::f2Cutoff, 500.0f + 1500.0f * rng.nextFloat());
            if (std::abs (real (g, ParamID::f2Env)) > 0.4f) setReal (g, ParamID::f2Env, 0.0f);
        }

        if (real (g, ParamID::noise) > 0.9f)    setReal (g, ParamID::noise, 0.9f);
        if (real (g, ParamID::uniVoices) > 5.0f) setReal (g, ParamID::uniVoices, 5.0f);

        // Matriz: tono y volumen con límites razonables
        for (int s = 0; s < ModSrc::count; ++s)
        {
            const auto pid = matrixID (s, ModDst::Pitch);
            const float pv = real (g, pid);
            if (std::abs (pv) > 0.25f) setReal (g, pid, std::copysign (0.25f, pv));
            const auto vid = matrixID (s, ModDst::Volume);
            if (real (g, vid) < -0.6f) setReal (g, vid, -0.6f);
        }

        if (isSamplerGenome (g) && real (g, ParamID::smpStart) > 0.85f)
            setReal (g, ParamID::smpStart, 0.85f * rng.nextFloat());
    }

    static float distance (const Genome& a, const Genome& b)
    {
        if (a.genes.size() != b.genes.size() || a.genes.empty()) return 0.0f;
        float sum = 0.0f;
        for (size_t i = 0; i < a.genes.size(); ++i)
            sum += juce::square (a.genes[i] - b.genes[i]);
        // muchos genes de la matriz están en 0: normalizamos con un número fijo de genes "activos"
        return juce::jlimit (0.0f, 1.0f, std::sqrt (sum / 45.0f) / 0.3f);
    }

    static juce::String toString (const Genome& g)
    {
        juce::StringArray s;
        for (auto v : g.genes) s.add (juce::String (v, 5));
        return s.joinIntoString (",");
    }

    static Genome fromString (const juce::String& str)
    {
        Genome g;
        for (auto& t : juce::StringArray::fromTokens (str, ",", ""))
            if (t.isNotEmpty()) g.genes.push_back (t.getFloatValue());
        return g;
    }

    float gauss()
    {
        const float u1 = juce::jmax (1.0e-6f, rng.nextFloat()), u2 = rng.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
    }

private:
    int weighted (std::initializer_list<float> w)
    {
        float total = 0.0f;
        for (auto x : w) total += x;
        float r = rng.nextFloat() * total;
        int i = 0;
        for (auto x : w) { if ((r -= x) <= 0.0f) return i; ++i; }
        return i - 1;
    }

    static bool isEnvelopeGene (const juce::String& id)
    {
        return id == ParamID::ampA || id == ParamID::ampD || id == ParamID::ampS || id == ParamID::ampR || id == ParamID::ampCurve;
    }

    static bool isFilterGene (const juce::String& id)
    {
        return id.startsWith ("filter") || id.startsWith ("f2") || id == ParamID::cutoff || id == ParamID::resonance
            || id == ParamID::drive || id == ParamID::lfoFilter
            || (id.startsWith ("m") && (id.endsWith ("_" + juce::String (ModDst::Cutoff1)) || id.endsWith ("_" + juce::String (ModDst::Reso1))
                                        || id.endsWith ("_" + juce::String (ModDst::Cutoff2))));
    }

    static bool isOscillatorGene (const juce::String& id)
    {
        return id.startsWith ("osc") || id == ParamID::oscMix || id == ParamID::fmAmount || id.startsWith ("uni");
    }

    static bool isSampleGene (const juce::String& id) { return id.startsWith ("smp"); }

    std::vector<juce::RangedAudioParameter*> params;
    std::unordered_map<std::string, int> index;
    juce::Random rng;
};
