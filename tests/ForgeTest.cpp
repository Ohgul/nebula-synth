// FORGE (síntesis inversa): criterios de aceptación de la primera etapa.
// Un 808 y un lead de referencia -> 8 recetas distintas del motor Synth -> elegir una -> suena sin el sample,
// se guarda y se recupera igual, es determinista, sus genes de tono, envolvente, filtro y FM cambian el audio
// de forma medible, no hace clics ni valores no finitos, y se puede mutar a una nueva generación.
//   NebulaTests Forge            pruebas
//   NebulaTests Forge tiempo     costo de análisis, render y comparación
//   NebulaTests Forge comparar   diagnóstico: el plugin contra el renderizador offline
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Forge/ForgeSearch.h"
#include "bench/Corpus.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

constexpr double sr = Bench::kSr;
constexpr int block = 256;
int failures = 0;

void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-60s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

std::unique_ptr<NebulaAudioProcessor> makeProc()
{
    std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    p->setPlayConfigDetails (0, 2, sr, block);
    p->prepareToPlay (sr, block);
    p->setRandomMode (true, 77);
    p->reset();
    return p;
}

juce::AudioBuffer<float> play (NebulaAudioProcessor& p, int note, double hold, double total)
{
    p.reset();
    const int n = (int) (total * sr);
    juce::AudioBuffer<float> out (2, n), buf (2, block);
    for (int pos = 0; pos < n; pos += block)
    {
        const int k = juce::jmin (block, n - pos);
        buf.setSize (2, k, false, false, true); buf.clear();
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        const int off = (int) (hold * sr);
        if (off >= pos && off < pos + k) midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, k);
    }
    return out;
}

double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double e = 0, d = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
        { e += (double) a.getSample (ch, i) * a.getSample (ch, i); const double x = a.getSample (ch, i) - b.getSample (ch, i); d += x * x; }
    return d <= 0 ? -400.0 : 10.0 * std::log10 (d / juce::jmax (1e-30, e));
}

bool safe (const juce::AudioBuffer<float>& b, float& peak)
{
    peak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (ch, i); if (! std::isfinite (v)) return false; peak = juce::jmax (peak, std::abs (v)); }
    return peak > 1.0e-3f && peak < 1.5f;
}

std::vector<double> seg (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto m = Bench::toMono (b);
    const int a = juce::jlimit (0, (int) m.size(), (int) (t0 * sr)), z = juce::jlimit (a, (int) m.size(), (int) (t1 * sr));
    return { m.begin() + a, m.begin() + z };
}
double medianHz (const juce::AudioBuffer<float>& b, double t0, double t1, double fmin)
{
    const auto tr = Bench::yinTrack (seg (b, t0, t1), sr, 0.01, fmin);
    std::vector<double> v; for (auto f : tr) if (f > 0) v.push_back (f);
    return v.empty() ? 0.0 : Bench::median (v);
}
double centroidBand (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto m = Bench::melSpectrogram (seg (b, t0, t1), sr, 32);
    std::vector<double> c; for (auto v : m.centroid) if (std::isfinite (v)) c.push_back (v);
    return c.empty() ? 0.0 : Bench::median (c);
}
double decayTo20 (const juce::AudioBuffer<float>& b)
{
    const auto l = Bench::loudnessCurve (b, sr);
    size_t pk = 0; for (size_t i = 0; i < l.size(); ++i) if (l[i] > l[pk]) pk = i;
    size_t i = pk; while (i + 1 < l.size() && l[i] > l[pk] - 20.0) ++i;
    return (double) (i - pk) * 0.01;
}
// Click: salto máximo de muestra a muestra respecto al nivel RMS (en dB)
double jumpDb (const juce::AudioBuffer<float>& b)
{
    double mx = 0, e = 0;
    for (int i = 1; i < b.getNumSamples(); ++i) { const double d = std::abs (b.getSample (0, i) - b.getSample (0, i - 1)); mx = juce::jmax (mx, d); e += (double) b.getSample (0, i) * b.getSample (0, i); }
    return 20.0 * std::log10 (mx / juce::jmax (1e-12, std::sqrt (e / b.getNumSamples())));
}

