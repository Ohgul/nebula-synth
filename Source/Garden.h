#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "GenomeOps.h"

// El Jardín: una semilla (sonido padre) y 8 brotes (mutaciones de la semilla).
// Todas las acciones se llaman desde el hilo de mensajes (la interfaz).
class Garden
{
public:
    static constexpr int numSprouts = 8;

    explicit Garden (juce::AudioProcessorValueTreeState& s)
        : state (s), ops (s.processor)
    {
        seed = capture();
        growSprouts();
        generation = 1;
    }

    //==========================================================================
    Genome capture() const
    {
        Genome g;
        for (auto* p : ops.parameters()) g.genes.push_back (p->getValue());
        return g;
    }

    void apply (const Genome& g)
    {
        const auto& params = ops.parameters();
        if ((int) g.genes.size() != (int) params.size()) return;
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (std::abs (params[i]->getValue() - g.genes[i]) < 1.0e-6f) continue;
            params[i]->beginChangeGesture();
            params[i]->setValueNotifyingHost (g.genes[i]);
            params[i]->endChangeGesture();
        }
    }

    float real (const Genome& g, const char* id) const { return ops.real (g, id); }
    const GenomeOps& operations() const { return ops; }

    //==========================================================================
    // Crea 8 brotes a partir del sonido que suena ahora
    void germinate()
    {
        const juce::ScopedLock sl (lock);
        pushHistory();
        seed = capture();
        growSprouts();
        selected = -1;
        ++generation;
        changed();
    }

    // Pone un sonido nuevo como semilla y germina (lo usan la imitación y la carga de samples)
    void plantGenome (const Genome& g)
    {
        {
            const juce::ScopedLock sl (lock);
            apply (g);
        }
        germinate();
    }

    void reroll()
    {
        const juce::ScopedLock sl (lock);
        pushHistory();
        growSprouts();
        selected = -1;
        apply (seed);
        changed();
    }

    void selectSprout (int i)
    {
        const juce::ScopedLock sl (lock);
        if (! juce::isPositiveAndBelow (i, numSprouts)) return;
        selected = i;
        apply (sprouts[(size_t) i]);
        changed();
    }

    void selectSeed()
    {
        const juce::ScopedLock sl (lock);
        selected = -1;
        apply (seed);
        changed();
    }

    void plant (int i)
    {
        if (! juce::isPositiveAndBelow (i, numSprouts)) return;
        plantGenome (sprouts[(size_t) i]);
    }

    void newRandomSeed()
    {
        Genome g;
        {
            const juce::ScopedLock sl (lock);
            if (ops.isSamplerGenome (capture()))
            {
                // Con sample: se conservan el sample y el motor, todo lo demás al azar
                g = capture();
                g = ops.mutate (g, 1.0f, false);
                g = ops.mutate (g, 0.7f, false);
            }
            else
            {
                const auto& presets = factoryPresets();
                applyPreset (state, ops.random().nextInt ((int) presets.size()));
                g = ops.mutate (capture(), 0.85f, false);
                g = ops.mutate (g, 0.6f, false);
            }
        }
        plantGenome (g);
    }

    bool back()
    {
        const juce::ScopedLock sl (lock);
        if (history.empty()) return false;
        auto h = history.back();
        history.pop_back();
        seed = h.seed;
        sprouts = h.sprouts;
        distances = h.distances;
        generation = h.generation;
        selected = -1;
        apply (seed);
        changed();
        return true;
    }

    bool canGoBack() const { return ! history.empty(); }

    //==========================================================================
    void saveTo (juce::ValueTree& root) const
    {
        const juce::ScopedLock sl (lock);
        auto t = root.getOrCreateChildWithName ("Garden", nullptr);
        t.setProperty ("seed", GenomeOps::toString (seed), nullptr);
        for (int i = 0; i < numSprouts; ++i)
            t.setProperty ("s" + juce::String (i), GenomeOps::toString (sprouts[(size_t) i]), nullptr);
        t.setProperty ("selected", selected, nullptr);
        t.setProperty ("generation", generation, nullptr);
        t.setProperty ("mutation", mutation, nullptr);
        t.setProperty ("lockEnv", lockEnvelope, nullptr);
        t.setProperty ("genes", (int) ops.size(), nullptr);
    }

    void loadFrom (const juce::ValueTree& root)
    {
        const juce::ScopedLock sl (lock);
        auto t = root.getChildWithName ("Garden");
        if (! t.isValid()) return;

        auto s = GenomeOps::fromString (t["seed"].toString());
        if ((int) s.genes.size() != ops.size()) return; // versión distinta: ignorar

        seed = s;
        for (int i = 0; i < numSprouts; ++i)
        {
            auto g = GenomeOps::fromString (t["s" + juce::String (i)].toString());
            sprouts[(size_t) i] = ((int) g.genes.size() == ops.size()) ? g : ops.mutate (seed, 0.3f, false);
            distances[(size_t) i] = GenomeOps::distance (seed, sprouts[(size_t) i]);
        }
        selected     = (int) t.getProperty ("selected", -1);
        generation   = (int) t.getProperty ("generation", 1);
        mutation     = (float) t.getProperty ("mutation", 0.5f);
        lockEnvelope = (bool) t.getProperty ("lockEnv", false);
        history.clear();
        changed();
    }

    //==========================================================================
    Genome seed;
    std::array<Genome, numSprouts> sprouts;
    std::array<float, numSprouts> distances {};
    int selected = -1;
    int generation = 1;
    float mutation = 0.5f;
    bool lockEnvelope = false;

    std::atomic<int> version { 0 }; // la interfaz lo vigila para redibujar
    juce::CriticalSection lock;

private:
    struct Snapshot
    {
        Genome seed;
        std::array<Genome, numSprouts> sprouts;
        std::array<float, numSprouts> distances;
        int generation;
    };

    void changed() { ++version; }

    void pushHistory()
    {
        history.push_back ({ seed, sprouts, distances, generation });
        if (history.size() > 40) history.erase (history.begin());
    }

    void growSprouts()
    {
        for (int k = 0; k < numSprouts; ++k)
        {
            // Cada brote muta un poco más que el anterior: alrededor del círculo
            // van de "primo cercano" a "pariente lejano".
            const float amt = mutation * (0.3f + 0.7f * (float) k / (float) (numSprouts - 1));
            sprouts[(size_t) k] = ops.mutate (seed, juce::jmax (0.04f, amt), lockEnvelope);
            distances[(size_t) k] = GenomeOps::distance (seed, sprouts[(size_t) k]);
        }
    }

    juce::AudioProcessorValueTreeState& state;
    GenomeOps ops;
    std::vector<Snapshot> history;
};
