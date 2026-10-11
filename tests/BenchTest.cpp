// ===========================================================================
// Nebula — Banco de pruebas (Paso 0): referencia congelada del motor actual.
//
// No modifica el motor. Mide, con sonidos reproducibles y métricas con unidad:
//   1. Validación de las métricas (controles inaudibles y audibles)
//   2. Fidelidad de RECREAR por sonido (+ repetibilidad, sostener, transponer, A/B)
//   3. Viabilidad del residuo de transitorio (con y sin fase)
//   4. CPU, 5. latencia, 6. estabilidad (fuzz), 7. clics
//   8. Proyectos v3 de referencia (compatibilidad)
// Uso: NebulaBenchTest <carpeta_salida> [--quick] [--fixtures <carpeta>]
//   Con --fixtures y una referencia existente, además comprueba compatibilidad.
// Escribe bench_results.json, bench_summary.md y pares de escucha en <salida>/escucha.
// ===========================================================================
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "bench/Corpus.h"
#include "bench/Metrics.h"
#include "bench/Transient.h"
#include <chrono>
#include <map>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

using namespace Bench;
static constexpr double sr = kSr;
static juce::uint32 kBenchSeed = 0x4E42u;   // --semilla N la cambia (para medir cuánto varían las métricas con el azar)
static juce::File outDir;
static bool quick = false;
static juce::StringArray only;
static bool dumpModels = false;
static juce::StringArray soundFilter;   // --sonidos 808,pad (prefijos; vacío = todos)
static bool wantSound (const juce::String& n) { if (soundFilter.isEmpty()) return true; for (auto& f : soundFilter) if (n.startsWith (f)) return true; return false; }   // --only 0,1,2 ... (vacío = todo)
static bool run (const char* sec) { return only.isEmpty() || only.contains (sec); }
static juce::String md;   // resumen legible

static void line (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); md << s << "\n"; }
static juce::var num (double v, int decimals = 3)
{
    if (! std::isfinite (v)) return {};
    const double k = std::pow (10.0, decimals);
    return std::round (v * k) / k;
}
static juce::String fmt (double v, int d = 2) { return std::isfinite (v) ? juce::String (v, d) : juce::String ("-"); }
using Obj = juce::DynamicObject::Ptr;
static Obj obj() { return new juce::DynamicObject(); }

// ---------- procesador ----------
static std::unique_ptr<NebulaAudioProcessor> makeProc (int block = 512)
{
    std::unique_ptr<NebulaAudioProcessor> p (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    p->setPlayConfigDetails (0, 2, sr, block);
    p->prepareToPlay (sr, block);
    p->setRandomMode (true, kBenchSeed);   // semilla fija: el banco se repite exactamente
    p->reset();
    return p;
}
static void setP (NebulaAudioProcessor& p, const juce::String& id, float v)
{
    if (auto* prm = p.state.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    else std::printf ("  (parametro %s no existe)\n", id.toRawUTF8());
}
// Sin efectos, macros en neutro, calidad 1x: se mide la recreación en sí
static void neutral (NebulaAudioProcessor& p)
{
    for (auto* prm : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if ((id.startsWith ("fx") && id.endsWith ("On")) ) rp->setValueNotifyingHost (0.0f);
            if (id.startsWith ("macro")) rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    setP (p, ParamID::quality, 0.0f);
}

struct Ev { double t; int note; bool on; int vel = 100; };
using BlockHook = std::function<void (int blockStart)>;

static juce::AudioBuffer<float> render (NebulaAudioProcessor& p, const std::vector<Ev>& evs, double seconds, int block = 512, BlockHook hook = {})
{
    p.reset();   // en modo reproducible: cada render empieza desde el mismo estado (en legacy no hace nada)
    const int total = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        if (hook) hook (pos);
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
    // apaga todo y deja sonar las colas para que la siguiente medición empiece en silencio
    juce::MidiBuffer off;
    for (int ch = 1; ch <= 16; ++ch) off.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    buf.setSize (2, block); buf.clear();
    p.processBlock (buf, off);
    juce::MidiBuffer none;
    for (int i = 0; i < (int) (4.0 * sr / block); ++i) { buf.clear(); p.processBlock (buf, none); }
    return out;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), sr, (unsigned) b.getNumChannels(), 24, {}, 0)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
static juce::AudioBuffer<float> toStereo (const juce::AudioBuffer<float>& b, int len = -1)
{
    const int n = len < 0 ? b.getNumSamples() : len;
    juce::AudioBuffer<float> o (2, n); o.clear();
    for (int ch = 0; ch < 2; ++ch) o.copyFrom (ch, 0, b, juce::jmin (ch, b.getNumChannels() - 1), 0, juce::jmin (n, b.getNumSamples()));
    return o;
}
static float peakOf (const juce::AudioBuffer<float>& b) { return b.getMagnitude (0, b.getNumSamples()); }

// Par ciego igualado en sonoridad (-20 LUFS, mismo margen de pico para los dos)
struct PairWriter
{
    juce::File dir; juce::String keyCsv; juce::Random rnd; int count = 0;
    PairWriter (juce::File d, juce::int64 seed) : dir (d), rnd (seed) { keyCsv = "par,sonido,A,B,nota\n"; }
    void add (const juce::String& what, const juce::String& labelX, juce::AudioBuffer<float> x, const juce::String& labelY, juce::AudioBuffer<float> y, const juce::String& note = {})
    {
        const int len = juce::jmax (x.getNumSamples(), y.getNumSamples());
        x = toStereo (x, len); y = toStereo (y, len);
        for (auto* b : { &x, &y })
        {
            const double l = integratedLufs (*b, sr);
            if (l > -100) b->applyGain ((float) std::pow (10.0, (-20.0 - l) / 20.0));
        }
        const float pk = juce::jmax (peakOf (x), peakOf (y));
        if (pk > 0.89f) { x.applyGain (0.89f / pk); y.applyGain (0.89f / pk); }
        const bool swap = rnd.nextBool();
        const auto id = juce::String (++count).paddedLeft ('0', 2);
        writeWav (dir.getChildFile ("par_" + id + "_A.wav"), swap ? y : x);
        writeWav (dir.getChildFile ("par_" + id + "_B.wav"), swap ? x : y);
        keyCsv << id << "," << what << "," << (swap ? labelY : labelX) << "," << (swap ? labelX : labelY) << "," << note << "\n";
    }
    void finish() { dir.getChildFile ("clave_NO_ABRIR_antes_de_escuchar.csv").replaceWithText (keyCsv); }
};

static std::vector<double> monoVec (const juce::AudioBuffer<float>& b) { return toMono (b); }

static double meanFlux (const MelSpec& m, int f0, int f1)
{
    const double floorDb = m.maxDb - 60.0;
    double acc = 0; int n = 0;
    for (int f = juce::jmax (1, f0); f < juce::jmin ((int) m.db.size(), f1); ++f)
    {
        double s = 0;
        for (int b = 0; b < m.bands; ++b) s += std::abs (juce::jmax (m.db[(size_t) f][(size_t) b], floorDb) - juce::jmax (m.db[(size_t) f - 1][(size_t) b], floorDb));
        acc += s; ++n;
    }
    return n > 0 ? acc / n : kNaN;
}
static double stdOf (const std::vector<double>& v, int a, int b)
{
    a = juce::jmax (0, a); b = juce::jmin ((int) v.size(), b);
    if (b - a < 2) return kNaN;
    double m = 0; for (int i = a; i < b; ++i) m += v[(size_t) i]; m /= (b - a);
    double s = 0; for (int i = a; i < b; ++i) s += (v[(size_t) i] - m) * (v[(size_t) i] - m);
    return std::sqrt (s / (b - a));
}
static double medianPitch (const juce::AudioBuffer<float>& b, double t0, double t1, double fmin = 50.0)
{
    const auto x = monoVec (b);
    const auto tr = yinTrack (x, sr, 0.01, fmin);
    std::vector<double> v;
    for (int f = (int) (t0 / 0.01); f < juce::jmin ((int) tr.size(), (int) (t1 / 0.01)); ++f) if (tr[(size_t) f] > 0) v.push_back (std::log2 (tr[(size_t) f]));
    return v.empty() ? kNaN : std::exp2 (median (v));
}

static Obj scoresJson (const Scores& s)
{
    auto o = obj();
    o->setProperty ("lufsRef", num (s.lufsRef, 2));            o->setProperty ("lufsDiffLU", num (s.lufsDiff, 2));
    o->setProperty ("onsetLagMs", num (s.onsetLagMs, 2));
    o->setProperty ("melAllDb", num (s.melAll, 2));            o->setProperty ("melAtkDb", num (s.melAtk, 2));
    o->setProperty ("melBodyDb", num (s.melBody, 2));          o->setProperty ("melTailDb", num (s.melTail, 2));
    o->setProperty ("colorDb", num (s.colorDb, 2));
    o->setProperty ("envErrDb", num (s.envErrDb, 2));
    o->setProperty ("pitchMedCents", num (s.pitchMedCents, 1)); o->setProperty ("pitchP95Cents", num (s.pitchP95Cents, 1));
    o->setProperty ("voicedAgree", num (s.voicedAgree, 3));     o->setProperty ("pitchCentreCents", num (s.pitchCentreCents, 1));
    o->setProperty ("fluxCorr", num (s.fluxCorr, 3));          o->setProperty ("fluxRatio", num (s.fluxRatio, 3));
    o->setProperty ("centroidCorr", num (s.centroidCorr, 3));  o->setProperty ("centroidErrBands", num (s.centroidErr, 2));
    o->setProperty ("attackRefMs", num (s.attackRefMs, 1));    o->setProperty ("attackTestMs", num (s.attackTestMs, 1));
    o->setProperty ("earlyRefDb", num (s.earlyRefDb, 2));      o->setProperty ("earlyTestDb", num (s.earlyTestDb, 2));
    o->setProperty ("stereoCorrDiff", num (s.stereoCorrDiff, 3)); o->setProperty ("sideMidDiffDb", num (s.sideMidDiffDb, 2));
    o->setProperty ("subFracRefDb", num (s.subFracRefDb, 2));   o->setProperty ("subDiffDb", num (s.subDiffDb, 2));
    o->setProperty ("hfFracRefDb", num (s.hfFracRefDb, 2));     o->setProperty ("hfDiffDb", num (s.hfDiffDb, 2));
    return o;
}

// ===========================================================================
// 1. Validación de métricas
// ===========================================================================
struct Control { juce::String id; bool audible; juce::String family; };

static juce::AudioBuffer<float> shelfCut (const juce::AudioBuffer<float>& in, double fc, double gainDb)
{
    // estante de agudos de primer orden: y = x + (g-1)·HP(x)
    auto out = in;
    const double g = std::pow (10.0, gainDb / 20.0);
    const double a = std::exp (-2.0 * kPi * fc / sr);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        double lp = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double x = in.getSample (ch, i);
            lp = (1 - a) * x + a * lp;
            out.setSample (ch, i, (float) (x + (g - 1.0) * (x - lp)));
        }
    }
    return out;
}
static juce::AudioBuffer<float> lowpass (const juce::AudioBuffer<float>& in, double fc)
{
    auto out = in;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        Biquad b; b.lowpass (fc, 0.7071);
        for (int i = 0; i < out.getNumSamples(); ++i) out.setSample (ch, i, (float) b.process (in.getSample (ch, i)));
    }
    return out;
}

