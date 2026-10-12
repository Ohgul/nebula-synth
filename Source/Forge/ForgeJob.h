#pragma once
#include <optional>
#include "ForgeSearch.h"

// ===========================================================================
// FORGE · trabajo en segundo plano: analiza la referencia y busca candidatos en su propio hilo (nunca en el
// de audio ni en el de la interfaz). Se puede cancelar; el resultado queda listo para leerse bajo candado.
// ===========================================================================
namespace Forge
{
    class Job : private juce::Thread
    {
    public:
        Job() : juce::Thread ("Nebula FORGE") {}
        ~Job() override { stop(); }

        std::function<void()> onFinished;          // se llama desde el hilo del trabajo

        // Busca recetas para "audio". "parent" (opcional): en vez de buscar desde cero, MUTA ese patch
        // (nueva generación de descendientes evaluados contra la misma referencia).
        void start (const juce::AudioBuffer<float>& audio, double sr, const juce::String& name, SearchSettings settings,
                    std::optional<PatchCandidate> parent = std::nullopt)
        {
            stop();
            input.makeCopyOf (audio);
            inputSr = sr; inputName = name; st = settings; parentCandidate = std::move (parent);
            progress = 0.0f; finished = false;
            startThread (juce::Thread::Priority::low);
        }
        void cancel()           { signalThreadShouldExit(); const juce::ScopedLock sl (lock); if (searcher != nullptr) searcher->cancel = true; }
        float currentProgress() const { const juce::ScopedLock sl (lock); return searcher != nullptr ? searcher->progress.load() : progress.load(); }
        void stop()             { cancel(); stopThread (8000); }
        bool isRunning() const  { return isThreadRunning(); }

        std::atomic<float> progress { 0.0f };
        std::atomic<bool> finished { false };

        struct Result
        {
            ReferenceFeatures features;
            std::vector<PatchCandidate> candidates;
            int evaluations = 0;
            double seconds = 0.0;
            bool cancelled = false;
            int generation = 0;
        };
        Result result() const { const juce::ScopedLock sl (lock); return res; }

    private:
        void run() override
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            Result r;
            r.features = analyse (input, inputSr, inputName);
            if (threadShouldExit()) return;
            {
                Searcher s (r.features);
                { const juce::ScopedLock sl (lock); searcher = &s; }
                if (parentCandidate)
                {
                    // nueva generación: descendientes del elegido, evaluados contra la referencia
                    juce::Random rng (st.seed);
                    for (int i = 0; i < st.candidates && ! threadShouldExit(); ++i)
                    {
                        auto child = Searcher::mutate (parentCandidate->patch, rng, 0.35f);
                        child.seed = st.seed;
                        auto c = s.measure (child, parentCandidate->id + "." + juce::String (i + 1));
                        c.generation = parentCandidate->generation + 1;
                        c.parentId = parentCandidate->id;
                        c.patch.architecture = parentCandidate->patch.architecture;
                        r.candidates.push_back (c);
                        s.progress = (float) (i + 1) / (float) st.candidates;
                    }
                    r.generation = parentCandidate->generation + 1;
                }
                else
                    r.candidates = s.run (st);
                r.evaluations = s.evaluations;
                r.cancelled = s.cancel.load() || threadShouldExit();
                const juce::ScopedLock sl (lock); searcher = nullptr;
            }
            r.seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            { const juce::ScopedLock sl (lock); res = std::move (r); }
            progress = 1.0f;
            finished = true;
            if (onFinished) onFinished();
        }

        juce::AudioBuffer<float> input;
        double inputSr = 48000.0;
        juce::String inputName;
        SearchSettings st;
        std::optional<PatchCandidate> parentCandidate;
        juce::CriticalSection lock;
        Searcher* searcher = nullptr;
        Result res;
    };
}
