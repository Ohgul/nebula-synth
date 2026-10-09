#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
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
    explicit GenomeOps (const juce::AudioProcessor& proc, juce::int64 seed = juce::Time::currentTimeMillis())
        : rng (seed)
    {
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID() != ParamID::master)
                    params.push_back (rp);
    }

    const std::vector<juce::RangedAudioParameter*>& parameters() const { return params; }
    int size() const { return (int) params.size(); }
    juce::Random& random() { return rng; }

    int indexOf (const char* id) const
    {
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i]->getParameterID() == id) return (int) i;
        return -1;
    }

    float real (const Genome& g, const char* id) const
    {
        const int i = indexOf (id);
        if (i < 0 || i >= (int) g.genes.size()) return 0.0f;
        return params[(size_t) i]->convertFrom0to1 (g.genes[(size_t) i]);
    }

    void setReal (Genome& g, const char* id, float realValue) const
    {
        const int i = indexOf (id);
        if (i >= 0 && i < (int) g.genes.size())
            g.genes[(size_t) i] = params[(size_t) i]->convertTo0to1 (realValue);
    }

    bool isSamplerGenome (const Genome& g) const { return real (g, ParamID::engine) > 0.5f; }

    //==========================================================================
    Genome mutate (const Genome& parent, float amt, bool lockEnv)
    {
        Genome g = parent;
        const bool sampler = isSamplerGenome (g);

        for (size_t i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const auto id = p->getParameterID();
            float& v = g.genes[i];

            if (id == ParamID::engine) continue;
            if (lockEnv && isEnvelopeGene (id)) continue;
            if (sampler && isOscillatorGene (id)) continue;      // en modo sample no tienen efecto
            if (! sampler && isSampleGene (id)) continue;

            if (id == ParamID::osc2Semi || id == ParamID::smpTune)
            {
                if (rng.nextFloat() < amt * (id == ParamID::smpTune ? 0.3f : 0.4f))
                {
                    static const int synthIntervals[]  = { -12, -12, -5, 0, 0, 0, 5, 7, 7, 12, 12, 19, 24 };
                    static const int sampleIntervals[] = { -12, -12, -7, -5, 0, 0, 0, 0, 5, 7, 12 };
                    v = id == ParamID::smpTune
                          ? p->convertTo0to1 ((float) sampleIntervals[rng.nextInt (juce::numElementsInArray (sampleIntervals))])
                          : p->convertTo0to1 ((float) synthIntervals[rng.nextInt (juce::numElementsInArray (synthIntervals))]);
                }
            }
            else if (id == ParamID::osc1Octave)
            {
                if (rng.nextFloat() < amt * 0.25f)
                {
                    const float cur = p->convertFrom0to1 (v);
                    v = p->convertTo0to1 (juce::jlimit (-2.0f, 1.0f, cur + (rng.nextBool() ? 1.0f : -1.0f)));
                }
            }
            else if (id == ParamID::filterType)
            {
                if (rng.nextFloat() < amt * 0.15f)
                    v = p->convertTo0to1 (rng.nextFloat() < 0.6f ? 0.0f : (float) (1 + rng.nextInt (2)));
            }
            else if (id == ParamID::smpReverse || id == ParamID::smpLoop)
            {
                if (rng.nextFloat() < amt * 0.2f)
                    v = v > 0.5f ? 0.0f : 1.0f;
            }
            else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                if (rng.nextFloat() < amt * 0.35f)
                    v = p->convertTo0to1 ((float) rng.nextInt (choice->choices.size()));
            }
            else
            {
                // FM, ruido y textura mutan con menos frecuencia (son "especias")
                const bool spice = (id == ParamID::fmAmount || id == ParamID::noise || id == ParamID::smpTexture);
                const float prob = spice ? amt * 0.4f : 0.3f + 0.7f * amt;
                if (rng.nextFloat() < prob)
                {
                    v += gauss() * amt * (spice ? 0.3f : 0.3f);
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

    // Evita que una mutación quede muda o inservible
    void sanitize (Genome& g)
    {
        if (real (g, ParamID::ampS) < 0.05f && real (g, ParamID::ampD) < 0.08f)
            setReal (g, ParamID::ampD, 0.25f);
        if (real (g, ParamID::ampA) > 1.5f)
            setReal (g, ParamID::ampA, 1.5f);

        const int ft = (int) real (g, ParamID::filterType);
        const float cut = real (g, ParamID::cutoff);
        const float env = real (g, ParamID::filterEnv);
        if (ft == 0)
        {
            // Corte "típico" mientras se sostiene la nota (la envolvente puede cerrarlo)
            const float fs = real (g, ParamID::fltS);
            const float sustainCut = cut * std::pow (2.0f, env * 6.0f * (env < 0.0f ? juce::jmax (fs, 0.4f) : fs));
            const float peakCut = cut * std::pow (2.0f, juce::jmax (0.0f, env) * 6.0f);
            if (sustainCut < 300.0f && peakCut < 900.0f)
                setReal (g, ParamID::cutoff, juce::jmin (20000.0f, cut * 400.0f / juce::jmax (1.0f, sustainCut)));
        }
        // Pasa altos y pasa banda pierden mucho volumen si se abren de más
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

        if (real (g, ParamID::noise) > 0.9f)
            setReal (g, ParamID::noise, 0.9f);

        if (isSamplerGenome (g) && real (g, ParamID::smpStart) > 0.85f)
            setReal (g, ParamID::smpStart, 0.85f * rng.nextFloat());
    }

    static float distance (const Genome& a, const Genome& b)
    {
        if (a.genes.size() != b.genes.size() || a.genes.empty()) return 0.0f;
        float sum = 0.0f;
        for (size_t i = 0; i < a.genes.size(); ++i)
            sum += juce::square (a.genes[i] - b.genes[i]);
        return juce::jlimit (0.0f, 1.0f, std::sqrt (sum / (float) a.genes.size()) / 0.3f);
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
    static bool isEnvelopeGene (const juce::String& id)
    {
        return id == ParamID::ampA || id == ParamID::ampD || id == ParamID::ampS || id == ParamID::ampR;
    }

    static bool isOscillatorGene (const juce::String& id)
    {
        return id == ParamID::osc1Wave || id == ParamID::osc1Octave || id == ParamID::osc2Wave
            || id == ParamID::osc2Semi || id == ParamID::osc2Fine || id == ParamID::oscMix || id == ParamID::fmAmount;
    }

    static bool isSampleGene (const juce::String& id)
    {
        return id.startsWith ("smp");
    }

    std::vector<juce::RangedAudioParameter*> params;
    juce::Random rng;
};