static juce::var validateMetrics (PairWriter& pairs)
{
    line ("\n## 1. Validacion de las metricas\n");
    line ("Controles inaudibles (la metrica no debe moverse) y audibles (debe moverse >= 3x su ruido de fondo).\n");
    const std::vector<Control> controls {
        { "semilla_ruido", false, "-" }, { "fases_aleatorias", false, "-" }, { "polaridad", false, "-" },
        { "retraso_2ms", false, "-" }, { "ganancia_-6dB", false, "-" },
        { "afinacion_+10ct", true, "tono" }, { "afinacion_+25ct", true, "tono" },
        { "agudos_-3dB_desde_1kHz", true, "color" },
        { "decaimiento_x0.8", true, "envolvente" }, { "ataque_suave_30ms", true, "ataque" },
        { "evolucion_congelada", true, "evolucion" }, { "estereo_a_mono", true, "estereo" } };
    const std::vector<std::pair<juce::String, Maker>> bases {
        { "pluck_sierra", pluck }, { "pad_vibrato", padVibrato }, { "caja", snare }, { "campana", bell },
        { "cuerda_frotada", bowed }, { "pad_evolutivo_12s", padEvolving }, { "pad_vocal_8s", padVocal }, { "pad_estereo_4s", padStereo } };
    // Dónde se espera que cada control audible se oiga (decidido de antemano por cómo está hecho el sonido)
    auto applies = [] (const juce::String& ctl, const juce::String& snd)
    {
        if (ctl == "decaimiento_x0.8")  return snd == "pluck_sierra" || snd == "caja" || snd == "campana";           // sonidos que decaen
        if (ctl == "ataque_suave_30ms") return snd == "pluck_sierra" || snd == "caja" || snd == "campana";           // ataques < 5 ms
        if (ctl == "evolucion_congelada") return snd == "pad_evolutivo_12s" || snd == "cuerda_frotada" || snd == "pad_vocal_8s";
        if (ctl == "estereo_a_mono")    return snd == "pad_estereo_4s";
        return true;
    };

    // Desviación de cada métrica (siempre >= 0, mayor = más diferente)
    struct M { const char* name; const char* family; std::function<double (const Scores&)> dev; };
    const std::vector<M> metrics {
        { "tono_cents",          "tono",       [] (const Scores& s) { return s.pitchMedCents; } },
        { "color_dB",            "color",      [] (const Scores& s) { return s.colorDb; } },
        { "mel_10ms_dB",         "color",      [] (const Scores& s) { return s.melAll; } },
        { "centroide_bandas",    "color",      [] (const Scores& s) { return s.centroidErr; } },
        { "envolvente_dB",       "envolvente", [] (const Scores& s) { return s.envErrDb; } },
        { "mel_ataque_dB",       "ataque",     [] (const Scores& s) { return s.melAtk; } },
        { "energia_temprana_dB", "ataque",     [] (const Scores& s) { return std::abs (s.earlyTestDb - s.earlyRefDb); } },
        { "tiempo_ataque_ms",    "ataque",     [] (const Scores& s) { return std::abs (s.attackTestMs - s.attackRefMs); } },
        { "evolucion_1-corrFlujo","evolucion", [] (const Scores& s) { return 1.0 - s.fluxCorr; } },
        { "evolucion_1-corrCentroide","evolucion", [] (const Scores& s) { return 1.0 - s.centroidCorr; } },
        { "estereo_corrDif",     "estereo",    [] (const Scores& s) { return s.stereoCorrDiff; } } };

    juce::Array<juce::var> out;
    // [métrica] -> filas de resumen
    std::map<juce::String, juce::Array<juce::var>> perMetric;
    for (auto& [name, mk] : bases)
    {
        const auto ref = mk (Variant {});
        const int len = ref.audio.getNumSamples();
        std::map<juce::String, Scores> sc;
        for (auto& c : controls)
        {
            juce::AudioBuffer<float> t;
            Variant v;
            if (c.id == "semilla_ruido")            { v.seed = 2; t = mk (v).audio; }
            else if (c.id == "fases_aleatorias")    { v.phaseSeed = 3; t = mk (v).audio; }
            else if (c.id == "polaridad")           { t = ref.audio; t.applyGain (-1.0f); }
            else if (c.id == "retraso_2ms")         { t = shifted (ref.audio, -(int) (0.002 * sr), len); }
            else if (c.id == "ganancia_-6dB")       { t = ref.audio; t.applyGain (0.5f); }
            else if (c.id == "afinacion_+10ct")     { if (! ref.pitched) continue; v.cents = 10; t = mk (v).audio; }
            else if (c.id == "afinacion_+25ct")     { if (! ref.pitched) continue; v.cents = 25; t = mk (v).audio; }
            else if (c.id == "agudos_-3dB_desde_1kHz") { t = shelfCut (ref.audio, 1000.0, -3.0); }
            else if (c.id == "decaimiento_x0.8")    { v.decay = 0.8; t = mk (v).audio; }
            else if (c.id == "ataque_suave_30ms")
            {
                t = ref.audio;
                for (int ch = 0; ch < t.getNumChannels(); ++ch)
                    for (int i = 0; i < (int) (0.03 * sr); ++i) t.setSample (ch, i, t.getSample (ch, i) * (float) (0.5 - 0.5 * std::cos (kPi * i / (0.03 * sr))));
            }
            else if (c.id == "evolucion_congelada")
            {
                if (! applies (c.id, name)) continue;
                v.freezeAfter = name == "cuerda_frotada" ? 0.3 : 1.0; t = mk (v).audio;
            }
            else if (c.id == "estereo_a_mono")
            {
                if (ref.audio.getNumChannels() < 2) continue;
                t = ref.audio;
                for (int i = 0; i < len; ++i) { const float m = 0.5f * (t.getSample (0, i) + t.getSample (1, i)); t.setSample (0, i, m); t.setSample (1, i, m); }
            }
            const auto s = compare (ref.audio, t, sr, ref.pitched);
            sc[c.id] = s;
            auto o = scoresJson (s);
            o->setProperty ("sonido", name); o->setProperty ("control", c.id); o->setProperty ("audible", c.audible);
            out.add (o.get());
            // Pares de escucha: sólo algunos controles representativos (para no hacer la sesión eterna)
            if (c.id == "fases_aleatorias" || c.id == "afinacion_+10ct" || c.id == "agudos_-3dB_desde_1kHz" || c.id == "evolucion_congelada" || c.id == "semilla_ruido")
                pairs.add (name + " / " + c.id, "original", ref.audio, c.id, t, c.audible ? "deberia oirse" : "no deberia oirse");
        }
        // Resumen por métrica
        for (auto& m : metrics)
        {
            double floorV = 0; juce::String floorBy;
            for (auto& c : controls) if (! c.audible && sc.count (c.id)) { const double d = m.dev (sc[c.id]); if (std::isfinite (d) && d > floorV) { floorV = d; floorBy = c.id; } }
            for (auto& c : controls)
            {
                if (! c.audible || c.family != juce::String (m.family) || ! sc.count (c.id) || ! applies (c.id, name)) continue;
                const double r = m.dev (sc[c.id]);
                if (! std::isfinite (r)) continue;
                auto row = obj();
                row->setProperty ("sonido", name); row->setProperty ("control", c.id);
                row->setProperty ("fondo", num (floorV, 3)); row->setProperty ("fondoPor", floorBy);
                row->setProperty ("respuesta", num (r, 3));
                const double ratio = r / juce::jmax (1e-9, floorV);
                row->setProperty ("razon", num (juce::jmin (ratio, 9999.0), 1));
                row->setProperty ("pasa", r >= 3.0 * floorV);
                perMetric[m.name].add (row.get());
            }
        }
    }

    auto summary = obj();
    line ("| Metrica | Control audible | Casos | Pasan (>=3x fondo) | Fondo max | Respuesta min |");
    line ("| --- | --- | --- | --- | --- | --- |");
    for (auto& m : metrics)
    {
        auto& rows = perMetric[m.name];
        std::map<juce::String, std::tuple<int, int, double, double>> byCtl;
        for (auto& r : rows)
        {
            auto& t = byCtl[r["control"].toString()];
            ++std::get<0> (t); if ((bool) r["pasa"]) ++std::get<1> (t);
            std::get<2> (t) = juce::jmax (std::get<2> (t), (double) r["fondo"]);
            const double resp = (double) r["respuesta"];
            std::get<3> (t) = std::get<0> (t) == 1 ? resp : juce::jmin (std::get<3> (t), resp);
        }
        for (auto& [ctl, t] : byCtl)
            line ("| " + juce::String (m.name) + " | " + ctl + " | " + juce::String (std::get<0> (t)) + " | " + juce::String (std::get<1> (t))
                  + " | " + fmt (std::get<2> (t), 3) + " | " + fmt (std::get<3> (t), 3) + " |");
        summary->setProperty (m.name, rows);
    }
    auto res = obj();
    res->setProperty ("controles", out);
    res->setProperty ("resumen", summary.get());
    return res.get();
}

