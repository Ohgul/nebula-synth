// COSMOS y ABISMO: cambian la estructura del sonido (no sólo efectos), en la dirección de su nombre;
// el centro es exactamente el original; el caos es reproducible; la salida está protegida (nivel, picos,
// valores no finitos) y el control de salida funciona.
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Transform.h"
#include "bench/Corpus.h"
#include "bench/Metrics.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

constexpr double sr = Bench::kSr;
constexpr int block = 256;
int failures = 0;

void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-56s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

std::unique_ptr<NebulaAudioProcessor> makeProc (const juce::AudioBuffer<float>* audio, const char* name, int preset = -1)
{
    std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    p->setPlayConfigDetails (0, 2, sr, block);
    p->prepareToPlay (sr, block);
    p->setRandomMode (true, 77);
    if (preset >= 0) p->loadPreset (preset);
    if (audio != nullptr) { p->setSample (SampleTools::fromBuffer (*audio, sr, name)); p->recreateFromSample(); }
    p->reset();
    return p;
}
void setP (NebulaAudioProcessor& p, const char* id, float v)
{
    auto* prm = p.state.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    p.applyDna();
}

juce::AudioBuffer<float> play (NebulaAudioProcessor& p, std::vector<int> notes, double hold, double seconds, std::function<void (int)> perBlock = {})
{
    p.reset();
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0, b = 0; pos < total; pos += block, ++b)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true); buf.clear();
        juce::MidiBuffer midi;
        if (pos == 0) for (int k : notes) midi.addEvent (juce::MidiMessage::noteOn (1, k, (juce::uint8) 100), 0);
        const int offAt = (int) (hold * sr);
        if (offAt >= pos && offAt < pos + n) for (int k : notes) midi.addEvent (juce::MidiMessage::noteOff (1, k), offAt - pos);
        if (perBlock) perBlock (b);
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}
int root (NebulaAudioProcessor& p) { auto m = p.resynthModel(); return m ? (int) std::lround (m->rootNote) : 57; }

bool finite (const juce::AudioBuffer<float>& b, float& peak)
{
    peak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (ch, i); if (! std::isfinite (v)) return false; peak = juce::jmax (peak, std::abs (v)); }
    return true;
}
double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double e = 0, d = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
        { e += (double) a.getSample (ch, i) * a.getSample (ch, i); const double x = a.getSample (ch, i) - b.getSample (ch, i); d += x * x; }
    return d <= 0 ? -400.0 : 10.0 * std::log10 (d / juce::jmax (1e-30, e));
}
std::vector<double> seg (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto m = Bench::toMono (b);
    const int a = juce::jlimit (0, (int) m.size(), (int) (t0 * sr)), z = juce::jlimit (a, (int) m.size(), (int) (t1 * sr));
    return { m.begin() + a, m.begin() + z };
}
double rmsDb (const std::vector<double>& x) { double a = 0; for (auto v : x) a += v * v; return Bench::db (a / std::max (1.0, (double) x.size())); }
// Nivel estéreo (energía de L y R, no la suma mono)
double levelDb (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    double a = 0; int n = 0;
    for (int ch = 0; ch < 2; ++ch) for (int i = (int) (t0 * sr); i < juce::jmin (b.getNumSamples(), (int) (t1 * sr)); ++i) { a += (double) b.getSample (ch, i) * b.getSample (ch, i); ++n; }
    return Bench::db (a / juce::jmax (1, n));
}

