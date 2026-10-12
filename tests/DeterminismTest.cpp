// Azar reproducible por nota.
//  1. Misma configuración (mismo estado guardado) + misma secuencia MIDI => audio idéntico bit a bit,
//     en los 34 presets, el motor de sample y la resíntesis con ruido y parciales, a 1x/2x/4x.
//  2. El mismo procesador tras reset() repite exactamente lo mismo.
//  3. Cada golpe sigue siendo distinto (dos notas iguales seguidas no son clones).
//  4. Dos instancias nuevas tienen semillas distintas (no suenan clonadas).
//  5. Los proyectos v3 (sin "randomMode") conservan el azar libre de antes ("legacy"),
//     y al volver a guardarlos siguen en legacy.
//  6. El parecido (%) que calcula la interfaz da siempre el mismo número para el mismo sample.
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

static constexpr double sr = 48000.0;
static int failures = 0, checks = 0;
static juce::File fixtures;

static void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    ++checks;
    std::printf ("%-64s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

static std::unique_ptr<NebulaAudioProcessor> makeProc (int block = 512)
{
    std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    p->setPlayConfigDetails (0, 2, sr, block);
    p->prepareToPlay (sr, block);
    return p;
}
static void setP (NebulaAudioProcessor& p, const char* id, float v)
{
    auto* prm = p.state.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// Frase: acorde, melodía con velocidades distintas, notas repetidas, todo suelto a los 2.2 s
static juce::AudioBuffer<float> phrase (NebulaAudioProcessor& p, int block = 512, double seconds = 3.0)
{
    struct Ev { double t; int note; int vel; bool on; };
    const std::vector<Ev> evs { { 0.00, 48, 100, true }, { 0.00, 55, 90, true }, { 0.30, 60, 70, true }, { 0.60, 64, 110, true },
                                { 0.90, 64, 0, false }, { 1.00, 64, 60, true }, { 1.30, 64, 0, false }, { 1.40, 67, 127, true },
                                { 2.20, 48, 0, false }, { 2.20, 55, 0, false }, { 2.20, 60, 0, false }, { 2.20, 64, 0, false }, { 2.20, 67, 0, false } };
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true); buf.clear();
        juce::MidiBuffer midi;
        for (auto& e : evs)
        {
            const int s = (int) (e.t * sr);
            if (s >= pos && s < pos + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos);
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}
static void flush (NebulaAudioProcessor& p, double seconds = 6.0)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    buf.clear(); p.processBlock (buf, off);
    juce::MidiBuffer none;
    for (int i = 0; i < (int) (seconds * sr / 512); ++i) { buf.clear(); p.processBlock (buf, none); }
}
// 0 si son idénticos; si no, diferencia en dB respecto a la señal
static double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, bool& identical)
{
    double d = 0, e = 0; identical = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
        {
            const float x = a.getSample (ch, i), y = b.getSample (ch, i);
            if (x != y) identical = false;
            d += (double) (x - y) * (x - y); e += (double) x * x;
        }
    return identical ? -999.0 : 10.0 * std::log10 (d / juce::jmax (1e-30, e));
}

