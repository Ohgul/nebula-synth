// MIDI y polifonía: modos Poly/Mono/Legato en todos los presets, RECREAR después de un preset Mono,
// límite de voces, pedal de sustain, pitch bend, notas colgadas (también con la cola real de la resíntesis)
// y clic al robar una voz.
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "bench/Corpus.h"
#include "bench/Metrics.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

constexpr double sr = 48000.0;
constexpr int block = 256;
int failures = 0;

void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-62s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
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
void setP (NebulaAudioProcessor& p, const char* id, float v) { auto* prm = p.state.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
float getP (NebulaAudioProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }

// Procesa "seconds" con los mensajes dados (tiempo en s) y devuelve el audio
juce::AudioBuffer<float> run (NebulaAudioProcessor& p, std::vector<std::pair<double, juce::MidiMessage>> evs, double seconds)
{
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true); buf.clear();
        juce::MidiBuffer midi;
        for (auto& [t, m] : evs) { const int s = (int) (t * sr); if (s >= pos && s < pos + n) midi.addEvent (m, s - pos); }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}
juce::MidiMessage on (int n, int v = 100) { return juce::MidiMessage::noteOn (1, n, (juce::uint8) v); }
juce::MidiMessage off (int n) { return juce::MidiMessage::noteOff (1, n); }
void silence (NebulaAudioProcessor& p, double s = 6.0)
{
    run (p, { { 0.0, juce::MidiMessage::allNotesOff (1) }, { 0.0, juce::MidiMessage::controllerEvent (1, 64, 0) } }, s);
}
double pitchHz (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto x = Bench::toMono (b);
    const auto tr = Bench::yinTrack (x, sr);
    std::vector<double> v;
    for (int f = (int) (t0 / 0.01); f < juce::jmin ((int) tr.size(), (int) (t1 / 0.01)); ++f) if (tr[(size_t) f] > 0) v.push_back (std::log2 (tr[(size_t) f]));
    return v.empty() ? 0.0 : std::exp2 (Bench::median (v));
}

} // namespace

