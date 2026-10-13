// Cerebro vivo (DISCOVER, NEBULA-CORE-050): resultados progresivos reales, parentesco, distancia a la
// referencia, preferencias, candados, sin duplicados, deshacer/rehacer y determinismo.
//   NebulaTests Brain
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/Forge/Brain.h"
#include "bench/Corpus.h"

namespace {

constexpr double sr = Bench::kSr;
int failures = 0;

void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-66s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

void wait (Forge::Brain& b, std::vector<int>* measuredOverTime = nullptr, std::vector<int>* provisionalOverTime = nullptr)
{
    juce::Thread::sleep (30);
    while (b.isWorking())
    {
        if (measuredOverTime != nullptr)
        {
            int m = 0, p = 0;
            for (auto& n : b.snapshot()) (n.provisional ? p : m)++;
            measuredOverTime->push_back (m);
            if (provisionalOverTime) provisionalOverTime->push_back (p);
        }
        juce::Thread::sleep (40);
    }
}

double paramDist (const Forge::SynthPatch& a, const Forge::SynthPatch& b)
{
    double s = 0; int n = 0;
    for (auto& info : Forge::synthParams()) { s += std::abs (a.getNorm (info.id) - b.getNorm (info.id)); ++n; }
    return s / n;
}

std::vector<Forge::Neuron> generation (const std::vector<Forge::Neuron>& all, int g)
{
    std::vector<Forge::Neuron> out; for (auto& n : all) if (n.generation == g) out.push_back (n); return out;
}

} // namespace