static juce::AudioBuffer<float> makeSound (const char* kind)
{
    juce::AudioBuffer<float> b (1, (int) (1.2 * sr));
    juce::Random r (3);
    static constexpr double pi2 = juce::MathConstants<double>::twoPi;
    double lp = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        double v = 0;
        if (juce::String (kind) == "caja") { lp += 0.5 * ((r.nextFloat() * 2 - 1) - lp); v = 0.5 * std::sin (pi2 * 185 * t) * std::exp (-t / 0.08) + 0.6 * (r.nextFloat() * 2 - 1 - lp) * std::exp (-t / 0.15); }
        else if (juce::String (kind) == "campana") { const double rt[] { 1, 2.76, 5.4, 8.93 }; for (int k = 0; k < 4; ++k) v += std::exp (-t / (1.2 / (k + 1))) * std::sin (pi2 * 330 * rt[k] * t) / (k + 1); v *= 0.4; }
        else { for (int k = 1; k < 30; ++k) v += std::sin (pi2 * k * 130.81 * t) / k * std::exp (-t * (1.5 + 0.4 * k)); v = 0.35 * v + 0.2 * (r.nextFloat() * 2 - 1) * std::exp (-t * 40); }
        b.setSample (0, i, (float) v);
    }
    return b;
}

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    fixtures = juce::File (argc > 1 ? juce::String (argv[1]) : juce::String ("../tests/fixtures/v3"));

    // ---- Configuraciones: 34 presets, sample con ruido, resíntesis (ruido, parciales, armónica) ----
    struct Config { juce::String name; juce::MemoryBlock state; };
    std::vector<Config> configs;
    {
        auto p = makeProc();
        for (int i = 0; i < (int) factoryPresets().size(); ++i)
        {
            p->loadPreset (i);
            Config c { "preset " + juce::String (i) + " " + factoryPresets()[(size_t) i].name, {} };
            p->getStateInformation (c.state);
            configs.push_back (std::move (c));
        }
        p->loadPreset (0);
        p->setSample (SampleTools::fromBuffer (makeSound ("caja"), sr, "caja"));
        setP (*p, ParamID::engine, (float) Engine::Sample);
        Config smp { "sample caja + ruido", {} }; setP (*p, ParamID::noise, 0.3f); p->getStateInformation (smp.state); configs.push_back (std::move (smp));
        for (auto kind : { "caja", "campana", "pluck" })
        {
            auto q = makeProc();
            q->loadPreset (0);
            q->setSample (SampleTools::fromBuffer (makeSound (kind), sr, kind));
            q->recreateFromSample();
            Config c { juce::String ("resintesis ") + kind, {} };
            q->getStateInformation (c.state);
            configs.push_back (std::move (c));
        }
    }

    // 1. Misma configuración + misma secuencia => idéntico (1x, 2x, 4x)
    int identicalCount = 0, total = 0;
    for (auto& c : configs)
        for (int q : { 0, 1, 2 })
        {
            if (q > 0 && ! c.name.startsWith ("preset 0 ") && ! c.name.startsWith ("resintesis") && ! c.name.startsWith ("sample")) continue;
            auto a = makeProc(), b = makeProc();
            a->setStateInformation (c.state.getData(), (int) c.state.getSize());
            b->setStateInformation (c.state.getData(), (int) c.state.getSize());
            setP (*a, ParamID::quality, (float) q); setP (*b, ParamID::quality, (float) q);
            bool same = false;
            const double d = diffDb (phrase (*a), phrase (*b), same);
            ++total; if (same) ++identicalCount;
            if (! same) check ("identico: " + c.name + " calidad " + juce::String (q), false, "diferencia " + juce::String (d, 1) + " dB");
        }
    check ("misma configuracion => mismo audio bit a bit", identicalCount == total, juce::String (identicalCount) + " de " + juce::String (total) + " renders identicos");

    // 2. Mismo procesador tras reset() (efectos apagados para que no queden colas)
    {
        int ok = 0, n = 0;
        for (auto& c : configs)
        {
            auto a = makeProc();
            a->setStateInformation (c.state.getData(), (int) c.state.getSize());
            for (auto id : { "fxEqOn", "fxCompOn", "fxChorusOn", "fxDelayOn", "fxRevOn" }) setP (*a, id, 0.0f);
            const auto r1 = phrase (*a); flush (*a); a->reset();
            const auto r2 = phrase (*a);
            bool same = false; const double d = diffDb (r1, r2, same);
            ++n; if (same || d < -120.0) ++ok;
            else
            {
                int first = -1;
                for (int i = 0; i < r1.getNumSamples() && first < 0; ++i) if (std::abs (r1.getSample (0, i) - r2.getSample (0, i)) > 1e-6f) first = i;
                std::printf ("   (tras reset difiere: %s %.1f dB, desde %.4f s)\n", c.name.toRawUTF8(), d, first / sr);
            }
        }
        check ("tras reset() se repite (<= -120 dB)", ok == n, juce::String (ok) + " de " + juce::String (n));
    }

    // 3. Dos golpes iguales seguidos no son clones (sample con ruido)
    {
        auto a = makeProc();
        a->setStateInformation (configs[(size_t) factoryPresets().size()].state.getData(), (int) configs[(size_t) factoryPresets().size()].state.getSize());
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<juce::AudioBuffer<float>> hits;
        for (int h = 0; h < 2; ++h)
        {
            juce::AudioBuffer<float> one (2, (int) (0.4 * sr));
            for (int pos = 0; pos < one.getNumSamples(); pos += 512)
            {
                juce::MidiBuffer midi; if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                buf.clear(); a->processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) one.copyFrom (ch, pos, buf, ch, 0, juce::jmin (512, one.getNumSamples() - pos));
            }
            flush (*a, 1.0);
            hits.push_back (one);
        }
        bool same = false; const double d = diffDb (hits[0], hits[1], same);
        check ("dos golpes iguales seguidos son distintos", ! same && d > -40.0, juce::String (d, 1) + " dB de diferencia");
    }

    // 4. Instancias nuevas: semillas distintas
    {
        auto a = makeProc(), b = makeProc();
        check ("dos instancias nuevas tienen semillas distintas", a->renderSeed() != b->renderSeed() && a->isSeededRandom() && b->isSeededRandom(),
               juce::String::toHexString ((juce::int64) a->renderSeed()) + " / " + juce::String::toHexString ((juce::int64) b->renderSeed()));
        juce::MemoryBlock mb; a->getStateInformation (mb);
        auto c = makeProc(); c->setStateInformation (mb.getData(), (int) mb.getSize());
        check ("la semilla se guarda y se recupera con el proyecto", c->renderSeed() == a->renderSeed() && c->isSeededRandom());
    }

    // 5. Proyectos v3 guardados antes de este cambio: azar libre (legacy), igual que antes
    {
        const auto f = fixtures.getChildFile ("recreado_caja.nebstate");
        if (! f.existsAsFile()) check ("proyecto v3 de referencia encontrado", false, f.getFullPathName());
        else
        {
            juce::MemoryBlock mb; f.loadFileAsData (mb);
            auto a = makeProc(), b = makeProc();
            a->setStateInformation (mb.getData(), (int) mb.getSize());
            b->setStateInformation (mb.getData(), (int) mb.getSize());
            check ("proyecto v3 se abre en modo legacy", ! a->isSeededRandom() && ! b->isSeededRandom());
            bool same = false; const double d = diffDb (phrase (*a), phrase (*b), same);
            check ("proyecto v3: el azar sigue libre como antes", ! same, juce::String (d, 1) + " dB entre dos cargas");
            juce::MemoryBlock re; a->getStateInformation (re);
            auto c = makeProc(); c->setStateInformation (re.getData(), (int) re.getSize());
            check ("proyecto v3 vuelto a guardar sigue en legacy", ! c->isSeededRandom());
            // y en modo reproducible (solo pruebas) ese mismo proyecto también se repite
            a->setRandomMode (true, 1234); b->setRandomMode (true, 1234);
            a->reset(); b->reset();
            flush (*a); flush (*b); a->reset(); b->reset();
            same = false; const double d2 = diffDb (phrase (*a), phrase (*b), same);
            check ("proyecto v3 forzado a semilla fija: identico", same, juce::String (d2, 1) + " dB");
        }
    }

    // 6. El % de parecido de la interfaz es estable
    {
        auto a = makeProc();
        a->loadPreset (0);
        a->setSample (SampleTools::fromBuffer (makeSound ("caja"), sr, "caja"));
        a->recreateFromSample();
        const float s1 = a->similarityOfCurrentPatch(), s2 = a->similarityOfCurrentPatch();
        check ("parecido de la interfaz estable", s1 == s2, juce::String (s1, 4) + " / " + juce::String (s2, 4));
    }

    std::printf ("\n%d comprobaciones, %d fallos\n%s\n", checks, failures, failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
