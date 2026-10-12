// FORGE · huella sonora, base de patches e índice (P2 de NEBULA-CORE-040)
//   NebulaTests ForgeDb generar <archivo> [por_categoria]   genera la base (determinista)
//   NebulaTests ForgeDb [archivo]                           pruebas de la base incluida (o de ese archivo)
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/Forge/PatchDataset.h"
#include "../Source/Forge/ForgeSearch.h"
#include "bench/Corpus.h"
#if NEBULA_HAS_FORGE_DATA
 #include <BinaryData.h>
#endif

namespace {
int failures = 0;
void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-62s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}
// Distancia de parámetros sólo sobre los que varían en la categoría (los fijos diluirían la comparación)
std::vector<std::vector<int>> variedParams;
double paramDistance (const Forge::DnaEntry& a, const Forge::DnaEntry& b)
{
    const auto& idx = variedParams[(size_t) a.category];
    double d = 0; for (int i : idx) d += std::abs (a.norm[(size_t) i] - b.norm[(size_t) i]);
    return d / (double) juce::jmax ((size_t) 1, idx.size());
}
void computeVaried (const Forge::Dataset& d)
{
    variedParams.assign ((size_t) Forge::Category::count, {});
    for (int c = 0; c < (int) Forge::Category::count; ++c)
    {
        const size_t np = Forge::synthParams().size();
        for (size_t i = 0; i < np; ++i)
        {
            double mn = 1, mx = 0;
            for (auto& e : d.entries) if ((int) e.category == c) { mn = juce::jmin (mn, (double) e.norm[i]); mx = juce::jmax (mx, (double) e.norm[i]); }
            if (mx - mn > 0.02) variedParams[(size_t) c].push_back ((int) i);
        }
    }
}
}

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace Forge;

    if (argc > 2 && juce::String (argv[1]) == "generar")
    {
        const int per = argc > 3 ? juce::String (argv[3]).getIntValue() : 286;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto d = Dataset::generate (per, [] (float p) { static int last = -1; const int pc = (int) (p * 20); if (pc != last) { last = pc; std::printf ("  %d %%\n", pc * 5); std::fflush (stdout); } });
        const auto mb = d.toBinary();
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.getParentDirectory().createDirectory();
        out.replaceWithData (mb.getData(), mb.getSize());
        std::printf ("%d patches en %.1f s, %s (%d bytes), etiqueta %s\n", (int) d.entries.size(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0,
                     out.getFullPathName().toRawUTF8(), (int) mb.getSize(), d.tag.toRawUTF8());
        return 0;
    }

    // ---- cargar la base ----
    Dataset d;
    bool loaded = false;
    if (argc > 1)
    {
        juce::MemoryBlock mb; juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])).loadFileAsData (mb);
        loaded = d.fromBinary (mb.getData(), mb.getSize());
    }
#ifdef NEBULA_HAS_FORGE_DATA
    else loaded = d.fromBinary (BinaryData::nebula_dna_ndb, (size_t) BinaryData::nebula_dna_ndbSize);