// Espectro promedio (potencia por bin) de un tramo
std::vector<double> avgSpectrum (const std::vector<double>& x, int order = 13)
{
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2);
    std::vector<double> acc ((size_t) N / 2, 0.0);
    for (size_t s = 0; s + (size_t) N <= x.size(); s += (size_t) N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (float) (x[s + (size_t) i] * (0.5 - 0.5 * std::cos (2.0 * Bench::kPi * i / N)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    return acc;
}
// Fracción de la energía que NO está cerca de un armónico de f0 (inarmonía / ruido): dB
double offHarmonicDb (const std::vector<double>& x, double f0)
{
    const int order = 13, N = 1 << order;
    const auto s = avgSpectrum (x, order);
    double on = 0, off = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * sr / N;
        if (f > 12000) break;
        const double h = f / f0, dist = std::abs (h - std::round (h)) * f0;
        (dist < 25.0 && h > 0.5 ? on : off) += s[(size_t) k];
    }
    return Bench::db (off / juce::jmax (1e-30, on + off));
}
// Ancho espectral (Hz): desviación de la frecuencia alrededor del centroide
double spectralSpread (const std::vector<double>& x)
{
    const int order = 13, N = 1 << order;
    const auto s = avgSpectrum (x, order);
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k) { num += s[(size_t) k] * k; den += s[(size_t) k]; }
    const double c = num / juce::jmax (1e-30, den);
    double v = 0; for (int k = 1; k < N / 2; ++k) v += s[(size_t) k] * (k - c) * (k - c);
    return std::sqrt (v / juce::jmax (1e-30, den)) * sr / N;
}
double sideMidDb (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    double m = 0, s = 0;
    for (int i = (int) (t0 * sr); i < juce::jmin (b.getNumSamples(), (int) (t1 * sr)); ++i)
    {
        const double l = b.getSample (0, i), r = b.getSample (1, i);
        m += (l + r) * (l + r); s += (l - r) * (l - r);
    }
    return Bench::db (s / juce::jmax (1e-30, m));
}
} // namespace