int NEBULA_TEST_ENTRY (int, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;

    // 1. Todos los presets: acorde de 4 notas -> 4 voces en Poly, 1 en Mono/Legato
    {
        auto p = makeProc();
        int polyOk = 0, polyN = 0, monoOk = 0, monoN = 0;
        juce::String bad;
        for (int i = 0; i < (int) factoryPresets().size(); ++i)
        {
            p->loadPreset (i);
            const int mode = juce::roundToInt (getP (*p, ParamID::polyMode));
            run (*p, { { 0.0, on (48) }, { 0.0, on (52) }, { 0.0, on (55) }, { 0.0, on (60) } }, 0.15);
            const int voices = p->activeVoices();
            silence (*p);
            if (mode == PolyMode::Poly) { ++polyN; if (voices == 4) ++polyOk; else bad << factoryPresets()[(size_t) i].name << " (" << voices << ") "; }
            else { ++monoN; if (voices == 1) ++monoOk; else bad << factoryPresets()[(size_t) i].name << " (mono " << voices << ") "; }
        }
        check ("Presets Poly: 4 notas = 4 voces", polyOk == polyN, juce::String (polyOk) + " de " + juce::String (polyN) + " " + bad);
        check ("Presets Mono/Legato: 1 voz (es lo esperado)", monoOk == monoN, juce::String (monoOk) + " de " + juce::String (monoN));
    }

    // 2. Cargar un preset Mono y después RECREAR: el sonido recreado debe ser polifónico
    {
        auto p = makeProc();
        int monoPreset = -1;
        for (int i = 0; i < (int) factoryPresets().size() && monoPreset < 0; ++i)
        {
            p->loadPreset (i);
            if (juce::roundToInt (getP (*p, ParamID::polyMode)) != PolyMode::Poly) monoPreset = i;
        }
        p->setSample (SampleTools::fromBuffer (Bench::padStereo (Bench::Variant {}).audio, sr, "pad"));
        p->recreateFromSample();
        run (*p, { { 0.0, on (48) }, { 0.0, on (52) }, { 0.0, on (55) }, { 0.0, on (60) } }, 0.15);
        check ("RECREAR tras un preset Mono queda polifonico", p->activeVoices() == 4,
               juce::String (p->activeVoices()) + " voces (preset previo: " + factoryPresets()[(size_t) juce::jmax (0, monoPreset)].name + ")");
        silence (*p);
    }

    // 3. Límite de voces
    {
        auto p = makeProc();
        p->loadPreset (0);
        setP (*p, ParamID::voiceCount, 6.0f);
        std::vector<std::pair<double, juce::MidiMessage>> ev;
        for (int k = 0; k < 8; ++k) ev.push_back ({ 0.01 * k, on (40 + 3 * k) });
        run (*p, ev, 0.2);
        check ("Limite de 6 voces con 8 notas", p->activeVoices() == 6, juce::String (p->activeVoices()) + " voces");
        silence (*p);
    }

    // 4. Pedal de sustain
    {
        auto p = makeProc();
        p->loadPreset (0);
        setP (*p, ParamID::ampR, 0.2f);
        run (*p, { { 0.0, on (60) }, { 0.05, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0.1, off (60) } }, 0.6);
        const int held = p->activeVoices();
        run (*p, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 0) } }, 1.0);
        const int after = p->activeVoices();
        check ("Sustain: la nota sigue con el pedal y se suelta al levantarlo", held == 1 && after == 0,
               "con pedal " + juce::String (held) + ", sin pedal " + juce::String (after));
        silence (*p);
    }

    // 5. Pitch bend (rango 2 semitonos)
    {
        auto p = makeProc();
        p->loadPreset (0);
        setP (*p, ParamID::bendRange, 2.0f);
        auto a = run (*p, { { 0.0, on (57) }, { 0.6, juce::MidiMessage::pitchWheel (1, 16383) } }, 1.2);
        const double h0 = pitchHz (a, 0.2, 0.55), h1 = pitchHz (a, 0.8, 1.15);
        const double cents = 1200.0 * std::log2 (h1 / h0);
        check ("Pitch bend al maximo = +2 semitonos", std::abs (cents - 200.0) < 8.0, juce::String (cents, 1) + " ct");
        run (*p, { { 0.0, juce::MidiMessage::pitchWheel (1, 8192) } }, 0.05);
        silence (*p);
    }

    // 6. Mono: dos notas superpuestas = 1 voz; al soltar la segunda vuelve a la primera
    {
        auto p = makeProc();
        p->loadPreset (0);
        setP (*p, ParamID::polyMode, (float) PolyMode::Mono);
        setP (*p, ParamID::glide, 0.0f);
        auto a = run (*p, { { 0.0, on (57) }, { 0.4, on (64) }, { 0.8, off (64) } }, 1.3);
        const double h1 = pitchHz (a, 0.1, 0.35), h2 = pitchHz (a, 0.5, 0.75), h3 = pitchHz (a, 0.95, 1.25);
        const int voices = p->activeVoices();
        check ("Mono: una voz, sigue la ultima nota y vuelve a la anterior",
               voices == 1 && std::abs (1200 * std::log2 (h1 / 220.0)) < 15 && std::abs (1200 * std::log2 (h2 / 329.63)) < 15 && std::abs (1200 * std::log2 (h3 / 220.0)) < 15,
               juce::String (h1, 0) + " -> " + juce::String (h2, 0) + " -> " + juce::String (h3, 0) + " Hz, voces " + juce::String (voices));
        silence (*p);
    }

    // 7. Notas colgadas: 300 eventos al azar y todo suelto -> 0 voces (sinte y resíntesis con cola real)
    for (int engine : { 0, 2 })
    {
        auto p = makeProc();
        p->loadPreset (0);
        if (engine == 2) { p->setSample (SampleTools::fromBuffer (Bench::padStereo (Bench::Variant {}).audio, sr, "pad")); p->recreateFromSample(); }
        juce::Random r (5);
        std::vector<std::pair<double, juce::MidiMessage>> ev;
        std::vector<int> held;
        for (int k = 0; k < 300; ++k)
        {
            const double t = k * 0.02;
            if (held.empty() || r.nextFloat() < 0.55f) { const int nt = 36 + r.nextInt (48); ev.push_back ({ t, on (nt, 1 + r.nextInt (127)) }); held.push_back (nt); }
            else { const int i = r.nextInt ((int) held.size()); ev.push_back ({ t, off (held[(size_t) i]) }); held.erase (held.begin() + i); }
            if (r.nextFloat() < 0.05f) ev.push_back ({ t, juce::MidiMessage::controllerEvent (1, 64, r.nextBool() ? 127 : 0) });
        }
        for (int nt : held) ev.push_back ({ 6.1, off (nt) });
        ev.push_back ({ 6.1, juce::MidiMessage::controllerEvent (1, 64, 0) });
        run (*p, ev, 6.2);
        run (*p, {}, 8.0);
        check (juce::String ("Sin notas colgadas (") + (engine == 2 ? "resintesis con cola real" : "sinte") + ")", p->activeVoices() == 0, juce::String (p->activeVoices()) + " voces activas al final");
    }

    // 8. Robo de voz sin clic: 12 notas sostenidas (límite) y una 13ª
    {
        auto clickAt = [] (bool steal)
        {
            auto p = makeProc();
            p->loadPreset (0);
            p->setSample (SampleTools::fromBuffer (Bench::padEvolving (Bench::Variant {}).audio, sr, "pad"));
            p->recreateFromSample();
            setP (*p, ParamID::ampA, 0.3f);
            std::vector<std::pair<double, juce::MidiMessage>> ev;
            const int count = steal ? 12 : 11;
            for (int k = 0; k < count; ++k) ev.push_back ({ 0.05 * k, on (40 + 2 * k) });
            ev.push_back ({ 2.0, on (75) });
            auto a = run (*p, ev, 3.0);
            return Bench::clickScoreDb (a, (int) (2.0 * sr), sr);
        };
        const double withSteal = clickAt (true), without = clickAt (false);
        check ("Robo de voz: clic menor a 6 dB sobre una nota normal", withSteal - without < 6.0,
               juce::String (withSteal - without, 1) + " dB (antes de la correccion: +19 dB)");
    }

    // 9. Automatización rápida del corte con notas: salida finita y sin saltos grandes
    {
        auto p = makeProc();
        p->loadPreset (2);
        auto* cut = p->state.getParameter (ParamID::cutoff);
        juce::AudioBuffer<float> buf (2, block);
        long bad = 0; float pk = 0.0f;
        for (int b = 0; b < (int) (2.0 * sr / block); ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) for (int nt : { 48, 55, 60, 64 }) midi.addEvent (on (nt), 0);
            cut->setValueNotifyingHost (0.5f + 0.5f * (float) std::sin (b * 0.3));
            buf.clear(); p->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) { const float v = buf.getSample (ch, i); if (! std::isfinite (v)) ++bad; else pk = juce::jmax (pk, std::abs (v)); }
        }
        check ("Automatizacion del corte cada bloque: finita y sin saturar", bad == 0 && pk < 1.0f, "pico " + juce::String (pk, 3));
    }

    std::printf ("\n%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
