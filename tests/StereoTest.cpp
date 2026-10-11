// Regresión: samples mono, estéreo, fuera de fase y de anchura extrema en el motor de resíntesis.
// Antes de la corrección, un sample estéreo ancho hacía que el retardo Haas de ResynthPlayer
// leyera fuera de su búfer (índice negativo) y la salida se llenaba de NaN.
// Comprueba, para cada sample × ancho de resíntesis × calidad (1x/2x/4x):
//   - todas las muestras son finitas y el pico no pasa del recorte (1.5)
//   - después de la nota, el silencio sigue siendo finito (el NaN no queda atrapado en filtros/efectos)
//   - un proyecto guardado con ese modelo se carga y suena finito
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

static constexpr double sr = 48000.0;
static constexpr int block = 512;
static int failures = 0, checks = 0;

static void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FALLO  %-58s %s\n", what.toRawUTF8(), detail.toRawUTF8()); }
}

static std::unique_ptr<NebulaAudioProcessor> makeProc()
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

struct Stats { long bad = 0; float peak = 0.0f; };
static Stats renderChord (NebulaAudioProcessor& p, double seconds, bool notes = true)
{
    Stats st;
    juce::AudioBuffer<float> buf (2, block);
    const int total = (int) (seconds * sr);
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (notes && pos == 0) for (int n : { 48, 55, 60, 64 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (notes && pos <= (int) (1.0 * sr) && pos + block > (int) (1.0 * sr)) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        buf.clear();
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) ++st.bad;
                else st.peak = juce::jmax (st.peak, std::abs (v));
            }
    }
    return st;
}

} // namespace

int NEBULA_TEST_ENTRY (int, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr double pi2 = juce::MathConstants<double>::twoPi;
    const int n = (int) (2.0 * sr);
    auto tone = [&] (double t, double det) { double s = 0; for (int k = 1; k < 25; ++k) s += std::sin (pi2 * k * 130.81 * (1.0 + det * (k % 3)) * t) / k; return 0.25 * s * std::min (1.0, t / 0.2); };

    struct Case { const char* name; juce::AudioBuffer<float> audio; };
    std::vector<Case> cases;
    auto make = [&] (const char* name, int chans, std::function<void (int, double, float&, float&)> g)
    {
        juce::AudioBuffer<float> b (chans, n);
        for (int i = 0; i < n; ++i) { float l = 0, r = 0; g (i, i / sr, l, r); b.setSample (0, i, l); if (chans > 1) b.setSample (1, i, r); }
        cases.push_back ({ name, b });
    };
    juce::Random rnd (5);
    make ("mono", 1, [&] (int, double t, float& l, float&) { l = (float) tone (t, 0.0); });
    make ("estereo_moderado", 2, [&] (int, double t, float& l, float& r) { l = (float) tone (t, 0.0); r = (float) (0.8 * tone (t, 0.0) + 0.2 * tone (t + 0.0007, 0.0)); });
    make ("estereo_ancho", 2, [&] (int, double t, float& l, float& r) { l = (float) tone (t, 0.002); r = (float) tone (t + 0.31, -0.002); });
    make ("fuera_de_fase", 2, [&] (int, double t, float& l, float& r) { l = (float) tone (t, 0.0); r = (float) (-tone (t, 0.0) + 0.02 * tone (t, 0.003)); });
    make ("solo_izquierda", 2, [&] (int, double t, float& l, float& r) { l = (float) tone (t, 0.0); r = 0.0f; });
    make ("ruido_estereo_independiente", 2, [&] (int, double t, float& l, float& r) { const double e = std::exp (-t * 2.0); l = (float) (0.3 * (rnd.nextFloat() * 2 - 1) * e + 0.5 * tone (t, 0)); r = (float) (0.3 * (rnd.nextFloat() * 2 - 1) * e - 0.5 * tone (t, 0.001)); });

    for (auto& c : cases)
    {
        auto p = makeProc();
        p->loadPreset (0);
        p->setSample (SampleTools::fromBuffer (c.audio, sr, c.name));
        const auto res = p->recreateFromSample();
        auto m = p->resynthModel();
        const float w = m != nullptr ? m->width : -1.0f;
        std::printf ("%-28s recrear=%s  ancho del modelo=%.2f\n", c.name, res == NebulaAudioProcessor::RecreateResult::Resynthesized ? "si" : "no", w);
        for (float rsW : { 0.0f, 0.5f, 1.0f })
            for (int q : { 0, 1, 2 })
            {
                setP (*p, ParamID::rsWidth, rsW);
                setP (*p, ParamID::quality, (float) q);
                const auto a = renderChord (*p, 1.6);
                const auto silence = renderChord (*p, 0.5, false);
                const auto tag = juce::String (c.name) + " ancho=" + juce::String (rsW, 1) + " calidad=" + juce::String (q);
                check (tag + " finito", a.bad == 0, juce::String (a.bad) + " muestras NaN/Inf");
                check (tag + " pico <= 1.5", a.peak <= 1.5f, juce::String (a.peak, 3));
                check (tag + " silencio finito despues", silence.bad == 0, juce::String (silence.bad) + " muestras NaN/Inf");
                check (tag + " suena", res != NebulaAudioProcessor::RecreateResult::Resynthesized || a.peak > 0.005f, juce::String (a.peak, 4));
            }
        // Con efectos (la reverb y el delay retienen cualquier NaN para siempre)
        {
            setP (*p, ParamID::rsWidth, 1.0f); setP (*p, ParamID::quality, 0.0f);
            for (auto id : { "fxDelayOn", "fxRevOn", "fxChorusOn" }) setP (*p, id, 1.0f);
            const auto a = renderChord (*p, 1.6);
            const auto silence = renderChord (*p, 1.0, false);
            check (juce::String (c.name) + " con efectos finito", a.bad == 0 && silence.bad == 0, juce::String (a.bad + silence.bad) + " NaN/Inf");
            for (auto id : { "fxDelayOn", "fxRevOn", "fxChorusOn" }) setP (*p, id, 0.0f);
        }
        // Proyecto guardado con este modelo y cargado en otra instancia
        {
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = makeProc();
            q->setStateInformation (mb.getData(), (int) mb.getSize());
            const auto a = renderChord (*q, 1.6);
            check (juce::String (c.name) + " proyecto recargado finito", a.bad == 0, juce::String (a.bad) + " NaN/Inf");
        }
    }
    std::printf ("\n%d comprobaciones, %d fallos\n%s\n", checks, failures, failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
