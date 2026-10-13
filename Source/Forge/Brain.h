#pragma once
#include <deque>
#include "ForgeSearch.h"
#include "PatchGraph.h"

// ===========================================================================
// CEREBRO VIVO (NEBULA-CORE-050 · DISCOVER)
//
// Una red de neuronas que crece mientras se busca. Cada neurona es un patch real del motor Synth (se puede
// escuchar, abrir y editar). Las conexiones son parentesco real: de qué semilla salió, de qué padre mutó, de
// qué dos padres se cruzó. Nada es decorativo: una neurona aparece cuando el trabajo de fondo produce ese
// patch, y su similitud es la medida completa contra la referencia.
//
//  - Generación 0: la búsqueda de FORGE (arquitecturas + índice) transmite cada semilla al empezar a
//    optimizarse (provisional) y al terminar (medida).
//  - Distancia a la referencia (0..1): 0 = recrear lo más fiel posible; 1 = exploración radical. Cambia el
//    objetivo (se busca una pérdida objetivo creciente, no la mínima) y la intensidad de la mutación.
//  - Evolucionar: hijos por mutación (parámetros y estructura) y cruce por módulos de los padres elegidos,
//    respetando candados; se descartan duplicados y se conserva la diversidad; las preferencias explícitas
//    (me gusta / no me gusta) entran al objetivo como distancia en la huella sonora.
//  - Todo es determinista con la semilla y la secuencia de acciones.
// ===========================================================================
namespace Forge
{
    struct Neuron
    {
        juce::String id;                     // estable
        juce::StringArray parents;           // ids de los padres (vacío = semilla de la referencia)
        int generation = 0;
        int action = 0;                      // número de la acción que la creó (deshacer/rehacer)
        SynthPatch patch;
        juce::String source;                 // estimacion / indice / exploracion / mutacion / cruce
        float similarity = -1.0f;            // fidelidad medida contra la referencia (-1 = sin medir aún)
        float novelty = -1.0f;               // 0 = igual a un patch de la base, 1 = lejos de todo lo conocido
        double objective = 0.0;              // lo que minimizó la búsqueda
        bool provisional = false;            // todavía optimizándose
        int feedback = 0;                    // +1 me gusta, -1 no me gusta
        bool hasFp = false;
        FingerprintVec fp {};
        Fidelity fidelity;
    };

    class Brain : private juce::Thread
    {
    public:
        Brain() : juce::Thread ("Nebula Brain") {}
        ~Brain() override { stop(); }

        std::function<void()> onChange;      // desde el hilo de fondo (avisar a la interfaz)

        // ---- acciones (cada una corre en el hilo de fondo; no bloquean ni al audio ni a la interfaz) ----
        bool startFromReference (const juce::AudioBuffer<float>& audio, double sr, const juce::String& name, juce::int64 seed, float distance)
        {
            stop();
            {
                const juce::ScopedLock sl (lock);
                pending = Job::Seed;
                refAudio.makeCopyOf (audio); refSr = sr; refName = name;
                baseSeed = seed; distanceValue = juce::jlimit (0.0f, 1.0f, distance);
                neurons.clear(); redo.clear(); actionCounter = 0; generationCounter = 0; nextId = 1;
                features = {};
                ++version;
            }
            startThread (juce::Thread::Priority::low);
            return true;
        }

        // Nueva generación desde los padres elegidos (1 = mutación; 2 o más = cruces y mutaciones)
        bool evolve (const juce::StringArray& parentIds, int children = 8)
        {
            if (isThreadRunning()) return false;
            {
                const juce::ScopedLock sl (lock);
                if (features.lengthS <= 0.0) return false;
                juce::StringArray valid;
                for (auto& id : parentIds) if (find (id) != nullptr) valid.add (id);
                if (valid.isEmpty()) return false;
                pending = Job::Evolve; jobParents = valid; jobChildren = juce::jlimit (2, 24, children);
            }
            startThread (juce::Thread::Priority::low);
            return true;
        }

        void cancel()           { signalThreadShouldExit(); const juce::ScopedLock sl (lock); if (searcher != nullptr) searcher->cancel = true; }
        void stop()             { cancel(); stopThread (10000); }
        bool isWorking() const  { return isThreadRunning(); }