// ===========================================================================
// 2. Fidelidad de RECREAR
// ===========================================================================
struct RecreatedCase { Sound sound; juce::AudioBuffer<float> rec; int key = 60; };

static juce::var engineFidelity (PairWriter& pairs, std::vector<RecreatedCase>& keep)
{
    line ("\n## 2. Fidelidad de RECREAR (motor actual)\n");
    line ("| Sonido | Cat. | Frames | Parc. | Ruido | Arm. | Recrear s | % UI (heuristico) | Raiz err ct | Color dB | Mel 10ms atq/cuerpo/cola | Env dB | Nota ct | Curva tono ct | Corr centroide | Corr flujo | Temprana ref/rec dB | Estereo dif | LU rec | LU A/B | Repet. dB | NaN | Sub dif dB | Agudos dif dB |");
    line ("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |");
    juce::Array<juce::var> rows;
    for (auto& e : corpus())
    {
        if (! wantSound (e.name)) continue;
        const auto s = e.make (Variant {});
        auto p = makeProc();
        p->loadPreset (0);
        p->setSample (SampleTools::fromBuffer (s.audio, sr, s.name));
        const auto t0 = std::chrono::steady_clock::now();
        const auto res = p->recreateFromSample();
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        neutral (*p);
        auto m = p->resynthModel();
        auto o = obj();
        o->setProperty ("sonido", s.name); o->setProperty ("categoria", s.category); o->setProperty ("notas", s.notes);
        o->setProperty ("recrearSegundos", num (secs, 3));
        o->setProperty ("resultado", res == NebulaAudioProcessor::RecreateResult::Resynthesized ? "resintesis" : "fallo");
        if (m == nullptr) { line ("| " + s.name + " | FALLO |"); rows.add (o.get()); continue; }
        const float uiSim = p->lastSimilarity.load();
        const int key = juce::jlimit (24, 96, (int) std::round (m->rootNote));
        o->setProperty ("modelo", [&] {
            auto mo = obj();
            mo->setProperty ("frames", m->frames()); mo->setProperty ("parciales", (int) m->partials.size());
            mo->setProperty ("bandasRuido", m->noise.empty() ? 0 : ResynthModel::numBands);
            mo->setProperty ("tablaArmonica", m->harmonic != nullptr);
            mo->setProperty ("sostenido", m->sustained);
            mo->setProperty ("ultimoFrameS", num (m->times.empty() ? 0.0 : m->times.back(), 3));
            mo->setProperty ("duracionS", num (s.audio.getNumSamples() / sr, 3));
            mo->setProperty ("raizDetectada", num (m->rootNote, 2));
            return juce::var (mo.get());
        }());
        {
            juce::MemoryBlock mb; p->getStateInformation (mb);
            o->getProperty ("modelo").getDynamicObject()->setProperty ("estadoProyectoKB", num (mb.getSize() / 1024.0, 1));
        }
        o->setProperty ("similitudUI_heuristica", num (uiSim, 3));
        if (dumpModels)
        {
            // Contenido interno del modelo, para diagnosticar (frames, ruido, armónicos de cada ciclo, curva de tono)
            auto dm = obj();
            juce::Array<juce::var> tms, nz, hs, pc, he;
            for (int f = 0; f < m->frames(); ++f)
            {
                tms.add (num (m->times[(size_t) f], 4));
                double e = 0; if ((size_t) f < m->noise.size()) for (auto v : m->noise[(size_t) f]) e += (double) v * v;
                nz.add (num (20.0 * std::log10 (std::sqrt (e) + 1e-9), 1));
                if (m->harmonic != nullptr)
                {
                    const float* cyc = m->harmonic->rawFrame (f);
                    juce::dsp::FFT fft (11); std::vector<float> b (4096, 0.0f);
                    for (int i = 0; i < 2048; ++i) b[(size_t) i] = cyc[i];
                    fft.performFrequencyOnlyForwardTransform (b.data(), true);
                    juce::Array<juce::var> row; const double ref = b[1] + 1e-9;
                    for (int k = 1; k <= 12; ++k) row.add (num (20.0 * std::log10 (b[(size_t) k] / ref + 1e-9), 1));
                    hs.add (row);
                }
            }
            for (size_t i = 0; i < m->pitchCurve.size(); i += 5) pc.add (num (m->pitchCurve[i], 1));
            for (size_t i = 0; i < m->harmEnv.size(); i += 10) he.add (num (20.0 * std::log10 (m->harmEnv[i] + 1e-9), 1));
            dm->setProperty ("tiempos", tms); dm->setProperty ("ruidoDb", nz); dm->setProperty ("armonicosDb", hs);
            dm->setProperty ("curvaTonoCents_cada50ms", pc); dm->setProperty ("envArmonicaDb_cada50ms", he);
            dm->setProperty ("raiz", num (m->rootNote, 2));
            outDir.getChildFile ("modelo_" + s.name + ".json").replaceWithText (juce::JSON::toString (dm.get(), false));
        }
        const double rootErr = s.nominalNote >= 0 && s.pitched ? (m->rootNote - s.nominalNote) * 100.0 : kNaN;
        o->setProperty ("raizErrorCents", num (rootErr, 1));
        o->setProperty ("teclaUsada", key);

        const double len = s.audio.getNumSamples() / sr;
        auto rec = render (*p, { { 0.0, key, true }, { len, key, false } }, len + 1.5);
        auto rec2 = render (*p, { { 0.0, key, true }, { len, key, false } }, len + 1.5);
        long bad = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < rec.getNumSamples(); ++i) if (! std::isfinite (rec.getSample (ch, i))) ++bad;
        o->setProperty ("muestrasNoFinitas", (juce::int64) bad);
        if (bad > 0)
        {
            line ("  ATENCION: " + s.name + " produce " + juce::String (bad) + " muestras NaN/Inf (ancho del modelo " + fmt (m->width, 2) + ")");
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < rec.getNumSamples(); ++i) if (! std::isfinite (rec.getSample (ch, i))) rec.setSample (ch, i, 0.0f);
        }
        o->getProperty ("modelo").getDynamicObject()->setProperty ("anchoEstereo", num (m->width, 3));
        const double lowHz = s.nominalNote > 0 && midiHz (s.nominalNote) < 70.0 ? 25.0 : 50.0;
        const auto sc = compare (s.audio, rec, sr, s.pitched, 0.0, lowHz);
        o->setProperty ("metricas", scoresJson (sc).get());

        // Repetibilidad: dos renders iguales del mismo modelo
        double d = 0, r = 0;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < rec.getNumSamples(); ++i) { const double a = rec.getSample (ch, i), b = rec2.getSample (ch, i); d += (a - b) * (a - b); r += a * a; }
        const double nullDb = db (d / juce::jmax (1e-20, r));
        const auto sRep = compare (toStereo (rec, s.audio.getNumSamples()), rec2, sr, s.pitched);
        o->setProperty ("repeticionNullDb", num (nullDb, 1));
        o->setProperty ("repeticionMelDb", num (sRep.melAll, 3));

        // A/B actual (el original tocado por la voz, igualado por pico)
        p->setAB (true);
        auto ab = render (*p, { { 0.0, key, true }, { len, key, false } }, len + 1.5);
        p->setAB (false);
        const double lRec = integratedLufs (rec, sr), lAb = integratedLufs (ab, sr);
        o->setProperty ("abSaltoLU", num (lAb - lRec, 2));

        // Sostener el doble de largo (¿sigue evolucionando o se congela?)
        if (s.category == "pad" || s.name == "cuerda_frotada" || s.name == "pad_vibrato" || s.name == "voz_a")
        {
            auto held = render (*p, { { 0.0, key, true }, { 2.0 * len, key, false } }, 2.0 * len + 1.0);
            const auto mr = melSpectrogram (monoVec (s.audio), sr), mh = melSpectrogram (monoVec (held), sr);
            const auto cr = loudnessCurve (s.audio, sr), chd = loudnessCurve (held, sr);
            const double cmax = *std::max_element (cr.begin(), cr.end());
            int b0 = 0; while (b0 < (int) cr.size() && cr[(size_t) b0] < cmax - 12) ++b0;
            int b1 = (int) cr.size() - 1; while (b1 > 0 && cr[(size_t) b1] < cmax - 12) --b1;
            const int h0 = (int) ((len + 0.3) / 0.01), h1 = (int) ((2.0 * len - 0.1) / 0.01);
            const double fRef = meanFlux (mr, b0 + 10, b1), fHeld = meanFlux (mh, h0, h1), fHeldBody = meanFlux (mh, b0 + 10, b1);
            auto ho = obj();
            ho->setProperty ("flujoRefCuerpo", num (fRef, 3));
            ho->setProperty ("flujoRecCuerpo", num (fHeldBody, 3));
            ho->setProperty ("flujoRecDespuesDelFinal", num (fHeld, 3));
            ho->setProperty ("movimientoDespuesDelFinal", num (fHeld / juce::jmax (1e-9, fRef), 3));
            ho->setProperty ("desvCentroideRefCuerpo", num (stdOf (mr.centroid, b0, b1), 3));
            ho->setProperty ("desvCentroideDespuesDelFinal", num (stdOf (mh.centroid, h0, h1), 3));
            double lb = 0; int nb = 0; for (int f = b0; f < b1 && f < (int) chd.size(); ++f) { lb += chd[(size_t) f]; ++nb; }
            double lh = 0; int nh = 0; for (int f = h0; f < h1 && f < (int) chd.size(); ++f) { lh += chd[(size_t) f]; ++nh; }
            ho->setProperty ("nivelDespuesMenosCuerpoDb", num (lh / juce::jmax (1, nh) - lb / juce::jmax (1, nb), 2));
            o->setProperty ("sostenerDoble", ho.get());
            writeWav (outDir.getChildFile ("renders/sostener_" + s.name + ".wav"), held);
        }

        // Transponer (sólo sonidos con nota)
        if (s.pitched && s.category != "inarmonico")   // YIN no es fiable con parciales inarmónicos
        {
            const double refHz = medianPitch (s.audio, 0.1, juce::jmin (len, 1.2), lowHz);
            auto to = obj(); double worst = 0;
            for (int dsemi : { -12, -7, 7, 12 })
            {
                auto tr = render (*p, { { 0.0, key + dsemi, true }, { 1.2, key + dsemi, false } }, 1.5);
                const double hz = medianPitch (tr, 0.1, 1.2, lowHz);
                const double err = 1200.0 * std::log2 (hz / (refHz * std::exp2 (dsemi / 12.0)));
                to->setProperty (juce::String (dsemi), num (err, 1));
                if (std::isfinite (err)) worst = juce::jmax (worst, std::abs (err));
            }
            to->setProperty ("peorCents", num (worst, 1));
            o->setProperty ("transponerErrorCents", to.get());
        }

        writeWav (outDir.getChildFile ("renders/recreado_" + s.name + ".wav"), rec);
        writeWav (outDir.getChildFile ("renders/original_" + s.name + ".wav"), s.audio);
        pairs.add (s.name, "original", s.audio, "recreacion_v3", rec, "% UI " + fmt (uiSim * 100.0, 0));

        line ("| " + s.name + " | " + s.category + " | " + juce::String (m->frames()) + " | " + juce::String ((int) m->partials.size())
              + " | " + (m->noise.empty() ? "no" : "si") + " | " + (m->harmonic ? "si" : "no") + " | " + fmt (secs, 2) + " | " + fmt (uiSim * 100, 0)
              + " | " + fmt (rootErr, 1) + " | " + fmt (sc.colorDb, 2) + " | " + fmt (sc.melAtk, 1) + " / " + fmt (sc.melBody, 1) + " / " + fmt (sc.melTail, 1)
              + " | " + fmt (sc.envErrDb, 1) + " | " + fmt (sc.pitchCentreCents, 1) + " | " + fmt (sc.pitchMedCents, 1) + " / " + fmt (sc.pitchP95Cents, 1)
              + " | " + fmt (sc.centroidCorr, 2) + " | " + fmt (sc.fluxCorr, 2)
              + " | " + fmt (sc.earlyRefDb, 1) + " / " + fmt (sc.earlyTestDb, 1) + " | " + fmt (sc.stereoCorrDiff, 2)
              + " | " + fmt (sc.lufsDiff, 1) + " | " + fmt (lAb - lRec, 1) + " | " + fmt (nullDb, 0) + " | " + juce::String (bad) + " | " + fmt (sc.subDiffDb, 2) + " | " + fmt (sc.hfDiffDb, 2) + " |");
        rows.add (o.get());
        keep.push_back ({ s, rec, key });
    }
    return rows;
}