int NEBULA_TEST_ENTRY (int, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ParamID;
    const Bench::Variant v {};
    const auto padAudio = Bench::padEvolving (v).audio;
    const auto leadAudio = Bench::lead (v).audio;

    // ---- 1. Centro = original exacto ----
    {
        auto p = makeProc (&leadAudio, "lead");
        const auto a = play (*p, { root (*p) }, 1.0, 1.3);
        setP (*p, transform, 0.8f); setP (*p, transform, -0.6f); setP (*p, transform, 0.0f);
        const auto b = play (*p, { root (*p) }, 1.0, 1.3);
        check ("Centro (Original) = sonido sin transformar", diffDb (a, b) < -200.0 && p->resynthModel() == p->baseResynthModel(),
               juce::String (diffDb (a, b), 1) + " dB");
    }

    // ---- 2. Cosmos sobre un pad recreado ----
    {
        auto p = makeProc (&padAudio, "pad");
        const int n = root (*p);
        const auto base = play (*p, { n }, 4.0, 4.5);
        setP (*p, transform, 1.0f);
        const bool structural = p->resynthModel() != p->baseResynthModel();
        const auto cos = play (*p, { n }, 4.0, 4.5);
        float peak = 0; const bool ok = finite (cos, peak);
        const double sp0 = spectralSpread (seg (base, 0.5, 3.9)), sp1 = spectralSpread (seg (cos, 0.5, 3.9));
        const double w0 = sideMidDb (base, 0.5, 3.9), w1 = sideMidDb (cos, 0.5, 3.9);
        const double l0 = levelDb (base, 0.5, 3.9), l1 = levelDb (cos, 0.5, 3.9);
        check ("Cosmos cambia el modelo (espectro), no solo efectos", structural);
        check ("Cosmos: expansion espectral", sp1 > sp0 * 1.15, "ancho " + juce::String (sp0, 0) + " -> " + juce::String (sp1, 0) + " Hz");
        check ("Cosmos: espacio estereo, compatible en mono", w1 > w0 + 6.0 && w1 < 0.0, "lados/centro " + juce::String (w0, 1) + " -> " + juce::String (w1, 1) + " dB");
        check ("Cosmos: nivel parecido al original (+-4 dB)", std::abs (l1 - l0) < 4.0, juce::String (l0, 1) + " -> " + juce::String (l1, 1) + " dB");
        check ("Cosmos: finito y sin picos", ok && peak < 1.5f, "pico " + juce::String (peak, 2));

        // Identidad al 100 %: vuelve al original (salvo el limitador)
        setP (*p, identity, 1.0f);
        const auto keep = play (*p, { n }, 4.0, 4.5);
        check ("Identidad 100 % conserva el original", diffDb (base, keep) < -30.0, juce::String (diffDb (base, keep), 1) + " dB");
        setP (*p, identity, 0.5f);
        const auto half = play (*p, { n }, 4.0, 4.5);
        const double sph = spectralSpread (seg (half, 0.5, 3.9)), wh = sideMidDb (half, 0.5, 3.9);
        check ("Identidad 50 % queda entre los dos", sph > sp0 * 1.05 && sph < sp1 * 0.95 && wh > w0 + 3.0 && wh < w1 - 1.0,
               "ancho " + juce::String (sph, 0) + " Hz, lados/centro " + juce::String (wh, 1) + " dB");
    }

    // ---- 3. Abismo sobre un lead recreado ----
    {
        auto p = makeProc (&leadAudio, "lead");
        const int n = root (*p);
        const double f0 = 440.0 * std::exp2 ((n - 69) / 12.0);
        const auto base = play (*p, { n }, 1.5, 1.8);
        setP (*p, transform, -1.0f);
        const bool structural = p->resynthModel() != p->baseResynthModel();
        const auto aby = play (*p, { n }, 1.5, 1.8);
        float peak = 0; const bool ok = finite (aby, peak);
        const double ih0 = offHarmonicDb (seg (base, 0.2, 1.4), f0), ih1 = offHarmonicDb (seg (aby, 0.2, 1.4), f0);
        const double l0 = levelDb (base, 0.2, 1.4), l1 = levelDb (aby, 0.2, 1.4);
        check ("Abismo cambia el modelo (espectro), no solo efectos", structural);
        check ("Abismo: transformacion inarmonica", ih1 > ih0 + 10.0, "energia fuera de los armonicos " + juce::String (ih0, 1) + " -> " + juce::String (ih1, 1) + " dB");
        check ("Abismo: nivel controlado (+-6 dB)", std::abs (l1 - l0) < 6.0, juce::String (l0, 1) + " -> " + juce::String (l1, 1) + " dB");
        check ("Abismo: finito y sin picos", ok && peak < 1.5f, "pico " + juce::String (peak, 2));

        // Control de salida
        setP (*p, transformOut, -12.0f);
        const auto quiet = play (*p, { n }, 1.5, 1.8);
        const double lq = levelDb (quiet, 0.2, 1.4);
        check ("Salida de Abismo -12 dB", std::abs ((lq - l1) + 12.0) < 2.0, juce::String (lq - l1, 1) + " dB");
        setP (*p, transformOut, 0.0f);

        // Caos reproducible: mismo proyecto, mismo render
        const auto again = play (*p, { n }, 1.5, 1.8);
        check ("Abismo reproducible (mismo render)", diffDb (aby, again) < -200.0, juce::String (diffDb (aby, again), 1) + " dB");
        auto p2 = makeProc (&leadAudio, "lead");
        setP (*p2, transform, -1.0f);
        const auto other = play (*p2, { n }, 1.5, 1.8);
        check ("Abismo reproducible (otra instancia, misma semilla)", diffDb (aby, other) < -200.0, juce::String (diffDb (aby, other), 1) + " dB");
        p2->setRandomMode (true, 78);
        const auto otherSeed = play (*p2, { n }, 1.5, 1.8);
        check ("Otra semilla de instancia = otro caos", diffDb (aby, otherSeed) > -20.0, juce::String (diffDb (aby, otherSeed), 1) + " dB");
    }

    // ---- 4. También en los otros motores (preset de osciladores) ----
    {
        auto p = makeProc (nullptr, "", 0);
        const auto base = play (*p, { 57 }, 1.5, 1.8);
        setP (*p, transform, 1.0f);
        const auto cos = play (*p, { 57 }, 1.5, 1.8);
        setP (*p, transform, -1.0f);
        const auto aby = play (*p, { 57 }, 1.5, 1.8);
        const double w0 = sideMidDb (base, 0.2, 1.4), w1 = sideMidDb (cos, 0.2, 1.4);
        const double f0 = 220.0;
        const double ih0 = offHarmonicDb (seg (base, 0.2, 1.4), f0), ih1 = offHarmonicDb (seg (aby, 0.2, 1.4), f0);
        check ("Osciladores: Cosmos abre el estereo, compatible en mono", w1 > w0 + 6.0 && w1 < 0.0, juce::String (w0, 1) + " -> " + juce::String (w1, 1) + " dB");
        check ("Osciladores: Abismo inarmonico", ih1 > ih0 + 6.0, juce::String (ih0, 1) + " -> " + juce::String (ih1, 1) + " dB");
    }

    // ---- 5. Estabilidad: acorde de 8 notas, automatización rápida de punta a punta ----
    {
        auto p = makeProc (&padAudio, "pad");
        const auto out = play (*p, { 48, 52, 55, 59, 60, 64, 67, 71 }, 3.0, 3.6, [&] (int b)
        {
            // barrido de -1 a +1 y vuelta cada 0.5 s (incluye el cambio de tabla en vivo)
            const double t = b * block / sr;
            const float x = (float) std::sin (2.0 * Bench::kPi * t / 0.5);
            auto* prm = p->state.getParameter (transform);
            prm->setValueNotifyingHost (prm->convertTo0to1 (x));
            if (b % 4 == 0) p->applyDna();   // como el temporizador (~20 Hz)
        });
        float peak = 0; const bool ok = finite (out, peak);
        check ("Automatizacion rapida con 8 voces: finito, sin picos", ok && peak <= 1.5f && p->nonFiniteBlocks.load() == 0, "pico " + juce::String (peak, 2));
        auto maxJump = [] (const juce::AudioBuffer<float>& b) { double mx = 0; for (int i = 1; i < b.getNumSamples(); ++i) mx = juce::jmax (mx, (double) std::abs (b.getSample (0, i) - b.getSample (0, i - 1))); return mx; };
        // Abismo pleno ya tiene saltos (reducción de resolución): el barrido no debe hacer más que eso
        setP (*p, transform, -1.0f);
        const auto fixedAbyss = play (*p, { 48, 52, 55, 59, 60, 64, 67, 71 }, 3.0, 3.6);
        const double mj = maxJump (out), mf = maxJump (fixedAbyss);
        check ("El barrido no agrega saltos (clics) propios", mj < mf * 1.5 + 0.05, "salto max " + juce::String (mj, 3) + " (Abismo fijo " + juce::String (mf, 3) + ")");
    }

    // ---- 6. Protección ante valores no finitos y picos (unidad) ----
    {
        TransformVoice tv;
        tv.prepare (sr);
        tv.reset (5);
        const auto am = Transform::make (-1.0f, 0.0f, 6.0f);
        bool allFinite = true; float peak = 0;
        for (int i = 0; i < (int) sr; ++i)
        {
            if (i % 16 == 0) tv.control (am, 110.0, 16);
            float l = i == 1000 ? std::numeric_limits<float>::quiet_NaN() : (i == 2000 ? std::numeric_limits<float>::infinity() : 4.0f * (float) std::sin (i * 0.05));
            float r = l;
            tv.process (l, r);
            allFinite = allFinite && std::isfinite (l) && std::isfinite (r);
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
        }
        check ("NaN/inf a la entrada: la salida sigue finita", allFinite);

        // Afinación: Cosmos deriva unos cents, Abismo salta semitonos; Identidad 100 % no mueve nada
        auto pitchRange = [&] (float tr, float id)
        {
            TransformVoice t; t.prepare (sr); t.reset (9);
            const auto a = Transform::make (tr, id, 0.0f);
            float lo = 1e9f, hi = -1e9f;
            for (int i = 0; i < (int) (4.0 * sr) / 16; ++i) { const float s = t.control (a, 220.0, 16); lo = juce::jmin (lo, s); hi = juce::jmax (hi, s); }
            return hi - lo;
        };
        const float cr = pitchRange (1.0f, 0.0f) * 100.0f, ar = pitchRange (-1.0f, 0.0f), ir = pitchRange (-1.0f, 1.0f);
        check ("Cosmos: deriva lenta de afinacion (5-40 cents)", cr > 5.0f && cr < 40.0f, juce::String (cr, 1) + " cents");
        check ("Abismo: caos de afinacion (saltos de semitonos)", ar > 6.0f, juce::String (ar, 1) + " semitonos");
        check ("Identidad 100 %: sin cambio de afinacion", ir == 0.0f);
        check ("Limitador: nunca pasa de 1.0 (con +6 dB y entrada x4)", peak <= 1.0f, "pico " + juce::String (peak, 3));
    }

    std::printf (failures == 0 ? "TODO OK\n" : "%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