        // ---- preferencias (sesión) ----
        void setFeedback (const juce::String& id, int value)
        {
            const juce::ScopedLock sl (lock);
            if (auto* n = find (id)) { n->feedback = juce::jlimit (-1, 1, value); ++version; }
        }
        void resetPreferences()
        {
            const juce::ScopedLock sl (lock);
            for (auto& n : neurons) n.feedback = 0;
            ++version;
        }

        // ---- distancia a la referencia ----
        void setDistance (float d) { const juce::ScopedLock sl (lock); distanceValue = juce::jlimit (0.0f, 1.0f, d); ++version; }
        float distance() const     { const juce::ScopedLock sl (lock); return distanceValue; }

        // ---- candados (los respeta la evolución) ----
        void setLocks (const ModuleLocks& l) { const juce::ScopedLock sl (lock); locks = l; }

        // ---- deshacer / rehacer (por acción: una generación entera) ----
        bool undo()
        {
            if (isThreadRunning()) return false;
            const juce::ScopedLock sl (lock);
            int last = 0; for (auto& n : neurons) last = juce::jmax (last, n.action);
            if (last <= 1) return false;            // la generación de la referencia no se deshace
            std::vector<Neuron> removed;
            for (auto it = neurons.begin(); it != neurons.end();)
                if (it->action == last) { removed.push_back (*it); it = neurons.erase (it); } else ++it;
            redo.push_back (std::move (removed));
            ++version;
            return true;
        }
        bool redoAction()
        {
            if (isThreadRunning()) return false;
            const juce::ScopedLock sl (lock);
            if (redo.empty()) return false;
            for (auto& n : redo.back()) neurons.push_back (n);
            redo.pop_back();
            ++version;
            return true;
        }
        bool canUndo() const { const juce::ScopedLock sl (lock); for (auto& n : neurons) if (n.action > 1) return true; return false; }
        bool canRedo() const { const juce::ScopedLock sl (lock); return ! redo.empty(); }

        // ---- lectura ----
        std::vector<Neuron> snapshot() const { const juce::ScopedLock sl (lock); return neurons; }
        int changeCount() const              { const juce::ScopedLock sl (lock); return version; }
        ReferenceFeatures reference() const  { const juce::ScopedLock sl (lock); return features; }
        juce::String working() const        { const juce::ScopedLock sl (lock); return workingId; }   // neurona en optimización ahora
        juce::String status() const          { const juce::ScopedLock sl (lock); return statusText; }
        float progress() const               { const juce::ScopedLock sl (lock); return searcher != nullptr ? searcher->progress.load() : progressValue; }

        // Mismas acciones, misma semilla => mismas neuronas (para pruebas y para reproducir una sesión)
        static juce::int64 actionSeed (juce::int64 base, int action) { return base * 1000003 + action * 7919; }

    private:
        enum class Job { None, Seed, Evolve };

        Neuron* find (const juce::String& id) { for (auto& n : neurons) if (n.id == id) return &n; return nullptr; }

        void publish()
        {
            { const juce::ScopedLock sl (lock); ++version; }
            if (onChange) onChange();
        }

        // ---------- objetivo: distancia a la referencia + preferencias ----------
        // referenceLoss: pérdida rápida contra la referencia (con regularización de complejidad).
        // Con distancia d se busca una pérdida objetivo d * 4 * escala (escala = mejor pérdida de las semillas):
        // d = 0 minimiza la pérdida; d alto premia sonidos emparentados pero lejos.
        double objectiveFor (const SynthPatch& p, double referenceLoss, double scale, float d,
                             const std::vector<FingerprintVec>& liked, const std::vector<FingerprintVec>& disliked)
        {
            const double target = d * 4.0 * scale;
            double obj = std::abs (referenceLoss - target);
            if (! liked.empty() || ! disliked.empty())
            {
                const auto fp = measurePatch (fpRenderer, p, category);
                const auto w = weightsFor (category);
                double dl = 1.0e9, dd = 1.0e9;
                for (auto& f : liked) dl = juce::jmin (dl, (double) fpDistance (fp, f, w));
                for (auto& f : disliked) dd = juce::jmin (dd, (double) fpDistance (fp, f, w));
                const double s = juce::jmax (1.0e-6, (double) fpScale);
                if (! liked.empty()) obj += prefWeight * scale * (dl / s);
                if (! disliked.empty()) obj += prefWeight * scale * juce::jmax (0.0, 1.0 - dd / s);
            }
            return obj;
        }