// ===========================================================================
// 3. Residuo del transitorio
// ===========================================================================
static juce::var transientStudy (const std::vector<RecreatedCase>& cases)
{
    line ("\n## 3. Viabilidad del residuo de transitorio\n");
    line ("SDR del transitorio estimado (dB, mas alto = mejor) / fuga tonal tras el ataque (dB respecto al original, mas bajo = mejor).\n");
    line ("| Sonido | Verdad ms | A tiempo (fase motor) | B magnitud (motor) | C tiempo (fase medida, 21 ms) | D magnitud (modelo) | E tiempo (fase medida, 11 ms) | Det. flujo ms | Det. residuo C a -20/-30/-40 dB ms |");
    line ("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
    juce::Array<juce::var> rows;
    for (auto& c : cases)
    {
        if (c.sound.transient.getNumSamples() == 0) continue;
        const auto x = monoVec (c.sound.audio), truth = monoVec (c.sound.transient);
        const int n = (int) x.size();
        const int lag = alignLag (x, monoVec (c.rec), sr);
        auto yb = shifted (c.rec, lag, n);
        auto y = monoVec (yb);
        // ganancia de mínimos cuadrados en el cuerpo (0.2-1.0 s)
        double xy = 0, yy = 0;
        for (int i = (int) (0.2 * sr); i < juce::jmin (n, (int) (1.0 * sr)); ++i) { xy += x[(size_t) i] * y[(size_t) i]; yy += y[(size_t) i] * y[(size_t) i]; }
        const double g = yy > 1e-12 ? xy / yy : 1.0;
        for (auto& v : y) v *= g;
        Vec rA (x.size()); for (size_t i = 0; i < x.size(); ++i) rA[i] = x[i] - y[i];
        const auto rB = magnitudeSubtract (x, y);
        const auto hm = harmonicModel (x, sr, midiHz (c.sound.nominalNote));
        Vec rC (x.size()); for (size_t i = 0; i < x.size(); ++i) rC[i] = x[i] - hm[i];
        const auto rD = magnitudeSubtract (x, hm);
        const auto hm512 = harmonicModel (x, sr, midiHz (c.sound.nominalNote), 512, 64, false);
        Vec rE (x.size()); for (size_t i = 0; i < x.size(); ++i) rE[i] = x[i] - hm512[i];

        const double d = c.sound.trueAttackMs;
        const int w1 = (int) ((juce::jmax (d, 5.0) + 30.0) / 1000.0 * sr);
        const int l1 = juce::jmin (n, w1 + (int) (0.5 * sr));
        auto o = obj();
        o->setProperty ("sonido", c.sound.name); o->setProperty ("verdadMs", d);
        const char* names[] { "A_tiempo_faseMotor", "B_magnitud_motor", "C_tiempo_faseMedida", "D_magnitud_modelo", "E_tiempo_faseMedida_ventana512" };
        const Vec* rs[] { &rA, &rB, &rC, &rD, &rE };
        juce::String cells;
        for (int k = 0; k < 5; ++k)
        {
            auto mo = obj();
            const double sdr = d > 0 ? sdrDb (truth, *rs[k], 0, w1) : kNaN;
            const double falso = d <= 0 ? energyRatioDb (*rs[k], x, 0, w1) : kNaN;   // sin transitorio real: lo que se inventa
            const double fuga = energyRatioDb (*rs[k], x, w1, l1);
            mo->setProperty ("sdrDb", num (sdr, 1)); mo->setProperty ("transitorioFalsoDb", num (falso, 1)); mo->setProperty ("fugaTonalDb", num (fuga, 1));
            o->setProperty (names[k], mo.get());
            cells << " | " << (d > 0 ? fmt (sdr, 1) : "falso " + fmt (falso, 1)) << " / " << fmt (fuga, 1);
        }
        const double dFlux = detectByFlux (x, sr);
        const double d20 = detectByResidual (x, rC, sr, -20), d30 = detectByResidual (x, rC, sr, -30), d40 = detectByResidual (x, rC, sr, -40);
        o->setProperty ("detectadoFlujoMs", num (dFlux, 0));
        o->setProperty ("detectadoResiduoC_20dB_Ms", num (d20, 0)); o->setProperty ("detectadoResiduoC_30dB_Ms", num (d30, 0)); o->setProperty ("detectadoResiduoC_40dB_Ms", num (d40, 0));
        line ("| " + c.sound.name + " | " + fmt (d, 0) + cells + " | " + fmt (dFlux, 0) + " | " + fmt (d20, 0) + " / " + fmt (d30, 0) + " / " + fmt (d40, 0) + " |");
        rows.add (o.get());
        if (c.sound.name == "piano_sint" || c.sound.name == "transitorio_50ms")
        {
            auto toBuf = [&] (const Vec& v) { juce::AudioBuffer<float> b (1, (int) v.size()); for (size_t i = 0; i < v.size(); ++i) b.setSample (0, (int) i, (float) v[i]); return b; };
            writeWav (outDir.getChildFile ("renders/residuo_" + c.sound.name + "_verdad.wav"), toBuf (truth));
            writeWav (outDir.getChildFile ("renders/residuo_" + c.sound.name + "_A_tiempo.wav"), toBuf (rA));
            writeWav (outDir.getChildFile ("renders/residuo_" + c.sound.name + "_B_magnitud.wav"), toBuf (rB));
            writeWav (outDir.getChildFile ("renders/residuo_" + c.sound.name + "_C_fase_medida.wav"), toBuf (rC));
        }
    }
    return rows;
}

// ===========================================================================
// 4. CPU
// ===========================================================================
static juce::var cpuBench (std::shared_ptr<const SampleData> padSample)
{
    line ("\n## 4. CPU (un nucleo, % del tiempo real, mediana)\n");
    line ("| Motor | Bloque | Calidad | Efectos | % tiempo real |");
    line ("| --- | --- | --- | --- | --- |");
    juce::Array<juce::var> rows;
    const int reps = quick ? 1 : 3;
    const double secs = quick ? 4.0 : 10.0;
    std::vector<Ev> chord;
    const int notes[] { 48, 52, 55, 59, 62, 64, 67, 71 };
    for (int k = 0; k < 8; ++k) chord.push_back ({ 0.01 * k, notes[k], true });
    for (int engine : { 2, 0 })
        for (int block : { 128, 512 })
            for (int q : { 0, 1, 2 })
                for (bool fxOn : { false, true })
                {
                    if (engine == 0 && (fxOn || q == 2)) continue;   // el sinte sólo como referencia
                    auto p = makeProc (block);
                    if (engine == 2) { p->loadPreset (0); p->setSample (padSample); p->recreateFromSample(); }
                    else p->loadPreset (8);   // Super Saw Lead (uno de los más pesados)
                    neutral (*p);
                    setP (*p, ParamID::quality, (float) q);
                    if (fxOn) for (auto id : { ParamID::fxEqOn, ParamID::fxCompOn, ParamID::fxChorusOn, ParamID::fxDelayOn, ParamID::fxRevOn }) setP (*p, id, 1.0f);
                    std::vector<double> t;
                    for (int r = 0; r < reps; ++r)
                    {
                        const auto a = std::chrono::steady_clock::now();
                        // render sin cola (medimos sólo el tramo con 8 notas)
                        const int total = (int) (secs * sr);
                        juce::AudioBuffer<float> buf (2, block);
                        for (int pos = 0; pos < total; pos += block)
                        {
                            juce::MidiBuffer midi;
                            if (pos == 0) for (auto& e : chord) midi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100), 0);
                            buf.clear();
                            p->processBlock (buf, midi);
                        }
                        t.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count());
                        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0); buf.clear(); p->processBlock (buf, off);
                        juce::MidiBuffer none; for (int i = 0; i < (int) (4 * sr / block); ++i) { buf.clear(); p->processBlock (buf, none); }
                    }
                    const double pct = 100.0 * median (t) / secs;
                    auto o = obj();
                    o->setProperty ("motor", engine == 2 ? "Resintesis" : "Sinte (Super Saw Lead)");
                    o->setProperty ("bloque", block); o->setProperty ("calidad", q == 0 ? "1x" : q == 1 ? "2x" : "4x");
                    o->setProperty ("efectos", fxOn); o->setProperty ("voces", 8);
                    o->setProperty ("porcentajeTiempoReal", num (pct, 1));
                    rows.add (o.get());
                    line ("| " + o->getProperty ("motor").toString() + " | " + juce::String (block) + " | " + o->getProperty ("calidad").toString()
                          + " | " + (fxOn ? "todos" : "ninguno") + " | " + fmt (pct, 1) + " |");
                }
    return rows;
}

