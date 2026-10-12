// ADN: cada gen cambia el audio de forma medible y en la dirección que dice su nombre, el ADN neutro no
// cambia nada (bit a bit), las mutaciones son reproducibles con la semilla, los candados se respetan,
// copiar/pegar y A/B devuelven exactamente el mismo ADN, el proyecto guarda el ADN y los genes
// estructurales se oyen también en una nota que ya está sonando (automatización).
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include <map>
#include <set>
#include "../Source/PluginProcessor.h"
#include "bench/Corpus.h"
#include "bench/Metrics.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

constexpr double sr = Bench::kSr;
constexpr int block = 256;
int failures = 0;

void check (const juce::String& what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-52s %s  %s\n", what.toRawUTF8(), ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

std::unique_ptr<NebulaAudioProcessor> makeProc (const juce::AudioBuffer<float>& audio, const char* name)
{
    std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    p->setPlayConfigDetails (0, 2, sr, block);
    p->prepareToPlay (sr, block);
    p->setRandomMode (true, 77);
    p->setSample (SampleTools::fromBuffer (audio, sr, name));
    p->recreateFromSample();
    p->reset();
    return p;
}
void setP (NebulaAudioProcessor& p, const char* id, float v)
{
    auto* prm = p.state.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    p.applyDna();   // en el plugin lo hace el temporizador (20 veces por segundo)
}
float getP (NebulaAudioProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }
void resetAll (NebulaAudioProcessor& p) { for (auto& g : Dna::genes()) if (auto* prm = p.state.getParameter (g.id)) prm->setValueNotifyingHost (prm->getDefaultValue()); p.applyDna(); }

int rootOf (NebulaAudioProcessor& p) { auto m = p.resynthModel(); return m ? (int) std::lround (m->rootNote) : 60; }

// Una nota (la raíz del modelo) de "hold" s, soltada, y "seconds" en total. "midway" se llama a mitad de la nota.
juce::AudioBuffer<float> note (NebulaAudioProcessor& p, double hold, double seconds, std::function<void()> midway = {})
{
    p.reset();
    const int total = (int) (seconds * sr), n0 = rootOf (p);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    bool midDone = false;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true); buf.clear();
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, n0, (juce::uint8) 100), 0);
        const int offAt = (int) (hold * sr);
        if (offAt >= pos && offAt < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, n0), offAt - pos);
        if (midway && ! midDone && pos >= (int) (hold * 0.5 * sr)) { midway(); midDone = true; }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}

bool finiteAndSafe (const juce::AudioBuffer<float>& b, float& peak)
{
    peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            if (! std::isfinite (v)) return false;
            peak = juce::jmax (peak, std::abs (v));
        }
    return peak < 4.0f;
}

// Diferencia relativa entre dos renders (dB; -inf = idénticos)
double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double e = 0, d = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
        { e += (double) a.getSample (ch, i) * a.getSample (ch, i); const double x = a.getSample (ch, i) - b.getSample (ch, i); d += x * x; }
    return d <= 0 ? -400.0 : 10.0 * std::log10 (d / juce::jmax (1e-30, e));
}

std::vector<double> segment (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto m = Bench::toMono (b);
    const int a = juce::jlimit (0, (int) m.size(), (int) (t0 * sr)), z = juce::jlimit (a, (int) m.size(), (int) (t1 * sr));
    return { m.begin() + a, m.begin() + z };
}