        float noveltyOf (const FingerprintVec& fp)
        {
            if (dataset == nullptr || dataset->entries.empty()) return -1.0f;
            const auto hits = dataset->nearest (fp, weightsFor (category), 1, 0b111);
            if (hits.empty()) return -1.0f;
            const double d = hits.front().distance;
            return (float) (d / (d + juce::jmax (1.0e-6, (double) datasetNN)));   // 0.5 = tan lejos como dos patches vecinos de la base
        }

        void fillMeasures (Neuron& n)
        {
            n.fp = measurePatch (fpRenderer, n.patch, category);
            n.hasFp = true;
            n.novelty = noveltyOf (n.fp);
        }

        void run() override
        {
            Job job; juce::StringArray parentIds; int children = 8;
            {
                const juce::ScopedLock sl (lock);
                job = pending; pending = Job::None; parentIds = jobParents; children = jobChildren;
            }
            if (job == Job::Seed) runSeed();
            else if (job == Job::Evolve) runEvolve (parentIds, children);
            { const juce::ScopedLock sl (lock); workingId = {}; progressValue = 1.0f; }
            publish();
        }

        void prepareContext()
        {
            dataset = builtInDataset();
            const double f0 = features.pitched ? 440.0 * std::exp2 ((features.note - 69) / 12.0) : 0.0;
            refFp = fingerprint (features.tuned, features.sampleRate, f0, features.releaseStartS);
            int cat = (int) Category::Lead;
            if (dataset != nullptr && ! dataset->entries.empty())
            {
                (void) dataset->query (refFp, 7, 0b110, 0.0f, &cat);
                // separación típica entre vecinos de la base (escala de la novedad), con una muestra fija
                std::vector<float> nn;
                for (size_t i = 0; i < dataset->entries.size() && nn.size() < 60; i += 37)
                {
                    const auto h = dataset->nearest (dataset->entries[i].fp, weightsFor (dataset->entries[i].category), 2, 0b111);
                    if (h.size() > 1) nn.push_back (h[1].distance);
                }
                std::sort (nn.begin(), nn.end());
                datasetNN = nn.empty() ? 1.0f : nn[nn.size() / 2];
            }
            category = (Category) juce::jlimit (0, (int) Category::count - 1, cat);
        }

        void runSeed()
        {
            ReferenceFeatures f;
            juce::AudioBuffer<float> audio; double sr; juce::String name; juce::int64 seed; float d;
            {
                const juce::ScopedLock sl (lock);
                audio.makeCopyOf (refAudio); sr = refSr; name = refName; seed = baseSeed; d = distanceValue;
                statusText = "Analizando la referencia";
            }
            publish();
            f = analyse (audio, sr, name);
            if (threadShouldExit()) return;
            { const juce::ScopedLock sl (lock); features = f; actionCounter = 1; generationCounter = 0; }
            prepareContext();

            Searcher s (f);
            { const juce::ScopedLock sl (lock); searcher = &s; statusText = "Generacion 0: semillas de la referencia"; }
            SearchSettings st;
            st.seed = seed; st.dataset = dataset;
            // escala de la distancia: la pérdida de la mejor estimación (se fija al ver la primera semilla)
            double scale = -1.0;
            if (d > 0.001f)
                s.objective = [this, &scale, d] (const SynthPatch& p, double l)
                {
                    if (scale < 0.0) scale = juce::jmax (0.5, l);
                    return objectiveFor (p, l, scale, d, {}, {});
                };
            s.onCandidate = [this] (const PatchCandidate& c, bool provisional)
            {
                Neuron n;
                n.id = c.id; n.generation = 0; n.action = 1; n.patch = c.patch; n.source = c.source;
                n.provisional = provisional; n.objective = c.fidelity.loss;
                if (! provisional) { n.similarity = c.fidelity.overall; n.fidelity = c.fidelity; fillMeasures (n); }
                {
                    const juce::ScopedLock sl (lock);
                    if (auto* old = find (n.id)) { n.feedback = old->feedback; *old = n; }
                    else neurons.push_back (n);
                    workingId = provisional ? n.id : juce::String();
                }
                publish();
            };
            s.run (st);
            {
                const juce::ScopedLock sl (lock);
                searcher = nullptr;
                // las que no llegaron a terminar (cancelado) quedan marcadas; las medidas, como generación 0
                neuronsScale = scale > 0 ? scale : 1.0;
                for (auto& n : neurons) if (n.provisional) n.source << " (sin terminar)";
                int measured = 0; for (auto& n : neurons) if (! n.provisional) ++measured;
                statusText = juce::String (measured) + " neuronas de la referencia";
                // escala de la huella: distancia media de la referencia a las semillas medidas
                double sum = 0; int k = 0;
                for (auto& n : neurons) if (n.hasFp) { sum += fpDistance (refFp, n.fp, weightsFor (category)); ++k; }
                fpScale = k > 0 ? (float) (sum / k) : 1.0f;
                if (neuronsScale <= 0.0 || scale < 0.0)
                {
                    double best = 1.0e9; for (auto& n : neurons) if (! n.provisional) best = juce::jmin (best, n.fidelity.loss);
                    neuronsScale = best < 1.0e8 ? juce::jmax (0.5, best) : 1.0;
                }
            }
        }