// ===========================================================================
// 5. Latencia
// ===========================================================================
static juce::var latencyBench()
{
    line ("\n## 5. Latencia\n");
    line ("| Calidad | Reportada al DAW (muestras) | Medida (muestras) |");
    line ("| --- | --- | --- |");
    juce::Array<juce::var> rows;
    juce::AudioBuffer<float> click (1, (int) (0.5 * sr)); click.clear();
    click.setSample (0, (int) (0.1 * sr), 0.9f);
    int base = -1;
    for (int q : { 0, 1, 2 })
    {
        auto p = makeProc (512);
        p->loadPreset (0);
        p->setSample (SampleTools::fromBuffer (click, sr, "click"));
        neutral (*p);
        setP (*p, ParamID::engine, (float) Engine::Sample);
        setP (*p, ParamID::ampA, 0.0f); setP (*p, ParamID::ampS, 1.0f);
        setP (*p, ParamID::quality, (float) q);
        auto out = render (*p, { { 0.0, 60, true }, { 0.4, 60, false } }, 0.5);
        int at = 0; float pk = 0;
        for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > pk) { pk = std::abs (out.getSample (0, i)); at = i; }
        if (q == 0) base = at;
        auto o = obj();
        o->setProperty ("calidad", q == 0 ? "1x" : q == 1 ? "2x" : "4x");
        o->setProperty ("reportada", p->getLatencySamples());
        o->setProperty ("pico1x", base); o->setProperty ("pico", at);
        o->setProperty ("medidaRespecto1x", at - base);
        o->setProperty ("picoAmplitud", num (pk, 3));
        rows.add (o.get());
        line ("| " + o->getProperty ("calidad").toString() + " | " + juce::String (p->getLatencySamples()) + " | " + juce::String (at - base)
              + (pk < 0.05f ? " (pico debil " + fmt (pk, 3) + ")" : "") + " |");
    }
    return rows;
}