// Centroide espectral medio (Hz) de un tramo
double centroid (const std::vector<double>& x)
{
    constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf (2 * N);
    double num = 0, den = 0;
    for (size_t s = 0; s + N <= x.size(); s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (float) (x[s + (size_t) i] * (0.5 - 0.5 * std::cos (2.0 * Bench::kPi * i / N)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; num += p * k * sr / N; den += p; }
    }
    return den > 0 ? num / den : 0.0;
}

// Centroide cada 50 ms (para medir cuánto se mueve el timbre)
std::vector<double> centroidCurve (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    std::vector<double> c;
    for (double t = t0; t + 0.1 <= t1; t += 0.05) c.push_back (centroid (segment (b, t, t + 0.1)));
    return c;
}
double spread (const std::vector<double>& v)
{
    if (v.size() < 2) return 0;
    double m = 0, s = 0; for (auto x : v) m += x; m /= (double) v.size();
    for (auto x : v) s += (x - m) * (x - m);
    return std::sqrt (s / (double) v.size());
}

// Amplitud a una frecuencia (Goertzel con ventana)
double toneAmp (const std::vector<double>& x, double hz)
{
    const double w = 2.0 * Bench::kPi * hz / sr;
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * Bench::kPi * (double) i / (double) x.size());
        re += x[i] * win * std::cos (w * (double) i); im -= x[i] * win * std::sin (w * (double) i);
    }
    return std::hypot (re, im) / (double) x.size();
}
double f0Of (const std::vector<double>& x)
{
    const auto tr = Bench::yinTrack (x, sr);
    std::vector<double> v; for (auto f : tr) if (f > 0) v.push_back (f);
    return v.empty() ? 0.0 : Bench::median (v);
}
// Desviación del tono (cents) alrededor de la mediana: cuánto vibrato/glide hay
double pitchSpreadCents (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto tr = Bench::yinTrack (segment (b, t0, t1), sr);
    std::vector<double> c; for (auto f : tr) if (f > 0) c.push_back (1200.0 * std::log2 (f));
    return spread (c);
}
// Fluctuación rápida de la envolvente (aspereza): desviación de la envolvente de 2 ms respecto a su media de 100 ms
double fastFlutter (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const auto e = Bench::rmsEnvelope (segment (b, t0, t1), sr, 0.004, 0.001);
    const auto sm = Bench::smoothCurve (e, 50);
    double acc = 0, ref = 0;
    for (size_t i = 0; i < e.size(); ++i) { acc += std::abs (e[i] - sm[i]); ref += sm[i]; }
    return ref > 0 ? acc / ref : 0.0;
}
double rmsDb (const std::vector<double>& x) { double a = 0; for (auto v : x) a += v * v; return Bench::db (a / std::max (1.0, (double) x.size())); }
// Tiempo hasta que la cola cae 40 dB por debajo del nivel al soltar
double tailSeconds (const juce::AudioBuffer<float>& b, double offT)
{
    const auto e = Bench::rmsEnvelope (Bench::toMono (b), sr, 0.02, 0.01);
    const int i0 = (int) (offT / 0.01);
    if (i0 >= (int) e.size()) return 0;
    const double lvl = e[(size_t) i0];
    for (int i = i0; i < (int) e.size(); ++i) if (e[(size_t) i] < lvl * 0.01) return (i - i0) * 0.01;
    return (e.size() - (size_t) i0) * 0.01;
}

// Tiempo hasta que la envolvente (20 ms) llega a la mitad de su máximo
double riseMs (const juce::AudioBuffer<float>& b)
{
    const auto e = Bench::rmsEnvelope (Bench::toMono (b), sr, 0.02, 0.001);
    double mx = 0; for (auto v : e) mx = juce::jmax (mx, v);
    for (size_t i = 0; i < e.size(); ++i) if (e[i] >= 0.5 * mx) return (double) i;
    return 0.0;
}

struct GeneCase
{
    const char* id; float value;
    const char* sound;                                    // qué sonido usa
    double hold, total;
    juce::String metricName;
    std::function<double (const juce::AudioBuffer<float>&)> metric;
    std::function<bool (double base, double changed)> expect;
    juce::String expectText;
};

} // namespace

