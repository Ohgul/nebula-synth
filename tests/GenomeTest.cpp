// ADN estructural (P4) y Cosmos/Abismo sobre la receta (P6).
//   - cada parámetro de síntesis pertenece a un módulo; el grafo refleja módulos activos, conexiones y rutas
//   - cruce por módulos completos, con candados; mutación que respeta candados y puede cambiar la estructura
//   - Cosmos/Abismo como operación pura sobre el patch: con 0 devuelve la base, es monótona en brillo,
//     reversible (también en el plugin y al guardar/abrir el proyecto) y respeta candados
//   NebulaTests Genome
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Forge/Polarity.h"
#include "bench/Corpus.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

constexpr double sr = Bench::kSr;
int failures = 0;

void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-64s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

std::unique_ptr<NebulaAudioProcessor> makeProc()
{
    std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    p->setPlayConfigDetails (0, 2, sr, 256);
    p->prepareToPlay (sr, 256);
    p->setRandomMode (true, 77);
    p->reset();
    return p;
}

bool safe (const juce::AudioBuffer<float>& b)
{
    float peak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (ch, i); if (! std::isfinite (v)) return false; peak = juce::jmax (peak, std::abs (v)); }
    return peak > 1.0e-3f && peak < 1.5f;
}

// planitud espectral (dB; 0 = ruido blanco) entre 100 Hz y 8 kHz
double flatnessDb (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto m = Bench::toMono (b);
    const int a = (int) (t0 * sr), z = juce::jmin ((int) m.size(), (int) (t1 * sr));
    constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<double> pw ((size_t) N / 2, 0.0);
    for (int s = a; s + N <= z; s += N / 2)
    {
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (float) (m[(size_t) (s + i)] * (0.5 - 0.5 * std::cos (2 * Bench::kPi * i / N)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k) pw[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    double lg = 0, ar = 0; int n = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * sr / N;
        if (f < 100.0 || f > 8000.0) continue;
        lg += std::log (pw[(size_t) k] + 1e-20); ar += pw[(size_t) k]; ++n;
    }
    return 10.0 * std::log10 (std::exp (lg / n) / (ar / n + 1e-20));
}

// después de soltar la nota (en noteOffS): tiempo hasta caer 30 dB bajo el nivel del momento de soltar
double tailS (const juce::AudioBuffer<float>& b, double noteOffS)
{
    const auto l = Bench::loudnessCurve (b, sr);
    const size_t off = (size_t) (noteOffS / 0.01);
    if (off >= l.size()) return 0.0;
    size_t i = off; while (i + 1 < l.size() && l[i] > l[off] - 30.0) ++i;
    return (double) (i - off) * 0.01;
}

// tiempo hasta llegar a 3 dB del pico de sonoridad
double attackS (const juce::AudioBuffer<float>& b)
{
    const auto l = Bench::loudnessCurve (b, sr);
    double pk = -200; for (auto v : l) pk = juce::jmax (pk, v);
    for (size_t i = 0; i < l.size(); ++i) if (l[i] > pk - 3.0) return (double) i * 0.01;
    return 0.0;
}

Forge::SynthPatch leadPatch()
{
    auto p = Forge::SynthPatch::init();
    p.set (ParamID::osc1Wave, (float) Wave::Saw);
    p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Pulse); p.set (ParamID::oscMix, 0.35f); p.set (ParamID::osc2Fine, 6.0f);
    p.set (ParamID::cutoff, 1800.0f); p.set (ParamID::resonance, 0.2f); p.set (ParamID::filterEnv, 0.4f);
    p.set (ParamID::fltD, 0.3f); p.set (ParamID::fltS, 0.2f);
    p.set (ParamID::ampA, 0.005f); p.set (ParamID::ampD, 0.4f); p.set (ParamID::ampS, 0.7f); p.set (ParamID::ampR, 0.25f);
    p.name = "lead base";
    return p;
}

Forge::SynthPatch bassPatch()
{
    auto p = Forge::SynthPatch::init();
    p.set (ParamID::osc1Wave, (float) Wave::Sine);
    p.set (ParamID::cutoff, 20000.0f);
    p.setMod (ModSrc::Env2, ModDst::Pitch, 0.5f); p.set (ParamID::fltA, 0.001f); p.set (ParamID::fltD, 0.08f); p.set (ParamID::fltS, 0.0f);
    p.set (ParamID::subOn, 1); p.set (ParamID::subLevel, 0.3f);
    p.set (ParamID::driveOn, 1); p.set (ParamID::drive, 0.2f);
    p.set (ParamID::ampA, 0.001f); p.set (ParamID::ampD, 1.2f); p.set (ParamID::ampS, 0.0f); p.set (ParamID::ampR, 0.3f);
    p.name = "bajo base";
    return p;
}

bool moduleFrom (const Forge::SynthPatch& child, const Forge::SynthPatch& src, Forge::Module m)
{
    for (auto& info : Forge::synthParams())
        if (Forge::moduleOf (info.id) == (int) m && std::abs (child.getNorm (info.id) - src.getNorm (info.id)) > 1.0e-6f) return false;
    return true;
}

} // namespace