// ===========================================================================
// 6. Estabilidad (fuzz con semilla)
// ===========================================================================
static juce::var stabilityBench (std::shared_ptr<const SampleData> drum, std::shared_ptr<const SampleData> pad, std::shared_ptr<const SampleData> wide)
{
    line ("\n## 6. Estabilidad (notas y automatizacion aleatorias, semilla fija)\n");
    line ("| Motor | Segundos | NaN/Inf | Pico | Muestras >1.0 | Bloque mas lento (x tiempo real) | Silencio: bloque mas lento | Notas | Cambios de parametro |");
    line ("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
    juce::Array<juce::var> rows;
    const double seconds = quick ? 30.0 : 200.0;
    for (int engine : { 0, 1, 2, 3 })   // 3 = resíntesis de un pad estéreo ancho (el caso que daba NaN)
    {
        auto p = makeProc (512);
        juce::Random rnd (2026 + engine);
        p->loadPreset (rnd.nextInt ((int) factoryPresets().size()));
        if (engine == 1) { p->setSample (drum); setP (*p, ParamID::engine, (float) Engine::Sample); }
        if (engine == 2) { p->setSample (pad); p->recreateFromSample(); }
        if (engine == 3) { p->setSample (wide); p->recreateFromSample(); }
        std::vector<juce::RangedAudioParameter*> prms;
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                const auto id = rp->getParameterID();
                if (id == ParamID::engine || id == ParamID::quality || id == ParamID::mpeOn) continue;
                prms.push_back (rp);
            }
        long nan = 0, over = 0; double pk = 0, worst = 0, dc = 0; long samples = 0; int notesPlayed = 0, changes = 0;
        std::vector<int> held;
        juce::AudioBuffer<float> buf (2, 512);
        const int total = (int) (seconds * sr);
        for (int pos = 0; pos < total;)
        {
            const int n = 32 << rnd.nextInt (5);   // 32..512
            buf.setSize (2, n, false, false, true); buf.clear();
            juce::MidiBuffer midi;
            if (rnd.nextFloat() < 0.15f && held.size() < 24) { const int nt = 36 + rnd.nextInt (49); midi.addEvent (juce::MidiMessage::noteOn (1, nt, (juce::uint8) (1 + rnd.nextInt (127))), rnd.nextInt (n)); held.push_back (nt); ++notesPlayed; }
            if (rnd.nextFloat() < 0.12f && ! held.empty()) { const int k = rnd.nextInt ((int) held.size()); midi.addEvent (juce::MidiMessage::noteOff (1, held[(size_t) k]), rnd.nextInt (n)); held.erase (held.begin() + k); }
            if (rnd.nextFloat() < 0.05f) midi.addEvent (juce::MidiMessage::pitchWheel (1, rnd.nextInt (16384)), 0);
            if (rnd.nextFloat() < 0.3f) for (int k = 0; k < 2; ++k) { prms[(size_t) rnd.nextInt ((int) prms.size())]->setValueNotifyingHost (rnd.nextFloat()); ++changes; }
            if (rnd.nextFloat() < 0.002f) setP (*p, ParamID::quality, (float) rnd.nextInt (3));
            const auto a = std::chrono::steady_clock::now();
            p->processBlock (buf, midi);
            const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
            worst = juce::jmax (worst, el / (n / sr));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) { ++nan; continue; }
                    pk = juce::jmax (pk, (double) std::abs (v));
                    if (std::abs (v) > 1.0f) ++over;
                    dc += v; ++samples;
                }
            pos += n;
        }
        // silencio después: ¿alguna cola deja el procesador lento (denormales)?
        juce::MidiBuffer off; for (int ch = 1; ch <= 16; ++ch) off.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        buf.setSize (2, 512); buf.clear(); p->processBlock (buf, off);
        double worstSilent = 0;
        for (int i = 0; i < (int) (10.0 * sr / 512); ++i)
        {
            buf.clear();
            juce::MidiBuffer none;
            const auto a = std::chrono::steady_clock::now();
            p->processBlock (buf, none);
            if (i > 400) worstSilent = juce::jmax (worstSilent, std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count() / (512 / sr));
        }
        auto o = obj();
        const char* en[] { "Sinte", "Sample", "Resintesis", "Resintesis estereo ancho" };
        o->setProperty ("motor", en[engine]); o->setProperty ("segundos", seconds);
        o->setProperty ("nanInf", (juce::int64) nan); o->setProperty ("pico", num (pk, 3));
        o->setProperty ("muestrasSobre1", (juce::int64) over);
        o->setProperty ("fraccionSobre1", num ((double) over / juce::jmax (1L, samples), 6));
        o->setProperty ("dcMedio", num (dc / juce::jmax (1L, samples), 6));
        o->setProperty ("bloqueMasLentoXTiempoReal", num (worst, 3));
        o->setProperty ("silencioBloqueMasLentoXTiempoReal", num (worstSilent, 4));
        o->setProperty ("notas", notesPlayed); o->setProperty ("cambiosParametro", changes);
        rows.add (o.get());
        line ("| " + juce::String (en[engine]) + " | " + fmt (seconds, 0) + " | " + juce::String (nan) + " | " + fmt (pk, 3) + " | " + juce::String (over)
              + " | " + fmt (worst, 2) + " | " + fmt (worstSilent, 3) + " | " + juce::String (notesPlayed) + " | " + juce::String (changes) + " |");
    }
    return rows;
}

// ===========================================================================
// 7. Clics
// ===========================================================================
static juce::var clickBench (std::shared_ptr<const SampleData> padSample)
{
    line ("\n## 7. Clics (pico de 2a derivada sobre su entorno, dB)\n");
    line ("| Evento | Con evento | Control sin evento | Diferencia |");
    line ("| --- | --- | --- | --- |");
    juce::Array<juce::var> rows;
    auto make = [&] { auto p = makeProc (512); p->loadPreset (0); p->setSample (padSample); p->recreateFromSample(); neutral (*p); return p; };
    auto add = [&] (const juce::String& name, double withEv, double ctl)
    {
        auto o = obj(); o->setProperty ("evento", name); o->setProperty ("conEventoDb", num (withEv, 1)); o->setProperty ("controlDb", num (ctl, 1));
        o->setProperty ("diferenciaDb", num (withEv - ctl, 1)); rows.add (o.get());
        line ("| " + name + " | " + fmt (withEv, 1) + " | " + fmt (ctl, 1) + " | " + fmt (withEv - ctl, 1) + " |");
    };
    const int at = (int) (2.0 * sr);
    // (a) nota nueva y robo de voz. Ataque de 300 ms: la nota nueva no debería producir ningún salto.
    //     El límite de voces por defecto es 12 (parámetro "Voces").
    //     A = 12 sostenidas + una 13a (roba una voz); B = 11 + una 12a (sin robo); C = 11 sin nota nueva.
    for (int engine : { 2, 0 })
    {
        auto p = engine == 2 ? make() : [&] { auto q = makeProc (512); q->loadPreset (2); neutral (*q); return q; }();
        setP (*p, ParamID::ampA, 0.3f);
        std::vector<Ev> a, b, c;
        const int limit = (int) p->state.getRawParameterValue (ParamID::voiceCount)->load();
        for (int k = 0; k < limit; ++k) { a.push_back ({ 0.05 * k, 40 + 2 * k, true }); if (k < limit - 1) { b.push_back ({ 0.05 * k, 40 + 2 * k, true }); c.push_back ({ 0.05 * k, 40 + 2 * k, true }); } }
        a.push_back ({ 2.0, 75, true }); b.push_back ({ 2.0, 75, true });
        auto ra = render (*p, a, 3.0), rb = render (*p, b, 3.0), rc = render (*p, c, 3.0);
        const juce::String en = engine == 2 ? "Resintesis" : "Sinte (Pad Nube)";
        add (en + ": nota nueva con ataque 300 ms", clickScoreDb (rb, at, sr), clickScoreDb (rc, at, sr));
        add (en + ": robo de voz (" + juce::String (limit + 1) + "a nota, limite " + juce::String (limit) + " voces)", clickScoreDb (ra, at, sr), clickScoreDb (rb, at, sr));
        if (engine == 2) { writeWav (outDir.getChildFile ("renders/clic_robo_con.wav"), ra); writeWav (outDir.getChildFile ("renders/clic_nota_nueva.wav"), rb); writeWav (outDir.getChildFile ("renders/clic_sin_nota.wav"), rc); }
    }
    // (a2) una sola nota desde silencio con ataque de 300 ms: ¿empieza en cero?
    for (int engine : { 2, 0 })
    {
        auto p = engine == 2 ? make() : [&] { auto q = makeProc (512); q->loadPreset (2); neutral (*q); return q; }();
        setP (*p, ParamID::ampA, 0.3f);
        auto r = render (*p, { { 0.5, 60, true } }, 1.5);
        const int on = (int) (0.5 * sr);
        auto pk = [&] (int a, int b) { float m = 0; for (int i = a; i < b; ++i) m = juce::jmax (m, std::abs (r.getSample (0, i))); return m; };
        const float first = pk (on, on + 48), at300 = pk (on + (int) (0.29 * sr), on + (int) (0.31 * sr));
        auto o = obj();
        const juce::String en = engine == 2 ? "Resintesis" : "Sinte (Pad Nube)";
        o->setProperty ("evento", en + ": nivel en el 1er ms de una nota con ataque 300 ms (dB respecto a 300 ms)");
        o->setProperty ("valorDb", num (20.0 * std::log10 (juce::jmax (1e-9f, first) / juce::jmax (1e-9f, at300)), 1));
        rows.add (o.get());
        line ("| " + en + ": nivel del 1er ms con ataque 300 ms (esperado < -40 dB) | " + fmt (20.0 * std::log10 (juce::jmax (1e-9f, first) / juce::jmax (1e-9f, at300)), 1) + " dB | | |");
        if (engine == 2) writeWav (outDir.getChildFile ("renders/nota_sola_ataque300.wav"), r);
    }
    // (b) salto de corte del filtro 20 kHz -> 300 Hz con un acorde sostenido
    {
        auto p = make();
        std::vector<Ev> chord; for (int k = 0; k < 4; ++k) chord.push_back ({ 0.0, 48 + 4 * k, true });
        auto ctl = render (*p, chord, 3.0);
        auto jump = render (*p, chord, 3.0, 512, [&] (int pos) { setP (*p, ParamID::cutoff, pos >= at ? 300.0f : 20000.0f); });
        setP (*p, ParamID::cutoff, 20000.0f);
        add ("salto de corte del filtro", clickScoreDb (jump, at - at % 512 + 512 * (at % 512 != 0), sr), clickScoreDb (ctl, at, sr));
        // (c) salto de volumen (suavizado en v3: control)
        auto vol = render (*p, chord, 3.0, 512, [&] (int pos) { setP (*p, ParamID::master, pos >= at ? -18.0f : -6.0f); });
        add ("salto de volumen master (-6 a -18 dB)", clickScoreDb (vol, at - at % 512 + 512 * (at % 512 != 0), sr), clickScoreDb (ctl, at, sr));
        // controles de la métrica: escalón de -40 dBFS (debe detectarse) y un punto cualquiera (no debe)
        auto step = ctl; for (int ch = 0; ch < 2; ++ch) for (int i = at; i < step.getNumSamples(); ++i) step.addSample (ch, i, 0.01f);
        add ("CONTROL escalon -40 dBFS", clickScoreDb (step, at, sr), clickScoreDb (ctl, at, sr));
        add ("CONTROL sin nada (t=1.5 s)", clickScoreDb (ctl, (int) (1.5 * sr), sr), clickScoreDb (ctl, at, sr));
    }
    // (d) soltar la nota
    {
        auto p = make();
        auto withOff = render (*p, { { 0.0, 48, true }, { 2.0, 48, false } }, 3.0);
        auto held = render (*p, { { 0.0, 48, true } }, 3.0);
        add ("soltar la nota", clickScoreDb (withOff, at, sr), clickScoreDb (held, at, sr));
        // (e) el modelo llega a su último frame (sostenido): ¿salto al congelarse?
        if (auto m = p->resynthModel(); m != nullptr && ! m->times.empty())
        {
            const int tf = (int) ((m->times.back()) * sr);
            auto longHeld = render (*p, { { 0.0, 48, true } }, m->times.back() + 1.0);
            add ("llegada al ultimo frame (" + fmt (m->times.back(), 2) + " s)", clickScoreDb (longHeld, tf, sr), clickScoreDb (longHeld, tf - (int) (0.5 * sr), sr));
        }
    }
    return rows;
}