int NEBULA_TEST_ENTRY (int, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ParamID;
    const Bench::Variant v {};

    std::map<juce::String, std::unique_ptr<NebulaAudioProcessor>> procs;
    procs["lead"]  = makeProc (Bench::lead (v).audio, "lead");
    procs["vib"]   = makeProc (Bench::padVibrato (v).audio, "vibrato");
    procs["pad"]   = makeProc (Bench::padEvolving (v).audio, "pad");
    procs["808"]   = makeProc (Bench::bass808 (0, v).audio, "808");
    procs["ruido"] = makeProc (Bench::snare (v).audio, "caja");
    procs["hihat"] = makeProc (Bench::hihat (v).audio, "hihat");
    procs["bell"]  = makeProc (Bench::bell (v).audio, "campana");

    for (auto& [name, p] : procs)
        check ("Modelo creado: " + name, p->resynthModel() != nullptr);

    // ---- 1. ADN neutro: la variante es el mismo modelo y el audio no cambia nada ----
    {
        auto& p = *procs["lead"];
        const bool same = p.resynthModel() == p.baseResynthModel();
        const auto a = note (p, 1.0, 1.5);
        // mover y devolver todos los genes a su valor por defecto
        setP (p, dnaTilt, 6.0f); setP (p, dnaAttack, 2.0f); setP (p, dnaDensity, 0.5f);
        resetAll (p);
        const auto b = note (p, 1.0, 1.5);
        check ("ADN neutro = modelo original", same && p.resynthModel() == p.baseResynthModel());
        check ("ADN neutro no cambia el audio", diffDb (a, b) < -200.0, juce::String (diffDb (a, b), 1) + " dB");
    }

    // ---- 2. Cada gen produce un cambio real, medible y en la dirección esperada ----
    auto cent = [] (double t0, double t1) { return [t0, t1] (const juce::AudioBuffer<float>& b) { return centroid (segment (b, t0, t1)); }; };
    const double leadHz = Bench::midiHz (69);
    std::vector<GeneCase> cases {
        { dnaTilt, 9.0f, "lead", 1.0, 1.5, "centroide Hz", cent (0.2, 0.9), [] (double a, double b) { return b > a * 1.3; }, "sube" },
        { dnaTilt, -9.0f, "lead", 1.0, 1.5, "centroide Hz", cent (0.2, 0.9), [] (double a, double b) { return b < a * 0.75; }, "baja" },
        { dnaTiltEvolve, -12.0f, "lead", 1.0, 1.5, "centroide final/inicial", [] (const juce::AudioBuffer<float>& b) { return centroid (segment (b, 0.75, 0.95)) / centroid (segment (b, 0.05, 0.25)); },
          [] (double a, double b) { return b < a * 0.85; }, "se oscurece con el tiempo" },
        { dnaOddEven, -1.0f, "lead", 1.0, 1.5, "armonico 2 / 1 (dB)", [leadHz] (const juce::AudioBuffer<float>& b) { const auto x = segment (b, 0.3, 0.8); const double f = f0Of (x) > 0 ? f0Of (x) : leadHz; return 20 * std::log10 (toneAmp (x, 2 * f) / toneAmp (x, f)); },
          [] (double a, double b) { return b < a - 15.0; }, "pares desaparecen" },
        { dnaHarmonics, 0.15f, "lead", 1.0, 1.5, "energia sobre el armonico 8 (dB)", [] (const juce::AudioBuffer<float>& b) { return Bench::bandFractionDb (segment (b, 0.2, 0.9), sr, 8.5 * 440.0, 20000.0); },
          [] (double a, double b) { return b < a - 15.0; }, "menos armonicos" },
        { dnaFormant, 7.0f, "lead", 1.0, 1.5, "centroide Hz", cent (0.2, 0.9), [] (double a, double b) { return b > a * 1.15; }, "formantes arriba" },
        { dnaFormant, 7.0f, "lead", 1.0, 1.5, "tono Hz", [] (const juce::AudioBuffer<float>& b) { return f0Of (segment (b, 0.2, 0.9)); },
          [] (double a, double b) { return std::abs (1200 * std::log2 (b / a)) < 10.0; }, "sin cambiar el tono" },
        { dnaPitchDepth, 0.0f, "vib", 2.0, 2.5, "vibrato (cents)", [] (const juce::AudioBuffer<float>& b) { return pitchSpreadCents (b, 0.5, 1.9); },
          [] (double a, double b) { return b < a * 0.4; }, "curva de tono plana" },
        { dnaPitchDepth, 2.0f, "vib", 2.0, 2.5, "vibrato (cents)", [] (const juce::AudioBuffer<float>& b) { return pitchSpreadCents (b, 0.5, 1.9); },
          [] (double a, double b) { return b > a * 1.5; }, "curva de tono exagerada" },
        { dnaNoiseTilt, 1.0f, "ruido", 0.4, 0.6, "centroide Hz", cent (0.0, 0.3), [] (double a, double b) { return b > a * 1.15; }, "ruido mas brillante" },
        { dnaNoiseTilt, -1.0f, "ruido", 0.4, 0.6, "centroide Hz", cent (0.0, 0.3), [] (double a, double b) { return b < a * 0.85; }, "ruido mas oscuro" },
        { dnaRough, 0.8f, "lead", 1.0, 1.5, "fluctuacion rapida", [] (const juce::AudioBuffer<float>& b) { return fastFlutter (b, 0.3, 0.9); },
          [] (double a, double b) { return b > a + 0.1; }, "aspereza" },
        { dnaDensity, 1.0f, "lead", 1.0, 1.5, "batido del armonico 3 (variacion)", [] (const juce::AudioBuffer<float>& b) {
              // amplitud del 3er armónico cada 40 ms: con copias desafinadas late (≈ 8 Hz)
              std::vector<double> a; for (double t = 0.2; t + 0.04 <= 0.95; t += 0.01) a.push_back (toneAmp (segment (b, t, t + 0.04), 3 * 440.0));
              double m = 0; for (auto x : a) m += x; m /= (double) a.size(); return spread (a) / juce::jmax (1e-12, m); },
          [] (double a, double b) { return b > a + 0.15; }, "batido de copias" },
        { dnaAttack, 4.0f, "pad", 5.0, 5.5, "llega a la mitad del maximo (ms)", [] (const juce::AudioBuffer<float>& b) { return riseMs (b); },
          [] (double a, double b) { return b > a * 1.6; }, "ataque mas lento" },
        { dnaAttack, 0.25f, "pad", 5.0, 5.5, "llega a la mitad del maximo (ms)", [] (const juce::AudioBuffer<float>& b) { return riseMs (b); },
          [] (double a, double b) { return b < a * 0.7; }, "ataque mas rapido" },
        { dnaTransient, 0.0f, "808", 1.0, 1.2, "energia 0-15 ms (dB)", [] (const juce::AudioBuffer<float>& b) { return rmsDb (segment (b, 0.0, 0.015)); },
          [] (double a, double b) { return b < a - 3.0; }, "sin golpe" },
        { dnaTransient, 2.0f, "808", 1.0, 1.2, "energia 0-15 ms (dB)", [] (const juce::AudioBuffer<float>& b) { return rmsDb (segment (b, 0.0, 0.015)); },
          [] (double a, double b) { return b > a + 2.0; }, "golpe mas fuerte" },
        { dnaRelease, 3.0f, "pad", 2.0, 9.0, "cola -40 dB (s)", [] (const juce::AudioBuffer<float>& b) { return tailSeconds (b, 2.0); },
          [] (double a, double b) { return b > a * 1.5; }, "cola mas larga" },
        { dnaEvolve, 0.0f, "pad", 4.0, 4.5, "movimiento del centroide (Hz)", [] (const juce::AudioBuffer<float>& b) { return spread (centroidCurve (b, 0.8, 3.9)); },
          [] (double a, double b) { return b < a * 0.5; }, "timbre quieto" },
        { dnaEvolve, 2.0f, "pad", 4.0, 4.5, "movimiento del centroide (Hz)", [] (const juce::AudioBuffer<float>& b) { return spread (centroidCurve (b, 0.8, 3.9)); },
          [] (double a, double b) { return b > a * 1.2; }, "timbre mas movido" },
        { dnaHold, 1.0f, "pad", 6.0, 6.5, "movimiento sostenido (Hz)", [] (const juce::AudioBuffer<float>& b) { return spread (centroidCurve (b, 3.0, 5.9)); },
          [] (double a, double b) { return b < a * 0.5; }, "congelado al sostener" },
        { rsPartials, 0.0f, "bell", 1.5, 2.0, "dif. con el original (dB)", {}, {}, "parciales fuera" },
        { rsNoise, 0.0f, "hihat", 0.4, 0.6, "nivel (dB)", [] (const juce::AudioBuffer<float>& b) { return rmsDb (segment (b, 0, 0.4)); },
          [] (double a, double b) { return b < a - 6.0; }, "ruido fuera" },
        { rsHarm, 0.3f, "lead", 1.0, 1.5, "nivel (dB)", [] (const juce::AudioBuffer<float>& b) { return rmsDb (segment (b, 0.2, 0.9)); },
          [] (double a, double b) { return b < a - 6.0; }, "capa armonica baja" },
        { rsStretch, 0.6f, "bell", 1.5, 2.0, "dif. con el original (dB)", {}, {}, "inarmonia" },
        { rsSpeed, 2.0f, "pad", 3.0, 3.5, "dif. con el original (dB)", {}, {}, "velocidad" },
        { rsWidth, 0.0f, "pad", 2.0, 2.5, "lados/centro (dB)", [] (const juce::AudioBuffer<float>& b) { return Bench::stereoBands (b, sr).sideMidDb; },
          [] (double a, double b) { return b < a - 6.0; }, "mas estrecho" },
    };

    std::set<juce::String> covered;
    for (auto& c : cases)
    {
        auto& p = *procs[c.sound];
        resetAll (p);
        const auto base = note (p, c.hold, c.total);
        setP (p, c.id, c.value);
        const auto changed = note (p, c.hold, c.total);
        resetAll (p);
        float peak = 0;
        const bool safe = finiteAndSafe (changed, peak);
        const double dif = diffDb (base, changed);
        juce::String label = juce::String (c.id) + " = " + juce::String (c.value, 2) + " (" + c.expectText + ")";
        if (c.metric)
        {
            const double a = c.metric (base), b = c.metric (changed);
            check (label, safe && std::isfinite (a) && std::isfinite (b) && c.expect (a, b),
                   c.metricName + ": " + juce::String (a, 3) + " -> " + juce::String (b, 3) + ", pico " + juce::String (peak, 2));
        }
        else
            check (label, safe && dif > -20.0, "diferencia " + juce::String (dif, 1) + " dB, pico " + juce::String (peak, 2));
        covered.insert (c.id);
    }
    // Todos los genes del editor (menos la transformación, que tiene su propia prueba) están medidos
    juce::StringArray missing;
    for (auto& g : Dna::genes())
        if (juce::String (g.group) != "F" && covered.count (g.id) == 0) missing.add (g.id);
    check ("Todos los genes A-E estan medidos", missing.isEmpty(), missing.joinIntoString (", "));

    // ---- 3. Mutación reproducible ----
    {
        auto p1 = makeProc (Bench::lead (v).audio, "lead"), p2 = makeProc (Bench::lead (v).audio, "lead");
        p1->mutateDna (0.6f, 1234); p2->mutateDna (0.6f, 1234);
        const auto j1 = p1->dnaToJson(), j2 = p2->dnaToJson();
        const auto a1 = note (*p1, 1.0, 1.3), a2 = note (*p2, 1.0, 1.3);
        check ("Misma semilla = mismo ADN", j1 == j2);
        check ("Misma semilla = mismo audio", diffDb (a1, a2) < -200.0, juce::String (diffDb (a1, a2), 1) + " dB");
        auto p3 = makeProc (Bench::lead (v).audio, "lead");
        p3->mutateDna (0.6f, 1235);
        check ("Otra semilla = otro ADN", p3->dnaToJson() != j1);
        float peak = 0;
        bool allSafe = true;
        for (juce::int64 s = 1; s <= 12; ++s)   // mutaciones fuertes encadenadas: siempre finito y sin picos
        {
            p3->mutateDna (1.0f, s);
            allSafe = allSafe && finiteAndSafe (note (*p3, 0.6, 0.9), peak);
        }
        check ("12 mutaciones fuertes: audio finito y sin picos", allSafe, "pico " + juce::String (peak, 2));
        const auto s1 = p1->nextDnaSeed(), s2 = p1->nextDnaSeed();
        check ("Semilla siguiente avanza", s2 == s1 + 1);
    }

    // ---- 4. Candados ----
    {
        auto p1 = makeProc (Bench::lead (v).audio, "lead"), p2 = makeProc (Bench::lead (v).audio, "lead");
        p1->setGeneLocked (dnaTilt, true);
        const float before = getP (*p1, dnaTilt);
        p1->mutateDna (1.0f, 99); p2->mutateDna (1.0f, 99);
        bool restSame = true;
        for (auto& g : Dna::genes())
            if (juce::String (g.id) != dnaTilt && std::abs (getP (*p1, g.id) - getP (*p2, g.id)) > 1e-6f) restSame = false;
        check ("Gen con candado no cambia", getP (*p1, dnaTilt) == before && getP (*p2, dnaTilt) != before);
        check ("Candado no cambia el resto de la mutacion", restSame);
        p1->setGeneLocked (dnaTilt, false);
        check ("Quitar candado", ! p1->isGeneLocked (dnaTilt));
    }

    // ---- 5. Copiar / pegar, A/B, reiniciar gen ----
    {
        auto p1 = makeProc (Bench::lead (v).audio, "lead"), p2 = makeProc (Bench::lead (v).audio, "lead");
        p1->mutateDna (0.7f, 7);
        check ("Pegar ADN", p2->dnaFromJson (p1->dnaToJson()) && p2->dnaToJson() == p1->dnaToJson());
        check ("Pegar texto que no es ADN se rechaza", ! p2->dnaFromJson ("{\"hola\":1}"));
        const auto a1 = note (*p1, 1.0, 1.3), a2 = note (*p2, 1.0, 1.3);
        check ("ADN pegado suena igual", diffDb (a1, a2) < -200.0, juce::String (diffDb (a1, a2), 1) + " dB");

        p1->storeDnaSlot (0);
        const auto ja = p1->dnaToJson();
        p1->mutateDna (0.7f, 8);
        p1->storeDnaSlot (1);
        const auto jb = p1->dnaToJson();
        p1->recallDnaSlot (0);
        const bool backA = p1->dnaToJson() == ja;
        p1->recallDnaSlot (1);
        check ("A/B: guardar y volver a A y B", backA && p1->dnaToJson() == jb && p1->hasDnaSlot (0) && p1->hasDnaSlot (1));

        setP (*p1, dnaTilt, 8.0f);
        p1->resetGene (dnaTilt);
        check ("Reiniciar un gen", std::abs (getP (*p1, dnaTilt)) < 1e-6f);
    }

    // ---- 6. El proyecto guarda el ADN (modelo base + genes) y suena igual al abrirlo ----
    {
        auto p1 = makeProc (Bench::lead (v).audio, "lead");
        p1->mutateDna (0.5f, 42);
        setP (*p1, dnaTilt, -6.0f);
        p1->setGeneLocked (dnaFormant, true);
        juce::MemoryBlock mb;
        p1->getStateInformation (mb);
        std::unique_ptr<NebulaAudioProcessor> p2 (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
        p2->setPlayConfigDetails (0, 2, sr, block);
        p2->prepareToPlay (sr, block);
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        p2->applyDna();
        p1->setRandomMode (true, 77); p2->setRandomMode (true, 77);
        const auto a1 = note (*p1, 1.0, 1.3), a2 = note (*p2, 1.0, 1.3);
        check ("Proyecto guarda ADN y candados", p2->dnaToJson() == p1->dnaToJson() && p2->isGeneLocked (dnaFormant));
        check ("Proyecto con ADN suena igual al abrirlo", diffDb (a1, a2) < -100.0, juce::String (diffDb (a1, a2), 1) + " dB");
        check ("Proyecto guarda el modelo base, no la variante", p2->baseResynthModel() != nullptr && p2->resynthModel() != p2->baseResynthModel());
    }

    // ---- 7. Automatización: un gen estructural cambia una nota que ya suena, sin clic ----
    {
        auto& p = *procs["lead"];
        resetAll (p);
        const auto b = note (p, 1.6, 1.8, [&] { setP (p, dnaTilt, -12.0f); });
        resetAll (p);
        const double c0 = centroid (segment (b, 0.3, 0.75)), c1 = centroid (segment (b, 0.9, 1.5));
        // la sierra ya tiene saltos en cada ciclo: se compara con la misma nota sin cambio
        const auto plain = note (p, 1.6, 1.8);
        const double jump = Bench::clickScoreDb (b, (int) (0.8 * sr), sr) - Bench::clickScoreDb (plain, (int) (0.8 * sr), sr);
        check ("Gen estructural cambia la nota que suena", c1 < c0 * 0.7, "centroide " + juce::String (c0, 0) + " -> " + juce::String (c1, 0) + " Hz");
        check ("El cambio en vivo no hace clic", jump < 3.0, "salto " + juce::String (jump, 1) + " dB sobre la nota sin cambio");
    }

    std::printf (failures == 0 ? "TODO OK\n" : "%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