#endif
    check ("La base se carga", loaded && ! d.entries.empty(), juce::String ((int) d.entries.size()) + " patches");
    if (! loaded || d.entries.empty()) { std::printf ("%d FALLOS\n", failures); return 1; }
    check ("La base corresponde a este motor (si no: regenerar)", d.isCurrent(), d.tag + " / motor " + engineTag());
    computeVaried (d);
    int per[(int) Category::count] {}, splits[3] {};
    for (auto& e : d.entries) { ++per[(int) e.category]; ++splits[e.split]; }
    juce::String perText; for (int c = 0; c < (int) Category::count; ++c) perText << categoryName ((Category) c) << " " << per[c] << "  ";
    check ("Las 7 categorias estan", std::all_of (std::begin (per), std::end (per), [] (int n) { return n > 0; }), perText);
    check ("Particion entrenamiento / validacion / prueba", splits[0] > 0 && splits[1] > 0 && splits[2] > splits[0] * 5,
           juce::String (splits[2]) + " / " + juce::String (splits[1]) + " / " + juce::String (splits[0]));

    // ---- determinismo: regenerar algunas entradas da exactamente lo mismo ----
    {
        PatchRenderer r;
        // (en otra plataforma el redondeo de seno/log difiere en ~1e-6: tolerancia 2e-3 sobre valores 0..1)
        int same = 0, tested = 0; float worst = 0;
        for (int ci = 0; ci < (int) Category::count; ++ci)
        {
            const auto cat = (Category) ci;
            // la primera entrada de cada categoría que se guardó (su semilla está en la base)
            for (auto& e : d.entries)
                if (e.category == cat)
                {
                    juce::Random rng ((juce::int64) e.seed);
                    const auto p = samplePatch (cat, rng);
                    const auto fp = measurePatch (r, p, cat);
                    float md = 0; for (int i = 0; i < fingerprintDims; ++i) md = juce::jmax (md, std::abs (fp[(size_t) i] - e.fp[(size_t) i]));
                    ++tested; if (md < 2.0e-3f) ++same; worst = juce::jmax (worst, md);
                    break;
                }
        }
        check ("Regenerar da la misma huella (determinista)", same == tested, juce::String (same) + " de " + juce::String (tested) + ", diferencia max " + juce::String (worst, 7));
    }

    // ---- recuperación interna: patches de prueba (no indexados) contra el índice de entrenamiento ----
    {
        int top1 = 0, top5 = 0, n = 0;
        double nearParam = 0, randParam = 0;
        juce::Random rr (7);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (auto& e : d.entries)
        {
            if (e.split != 0) continue;
            // la categoría no se conoce: primero con pesos generales, luego con los de la categoría más votada
            const auto first = d.nearest (e.fp, weightsFor (Category::Lead), 5, 0b100);
            int votes[(int) Category::count] {};
            for (auto& h : first) ++votes[(int) d.entries[(size_t) h.index].category];
            const int guess = (int) (std::max_element (std::begin (votes), std::end (votes)) - std::begin (votes));
            const auto hits = d.nearest (e.fp, weightsFor ((Category) guess), 5, 0b100);
            if (hits.empty()) continue;
            ++n;
            if (d.entries[(size_t) hits[0].index].category == e.category) ++top1;
            for (auto& h : hits) if (d.entries[(size_t) h.index].category == e.category) { ++top5; break; }
            nearParam += paramDistance (e, d.entries[(size_t) hits[0].index]);
            // vecino al azar de la misma categoría (línea base)
            std::vector<int> same; for (size_t i = 0; i < d.entries.size(); ++i) if (d.entries[i].split == 2 && d.entries[i].category == e.category) same.push_back ((int) i);
            randParam += paramDistance (e, d.entries[(size_t) same[(size_t) rr.nextInt ((int) same.size())]]);
        }
        const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / juce::jmax (1, n);
        check ("Recuperacion: categoria correcta (top-1)", n > 0 && top1 >= n * 0.7, juce::String (100.0 * top1 / juce::jmax (1, n), 1) + " % de " + juce::String (n));
        check ("Recuperacion: categoria correcta (top-5)", n > 0 && top5 >= n * 0.9, juce::String (100.0 * top5 / juce::jmax (1, n), 1) + " %");
        check ("El vecino tiene parametros mas cercanos que uno al azar", nearParam < randParam * 0.85,
               juce::String (nearParam / juce::jmax (1, n), 3) + " vs " + juce::String (randParam / juce::jmax (1, n), 3) + " (distancia media normalizada)");
        check ("Busqueda rapida", ms < 20.0, juce::String (ms, 2) + " ms por consulta (2 pasadas)");
    }

    // ---- normalización de tono: el mismo patch en otra nota se sigue encontrando ----
    {
        PatchRenderer r;
        int ok = 0, n = 0;
        for (auto& e : d.entries)
        {
            if (e.split != 0 || e.category == Category::Noise || n >= 30) continue;
            const auto& s = specFor (e.category);
            const int note = s.note + 5;
            const auto audio = r.render (e.patch(), note, s.holdS, s.totalS, 32000.0);
            const auto fp = fingerprint (audio, 32000.0, 440.0 * std::exp2 ((note - 69) / 12.0), s.holdS);
            const auto hits = d.nearest (fp, weightsFor (e.category), 5, 0b100);
            ++n;
            for (auto& h : hits) if (d.entries[(size_t) h.index].category == e.category) { ++ok; break; }
        }
        check ("Otra nota (+5 st): misma categoria en el top-5", ok >= n * 0.85, juce::String (ok) + " de " + juce::String (n));
    }

    // ---- referencias externas (no generadas por el motor) y relevancia acústica (AT003) ----
    {
        const Bench::Variant v {};
        struct Ext { Bench::Sound s; std::vector<Category> expected; };
        std::vector<Ext> ext {
            { Bench::bass808 (0, v), { Category::Eight08, Category::Bass } }, { Bench::bass808 (2, v), { Category::Eight08, Category::Bass } },
            { Bench::lead (v), { Category::Lead, Category::Pad, Category::Bass } }, { Bench::pluck (v), { Category::Pluck, Category::FmBell } },
            { Bench::padEvolving (v), { Category::Pad, Category::Lead } }, { Bench::bell (v), { Category::FmBell, Category::Pluck } },
            { Bench::noiseHit (v), { Category::Noise } } };
        PatchRenderer r;
        int relevantCat = 0, betterThanRandom = 0;
        for (auto& x : ext)
        {
            auto f = analyse (x.s.audio, Bench::kSr, x.s.name);
            const double f0 = f.pitched ? 440.0 * std::exp2 ((f.note - 69) / 12.0) : 0.0;
            const auto fp = fingerprint (f.tuned, f.sampleRate, f0, f.releaseStartS);
            const auto first = d.nearest (fp, weightsFor (Category::Lead), 7, 0b110);
            int votes[(int) Category::count] {};
            for (auto& h : first) ++votes[(int) d.entries[(size_t) h.index].category];
            const int guess = (int) (std::max_element (std::begin (votes), std::end (votes)) - std::begin (votes));
            const auto hits = d.nearest (fp, weightsFor ((Category) guess), 5, 0b110, 0.05f);
            juce::StringArray cats; for (auto& h : hits) cats.add (categoryName (d.entries[(size_t) h.index].category));
            bool rel = false; for (auto c : x.expected) if (cats.contains (categoryName (c))) rel = true;
            relevantCat += rel ? 1 : 0;
            // relevancia acústica: los 5 recuperados, tocados en la nota de la referencia, contra 5 al azar
            Evaluator ev (f);
            double lossHits = 0, lossRand = 0;
            for (auto& h : hits) lossHits += ev.fastLoss (ev.render (r, d.entries[(size_t) h.index].patch(), nullptr, true));
            juce::Random rr (11);
            for (int i = 0; i < 5; ++i) lossRand += ev.fastLoss (ev.render (r, d.entries[(size_t) rr.nextInt ((int) d.entries.size())].patch(), nullptr, true));
            betterThanRandom += lossHits < lossRand ? 1 : 0;
            std::printf ("   %-18s clase %-10s -> %s   perdida recuperados %.2f vs azar %.2f\n", x.s.name.toRawUTF8(), className (f.soundClass).toRawUTF8(),
                         cats.joinIntoString (" ").toRawUTF8(), lossHits / 5, lossRand / 5);
        }
        check ("Externos: categoria esperada en el top-5", relevantCat >= (int) ext.size() - 1, juce::String (relevantCat) + " de " + juce::String ((int) ext.size()));
        check ("AT003: los recuperados suenan mas parecidos que patches al azar", betterThanRandom >= (int) ext.size() - 1,
               juce::String (betterThanRandom) + " de " + juce::String ((int) ext.size()));
    }

    std::printf (failures == 0 ? "TODO OK\n" : "%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