        void runEvolve (const juce::StringArray& parentIds, int children)
        {
            std::vector<Neuron> parents, likedNeurons; std::vector<FingerprintVec> liked, disliked;
            float d; ModuleLocks lk; juce::int64 seed; int action, gen;
            ReferenceFeatures f;
            {
                const juce::ScopedLock sl (lock);
                for (auto& id : parentIds) if (auto* n = find (id)) parents.push_back (*n);
                for (auto& n : neurons) if (n.hasFp) { if (n.feedback > 0) { liked.push_back (n.fp); likedNeurons.push_back (n); } else if (n.feedback < 0) disliked.push_back (n.fp); }
                d = distanceValue; lk = locks;
                int last = 0; for (auto& n : neurons) last = juce::jmax (last, n.action);
                action = last + 1; redo.clear();
                gen = 0; for (auto& p : parents) gen = juce::jmax (gen, p.generation);
                ++gen;                                             // profundidad: un paso más que el padre más nuevo
                seed = actionSeed (baseSeed, action);
                f = features;
                statusText = "Generacion " + juce::String (gen) + ": evolucionando";
            }
            publish();
            Searcher s (f);   // sólo para medir y la pérdida rápida
            { const juce::ScopedLock sl (lock); searcher = &s; }
            juce::Random rng (seed);
            const float intensity = 0.15f + 0.5f * d;          // más lejos = mutaciones más fuertes
            const float structural = 0.1f + 0.5f * d;
            const int offspring = children * 4;                // se generan 4 veces más y se eligen
            // lo que te gusta también participa como pareja de cruce (la preferencia guía la generación,
            // no sólo la elección); queda registrado como padre
            std::vector<Neuron> mates = parents;
            for (auto& l : likedNeurons)
            {
                bool already = false; for (auto& p : parents) already = already || p.id == l.id;
                if (! already) mates.push_back (l);
            }
            struct Child { SynthPatch patch; juce::StringArray from; juce::String source; double obj = 1e9; };
            std::vector<Child> pool;
            for (int k = 0; k < offspring && ! threadShouldExit(); ++k)
            {
                Child c;
                if (mates.size() >= 2 && k % 2 == 1)
                {
                    // un padre elegido por vos con una pareja (otro padre o algo que te gusta)
                    const int a = rng.nextInt ((int) parents.size());
                    int b = rng.nextInt ((int) mates.size() - 1); if (b >= a) ++b;
                    c.patch = crossover (parents[(size_t) a].patch, mates[(size_t) b].patch, rng, lk);
                    c.patch = mutateGenome (c.patch, rng, intensity * 0.4f, lk);
                    c.from = { parents[(size_t) a].id, mates[(size_t) b].id };
                    c.source = "cruce";
                }
                else
                {
                    const auto& p = parents[(size_t) (k % (int) parents.size())];
                    c.patch = mutateGenome (p.patch, rng, intensity, lk, structural);
                    c.from = { p.id };
                    c.source = "mutacion";
                }
                // pérdida rápida contra la referencia + objetivo (distancia y preferencias)
                int cx = 0; describe (c.patch, &cx);
                const double l = s.eval.fastLoss (s.eval.render (s.renderer, c.patch, nullptr, true)) + 0.01 * cx;
                ++s.evaluations;
                c.obj = objectiveFor (c.patch, l, neuronsScale, d, liked, disliked);
                pool.push_back (std::move (c));
                s.progress = 0.6f * (float) (k + 1) / (float) offspring;
            }
            // los mejores, sin duplicados (ni entre sí ni con neuronas existentes) y con diversidad estructural
            std::stable_sort (pool.begin(), pool.end(), [] (const Child& a, const Child& b) { return a.obj < b.obj; });
            std::vector<SynthPatch> existing;
            { const juce::ScopedLock sl (lock); for (auto& n : neurons) existing.push_back (n.patch); }
            auto dist = [] (const SynthPatch& a, const SynthPatch& b)
            {
                double s2 = 0; int n = 0;
                for (auto& info : synthParams()) { s2 += std::abs (a.getNorm (info.id) - b.getNorm (info.id)); ++n; }
                return s2 / juce::jmax (1, n);
            };
            std::vector<Child> chosen; juce::StringArray keys;
            for (int pass = 0; pass < 2; ++pass)
                for (auto& c : pool)
                {
                    if ((int) chosen.size() >= children) break;
                    bool dup = false;
                    for (auto& e : existing) if (dist (c.patch, e) < 0.003) { dup = true; break; }
                    for (auto& o : chosen) if (! dup && dist (c.patch, o.patch) < 0.003) dup = true;
                    if (dup) continue;
                    const auto key = structureKey (c.patch);
                    if (pass == 0 && keys.contains (key)) continue;     // primero una por estructura
                    keys.addIfNotAlreadyThere (key);
                    chosen.push_back (c);
                    existing.push_back (c.patch);
                }
            int k = 0;
            for (auto& c : chosen)
            {
                if (threadShouldExit()) break;
                Neuron n;
                n.id = "N" + juce::String (action) + "." + juce::String (++k);   // único y estable (acción.orden)
                n.parents = c.from; n.generation = gen; n.action = action; n.source = c.source;
                n.patch = c.patch; n.patch.name = "Nebula " + n.id; n.patch.origin = "cerebro";
                n.objective = c.obj;
                auto pc = s.measure (n.patch, n.id);
                n.fidelity = pc.fidelity; n.similarity = pc.fidelity.overall;
                fillMeasures (n);
                { const juce::ScopedLock sl (lock); neurons.push_back (n); }
                s.progress = 0.6f + 0.4f * (float) k / (float) juce::jmax (1, (int) chosen.size());
                publish();
            }
            const juce::ScopedLock sl (lock);
            searcher = nullptr;
            statusText = "Generacion " + juce::String (gen) + ": " + juce::String (k) + " neuronas nuevas"
                       + (liked.empty() && disliked.empty() ? juce::String() : " (preferencias: " + juce::String ((int) liked.size()) + " me gusta, " + juce::String ((int) disliked.size()) + " no)");
        }

        mutable juce::CriticalSection lock;
        std::vector<Neuron> neurons;
        std::vector<std::vector<Neuron>> redo;
        Job pending = Job::None;
        juce::StringArray jobParents; int jobChildren = 8;
        juce::AudioBuffer<float> refAudio; double refSr = 48000.0; juce::String refName;
        juce::int64 baseSeed = 1;
        float distanceValue = 0.0f, progressValue = 0.0f;
        ModuleLocks locks;
        ReferenceFeatures features;
        FingerprintVec refFp {};
        Category category = Category::Lead;
        const Dataset* dataset = nullptr;
        float datasetNN = 1.0f, fpScale = 1.0f;
        double neuronsScale = 1.0, prefWeight = 3.0;   // peso de "me gusta"/"no me gusta" (prueba Brain)
        int version = 0, actionCounter = 0, generationCounter = 0, nextId = 1;
        juce::String workingId, statusText;
        Searcher* searcher = nullptr;
        PatchRenderer fpRenderer;
    };
}