int NEBULA_TEST_ENTRY (int, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;
    Forge::PatchRenderer r;

    // ================= P4: ADN estructural =================
    {
        int orphans = 0, unknown = 0;
        for (auto& info : Forge::synthParams()) if (Forge::moduleOf (info.id) < 0) { ++orphans; std::printf ("   sin modulo: %s\n", info.id.toRawUTF8()); }
        for (auto& mi : Forge::moduleTable()) for (auto* id : mi.params) if (Forge::findParam (id) == nullptr) ++unknown;
        check ("Cada parametro de sintesis pertenece a un modulo", orphans == 0 && unknown == 0,
               juce::String ((int) Forge::synthParams().size()) + " parametros, " + juce::String (Forge::moduleCount) + " modulos");
    }
    {
        const auto g = Forge::PatchGraph::from (Forge::SynthPatch::init());
        auto has = [&] (const Forge::PatchGraph& gr, const char* a, const char* b, bool mod)
        { for (auto& e : gr.edges) if (e.from == a && e.to == b && e.modulation == mod) return true; return false; };
        check ("Grafo del patch inicial: osc1 -> filtro -> amp -> salida", has (g, "osc1", "f1", false) && has (g, "f1", "amp", false) && has (g, "amp", "out", false)
               && ! g.nodes[(size_t) Forge::Module::Osc2].active, juce::String (g.activeNodes()) + " modulos activos");
        auto b = bassPatch();
        const auto gb = Forge::PatchGraph::from (b);
        check ("Grafo del bajo: sub y drive en el camino, Env2 ~> osc1 (ruta de tono)", has (gb, "sub", "f1", false) && has (gb, "f1", "drive", false) && has (gb, "drive", "amp", false)
               && has (gb, "env2", "osc1", true));
        // binding: la cantidad de la arista es el valor del parámetro que la controla
        float before = 0, after = 0;
        for (auto& e : gb.edges) if (e.param == matrixID (ModSrc::Env2, ModDst::Pitch)) before = e.amount;
        b.setMod (ModSrc::Env2, ModDst::Pitch, 0.8f);
        for (auto& e : Forge::PatchGraph::from (b).edges) if (e.param == matrixID (ModSrc::Env2, ModDst::Pitch)) after = e.amount;
        check ("Cada ruta esta ligada a su parametro (binding)", std::abs (before - 0.5f) < 1e-4f && std::abs (after - 0.8f) < 1e-4f);
        std::printf ("%s\n", Forge::PatchGraph::from (b).toText().toRawUTF8());
    }
    {
        const auto a = leadPatch(), b = bassPatch();
        bool whole = true; int fromB = 0;
        for (int s = 1; s <= 20; ++s)
        {
            juce::Random rng (s);
            const auto c = Forge::crossover (a, b, rng);
            for (auto& mi : Forge::moduleTable())
            {
                const bool fa = moduleFrom (c, a, mi.m), fb = moduleFrom (c, b, mi.m);
                if (! fa && ! fb) whole = false;
                if (fb && ! fa) ++fromB;
            }
        }
        check ("Cruce: cada modulo viene entero de un padre", whole && fromB > 0, juce::String (fromB) + " modulos tomados de B en 20 cruces");
        Forge::ModuleLocks locks; locks.set (Forge::Module::Filter1, true); locks.set (Forge::Module::Amp, true);
        bool respected = true;
        for (int s = 1; s <= 20; ++s)
        {
            juce::Random rng (s);
            const auto c = Forge::crossover (a, b, rng, locks, 1.0f);
            respected = respected && moduleFrom (c, a, Forge::Module::Filter1) && moduleFrom (c, a, Forge::Module::Amp) && moduleFrom (c, b, Forge::Module::Osc1);
        }
        check ("Cruce: los modulos con candado vienen del padre A", respected);
        juce::Random r1 (7), r2 (7);
        check ("Cruce determinista con la semilla", Forge::crossover (a, b, r1).sameSound (Forge::crossover (a, b, r2)));
        juce::Random r3 (3), r4 (3);
        check ("Cruce extremo: pB = 0 da A, pB = 1 da B", Forge::crossover (a, b, r3, {}, 0.0f).sameSound (a) && Forge::crossover (a, b, r4, {}, 1.0f).sameSound (b));
        juce::Random r5 (11);
        check ("El hijo de un cruce suena (finito, sin picos)", safe (r.render (Forge::crossover (a, b, r5), 48, 0.8, 1.2, sr)));
    }
    {
        const auto base = leadPatch();
        Forge::ModuleLocks locks; locks.set (Forge::Module::Filter1, true); locks.set (Forge::Module::Osc2, true);
        bool kept = true, changed = false;
        juce::StringArray keys;
        for (int s = 1; s <= 40; ++s)
        {
            juce::Random rng (s);
            const auto m = Forge::mutateGenome (base, rng, 0.35f, locks, 0.5f);
            kept = kept && moduleFrom (m, base, Forge::Module::Filter1) && moduleFrom (m, base, Forge::Module::Osc2);
            changed = changed || ! moduleFrom (m, base, Forge::Module::Amp);
            keys.addIfNotAlreadyThere (Forge::structureKey (m));
        }
        check ("Mutacion: los modulos con candado no cambian", kept && changed);
        check ("Mutacion estructural: cambia que modulos usa la receta", keys.size() >= 3, juce::String (keys.size()) + " estructuras en 40 mutaciones");
        juce::Random r1 (5), r2 (5);
        check ("Mutacion determinista con la semilla", Forge::mutateGenome (base, r1, 0.3f, locks, 0.5f).sameSound (Forge::mutateGenome (base, r2, 0.3f, locks, 0.5f)));
    }
    {
        Forge::ModuleLocks l; l.set (Forge::Module::Drive, true); l.set (Forge::Module::Lfo2, true);
        check ("Candados: guardar y cargar", Forge::ModuleLocks::fromString (l.toString()).locked == l.locked && l.count() == 2, l.toString());
    }

    // ================= P6: Cosmos / Abismo sobre la receta =================
    for (auto base : { leadPatch(), bassPatch() })
    {
        const int note = base.name == "lead base" ? 60 : 36;
        check (base.name + ": cantidad 0 devuelve la base exacta", Forge::polarize (base, 0.0f).sameSound (base));
        // Abismo: más caos (planitud espectral: ruido, inarmonía, distorsión) a medida que se baja
        std::vector<double> fl;
        bool finite = true;
        for (float a : { 0.0f, -0.5f, -1.0f })
        {
            const auto audio = r.render (Forge::polarize (base, a), note, 1.0, 1.4, sr);
            finite = finite && safe (audio);
            fl.push_back (flatnessDb (audio, 0.05, 0.9));
        }
        for (float a : { 0.5f, 1.0f }) finite = finite && safe (r.render (Forge::polarize (base, a), note, 1.0, 1.4, sr));
        check (base.name + ": todas las cantidades suenan (finito, sin picos)", finite);
        check (base.name + ": Abismo agrega caos de forma monotona", fl[1] > fl[0] + 0.5 && fl[2] > fl[1] + 0.5,
               juce::String::formatted ("planitud %.1f -> %.1f -> %.1f dB", fl[0], fl[1], fl[2]));
        const auto c = r.render (Forge::polarize (base, 1.0f), note, 1.0, 1.4, sr), o = r.render (base, note, 1.0, 1.4, sr);
        check (base.name + ": Cosmos abre el ataque", attackS (c) > attackS (o) + 0.02, juce::String (attackS (o), 2) + " s -> " + juce::String (attackS (c), 2) + " s");
        check (base.name + ": Cosmos alarga la cola", tailS (c, 1.0) > tailS (o, 1.0) + 0.05, juce::String (tailS (o, 1.0), 2) + " s -> " + juce::String (tailS (c, 1.0), 2) + " s");
        Forge::ModuleLocks locks; locks.set (Forge::Module::Filter1, true);
        const auto lockedC = Forge::polarize (base, 1.0f, locks), lockedA = Forge::polarize (base, -1.0f, locks);
        check (base.name + ": con candado en el filtro, Cosmos/Abismo no lo tocan", moduleFrom (lockedC, base, Forge::Module::Filter1) && moduleFrom (lockedA, base, Forge::Module::Filter1)
               && ! lockedC.sameSound (base));
        Forge::PolarityEdit e; e.begin (base);
        for (float a : { 0.3f, -0.8f, 1.0f, -0.1f }) { e.amount = a; (void) e.result(); }
        const auto back = Forge::PolarityEdit::fromValueTree (e.toValueTree());
        check (base.name + ": edicion reversible (guardar/cargar y volver)", back.amount == e.amount && back.result().sameSound (e.result())
               && back.base.sameSound (base) && e.revert().sameSound (base));
    }

    // ================= En el plugin =================
    {
        auto p = makeProc();
        const auto base = leadPatch();
        p->applySynthPatch (base);
        p->setRecipePolarity (0.7f);
        const bool moved = ! p->currentSynthPatch().sameSound (base, 1.0e-4f);
        p->setRecipePolarity (-0.4f);
        const bool abyss = p->currentSynthPatch().sameSound (Forge::polarize (base, -0.4f), 1.0e-4f);
        // guardar y abrir el proyecto con la edición puesta
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto p2 = makeProc(); p2->setStateInformation (mb.getData(), (int) mb.getSize());
        const bool persisted = std::abs (p2->recipePolarity() + 0.4f) < 1.0e-4f && p2->recipePolarityActive();
        p2->revertRecipePolarity();
        check ("Plugin: Cosmos/Abismo mueven la receta y se revierten exactos (tambien tras abrir el proyecto)",
               moved && abyss && persisted && p2->currentSynthPatch().sameSound (base, 1.0e-4f) && ! p2->recipePolarityActive());
        // si se tocan perillas despues, la nueva base incluye ese cambio
        p->revertRecipePolarity();
        p->setRecipePolarity (0.5f);
        if (auto* prm = p->state.getParameter (ParamID::ampD)) prm->setValueNotifyingHost (prm->convertTo0to1 (1.7f));
        p->setRecipePolarity (0.0f);
        check ("Plugin: una perilla tocada despues de Cosmos no se pierde", std::abs (p->currentSynthPatch().get (ParamID::ampD) - 1.7f) < 0.01f);
        p->setForgeLock (Forge::Module::Drive, true);
        juce::MemoryBlock mb2; p->getStateInformation (mb2);
        auto p3 = makeProc(); p3->setStateInformation (mb2.getData(), (int) mb2.getSize());
        check ("Plugin: los candados se guardan con el proyecto", p3->forgeLocks().isLocked (Forge::Module::Drive) && ! p3->forgeLocks().isLocked (Forge::Module::Amp));
    }
    {
        // cruce de dos candidatos de FORGE, con candado en el amplificador
        auto p = makeProc();
        const auto s = Bench::lead (Bench::Variant {});
        p->setSample (SampleTools::fromBuffer (s.audio, sr, s.name));
        p->setRegion (0.0f, 1.0f);
        check ("Cruce sin candidatos se rechaza", ! p->forgeCrossover (0, 1, 1));
        p->startForge (1, 8);
        while (p->isForging()) juce::Thread::sleep (50);
        p->collectForgeResult();
        const auto parents = p->forgeCandidates();
        p->setForgeLock (Forge::Module::Amp, true);
        const bool started = parents.size() >= 2 && p->forgeCrossover (0, 1, 3);
        while (p->isForging()) juce::Thread::sleep (50);
        p->collectForgeResult();
        const auto kids = p->forgeCandidates();
        bool ok = started && kids.size() >= 8;
        int fromSecond = 0;
        for (auto& k : kids)
        {
            ok = ok && moduleFrom (k.patch, parents[0].patch, Forge::Module::Amp) && k.parentId.contains (" x ") && k.source == "cruce";
            for (auto& mi : Forge::moduleTable())
                if (! moduleFrom (parents[0].patch, parents[1].patch, mi.m) && moduleFrom (k.patch, parents[1].patch, mi.m) && ! moduleFrom (k.patch, parents[0].patch, mi.m)) { ++fromSecond; break; }
        }
        check ("FORGE: cruzar dos candidatos da 8 hijos medidos (candado respetado)", ok && fromSecond > 0,
               juce::String ((int) kids.size()) + " hijos, " + juce::String (fromSecond) + " con modulos del segundo padre; mejor " + juce::String (kids.empty() ? 0.0f : kids[0].fidelity.overall * 100, 0) + " %");
        check ("FORGE: la generacion de los padres queda guardada", ! p->forgeGenerations().empty());
    }

    std::printf ("\n%s\n", failures == 0 ? "TODO OK" : (juce::String (failures) + " FALLOS").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
