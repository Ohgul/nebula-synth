// AT009 · Tiempo real: ¿qué hace que un bloque de audio tarde más que su duración? (tiempo de CPU del hilo)
// Misma tortura que el banco de estabilidad (notas, automatización y tamaños de bloque al azar, semilla fija),
// pero cada bloque lento se registra con lo que pasó en él (qué parámetros cambiaron, notas, calidad), para
// encontrar la causa en vez de sólo medir el peor caso.
//   NebulaTests Realtime [segundos=60]
#include <juce_audio_utils/juce_audio_utils.h>
#include <chrono>
#if ! JUCE_WINDOWS
 #include <time.h>
#endif
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "bench/Corpus.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

// Contador de asignaciones de memoria (reemplaza el operator new global de todo el ejecutable de pruebas;
// sólo cuenta mientras countAllocs está activo, en el hilo que llama a processBlock)
static std::atomic<long> allocCount { 0 };
static thread_local bool countAllocs = false;
void* operator new (std::size_t n)
{
    if (countAllocs) ++allocCount;
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (countAllocs) ++allocCount;
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace {

constexpr double sr = 48000.0;

struct BlockLog { double ms = 0, ratio = 0; int n = 0, notes = 0; bool quality = false; long allocs = 0; juce::StringArray changed; };

// Tiempo de CPU del hilo (no cuenta cuando el sistema le quita el núcleo: así un pico es del plugin y no del
// equipo compartido). En Windows la resolución del tiempo de hilo es de ~15 ms: se usa el reloj de pared.
double threadMs()
{
   #if JUCE_WINDOWS
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count();
   #else
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1.0e6;
   #endif
}

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double seconds = argc > 1 ? juce::jmax (5.0, juce::String (argv[1]).getDoubleValue()) : 60.0;
    int failures = 0;
    {
        // el contador funciona (si no, "0 asignaciones" no probaría nada)
        const long a0 = allocCount.load();
        countAllocs = true;
        auto* v = new std::vector<int> (1000, 7);
        const int sum = (*v)[999];
        delete v;
        countAllocs = false;
        const bool works = allocCount.load() - a0 >= 2 && sum == 7;
        std::printf ("%-60s %s\n", "El contador de asignaciones funciona", works ? "OK   " : "FALLO");
        if (! works) ++failures;
    }
    const char* names[] { "Sinte", "Sample", "Resintesis" };
    for (int engine = 0; engine < 3; ++engine)
    {
        std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
        p->setPlayConfigDetails (0, 2, sr, 512);
        p->prepareToPlay (sr, 512);
        p->setRandomMode (true, 0x4E42u);
        juce::Random rnd (2026 + engine);
        p->loadPreset (rnd.nextInt (20));
        if (engine > 0)
        {
            const auto s = engine == 1 ? Bench::snare (Bench::Variant {}) : Bench::lead (Bench::Variant {});
            p->setSample (SampleTools::fromBuffer (s.audio, Bench::kSr, s.name));
            if (engine == 1) { auto* e = p->state.getParameter (ParamID::engine); e->setValueNotifyingHost (e->convertTo0to1 ((float) Engine::Sample)); }
            else p->recreateFromSample();
        }
        std::vector<juce::RangedAudioParameter*> prms;
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                const auto id = rp->getParameterID();
                if (id == ParamID::engine || id == ParamID::quality || id == ParamID::mpeOn) continue;
                prms.push_back (rp);
            }

        std::vector<BlockLog> logs;
        std::map<juce::String, int> changesPer, slowPer;
        int slowQuality = 0, qualityChanges = 0, slowNoChange = 0, blocksNoChange = 0;
        std::vector<int> held;
        juce::AudioBuffer<float> buf (2, 512);
        const int total = (int) (seconds * sr);
        for (int pos = 0; pos < total;)
        {
            const int n = 32 << rnd.nextInt (5);
            buf.setSize (2, n, false, false, true); buf.clear();
            juce::MidiBuffer midi;
            BlockLog lg; lg.n = n;
            if (rnd.nextFloat() < 0.15f && held.size() < 24) { const int nt = 36 + rnd.nextInt (49); midi.addEvent (juce::MidiMessage::noteOn (1, nt, (juce::uint8) (1 + rnd.nextInt (127))), rnd.nextInt (n)); held.push_back (nt); ++lg.notes; }
            if (rnd.nextFloat() < 0.12f && ! held.empty()) { const int k = rnd.nextInt ((int) held.size()); midi.addEvent (juce::MidiMessage::noteOff (1, held[(size_t) k]), rnd.nextInt (n)); held.erase (held.begin() + k); }
            if (rnd.nextFloat() < 0.3f)
                for (int k = 0; k < 2; ++k)
                {
                    auto* prm = prms[(size_t) rnd.nextInt ((int) prms.size())];
                    prm->setValueNotifyingHost (rnd.nextFloat());
                    lg.changed.add (prm->getParameterID());
                }
            if (rnd.nextFloat() < 0.002f) { auto* q = p->state.getParameter (ParamID::quality); q->setValueNotifyingHost (q->convertTo0to1 ((float) rnd.nextInt (3))); lg.quality = true; ++qualityChanges; }
            const long a0 = allocCount.load();
            countAllocs = true;
            const double a = threadMs();
            p->processBlock (buf, midi);
            lg.ms = threadMs() - a;
            countAllocs = false;
            lg.allocs = allocCount.load() - a0;
            lg.ratio = lg.ms / (1000.0 * n / sr);
            const bool slow = lg.ratio > 1.0;
            for (auto& id : lg.changed) { ++changesPer[id]; if (slow) ++slowPer[id]; }
            if (lg.quality) slowQuality += slow ? 1 : 0;
            if (lg.changed.isEmpty() && ! lg.quality) { ++blocksNoChange; slowNoChange += slow ? 1 : 0; }
            logs.push_back (lg);
            pos += n;
        }

        std::vector<size_t> order (logs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort (order.begin(), order.end(), [&] (size_t x, size_t y) { return logs[x].ratio > logs[y].ratio; });
        int slowCount = 0; for (auto& l : logs) slowCount += l.ratio > 1.0 ? 1 : 0;
        std::printf ("\n== %s: %d bloques, %d mas lentos que su duracion (%.3f %%); sin cambios de parametro: %d de %d lentos\n", names[engine], (int) logs.size(), slowCount,
                     100.0 * slowCount / juce::jmax ((size_t) 1, logs.size()), slowNoChange, blocksNoChange);
        double worstQuality = 0;
        long allocBlocks = 0, allocBlocksQuality = 0, allocTotal = 0;
        std::map<juce::String, int> allocWith;
        for (auto& l : logs)
        {
            if (l.quality) worstQuality = juce::jmax (worstQuality, l.ratio);
            if (l.allocs > 0)
            {
                ++allocBlocks; allocTotal += l.allocs;
                if (l.quality) ++allocBlocksQuality;
                for (auto& id : l.changed) ++allocWith[id];
                if (l.changed.isEmpty() && ! l.quality) ++allocWith[l.notes > 0 ? "(nota)" : "(nada)"];
            }
        }
        std::printf ("   bloques que piden memoria: %ld de %d (%ld con cambio de calidad), %ld asignaciones en total\n", allocBlocks, (int) logs.size(), allocBlocksQuality, allocTotal);
        std::printf ("%-60s %s  %ld bloques\n", (juce::String (names[engine]) + ": el hilo de audio no pide memoria").toRawUTF8(), allocBlocks == 0 ? "OK   " : "FALLO", allocBlocks);
        if (allocBlocks != 0) ++failures;
        {
            std::vector<std::pair<int, juce::String>> v; for (auto& [id, c] : allocWith) v.push_back ({ c, id });
            std::sort (v.begin(), v.end(), [] (auto& x, auto& y) { return x.first > y.first; });
            for (size_t k = 0; k < juce::jmin ((size_t) 10, v.size()); ++k) std::printf ("     con %-16s %d bloques\n", v[k].second.toRawUTF8(), v[k].first);
        }
        std::printf ("   cambios de calidad: %d (%d lentos, el peor %.2fx su duracion)\n", qualityChanges, slowQuality, worstQuality);
        for (size_t k = 0; k < juce::jmin ((size_t) 8, order.size()); ++k)
        {
            const auto& l = logs[order[k]];
            std::printf ("   %.2fx  %.2f ms  bloque %d  notas %d%s  cambios: %s\n", l.ratio, l.ms, l.n, l.notes, l.quality ? "  CALIDAD" : "", l.changed.joinIntoString (",").toRawUTF8());
        }
        // parámetros cuya automatización da bloques lentos más seguido que el resto
        std::vector<std::pair<double, juce::String>> rate;
        const double base = blocksNoChange > 0 ? (double) slowNoChange / blocksNoChange : 0.0;
        for (auto& [id, c] : changesPer) if (c >= 5) rate.push_back ({ (double) slowPer[id] / c, id });
        std::sort (rate.begin(), rate.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        std::printf ("   base (bloques sin cambios): %.2f %% lentos. Parametros con mas bloques lentos al cambiar:\n", 100 * base);
        for (size_t k = 0; k < juce::jmin ((size_t) 8, rate.size()); ++k)
            std::printf ("     %-16s %.1f %% (%d de %d)\n", rate[k].second.toRawUTF8(), 100 * rate[k].first, slowPer[rate[k].second], changesPer[rate[k].second]);
        // ¿picos aislados o carga sostenida? costo por muestra de cada bloque contra la mediana de sus 20 vecinos
        double worstSpike = 0, worstNoQuality = 0; size_t spikeAt = 0; int spikes = 0;
        for (size_t i = 0; i < logs.size(); ++i)
        {
            if (! logs[i].quality) worstNoQuality = juce::jmax (worstNoQuality, logs[i].ratio);
            std::vector<double> nb;
            for (size_t j = (i > 10 ? i - 10 : 0); j < juce::jmin (logs.size(), i + 11); ++j) if (j != i) nb.push_back (logs[j].ms / logs[j].n);
            std::nth_element (nb.begin(), nb.begin() + (long) nb.size() / 2, nb.end());
            const double rel = (logs[i].ms / logs[i].n) / juce::jmax (1.0e-9, nb[nb.size() / 2]);
            // un pico sólo importa si además el bloque se pasó de su duración
            if (logs[i].ratio > 1.0 && rel > 3.0 && ! logs[i].quality) { ++spikes; if (rel > worstSpike) { worstSpike = rel; spikeAt = i; } }
        }
        std::printf ("   peor bloque sin cambio de calidad: %.2fx su duracion. Picos aislados (lento y > 3x sus vecinos): %d", worstNoQuality, spikes);
        if (spikes > 0) std::printf ("; el peor %.1fx sus vecinos (%.2f ms, bloque %d, notas %d, cambios: %s)", worstSpike, logs[spikeAt].ms, logs[spikeAt].n, logs[spikeAt].notes,
                                     logs[spikeAt].changed.joinIntoString (",").toRawUTF8());
        std::printf ("\n");
        // Informativo: los picos aislados dependen de la máquina (en el CI de macOS, el mismo binario dio 0 y
        // 16 picos en dos corridas, en bloques sin notas ni cambios). El criterio que frena es el de memoria.
        std::printf ("   (informativo) picos aislados: %d en %d bloques\n", spikes, (int) logs.size());
    }
    std::printf ("\n%s\n", failures == 0 ? "TODO OK" : (juce::String (failures) + " FALLOS").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