int NEBULA_TEST_ENTRY (int, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto lead = Bench::lead (Bench::Variant {});

    Forge::Brain brain;
    std::vector<int> measured, provisional;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    check ("Arranca desde la referencia", brain.startFromReference (lead.audio, sr, "lead", 1, 0.0f));
    wait (brain, &measured, &provisional);
    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    auto all = brain.snapshot();
    auto g0 = generation (all, 0);
    {
        // progresivo: el número de neuronas medidas crece en varios pasos mientras se busca, y hubo neuronas
        // provisionales (en optimización) antes de medirse
        std::vector<int> steps; for (int m : measured) if (steps.empty() || m != steps.back()) steps.push_back (m);
        int maxProv = 0; for (int p : provisional) maxProv = juce::jmax (maxProv, p);
        check ("Generacion 0 llega de a poco (resultados progresivos)", steps.size() >= 4 && maxProv >= 1,
               juce::String ((int) steps.size()) + " escalones, " + juce::String ((int) g0.size()) + " neuronas en " + juce::String (secs, 1) + " s");
    }
    {
        bool allMeasured = ! g0.empty(), ids = true;
        juce::StringArray seen;
        for (auto& n : g0) { allMeasured = allMeasured && ! n.provisional && n.similarity >= 0.0f && n.hasFp; ids = ids && ! seen.contains (n.id); seen.add (n.id); }
        check ("Al terminar, todas medidas y con id unico", allMeasured && ids);
        // la similitud mostrada es la medida real: se vuelve a medir una neurona por separado
        Forge::Searcher s (brain.reference());
        const auto& n = g0.front();
        const auto again = s.measure (n.patch, n.id);
        check ("La similitud de una neurona es la medida real (se repite al medir aparte)", std::abs (again.fidelity.overall - n.similarity) < 1.0e-4f,
               juce::String (n.similarity * 100, 1) + " % / " + juce::String (again.fidelity.overall * 100, 1) + " %");
        bool novelty = true; for (auto& x : g0) novelty = novelty && x.novelty >= 0.0f && x.novelty <= 1.0f;
        check ("Novedad entre 0 y 1 en todas", novelty);
        Forge::PatchRenderer r; bool finite = true;
        for (auto& x : g0)
        {
            const auto a = r.render (x.patch, 60, 0.8, 1.0, sr);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < a.getNumSamples(); ++i) finite = finite && std::isfinite (a.getSample (ch, i));
        }
        check ("Cada neurona es un patch que suena (finito)", finite);
    }

    // ---- evolucionar desde las dos mejores ----
    std::sort (g0.begin(), g0.end(), [] (auto& a, auto& b) { return a.similarity > b.similarity; });
    const juce::StringArray parents { g0[0].id, g0[1].id };
    Forge::ModuleLocks locks; locks.set (Forge::Module::Amp, true);
    brain.setLocks (locks);
    check ("Evolucionar arranca", brain.evolve (parents, 8));
    wait (brain);
    all = brain.snapshot();
    auto g1 = generation (all, 1);
    {
        bool family = g1.size() >= 6, ampKept = true, noDup = true, cross = false;
        for (auto& n : g1)
        {
            for (auto& p : n.parents) family = family && parents.contains (p);   // sin "me gusta", sólo los elegidos
            const auto& par = n.parents[0] == g0[0].id ? g0[0] : g0[1];
            for (auto& info : Forge::synthParams())
                if (Forge::moduleOf (info.id) == (int) Forge::Module::Amp && n.parents.size() == 1 && std::abs (n.patch.getNorm (info.id) - par.patch.getNorm (info.id)) > 1e-6f) ampKept = false;
            if (n.parents.size() == 2) cross = true;
        }
        for (size_t i = 0; i < all.size(); ++i)
            for (size_t j = i + 1; j < all.size(); ++j) if (paramDist (all[i].patch, all[j].patch) < 0.003) noDup = false;
        check ("Generacion 1: hijos de los padres elegidos (mutacion y cruce)", family && cross, juce::String ((int) g1.size()) + " neuronas");
        check ("Candado (amplificador) respetado al mutar", ampKept);
        check ("Sin duplicados en toda la red", noDup);
    }

    // ---- deshacer / rehacer ----
    {
        const auto before = brain.snapshot().size();
        const bool u = brain.undo();
        const auto afterUndo = brain.snapshot().size();
        const bool r = brain.redoAction();
        const auto back = brain.snapshot();
        bool same = back.size() == before;
        for (auto& n : g1) { bool found = false; for (auto& b : back) if (b.id == n.id && b.patch.sameSound (n.patch)) found = true; same = same && found; }
        check ("Deshacer quita la generacion y rehacer la devuelve igual", u && r && afterUndo == before - g1.size() && same);
    }

    // ---- distancia a la referencia: lejos = menos parecido y mas cambio ----
    double simNear = 0, simFar = 0, moveNear = 0, moveFar = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        brain.undo();                       // se vuelve a evolucionar desde el mismo punto (misma semilla de acción)
        brain.setDistance (pass == 0 ? 0.0f : 1.0f);
        brain.evolve ({ g0[0].id }, 8); wait (brain);
        auto gen = brain.snapshot(); int gmax = 0; for (auto& n : gen) gmax = juce::jmax (gmax, n.generation);
        double sim = 0, mv = 0; int k = 0;
        for (auto& n : generation (gen, gmax)) { sim += n.similarity; mv += paramDist (n.patch, g0[0].patch); ++k; }
        (pass == 0 ? simNear : simFar) = sim / juce::jmax (1, k);
        (pass == 0 ? moveNear : moveFar) = mv / juce::jmax (1, k);
    }
    check ("Distancia lejana: menos parecido y mas cambio que cercana", simFar < simNear && moveFar > moveNear,
           juce::String::formatted ("similitud %.0f%% -> %.0f%%, cambio %.3f -> %.3f", simNear * 100, simFar * 100, moveNear, moveFar));

    // ---- preferencias: los hijos se acercan a lo que gusta ----
    {
        brain.undo(); brain.setDistance (0.5f);
        brain.evolve ({ g0[0].id, g0[1].id }, 8); wait (brain);
        auto plain = brain.snapshot(); int gmax = 0; for (auto& n : plain) gmax = juce::jmax (gmax, n.generation);
        const auto noPref = generation (plain, gmax);
        // me gusta: la neurona de la generación 0 más lejana a la mejor (una dirección clara)
        const Forge::Neuron* likedN = nullptr; double far = -1;
        for (auto& n : g0) { const double dd = Forge::fpDistance (n.fp, g0[0].fp, Forge::weightsFor (Forge::Category::Lead)); if (n.id != g0[0].id && dd > far) { far = dd; likedN = &n; } }
        brain.undo();
        brain.setFeedback (likedN->id, +1);
        brain.evolve ({ g0[0].id, g0[1].id }, 8); wait (brain);
        auto withP = brain.snapshot(); gmax = 0; for (auto& n : withP) gmax = juce::jmax (gmax, n.generation);
        const auto pref = generation (withP, gmax);
        std::printf ("   %s\n", brain.status().toRawUTF8());
        auto meanTo = [&] (const std::vector<Forge::Neuron>& v) { double s = 0; for (auto& n : v) s += Forge::fpDistance (n.fp, likedN->fp, Forge::weightsFor (Forge::Category::Lead)); return s / juce::jmax ((size_t) 1, v.size()); };
        check ("Me gusta: la generacion se acerca a la neurona elegida", meanTo (pref) < meanTo (noPref),
               juce::String (meanTo (noPref), 3) + " -> " + juce::String (meanTo (pref), 3) + " (distancia de huella)");
        brain.resetPreferences();
        bool cleared = true; for (auto& n : brain.snapshot()) cleared = cleared && n.feedback == 0;
        check ("Reiniciar preferencias", cleared);
    }

    // ---- determinismo: misma semilla y mismas acciones = mismas neuronas ----
    {
        Forge::Brain b2;
        b2.startFromReference (lead.audio, sr, "lead", 1, 0.0f); wait (b2);
        b2.setLocks (locks);
        b2.evolve (parents, 8); wait (b2);
        Forge::Brain b3;
        b3.startFromReference (lead.audio, sr, "lead", 1, 0.0f); wait (b3);
        b3.setLocks (locks);
        b3.evolve (parents, 8); wait (b3);
        const auto x = b2.snapshot(), y = b3.snapshot();
        bool same = x.size() == y.size();
        for (size_t i = 0; same && i < x.size(); ++i) same = x[i].id == y[i].id && x[i].patch.sameSound (y[i].patch) && x[i].parents == y[i].parents;
        check ("Determinista con la semilla (misma red completa)", same, juce::String ((int) x.size()) + " neuronas");
    }

    std::printf ("\n%s\n", failures == 0 ? "TODO OK" : (juce::String (failures) + " FALLOS").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