void waitForge (NebulaAudioProcessor& p)
{
    while (p.isForging()) juce::Thread::sleep (50);
    p.collectForgeResult();
}

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const Bench::Variant v {};
    // FORGE 2.0: las nuevas semillas FM deben existir, sonar y cubrir
    // varias relaciones inarmónicas sin introducir muestras no finitas.
    {
        const auto arch = Forge::leadArchitectures();
        const auto ref = Forge::analyse (Bench::lead (v).audio, sr, "fm-architectures");
        Forge::PatchRenderer renderer;
        int fmCount = 0;
        bool allFinite = true;
        for (const auto& a : arch)
            if (a.key == "L9" || a.key == "L10" || a.key == "L11" || a.key == "L12")
            {
                ++fmCount;
                const auto p = a.estimate (ref);
                const auto audio = renderer.render (p, 69, 0.4, 0.8, sr);
                float peak = 0.0f;
                allFinite = allFinite && p.get (ParamID::fmAmount) > 0.0f
                    && safe (audio, peak);
            }
        check ("FORGE 2.0: cuatro arquitecturas FM audibles", fmCount == 4 && allFinite,
               juce::String (fmCount) + " semillas");
    }
    // FORGE 2.0: una cola cortada debe perder frente a una referencia idéntica.
    // Evalúa el mismo audio con y sin los últimos 300 ms: protege el término
    // temporal añadido a fastLoss sin depender de la búsqueda estocástica.
    {
        auto pad = Bench::lead (v);
        auto ref = Forge::analyse (pad.audio, sr, "tail-regression");
        Forge::Evaluator evaluator (ref);
        const auto& original = ref.tuned;
        juce::AudioBuffer<float> truncated (original);
        const int cut = juce::jmax (0, truncated.getNumSamples() - (int) (0.3 * sr));
        for (int ch = 0; ch < truncated.getNumChannels(); ++ch)
            truncated.clear (ch, cut, truncated.getNumSamples() - cut);
        const double intactLoss = evaluator.fastLoss (original);
        const double truncatedLoss = evaluator.fastLoss (truncated);
        check ("FORGE 2.0: penaliza recorte de cola", std::isfinite (intactLoss)
               && std::isfinite (truncatedLoss) && truncatedLoss > intactLoss,
               juce::String (intactLoss, 3) + " -> " + juce::String (truncatedLoss, 3));
    }

    if (argc > 1 && juce::String (argv[1]) == "tiempo")
    {
        auto s = Bench::bass808 (0, v);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto f = Forge::analyse (s.audio, sr, s.name);
        std::printf ("analisis %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
        Forge::Evaluator ev (f); Forge::PatchRenderer r;
        auto p = Forge::bassArchitectures()[0].estimate (f);
        t0 = juce::Time::getMillisecondCounterHiRes();
        auto a = ev.render (r, p);
        std::printf ("render %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
        t0 = juce::Time::getMillisecondCounterHiRes();
        auto sc = ev.score (a);
        std::printf ("comparacion completa %.0f ms (perdida %.2f)\n", juce::Time::getMillisecondCounterHiRes() - t0, sc.loss);
        t0 = juce::Time::getMillisecondCounterHiRes();
        const double fl = ev.fastLoss (ev.render (r, p, nullptr, true));
        std::printf ("render + perdida rapida %.0f ms (perdida %.2f)\n", juce::Time::getMillisecondCounterHiRes() - t0, fl);
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "comparar")
    {
        auto pp = makeProc();
        auto q = Forge::SynthPatch::init();
        q.set (ParamID::osc1Wave, (float) Wave::Saw); q.set (ParamID::cutoff, 300.0f);
        pp->applySynthPatch (q);
        const auto a = play (*pp, 69, 1.0, 1.2);
        Forge::PatchRenderer r;
        const auto b = r.render (q, 69, 1.0, 1.2, sr);
        std::printf ("plugin: centroide %.2f rms %.4f | offline: centroide %.2f rms %.4f\n", centroidBand (a, 0.1, 0.8), a.getRMSLevel (0, 0, a.getNumSamples()),
                     centroidBand (b, 0.1, 0.8), b.getRMSLevel (0, 0, b.getNumSamples()));
        {
            auto plain = Forge::SynthPatch::init();
            pp->applySynthPatch (plain);
            const auto pa = play (*pp, 69, 1.0, 1.2), pb = r.render (plain, 69, 1.0, 1.2, sr);
            auto amp = [] (const juce::AudioBuffer<float>& b, double hz)
            {
                const auto x = seg (b, 0.2, 0.9); double re = 0, im = 0;
                for (size_t i = 0; i < x.size(); ++i) { const double w = 0.5 - 0.5 * std::cos (2 * Bench::kPi * i / x.size()); re += x[i] * w * std::cos (2 * Bench::kPi * hz * i / sr); im += x[i] * w * std::sin (2 * Bench::kPi * hz * i / sr); }
                return std::hypot (re, im);
            };
            for (int h : { 1, 2, 3, 5, 10, 20, 40 })
                std::printf ("  armonico %d: plugin/offline %.2f dB\n", h, 20 * std::log10 (amp (pa, 440.0 * h) / amp (pb, 440.0 * h)));
            const auto sc = Bench::compare (pb, pa, sr, true);
            std::printf ("  plano: color %.2f dB env %.2f dB, primeros 5 ms plugin %.4f offline %.4f\n", sc.colorDb, sc.envErrDb,
                         pa.getMagnitude (0, 0, 240) / pa.getMagnitude (0, 24000, 240), pb.getMagnitude (0, 0, 240) / pb.getMagnitude (0, 24000, 240));
            { const auto la = Bench::loudnessCurve (pa, sr), lb = Bench::loudnessCurve (pb, sr);
              for (size_t i = 0; i < la.size() && i < lb.size(); i += 5) if (i < 10 || i > 90) std::printf ("    t %.2f plugin %.1f offline %.1f\n", i * 0.01, la[i], lb[i]); }
            std::printf ("  L/R plugin %.4f %.4f offline %.4f %.4f\n", pa.getRMSLevel (0, 0, pa.getNumSamples()), pa.getRMSLevel (1, 0, pa.getNumSamples()), pb.getRMSLevel (0, 0, pb.getNumSamples()), pb.getRMSLevel (1, 0, pb.getNumSamples()));
        }
        {
            auto f = Forge::analyse (Bench::lead (v).audio, sr, "lead");
            for (auto& arch : Forge::leadArchitectures())
            {
                auto e = arch.estimate (f);
                pp->applySynthPatch (e);
                const auto pa = play (*pp, 69, 1.8, 2.0);
                const auto pb = r.render (e, 69, 1.8, 2.0, sr);
                const auto pc = r.render (e, 69, 1.8, 2.0, sr, 12345);
                std::printf ("  %s: color plugin vs offline %.2f dB, offline semilla A vs B %.2f dB\n", arch.label.toRawUTF8(), Bench::compare (pb, pa, sr, true).colorDb,
                             Bench::compare (pb, pc, sr, true).colorDb);
            }
            auto e = Forge::leadArchitectures()[0].estimate (f);
            e.set (ParamID::lfoPitch, 0.0f); e.set (ParamID::filterEnv, 0.0f);
            for (auto* id : { ParamID::lfoFade, ParamID::resonance, ParamID::ampA, ParamID::ampR, ParamID::fltA, ParamID::cutoff })
            {
                auto e2 = e; e2.set (id, juce::String (id) == ParamID::cutoff ? 20000.0f : 0.0f);
                pp->applySynthPatch (e2);
                std::printf ("  sin %s: %.2f dB\n", id, Bench::compare (r.render (e2, 69, 1.8, 2.0, sr), play (*pp, 69, 1.8, 2.0), sr, true).colorDb);
            }
        }
        for (auto& info : Forge::synthParams())
        {
            const float pv = pp->state.getRawParameterValue (info.id)->load();
            if (std::abs (pv - q.get (info.id)) > 1e-4f) std::printf ("  difiere %s: plugin %.4f patch %.4f\n", info.id.toRawUTF8(), pv, q.get (info.id));
        }
        return 0;
    }

    // ---- Comparación de optimizadores con el mismo presupuesto (P3: CMA-ES contra búsqueda local) ----
    if (argc > 1 && juce::String (argv[1]) == "optimizadores")
    {
        struct S { const char* name; Bench::Sound s; };
        std::vector<S> sounds { { "808", Bench::bass808 (0, v) }, { "lead", Bench::lead (v) }, { "pluck", Bench::pluck (v) } };
        for (auto& s : sounds)
        {
            const auto f = Forge::analyse (s.s.audio, sr, s.name);
            for (int mode = 0; mode < 2; ++mode)
                for (juce::int64 seed : { 1, 2, 3 })
                {
                    Forge::SearchSettings st; st.seed = seed; st.dataset = Forge::builtInDataset();
                    st.optimizer = mode == 0 ? Forge::SearchSettings::Optimizer::CmaThenLocal : Forge::SearchSettings::Optimizer::Local;
                    // mismo presupuesto por semilla: CMA 44 + pulido 12 = local 56
                    st.evaluationsPerArchitecture = st.cmaBudget + st.polishBudget;
                    Forge::Searcher sr0 (f);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto list = sr0.run (st);
                    float mean = 0; for (auto& k : list) mean += k.fidelity.overall;
                    std::printf ("%-6s %-10s semilla %d: mejor %3.0f%%  media %3.0f%%  perdida final %.2f  %d evals  %.1f s\n", s.name, mode == 0 ? "CMA+local" : "local",
                                 (int) seed, list.empty() ? 0.0f : list[0].fidelity.overall * 100, list.empty() ? 0.0f : mean / list.size() * 100,
                                 sr0.diagnostics.bestFinalLoss, sr0.evaluations, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
                }
        }
        return 0;
    }

    // ---- INTERNAL_RECOVERY: patches de prueba del índice (ocultos para la búsqueda) -> FORGE -> fidelidad ----
    //   NebulaTests Forge recuperacion [porCategoria=2] [semillas=2]
    if (argc > 1 && juce::String (argv[1]) == "recuperacion")
    {
        const int perCat = argc > 2 ? juce::jmax (1, juce::String (argv[2]).getIntValue()) : 2;
        const int nSeeds = argc > 3 ? juce::jmax (1, juce::String (argv[3]).getIntValue()) : 2;
        const auto* db = Forge::builtInDataset();
        if (db == nullptr) { std::printf ("sin base de patches\n"); return 1; }
        Forge::PatchRenderer r;
        double sumFid = 0, sumSecs = 0, sumSpread = 0, sumParam = 0, sumRandParam = 0; int n = 0;
        juce::Random rr (4242);
        for (int c = 0; c < (int) Forge::Category::count; ++c)
        {
            int taken = 0;
            for (auto& e : db->entries)
            {
                if ((int) e.category != c || e.split != 0) continue;
                if (taken++ >= perCat) break;
                const auto& spec = Forge::specFor (e.category);
                const auto hidden = e.patch();
                const auto audio = r.render (hidden, spec.note, spec.holdS, spec.totalS, sr);
                const auto f = Forge::analyse (audio, sr, "oculto");
                float best = 0, worst = 1; double secs = 0, pd = 0; int evals = 0;
                for (int s = 1; s <= nSeeds; ++s)
                {
                    Forge::SearchSettings st; st.seed = s; st.dataset = db;    // máscara por defecto: sin la partición de prueba
                    Forge::Searcher search (f);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto list = search.run (st);
                    secs += (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                    evals += search.evaluations;
                    const float fid = list.empty() ? 0.0f : list[0].fidelity.overall;
                    best = juce::jmax (best, fid); worst = juce::jmin (worst, fid);
                    if (s == 1 && ! list.empty())
                    {
                        const auto& ids = db->variedParams (e.category);
                        for (int i : ids) pd += std::abs (list[0].patch.getNorm (Forge::synthParams()[(size_t) i].id) - hidden.getNorm (Forge::synthParams()[(size_t) i].id));
                        pd /= juce::jmax ((size_t) 1, ids.size());
                    }
                }
                // referencia de distancia: otro patch de la misma categoría al azar
                double rd = 0;
                {
                    auto other = Forge::samplePatch (e.category, rr);
                    const auto& ids = db->variedParams (e.category);
                    for (int i : ids) rd += std::abs (other.getNorm (Forge::synthParams()[(size_t) i].id) - hidden.getNorm (Forge::synthParams()[(size_t) i].id));
                    rd /= juce::jmax ((size_t) 1, ids.size());
                }
                std::printf ("%-13s semilla %u  fidelidad %3.0f%% (peor semilla %3.0f%%)  dist. parametros %.3f (azar %.3f)  %.1f s/busqueda  %d evaluaciones  ref %.2f s\n",
                             Forge::categoryName (e.category), (unsigned) e.seed, best * 100, worst * 100, pd, rd, secs / nSeeds, evals / nSeeds, f.lengthS);
                sumFid += best; sumSpread += best - worst; sumSecs += secs / nSeeds; sumParam += pd; sumRandParam += rd; ++n;
            }
        }
        std::printf ("\nRECUPERACION INTERNA: %d patches ocultos, fidelidad media %.1f %%, variacion entre semillas %.1f puntos, distancia de parametros %.3f (azar %.3f), %.1f s por busqueda\n",
                     n, 100 * sumFid / juce::jmax (1, n), 100 * sumSpread / juce::jmax (1, n), sumParam / juce::jmax (1, n), sumRandParam / juce::jmax (1, n), sumSecs / juce::jmax (1, n));
        return 0;
    }

    // ---- 0. El patch es datos: guardar/cargar el formato, y lo que queda fuera de la receta ----
    {
        auto p = Forge::SynthPatch::init();
        p.set (ParamID::osc1Wave, (float) Wave::Sine); p.set (ParamID::ampD, 1.25f); p.setMod (ModSrc::Env2, ModDst::Pitch, 0.7f);
        p.name = "prueba"; p.seed = 99;
        const auto back = Forge::SynthPatch::fromXmlString (p.toXmlString());
        check ("SynthPatch: guardar y cargar da el mismo patch", back.sameSound (p) && back.seed == 99 && back.name == "prueba");
        bool clean = true;
        for (auto& info : Forge::synthParams())
            if (info.id.startsWith ("fx") || info.id.startsWith ("macro") || info.id == ParamID::transform || info.id.startsWith ("rs") || info.id == ParamID::engine) clean = false;
        check ("La receta no incluye efectos, macros, Cosmos/Abismo ni resintesis", clean, juce::String ((int) Forge::synthParams().size()) + " parametros de sintesis");
        Forge::PatchRenderer r1, r2;
        const auto a = r1.render (p, 36, 1.0, 1.2, sr), b = r2.render (p, 36, 1.0, 1.2, sr);
        float pk = 0;
        check ("Render offline determinista (dos renderizadores)", diffDb (a, b) < -200.0 && safe (a, pk), juce::String (diffDb (a, b), 1) + " dB");
    }

    struct Case { juce::String name; Bench::Sound sound; float minFidelity; };
    // umbrales = protección contra regresiones con lo que hoy se alcanza (no son metas)
    std::vector<Case> cases { { "808", Bench::bass808 (0, v), 0.60f }, { "lead", Bench::lead (v), 0.60f } };
    for (auto& c : cases)
    {
        std::printf ("\n== %s\n", c.name.toRawUTF8());
        auto p = makeProc();
        p->setSample (SampleTools::fromBuffer (c.sound.audio, sr, c.sound.name));
        p->setRegion (0.0f, 1.0f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        check (c.name + ": FORGE arranca con un sample", p->startForge (1, 8));
        waitForge (*p);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        const auto list = p->forgeCandidates();
        const auto ref = p->forgeReference();
        std::printf ("   referencia: %s\n", ref.summary().toRawUTF8());
        std::printf ("   busqueda: %s\n", p->forgeDiagnosticsText().toRawUTF8());
        for (auto& w : ref.warnings) std::printf ("   aviso: %s\n", w.toRawUTF8());
        for (auto& k : list)
            std::printf ("   %-6s %-11s desde %3.0f%% -> %3.0f%%  color %.2f dB  env %.2f dB  tono %.1f ct  golpe %.1f dB  cx %d  cpu %.1f%%  %s | %s\n", k.id.toRawUTF8(), k.source.toRawUTF8(), k.initialFidelity * 100, k.fidelity.overall * 100,
                         k.fidelity.colorDb, k.fidelity.envelopeDb, k.fidelity.pitchCents, k.fidelity.attackDb, k.complexity, k.cpuPercent,
                         k.patch.architecture.toRawUTF8(), k.structure.toRawUTF8());
        check (c.name + ": 8 candidatos", list.size() >= 8, juce::String ((int) list.size()) + " en " + juce::String (secs, 1) + " s");
        juce::StringArray archs, structs; for (auto& k : list) { archs.addIfNotAlreadyThere (k.patch.architecture); structs.addIfNotAlreadyThere (Forge::structureKey (k.patch)); }
        check (c.name + ": candidatos distintos (al menos 5 estructuras de receta)", structs.size() >= 5,
               juce::String (structs.size()) + " estructuras, " + juce::String (archs.size()) + " arquitecturas de partida");
        bool ordered = true; for (size_t i = 1; i < list.size(); ++i) if (list[i].fidelity.overall > list[i - 1].fidelity.overall + 0.021f) ordered = false;
        check (c.name + ": ordenados por fidelidad medida", ordered);
        if (list.empty()) continue;
        check (c.name + ": fidelidad del mejor", list[0].fidelity.overall >= c.minFidelity, juce::String (list[0].fidelity.overall * 100, 0) + " %");
        {
            // AT004: optimizar mejora la similitud medida (fidelidad completa, no la pérdida interna)
            int improved = 0, worse = 0; float bestStart = 0, bestEnd = 0;
            for (auto& k : list)
            {
                if (k.fidelity.overall > k.initialFidelity + 0.005f) ++improved;
                if (k.fidelity.overall < k.initialFidelity - 0.02f) ++worse;
                bestStart = juce::jmax (bestStart, k.initialFidelity); bestEnd = juce::jmax (bestEnd, k.fidelity.overall);
            }
            check (c.name + ": AT004 optimizar mejora la fidelidad medida", bestEnd > bestStart + 0.01f && worse == 0 && improved >= (int) list.size() / 2,
                   "mejor punto de partida " + juce::String (bestStart * 100, 0) + " % -> " + juce::String (bestEnd * 100, 0) + " %, mejoran " + juce::String (improved)
                   + " de " + juce::String ((int) list.size()) + ", empeoran " + juce::String (worse));
            int fromIndex = 0; for (auto& k : list) if (k.source == "indice") ++fromIndex;
            check (c.name + ": el indice aporta candidatos", fromIndex > 0, juce::String (fromIndex) + " de " + juce::String ((int) list.size()));
        }

        // Determinismo de la búsqueda
        {
            auto p2 = makeProc();
            p2->setSample (SampleTools::fromBuffer (c.sound.audio, sr, c.sound.name));
            p2->setRegion (0.0f, 1.0f);
            p2->startForge (1, 8); waitForge (*p2);
            const auto l2 = p2->forgeCandidates();
            bool same = l2.size() == list.size();
            for (size_t i = 0; same && i < list.size(); ++i) same = l2[i].id == list[i].id && l2[i].patch.sameSound (list[i].patch);
            check (c.name + ": misma semilla = mismos candidatos", same);
        }

        // A/B: escuchar candidato y referencia sin cambiar el sonido
        p->auditionForgeCandidate (0);
        const bool prevA = p->isPreviewing();
        p->stopPreview(); p->auditionForgeReference();
        check (c.name + ": A/B candidato / referencia", prevA && p->isPreviewing());
        p->stopPreview();

        // Elegir el mejor -> suena en el plugin
        const auto chosen = list[0];
        p->applySynthPatch (chosen.patch);
        const double total = juce::jmin (3.0, ref.lengthS + 0.3), hold = ref.releaseStartS > 0 ? ref.releaseStartS : total;
        const auto withSample = play (*p, ref.note, hold, total);
        float pk = 0;
        check (c.name + ": el patch elegido suena en el plugin (finito, sin picos)", safe (withSample, pk), "pico " + juce::String (pk, 2));
        check (c.name + ": sin clics", jumpDb (withSample) < 15.0, juce::String (jumpDb (withSample), 1) + " dB de salto sobre el RMS");
        {
            Forge::PatchRenderer r;
            const auto off = r.render (chosen.patch, ref.note, hold, total, sr);
            // el azar por nota (fases, ruido) cambia un poco entre renders: se compara contra esa misma variación
            const auto other = r.render (chosen.patch, ref.note, hold, total, sr, 12345);
            const double same = Bench::compare (off, withSample, sr, true).colorDb, seedVar = Bench::compare (off, other, sr, true).colorDb;
            check (c.name + ": el plugin suena como el render de FORGE", same <= seedVar + 0.3,
                   "color " + juce::String (same, 2) + " dB (otra semilla offline: " + juce::String (seedVar, 2) + " dB)");
        }

        // Sin el sample: sigue sonando igual
        p->setSample (nullptr);
        const auto noSample = play (*p, ref.note, hold, total);
        check (c.name + ": suena igual sin el sample original", p->sampleSource.get() == nullptr && diffDb (withSample, noSample) < -100.0,
               juce::String (diffDb (withSample, noSample), 1) + " dB");

        // Guardar y recuperar el proyecto (sin sample)
        {
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto p3 = makeProc();
            p3->setStateInformation (mb.getData(), (int) mb.getSize());
            const auto restored = play (*p3, ref.note, hold, total);
            check (c.name + ": proyecto guardado y abierto suena igual", diffDb (withSample, restored) < -100.0 && p3->currentSynthPatch().sameSound (chosen.patch, 1.0e-4f),
                   juce::String (diffDb (withSample, restored), 1) + " dB");
        }

        // Todos los parámetros de la receta son automatizables desde el DAW
        {
            int notAuto = 0;
            for (auto& info : Forge::synthParams()) if (auto* prm = p->state.getParameter (info.id)) { if (! prm->isAutomatable()) ++notAuto; } else ++notAuto;
            check (c.name + ": genes automatizables", notAuto == 0, juce::String (notAuto) + " no automatizables");
        }

        // Genes con efecto medible (cada uno, sobre el patch elegido)
        auto edited = [&] (std::function<void (Forge::SynthPatch&)> edit)
        {
            auto q = chosen.patch; edit (q); p->applySynthPatch (q);
            auto out = play (*p, ref.note, hold, total);
            p->applySynthPatch (chosen.patch);
            return out;
        };
        const double fmin = ref.f0 < 80 ? 25.0 : 50.0;
        {
            const auto up = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::osc1Octave, (float) juce::jmin (2, q.geti (ParamID::osc1Octave) + 1)); });
            const double h0 = medianHz (withSample, 0.3, juce::jmin (1.2, total), fmin), h1 = medianHz (up, 0.3, juce::jmin (1.2, total), fmin);
            check (c.name + ": gen de tono (octava +1) dobla la frecuencia", h0 > 0 && std::abs (1200.0 * std::log2 (h1 / h0) - 1200.0) < 60.0,
                   juce::String (h0, 1) + " -> " + juce::String (h1, 1) + " Hz");
        }
        {
            const auto shortD = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::ampS, 0.0f); q.set (ParamID::ampD, q.get (ParamID::ampD) * 0.3f); q.set (ParamID::ampCurve, 0.5f); });
            const auto longD = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::ampS, 0.0f); q.set (ParamID::ampD, juce::jmin (5.0f, q.get (ParamID::ampD) * 1.5f + 0.5f)); q.set (ParamID::ampCurve, 0.5f); });
            const double d0 = decayTo20 (shortD), d1 = decayTo20 (longD);
            check (c.name + ": gen de envolvente (decaimiento) cambia la caida", d1 > d0 * 1.5, juce::String (d0, 2) + " s -> " + juce::String (d1, 2) + " s");
        }
        {
            const auto dark = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::osc1Wave, (float) Wave::Saw); q.set (ParamID::cutoff, 300.0f); q.set (ParamID::filterEnv, 0.0f); });
            const auto bright = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::osc1Wave, (float) Wave::Saw); q.set (ParamID::cutoff, 8000.0f); q.set (ParamID::filterEnv, 0.0f); });
            // energía por encima de 1.5 kHz (dB de la fracción)
            const double c0 = Bench::bandFractionDb (seg (dark, 0.1, 0.8), sr, 1500.0, 16000.0), c1 = Bench::bandFractionDb (seg (bright, 0.1, 0.8), sr, 1500.0, 16000.0);
            check (c.name + ": gen de filtro (corte) cambia el brillo", c1 > c0 + 10.0, juce::String (c0, 1) + " -> " + juce::String (c1, 1) + " dB sobre 1.5 kHz");
        }
        {
            const auto noFm = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::osc1Wave, (float) Wave::Sine); q.set (ParamID::fmAmount, 0.0f); q.setMod (ModSrc::Env2, ModDst::Fm, 0.0f); q.set (ParamID::cutoff, 20000.0f); });
            const auto fm = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::osc1Wave, (float) Wave::Sine); q.set (ParamID::osc2Wave, (float) Wave::Sine); q.set (ParamID::osc2Semi, 12);
                                                              q.set (ParamID::fmAmount, 0.6f); q.setMod (ModSrc::Env2, ModDst::Fm, 0.0f); q.set (ParamID::cutoff, 20000.0f); });
            const double c0 = centroidBand (noFm, 0.1, 0.8), c1 = centroidBand (fm, 0.1, 0.8);
            check (c.name + ": gen de FM agrega armonicos", c1 > c0 + 1.5, juce::String (c0, 1) + " -> " + juce::String (c1, 1) + " bandas mel");
        }
        {
            const auto flat = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::noiseOn, 0); q.setMod (ModSrc::Env2, ModDst::Pitch, 0.0f); q.set (ParamID::fltA, 0.001f); q.set (ParamID::fltD, 0.3f); q.set (ParamID::fltS, 0.0f); });
            const auto drop = edited ([] (Forge::SynthPatch& q) { q.set (ParamID::noiseOn, 0); q.setMod (ModSrc::Env2, ModDst::Pitch, 1.0f); q.set (ParamID::fltA, 0.001f); q.set (ParamID::fltD, 0.3f); q.set (ParamID::fltS, 0.0f); });
            // frecuencia de los primeros 60 ms por cruces por cero
            auto zcHz = [] (const juce::AudioBuffer<float>& b)
            {
                const auto x = seg (b, 0.0, 0.06); int z = 0;
                for (size_t i = 1; i < x.size(); ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z;
                return z / 2.0 / 0.06;
            };
            check (c.name + ": gen de curva de tono (Env2 > Tono) sube el ataque", zcHz (drop) > zcHz (flat) * 1.3, juce::String (zcHz (flat), 0) + " -> " + juce::String (zcHz (drop), 0) + " cruces/s");
        }

        // Mutar: nueva generación de descendientes, la anterior se conserva
        // (los descendientes se miden contra la referencia: sin el sample, la mutación se rechaza en vez de
        //  entregar hijos sin medir; se vuelve a cargar el sample sólo para medirlos)
        check (c.name + ": mutar sin referencia se rechaza", ! p->forgeMutate (0, 2));
        {
            p->setSample (SampleTools::fromBuffer (c.sound.audio, sr, c.sound.name));
            p->setRegion (0.0f, 1.0f);
            check (c.name + ": mutar (con referencia)", p->forgeMutate (0, 2));
            waitForge (*p);
            const auto kids = p->forgeCandidates();
            bool family = ! kids.empty();
            for (auto& k : kids) family = family && k.parentId == chosen.id && k.generation == chosen.generation + 1 && ! k.patch.sameSound (chosen.patch);
            check (c.name + ": 8 descendientes del elegido, distintos de el", kids.size() >= 8 && family, juce::String ((int) kids.size()));
            check (c.name + ": la generacion anterior queda guardada", ! p->forgeGenerations().empty() && p->forgeGenerations().back().size() == list.size());
            if (! kids.empty())
                std::printf ("   hijo %s %3.0f%% vs padre %3.0f%%  %s\n", kids[0].id.toRawUTF8(), kids[0].fidelity.overall * 100, chosen.fidelity.overall * 100, kids[0].structure.toRawUTF8());
        }
    }

    std::printf (failures == 0 ? "TODO OK\n" : "%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