// ===========================================================================
// 8. Proyectos v3 de referencia (compatibilidad)
// ===========================================================================
struct Features { double lufs = kNaN; std::vector<double> meanMel, curve; };
static Features features (const juce::AudioBuffer<float>& b)
{
    Features f;
    f.lufs = integratedLufs (b, sr);
    const auto m = melSpectrogram (monoVec (b), sr);
    f.meanMel.assign ((size_t) m.bands, 0.0);
    int n = 0;
    for (auto& fr : m.db)
    {
        double e = 0; for (auto v : fr) e = juce::jmax (e, v);
        if (e < m.maxDb - 40) continue;
        for (int k = 0; k < m.bands; ++k) f.meanMel[(size_t) k] += juce::jmax (fr[(size_t) k], m.maxDb - 80);
        ++n;
    }
    for (auto& v : f.meanMel) v /= juce::jmax (1, n);
    const auto c = loudnessCurve (b, sr);
    for (size_t i = 0; i < c.size(); i += 5) f.curve.push_back (juce::jmax (c[i], -80.0));
    return f;
}
static double featDist (const Features& a, const Features& b, double& lufsD, double& curveD)
{
    double m = 0; for (size_t k = 0; k < a.meanMel.size() && k < b.meanMel.size(); ++k) m = juce::jmax (m, std::abs (a.meanMel[k] - b.meanMel[k]));
    lufsD = std::abs (a.lufs - b.lufs);
    double c = 0; int n = 0;
    for (size_t k = 0; k < a.curve.size() && k < b.curve.size(); ++k) if (a.curve[k] > -60 || b.curve[k] > -60) { c += std::abs (a.curve[k] - b.curve[k]); ++n; }
    curveD = n > 0 ? c / n : 0.0;
    return m;
}
static const std::vector<Ev>& phrase()
{
    static const std::vector<Ev> p { { 0.0, 48, true }, { 0.0, 55, true }, { 0.5, 60, true }, { 1.0, 64, true },
                                     { 2.0, 48, false }, { 2.0, 55, false }, { 2.0, 60, false }, { 2.0, 64, false } };
    return p;
}
// Un proyecto v3 se abre tal cual (queda en modo legacy, como en Ableton) y, solo para medir,
// se fuerza la semilla fija del banco: así dos versiones del motor se pueden comparar muestra a muestra.
static juce::AudioBuffer<float> renderState (const juce::MemoryBlock& mb, bool* wasLegacy = nullptr)
{
    auto p = makeProc (512);
    p->setStateInformation (mb.getData(), (int) mb.getSize());
    if (wasLegacy != nullptr) *wasLegacy = ! p->isSeededRandom();
    p->setRandomMode (true, kBenchSeed);
    setP (*p, ParamID::quality, 0.0f);
    return render (*p, phrase(), 3.5);
}
static void writeFlac (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (flac.createWriterFor (os.release(), sr, (unsigned) b.getNumChannels(), 24, {}, 0)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
static juce::AudioBuffer<float> readAudio (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    juce::AudioBuffer<float> b;
    if (r == nullptr) return b;
    b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    return b;
}
// Diferencia (dB respecto a la referencia); ignora el ruido de cuantización de 24 bits (~-144 dB)
static double nullDb (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& x)
{
    double d = 0, e = 0;
    for (int ch = 0; ch < juce::jmin (ref.getNumChannels(), x.getNumChannels()); ++ch)
        for (int i = 0; i < juce::jmin (ref.getNumSamples(), x.getNumSamples()); ++i)
        {
            const double a = ref.getSample (ch, i), c = juce::jlimit (-1.0f, 1.0f, x.getSample (ch, i));   // el FLAC guarda -1..1
            d += (a - c) * (a - c); e += a * a;
        }
    return e < 1e-20 ? (d < 1e-20 ? -200.0 : 0.0) : juce::jmax (-200.0, 10.0 * std::log10 (juce::jmax (1e-30, d) / e));
}

static juce::var fixturesBench (const juce::File& fixtureDir, bool regenerate)
{
    line ("\n## 8. Proyectos v3 de referencia (compatibilidad)\n");
    auto o = obj();
    const auto refFile = fixtureDir.getChildFile ("reference.json");
    const auto audioDir = fixtureDir.getChildFile ("renders");
    if (regenerate || ! refFile.existsAsFile())
    {
        // Los .nebstate son los proyectos guardados con v3.0 y NO se reescriben si ya existen:
        // solo se regenera el audio de referencia con el motor actual.
        fixtureDir.createDirectory(); audioDir.createDirectory();
        juce::Array<juce::var> items;
        auto add = [&] (const juce::String& name, std::function<void (NebulaAudioProcessor&)> build)
        {
            const auto f = fixtureDir.getChildFile (name + ".nebstate");
            juce::MemoryBlock mb;
            if (f.existsAsFile()) f.loadFileAsData (mb);
            else { auto p = makeProc(); build (*p); p->getStateInformation (mb); f.replaceWithData (mb.getData(), mb.getSize()); }
            bool legacy = false;
            const auto r1 = renderState (mb, &legacy), r2 = renderState (mb);
            writeFlac (audioDir.getChildFile (name + ".flac"), r1);
            const auto f1 = features (r1);
            auto it = obj();
            it->setProperty ("nombre", name); it->setProperty ("kb", num (mb.getSize() / 1024.0, 1));
            it->setProperty ("abreEnLegacy", legacy);
            it->setProperty ("lufs", num (f1.lufs, 3));
            it->setProperty ("repeticionDb", num (nullDb (r1, r2), 1));
            juce::Array<juce::var> mm; for (auto v : f1.meanMel) mm.add (num (v, 3));
            it->setProperty ("melMedio", mm);
            items.add (it.get());
        };
        const auto& presets = factoryPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            add ("preset_" + juce::String (i).paddedLeft ('0', 2), [i] (NebulaAudioProcessor& p) { p.loadPreset (i); });
        for (auto mk : { pluck, bell, snare, voice })
        {
            const auto snd = mk (Variant {});
            add ("recreado_" + snd.name, [snd] (NebulaAudioProcessor& p) { p.loadPreset (0); p.setSample (SampleTools::fromBuffer (snd.audio, sr, snd.name)); p.recreateFromSample(); });
        }
        o->setProperty ("proyectos", items);
        o->setProperty ("formatoEstado", 3);
        o->setProperty ("semillaBanco", (int) kBenchSeed);
        o->setProperty ("frase", "C3+G3 en 0 s, C4 en 0.5 s, E4 en 1 s, todo suelto en 2 s; render 3.5 s a 48 kHz, bloque 512, calidad 1x, semilla fija del banco");
        refFile.replaceWithText (juce::JSON::toString (o.get(), false));
        int legacyCount = 0; double worstRep = -200;
        for (auto& it : items) { if ((bool) it["abreEnLegacy"]) ++legacyCount; worstRep = juce::jmax (worstRep, (double) it["repeticionDb"]); }
        line ("Referencia regenerada: " + juce::String (items.size()) + " proyectos (" + juce::String (legacyCount) + " se abren en modo legacy), audio en " + audioDir.getFullPathName());
        line ("Dos renders del mismo proyecto con la semilla del banco: peor diferencia " + fmt (worstRep, 1) + " dB (-200 = identicos)");
        o->setProperty ("modo", "regenerada");
    }
    else
    {
        // Prueba nula contra el audio de referencia: debe quedar en <= -90 dB
        auto ref = juce::JSON::parse (refFile);
        juce::Array<juce::var> res; int fails = 0; double worst = -200;
        for (auto& it : *ref["proyectos"].getArray())
        {
            juce::MemoryBlock mb;
            fixtureDir.getChildFile (it["nombre"].toString() + ".nebstate").loadFileAsData (mb);
            const auto refAudio = readAudio (audioDir.getChildFile (it["nombre"].toString() + ".flac"));
            const double d = nullDb (refAudio, renderState (mb));
            worst = juce::jmax (worst, d);
            const bool ok = d <= -90.0;
            if (! ok) { ++fails; line ("  NO COINCIDE: " + it["nombre"].toString() + " " + fmt (d, 1) + " dB"); }
            auto x = obj(); x->setProperty ("nombre", it["nombre"]); x->setProperty ("diferenciaDb", num (d, 1)); x->setProperty ("pasa", ok); res.add (x.get());
        }
        line ("Compatibilidad: " + juce::String (res.size() - fails) + " de " + juce::String (res.size()) + " proyectos v3 suenan igual que su referencia (peor diferencia " + fmt (worst, 1) + " dB; criterio <= -90 dB)");
        o->setProperty ("modo", "comprobados"); o->setProperty ("resultados", res); o->setProperty ("fallos", fails); o->setProperty ("peorDb", num (worst, 1));
    }
    return o.get();
}

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::setvbuf (stdout, nullptr, _IOLBF, 0);
    outDir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    juce::File fixtureDir = outDir.getChildFile ("fixtures_v3");
    bool regenerate = false;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--quick") quick = true;
        if (a == "--volcar-modelo") dumpModels = true;
        if (a == "--sonidos" && i + 1 < argc) soundFilter = juce::StringArray::fromTokens (juce::String (argv[++i]), ",", "");
        if (a == "--only" && i + 1 < argc) only = juce::StringArray::fromTokens (juce::String (argv[++i]), ",", "");
        if (a == "--fixtures" && i + 1 < argc) fixtureDir = juce::File (juce::String (argv[++i]));
        if (a == "--regenerar-referencia") regenerate = true;
        if (a == "--semilla" && i + 1 < argc) kBenchSeed = (juce::uint32) juce::String (argv[++i]).getLargeIntValue();
    }
    const auto started = std::chrono::steady_clock::now();
    line ("# Nebula - banco de pruebas (Paso 0)\n");
    line ("Fecha: " + juce::Time::getCurrentTime().toString (true, true) + " | " + juce::SystemStats::getCpuModel() + " | "
          + juce::String (juce::SystemStats::getNumCpus()) + " CPU | " + (quick ? "modo rapido" : "modo completo"));

    auto root = obj();
    root->setProperty ("version", "nebula-3.0.0 (aad1831)");
    root->setProperty ("fecha", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("cpu", juce::SystemStats::getCpuModel());
    root->setProperty ("rapido", quick);
    root->setProperty ("semilla", (juce::int64) kBenchSeed);

    // ---- 0. Autocomprobación de los medidores con señales de valor conocido ----
    if (run ("0"))
    {
        line ("\n## 0. Autocomprobacion de los medidores\n");
        auto self = obj();
        juce::AudioBuffer<float> sine (1, (int) (3.0 * sr)), saw (1, (int) (2.0 * sr));
        for (int i = 0; i < sine.getNumSamples(); ++i) sine.setSample (0, i, (float) std::sin (k2Pi * 997.0 * i / sr));
        BlepSaw bs; for (int i = 0; i < saw.getNumSamples(); ++i) saw.setSample (0, i, (float) (0.5 * bs.next (261.6256)));
        const double l = integratedLufs (sine, sr);
        const double hz = medianPitch (saw, 0.1, 1.9);
        const double c = 1200.0 * std::log2 (hz / 261.6256);
        auto same = compare (saw, saw, sr, true);
        line ("Seno 997 Hz a 0 dBFS (mono, contado en 2 canales): " + fmt (l, 2) + " LUFS (esperado 0.00 +-0.05)");
        line ("YIN sobre sierra de 261.63 Hz: " + fmt (c, 2) + " ct (esperado 0 +-1)");
        line ("Sonido contra si mismo: mel " + fmt (same.melAll, 3) + " dB, env " + fmt (same.envErrDb, 3) + " dB, tono " + fmt (same.pitchMedCents, 2) + " ct, corr flujo " + fmt (same.fluxCorr, 3));
        self->setProperty ("lufsSeno0dBFS", num (l, 3)); self->setProperty ("yinErrorCents", num (c, 3));
        self->setProperty ("autoMelDb", num (same.melAll, 4)); self->setProperty ("autoEnvDb", num (same.envErrDb, 4));
        const bool ok = std::abs (l) < 0.05 && std::abs (c) < 1.0 && same.melAll < 1e-6 && same.envErrDb < 1e-6;
        self->setProperty ("pasa", ok);
        line (ok ? "Autocomprobacion: OK" : "Autocomprobacion: FALLO");
        root->setProperty ("autocomprobacion", self.get());
    }

    PairWriter controlPairs (outDir.getChildFile ("escucha/controles"), 99);
    PairWriter recPairs (outDir.getChildFile ("escucha/recreacion"), 4242);
    if (run ("1")) root->setProperty ("validacionMetricas", validateMetrics (controlPairs));
    std::vector<RecreatedCase> recs;
    if (run ("2")) root->setProperty ("fidelidad", engineFidelity (recPairs, recs));
    if (run ("2") || run ("3")) root->setProperty ("transitorio", transientStudy (recs));
    controlPairs.finish(); recPairs.finish();

    auto padS = SampleTools::fromBuffer (padEvolving (Variant {}).audio, sr, "pad_evolutivo_12s");
    auto vocS = SampleTools::fromBuffer (padVocal (Variant {}).audio, sr, "pad_vocal_8s");
    auto drum = SampleTools::fromBuffer (snare (Variant {}).audio, sr, "caja");
    if (run ("4")) root->setProperty ("cpuTiempoReal", cpuBench (padS));
    if (run ("5")) root->setProperty ("latencia", latencyBench());
    auto wideS = SampleTools::fromBuffer (padStereo (Variant {}).audio, sr, "pad_estereo_4s");
    if (run ("6")) root->setProperty ("estabilidad", stabilityBench (drum, vocS, wideS));
    if (run ("7")) root->setProperty ("clics", clickBench (padS));
    if (run ("8")) root->setProperty ("compatibilidad", fixturesBench (fixtureDir, regenerate));

    const double total = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    root->setProperty ("duracionBancoS", num (total, 1));
    line ("\nDuracion del banco: " + fmt (total, 0) + " s");
    outDir.getChildFile ("bench_results.json").replaceWithText (juce::JSON::toString (root.get(), false));
    outDir.getChildFile ("bench_summary.md").replaceWithText (md);
    return 0;
}
