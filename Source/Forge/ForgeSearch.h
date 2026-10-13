#pragma once
#include "ReferenceFeatures.h"
#include "PatchRenderer.h"
#include "../Similarity.h"
#include "FastPitch.h"
#include "PatchDataset.h"
#include "CmaEs.h"

// ===========================================================================
// FORGE · síntesis inversa (CandidateSearcher)
//
// 1. Estimación: cada "arquitectura" (una receta tipo: seno con caída de tono, sierra con filtro, FM...)
//    traduce lo medido en la referencia a valores iniciales de parámetros reales del motor.
// 2. Refinamiento: búsqueda por coordenadas en el espacio normalizado de sus genes (pasos que se achican),
//    con orden de genes sorteado por la semilla: determinista, sin depender sólo del azar.
// 3. Evaluación: render offline del patch y comparación con la referencia (afinada a la tecla) con las
//    métricas validadas del banco: color, envolvente, tono y golpe inicial. Un candidato mudo pierde.
// 4. Colección: el mejor de cada arquitectura + variaciones exploradas; se eliminan los casi idénticos
//    y, a fidelidad parecida (5 %), gana la receta más simple.
// Todo corre fuera del hilo de audio; se puede cancelar y tiene un tope de evaluaciones y de tiempo.
// ===========================================================================
namespace Forge
{
    struct Fidelity
    {
        // errores medidos (unidades físicas) y su lectura 0..1 (Similarity.h); la "calidad musical" del patch
        // NO se mide aquí: fidelidad de recreación sólo dice cuánto se parece a la referencia
        double colorDb = Bench::kNaN, envelopeDb = Bench::kNaN, pitchCents = Bench::kNaN, attackDb = Bench::kNaN;
        float color = -1, envelope = -1, pitch = -1, attack = -1, overall = -1;
        double loss = 1.0e9;
        bool silent = false;
    };

    struct PatchCandidate
    {
        juce::String id;                 // "A3-7": arquitectura, variante
        SynthPatch patch;
        Fidelity fidelity;
        int complexity = 0;              // módulos y rutas de modulación activos
        double cpuPercent = 0.0;         // % de un núcleo para una voz (medido al renderizar)
        int generation = 0;
        juce::String parentId;
        juce::String structure;          // resumen legible de la receta
        juce::String source;             // "estimacion", "indice" o "exploracion"
        SynthPatch initialPatch;         // punto de partida antes de optimizar
        double initialLoss = 0.0;
        float initialFidelity = -1.0f;   // fidelidad medida del punto de partida (AT004)
    };

    // ---------- descripción y complejidad de una receta ----------
    inline juce::String describe (const SynthPatch& p, int* complexityOut = nullptr)
    {
        int cx = 0;
        juce::StringArray parts;
        const auto& waves = waveNames();
        if (p.geti (ParamID::osc1On)) { ++cx; parts.add ("Osc1 " + waves[p.geti (ParamID::osc1Wave)] + (p.geti (ParamID::osc1Octave) != 0 ? juce::String (" ") + juce::String (p.geti (ParamID::osc1Octave)) + " oct" : juce::String())); }
        if (p.geti (ParamID::osc2On) && p.get (ParamID::oscMix) > 0.01f) { ++cx; parts.add ("Osc2 " + waves[p.geti (ParamID::osc2Wave)] + juce::String::formatted (" %+d st %+.0f ct", p.geti (ParamID::osc2Semi), p.get (ParamID::osc2Fine))); }
        if (p.get (ParamID::fmAmount) > 0.01f) { ++cx; parts.add (juce::String::formatted ("FM %.0f %%", p.get (ParamID::fmAmount) * 100)); }
        if (p.geti (ParamID::op3On) && (p.get (ParamID::op3Amount) > 0.0005f || std::abs (p.get (ParamID::op3Env)) > 0.0005f))
        { ++cx; parts.add (juce::String::formatted ("Op3 x%.2f %.0f %% > Osc%d", p.get (ParamID::op3Ratio), p.get (ParamID::op3Amount) * 100, p.geti (ParamID::op3Target) + 1)); }
        if (p.geti (ParamID::subOn) && p.get (ParamID::subLevel) > 0.01f) { ++cx; parts.add (juce::String::formatted ("Sub %.0f %%", p.get (ParamID::subLevel) * 100)); }
        if (p.geti (ParamID::noiseOn) && (p.get (ParamID::noise) > 0.01f || std::abs (p.getMod (ModSrc::Env2, ModDst::Noise)) > 0.01f)) { ++cx; parts.add ("Ruido"); }
        if (p.geti (ParamID::f1On) && p.get (ParamID::cutoff) < 19000.0f) { ++cx; parts.add (juce::String::formatted ("Filtro %.0f Hz", p.get (ParamID::cutoff))); }
        if (p.geti (ParamID::driveOn) && p.get (ParamID::drive) > 0.01f) { ++cx; parts.add (juce::String::formatted ("Drive %.0f %%", p.get (ParamID::drive) * 100)); }
        const char* srcN[] { "Env2", "Env3", "LFO1", "LFO2" };
        const char* dstN[] { "Tono", "Forma1", "Forma2", "Mezcla", "FM", "Corte", "Reso", "Corte2", "Volumen", "Paneo", "Ruido" };
        for (int s = 0; s < 4; ++s)
            for (int d = 0; d < ModDst::count; ++d)
                if (std::abs (p.getMod (s, d)) > 0.01f)
                {
                    ++cx;
                    parts.add (juce::String (srcN[s]) + ">" + dstN[d] + (d == ModDst::Pitch ? juce::String::formatted (" %+.1f st", p.getMod (s, d) * 12.0f) : juce::String()));
                }
        if (p.get (ParamID::lfoPitch) > 0.5f) { ++cx; parts.add (juce::String::formatted ("Vibrato %.1f Hz %.0f ct", p.get (ParamID::lfoRate), p.get (ParamID::lfoPitch))); }
        if (std::abs (p.get (ParamID::filterEnv)) > 0.01f) { ++cx; parts.add ("Env2>Filtro"); }
        parts.add (juce::String::formatted ("Amp A %.0f ms D %.2f s S %.0f %%", p.get (ParamID::ampA) * 1000, p.get (ParamID::ampD), p.get (ParamID::ampS) * 100));
        if (complexityOut != nullptr) *complexityOut = cx;
        return parts.joinIntoString (" | ");
    }

    // ---------- estructura de una receta (qué módulos y rutas usa, sin valores finos) ----------
    // Dos recetas con la misma clave sólo difieren en perillas; con clave distinta son otra forma de construir
    // el sonido (otra onda, otro módulo, otra ruta de modulación). Se usa para la diversidad explícita.
    inline juce::String structureKey (const SynthPatch& p)
    {
        juce::String k;
        if (p.geti (ParamID::osc1On)) k << "o1w" << p.geti (ParamID::osc1Wave) << "o" << p.geti (ParamID::osc1Octave);
        if (p.geti (ParamID::osc2On) && p.get (ParamID::oscMix) > 0.01f) k << "|o2w" << p.geti (ParamID::osc2Wave) << (p.geti (ParamID::osc2Semi) != 0 ? "s" : "");
        if (p.get (ParamID::fmAmount) > 0.01f) k << "|fm";
        if (p.geti (ParamID::op3On) && (p.get (ParamID::op3Amount) > 0.0005f || std::abs (p.get (ParamID::op3Env)) > 0.0005f)) k << "|op3t" << p.geti (ParamID::op3Target);
        if (p.geti (ParamID::subOn) && p.get (ParamID::subLevel) > 0.01f) k << "|sub";
        if (p.geti (ParamID::noiseOn) && (p.get (ParamID::noise) > 0.01f || std::abs (p.getMod (ModSrc::Env2, ModDst::Noise)) > 0.01f)) k << "|nz";
        if (p.geti (ParamID::f1On) && p.get (ParamID::cutoff) < 19000.0f) k << "|flt";
        if (p.geti (ParamID::driveOn) && p.get (ParamID::drive) > 0.01f) k << "|drv";
        if (std::abs (p.get (ParamID::filterEnv)) > 0.01f) k << "|fenv";
        if (p.get (ParamID::lfoPitch) > 0.5f) k << "|vib";
        for (int s = 0; s < 4; ++s)
            for (int d = 0; d < ModDst::count; ++d)
                if (std::abs (p.getMod (s, d)) > 0.01f) k << "|m" << s << "." << d;
        return k;
    }

    // ---------- evaluación ----------
    class Evaluator
    {
    public:
        explicit Evaluator (const ReferenceFeatures& r) : ref (r)
        {
            const int n = ref.tuned.getNumSamples();
            holdS = ref.releaseStartS > 0 ? ref.releaseStartS * (n / juce::jmax (1.0, ref.lengthS * ref.sampleRate)) : n / ref.sampleRate;
            mono.setSize (1, n);
            mono.copyFrom (0, 0, ref.tuned, 0, 0, n);
            mono.addFrom (0, 0, ref.tuned, 1, 0, n);
            mono.applyGain (0.5f);
            lowestHz = ref.f0 > 0 && ref.f0 < 80.0 ? 25.0 : 50.0;

            // La búsqueda compara a 32 kHz con la referencia ya medida (rápido); la fidelidad que se
            // muestra al final se mide a la frecuencia original con la comparación completa (score()).
            fastSr = juce::jmin (32000.0, ref.sampleRate);
            fastRef = resample (mono, ref.sampleRate, fastSr);
            refF = features (fastRef);
        }

        juce::AudioBuffer<float> render (PatchRenderer& r, const SynthPatch& p, double* seconds = nullptr, bool fast = false) const
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const double rate = fast ? fastSr : ref.sampleRate;
            auto out = r.render (p, ref.note, holdS, mono.getNumSamples() / ref.sampleRate, rate);
            if (seconds != nullptr) *seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            return out;
        }

        // Pérdida rápida para la búsqueda (mismas magnitudes que el banco, referencia precalculada)
        double fastLoss (const juce::AudioBuffer<float>& stereo) const
        {
            juce::AudioBuffer<float> m (1, stereo.getNumSamples());
            m.copyFrom (0, 0, stereo, 0, 0, stereo.getNumSamples());
            m.addFrom (0, 0, stereo, 1, 0, stereo.getNumSamples());
            m.applyGain (0.5f);
            const double lt = Bench::integratedLufs (m, fastSr);
            if (lt < -70.0 || ! std::isfinite (m.getMagnitude (0, 0, m.getNumSamples()))) return 1.0e6;
            m.applyGain ((float) std::pow (10.0, (refF.lufs - lt) / 20.0));
            const auto c = features (m);
            // color: celdas mel suavizadas por encima del piso
            double acc = 0; int n = 0;
            const size_t F = juce::jmin (refF.mel.db.size(), c.mel.db.size());
            for (size_t f = 0; f < F; ++f)
                for (int b = 0; b < refF.mel.bands; ++b)
                {
                    const double a = juce::jmax (refF.mel.db[f][(size_t) b], refF.floorDb), x = juce::jmax (c.mel.db[f][(size_t) b], refF.floorDb);
                    if (a <= refF.floorDb && x <= refF.floorDb) continue;
                    acc += std::abs (a - x); ++n;
                }
            const double colorDb = n > 0 ? acc / n : 20.0;
            // envolvente: RMS de la curva de sonoridad (zona audible de la referencia)
            double e = 0; int en = 0;
            for (size_t i = 0; i < refF.loud.size() && i < c.loud.size(); ++i)
                if (refF.loud[i] > refF.loudMax - 40.0) { const double d = refF.loud[i] - juce::jmax (c.loud[i], refF.loudMax - 60.0); e += d * d; ++en; }
            const double envDb = en > 0 ? std::sqrt (e / en) : 20.0;
            // tono: diferencia cuadro a cuadro (cada 25 ms)
            double pitchTerm = 0.0;
            if (ref.pitched)
            {
                // curva lenta (caídas, glides) cuadro a cuadro + cuánto vibra por tramo: así un vibrato con
                // otra fase no se castiga como si estuviera desafinado
                const auto rc = centsCurve (refF.pitch), cc = centsCurve (c.pitch);
                const auto rs = Bench::smoothCurve (rc, 4), cs = Bench::smoothCurve (cc, 4);
                double d = 0; int nd = 0;
                for (size_t i = 0; i < rc.size() && i < cc.size(); ++i)
                    if (refF.pitch[i] > 0) { d += c.pitch[i] > 0 ? juce::jmin (1200.0, std::abs (cs[i] - rs[i])) : 300.0; ++nd; }
                double depth = 0; int ns = 0;
                for (size_t i = 0; i + 8 <= rc.size() && i + 8 <= cc.size(); i += 8)
                {
                    double er = 0, ec = 0;
                    for (size_t j = i; j < i + 8; ++j) { er += (rc[j] - rs[j]) * (rc[j] - rs[j]); ec += (cc[j] - cs[j]) * (cc[j] - cs[j]); }
                    depth += std::abs (std::sqrt (er / 8) - std::sqrt (ec / 8)); ++ns;
                }
                pitchTerm = (nd > 0 ? d / nd : 0.0) + (ns > 0 ? depth / ns : 0.0);
            }
            const double atk = std::abs (c.early - refF.early);
            // La envolvente general ignora la referencia por debajo de -40 dB.
            // En pads eso deja sin castigo colas audibles que se cortan pronto.
            // La cola se compara después del note-off, sólo donde la referencia
            // está dentro de 55 dB de su máximo. El peso es deliberadamente
            // moderado para no perjudicar ataques/bajos ya recuperados.
            double tailError = 0.0;
            double tailWeight = 0.0;
            const size_t tailStart = (size_t) juce::jlimit (0, (int) refF.loud.size(),
                (int) std::floor (holdS / 0.01)); // loudnessCurve usa pasos de 10 ms
            for (size_t i = tailStart; i < refF.loud.size() && i < c.loud.size(); ++i)
            {
                const double r = refF.loud[i];
                if (r <= refF.loudMax - 55.0) continue;
                const double x = juce::jmax (c.loud[i], refF.loudMax - 75.0);
                const double d = juce::jlimit (-35.0, 35.0, r - x);
                const double weight = juce::jlimit (0.0, 1.0, (r - (refF.loudMax - 55.0)) / 15.0);
                tailError += weight * d * d;
                tailWeight += weight;
            }
            const double tailDb = tailWeight > 0.0 ? std::sqrt (tailError / tailWeight) : 0.0;
            return colorDb / 3.0 + envDb / 3.0 + pitchTerm / 30.0 + atk / 6.0 + tailDb / 6.0;
        }

        // Fidelidad completa (la que se muestra): comparación del banco a la frecuencia original
        Fidelity score (const juce::AudioBuffer<float>& stereo) const
        {
            Fidelity f;
            juce::AudioBuffer<float> test (1, stereo.getNumSamples());
            test.copyFrom (0, 0, stereo, 0, 0, stereo.getNumSamples());
            test.addFrom (0, 0, stereo, 1, 0, stereo.getNumSamples());
            test.applyGain (0.5f);
            // un patch mudo o casi mudo no puede "ganar" al igualar el volumen
            if (Bench::integratedLufs (test, ref.sampleRate) < -70.0 || ! std::isfinite (test.getMagnitude (0, 0, test.getNumSamples())))
            {
                f.silent = true; f.loss = 1.0e6; f.overall = 0.0f;
                return f;
            }
            const auto s = Bench::compare (mono, test, ref.sampleRate, ref.pitched, 0.0, lowestHz);
            const auto rep = Similarity::fromScores (s, ref.pitched);
            f.colorDb = s.colorDb; f.envelopeDb = s.envErrDb; f.pitchCents = rep.pitchCents;
            f.attackDb = std::isfinite (s.earlyRefDb) && std::isfinite (s.earlyTestDb) ? std::abs (s.earlyTestDb - s.earlyRefDb) : Bench::kNaN;
            f.color = rep.color; f.envelope = rep.envelope; f.pitch = rep.pitch;
            f.attack = Similarity::fromError (f.attackDb, 6.0);
            double logSum = 0, w = 0;
            auto add = [&] (float v, double wt) { if (v >= 0) { logSum += wt * std::log (juce::jmax (1.0e-4f, v)); w += wt; } };
            add (f.color, 0.45); add (f.envelope, 0.25); add (f.pitch, 0.2); add (f.attack, 0.1);
            f.overall = w > 0 ? (float) std::exp (logSum / w) : 0.0f;
            auto nz = [] (double v, double dflt) { return std::isfinite (v) ? v : dflt; };
            f.loss = nz (s.colorDb, 20.0) / 3.0 + nz (s.envErrDb, 20.0) / 3.0
                   + (ref.pitched ? nz (f.pitchCents, 600.0) / 30.0 : 0.0) + nz (f.attackDb, 20.0) / 6.0;
            return f;
        }

        const ReferenceFeatures& ref;
        double holdS = 1.0, fastSr = 32000.0, lowestHz = 50.0;
        juce::AudioBuffer<float> mono, fastRef;

    private:
        struct Feat { Bench::MelSpec mel; std::vector<double> loud, pitch; double loudMax = -200, floorDb = -200, early = 0, lufs = -70; };
        Feat refF;

        Feat features (const juce::AudioBuffer<float>& m) const
        {
            Feat f;
            const auto x = Bench::toMono (m);
            f.mel = Bench::smoothMel (Bench::melSpectrogram (x, fastSr, 32), 25);
            f.floorDb = f.mel.maxDb - 60.0;
            f.loud = Bench::loudnessCurve (m, fastSr);
            for (auto v : f.loud) f.loudMax = juce::jmax (f.loudMax, v);
            if (ref.pitched) f.pitch = pitchTrack (x, fastSr, ref.f0);
            double e10 = 0, e100 = 0;
            for (int i = 0; i < (int) (0.1 * fastSr) && i < (int) x.size(); ++i) { const double v = x[(size_t) i] * x[(size_t) i]; e100 += v; if (i < (int) (0.01 * fastSr)) e10 += v; }
            f.early = Bench::db (e10 / juce::jmax (1e-20, e100));
            f.lufs = Bench::integratedLufs (m, fastSr);
            return f;
        }

        static std::vector<double> pitchTrack (const std::vector<double>& x, double sr, double f0) { return fastPitchTrack (x, sr, f0, 0.025); }

        std::vector<double> centsCurve (const std::vector<double>& hz) const
        {
            std::vector<double> c (hz.size(), 0.0);
            for (size_t i = 0; i < hz.size(); ++i) c[i] = hz[i] > 0 ? 1200.0 * std::log2 (hz[i] / ref.f0) : (i > 0 ? c[i - 1] : 0.0);
            return c;
        }

        static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double from, double to)
        {
            if (std::abs (from - to) < 1.0) return in;
            const int n = (int) (in.getNumSamples() * to / from);
            juce::AudioBuffer<float> out (1, n);
            juce::LagrangeInterpolator li;
            li.process (from / to, in.getReadPointer (0), out.getWritePointer (0), n, in.getNumSamples(), 0);
            return out;
        }
    };

    // ---------- arquitecturas ----------
    struct Gene { juce::String id; float lo = 0.0f, hi = 1.0f; };   // rango permitido en el espacio normalizado

    struct Architecture
    {
        juce::String key, label;
        std::function<SynthPatch (const ReferenceFeatures&)> estimate;
        std::vector<Gene> genes;
    };

    inline Gene geneAround (const SynthPatch& p, const juce::String& id, float halfWidth)
    {
        const float n = p.getNorm (id);
        return { id, juce::jmax (0.0f, n - halfWidth), juce::jmin (1.0f, n + halfWidth) };
    }

    // Envolvente de amplitud a partir de la referencia
    inline void estimateAmp (SynthPatch& p, const ReferenceFeatures& f)
    {
        p.set (ParamID::ampA, (float) juce::jlimit (0.001, 2.0, f.attackS * 0.8));
        float s = (float) juce::jlimit (0.0, 1.0, std::pow (10.0, f.sustainDb / 20.0));
        if (f.sustainDb > -4.5) s = 1.0f;   // sostenido pleno: el decaimiento medido es la suelta, no la "D"
        p.set (ParamID::ampS, s < 0.08f ? 0.0f : s);
        // una caída de 20 dB: con la curva por defecto la decaída es casi lineal en amplitud
        p.set (ParamID::ampD, (float) (s >= 1.0f ? 0.3 : juce::jlimit (0.01, 5.0, f.decayS > 0 ? f.decayS * (s < 0.08f ? 1.6 : 1.0) : 0.3)));
        p.set (ParamID::ampCurve, s < 0.08f ? 0.6f : 0.0f);
        p.set (ParamID::ampR, (float) juce::jlimit (0.01, 8.0, f.releaseS > 0 ? f.releaseS * 0.6 : 0.15));
    }

    inline std::vector<Architecture> bassArchitectures()
    {
        // Base común del 808: tono con caída inicial (Env2 > Tono) y glide tardío (Env3 > Tono)
        auto base808 = [] (const ReferenceFeatures& f, int wave)
        {
            auto p = SynthPatch::init();
            p.set (ParamID::osc1Wave, (float) wave);
            p.set (ParamID::filterEnv, 0.0f);
            estimateAmp (p, f);
            const double drop = juce::jlimit (-1.0, 1.0, f.pitchStartCents / 1200.0);
            if (std::abs (f.pitchStartCents) > 30.0)
            {
                p.setMod (ModSrc::Env2, ModDst::Pitch, (float) drop);
                p.set (ParamID::fltA, 0.001f);
                p.set (ParamID::fltD, (float) juce::jlimit (0.005, 1.0, juce::jmax (0.01, f.pitchDropTimeS) * 2.5));
                p.set (ParamID::fltS, 0.0f);
                p.set (ParamID::fltCurve, 0.7f);
            }
            if (std::abs (f.pitchEndCents) > 50.0)
            {
                p.setMod (ModSrc::Env3, ModDst::Pitch, (float) juce::jlimit (-1.0, 1.0, f.pitchEndCents / 1200.0));
                p.set (ParamID::env3A, (float) juce::jlimit (0.01, 5.0, f.glideStartS + f.glideTimeS));
                p.set (ParamID::env3S, 1.0f);
                p.set (ParamID::env3Curve, -0.8f);   // empieza lento: el glide llega al final
                p.set (ParamID::env3D, 0.1f);
            }
            return p;
        };
        std::vector<Architecture> a;
        auto add = [&] (const char* key, const char* label, std::function<void (SynthPatch&, const ReferenceFeatures&)> extra, std::vector<juce::String> extraGenes, int wave = Wave::Sine)
        {
            Architecture arch;
            arch.key = key; arch.label = label;
            arch.estimate = [=] (const ReferenceFeatures& f) { auto p = base808 (f, wave); if (extra) extra (p, f); return p; };
            // los genes dependen de la estimación (rangos alrededor de ella): se calculan al buscar
            arch.genes = {};
            for (auto& id : extraGenes) arch.genes.push_back ({ id, 0.0f, 1.0f });
            a.push_back (arch);
        };
        add ("B1", "Seno con caida de tono", {}, {});
        add ("B2", "Seno saturado (drive)", [] (SynthPatch& p, const ReferenceFeatures& f)
             { p.set (ParamID::driveOn, 1); p.set (ParamID::drive, f.harmonicsDb[2] > -30.0 ? 0.6f : 0.35f); }, { ParamID::drive });
        add ("B3", "Seno + clic de ruido", [] (SynthPatch& p, const ReferenceFeatures& f)
             { p.set (ParamID::noiseOn, 1); p.set (ParamID::noise, 0.0f); p.setMod (ModSrc::Env2, ModDst::Noise, f.earlyDb > -8.0 ? 0.5f : 0.25f);
               p.set (ParamID::fltA, 0.001f); if (p.getMod (ModSrc::Env2, ModDst::Pitch) == 0.0f) { p.set (ParamID::fltD, 0.01f); p.set (ParamID::fltS, 0.0f); } },
             { "m0_10", ParamID::noiseColor });
        add ("B4", "Triangulo con caida de tono", {}, {}, Wave::Triangle);
        add ("B5", "Seno + sub", [] (SynthPatch& p, const ReferenceFeatures&) { p.set (ParamID::subOn, 1); p.set (ParamID::subLevel, 0.4f); p.set (ParamID::osc1Octave, 1); },
             { ParamID::subLevel });
        add ("B6", "Seno con FM (golpe brillante)", [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2Wave, (float) Wave::Sine); p.set (ParamID::osc2Semi, 12); p.set (ParamID::osc2Fine, 0); p.set (ParamID::fmAmount, 0.05f);
               p.setMod (ModSrc::Env2, ModDst::Fm, 0.3f); p.set (ParamID::fltA, 0.001f); },
             { ParamID::fmAmount, "m0_4" });
        add ("B7", "Seno saturado + clic", [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::driveOn, 1); p.set (ParamID::drive, 0.45f); p.set (ParamID::noiseOn, 1); p.setMod (ModSrc::Env2, ModDst::Noise, 0.35f); },
             { ParamID::drive, "m0_10" });
        add ("B8", "Pulso filtrado", [] (SynthPatch& p, const ReferenceFeatures& f)
             { p.set (ParamID::osc1Shape, 0.5f); p.set (ParamID::cutoff, (float) juce::jlimit (60.0, 2000.0, f.f0 * 2.5)); p.set (ParamID::resonance, 0.1f); },
             { ParamID::cutoff, ParamID::osc1Shape }, Wave::Pulse);
        return a;
    }

    inline std::vector<Architecture> leadArchitectures()
    {
        auto baseLead = [] (const ReferenceFeatures& f, int wave)
        {
            auto p = SynthPatch::init();
            p.set (ParamID::osc1Wave, (float) wave);
            estimateAmp (p, f);
            // brillo: el centroide (bandas mel 0..31) da un corte de partida
            const double cutHz = juce::jlimit (200.0, 16000.0, Bench::melToHz (Bench::hzToMel (40.0) + (Bench::hzToMel (16000.0) - Bench::hzToMel (40.0)) * (f.centroidBody + 4.0) / 32.0));
            p.set (ParamID::cutoff, (float) cutHz);
            p.set (ParamID::resonance, 0.15f);
            // el filtro se abre o cierra si el brillo del ataque es distinto del cuerpo
            const double dc = f.centroidBody - f.centroidAttack;
            p.set (ParamID::filterEnv, (float) juce::jlimit (-0.6, 0.6, -dc / 10.0));
            p.set (ParamID::fltA, (float) (dc > 0 ? juce::jmax (0.05, f.attackS + 0.2) : 0.002));
            p.set (ParamID::fltD, 0.4f); p.set (ParamID::fltS, dc > 0 ? 1.0f : 0.2f);
            if (dc > 1.0) { p.set (ParamID::filterEnv, (float) juce::jlimit (0.1, 0.8, dc / 8.0)); p.set (ParamID::cutoff, (float) (cutHz * 0.4)); }
            if (f.vibratoCents > 5.0)
            {
                p.set (ParamID::lfoRate, (float) f.vibratoHz); p.set (ParamID::lfoPitch, (float) juce::jmin (100.0, f.vibratoCents));
                p.set (ParamID::lfoFade, (float) juce::jlimit (0.0, 4.0, f.vibratoStartS)); p.set (ParamID::lfoRetrig, 1.0f);
            }
            return p;
        };
        std::vector<Architecture> a;
        auto add = [&] (const char* key, const char* label, int wave, std::function<void (SynthPatch&, const ReferenceFeatures&)> extra, std::vector<juce::String> extraGenes)
        {
            Architecture arch;
            arch.key = key; arch.label = label;
            arch.estimate = [=] (const ReferenceFeatures& f) { auto p = baseLead (f, wave); if (extra) extra (p, f); return p; };
            for (auto& id : extraGenes) arch.genes.push_back ({ id, 0.0f, 1.0f });
            a.push_back (arch);
        };
        add ("L1", "Sierra + filtro", Wave::Saw, {}, {});
        add ("L2", "Dos sierras desafinadas + filtro", Wave::Saw, [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Saw); p.set (ParamID::osc2Semi, 0); p.set (ParamID::osc2Fine, 10.0f); p.set (ParamID::oscMix, 0.5f); },
             { ParamID::osc2Fine, ParamID::oscMix });
        add ("L3", "Pulso + filtro", Wave::Pulse, [] (SynthPatch& p, const ReferenceFeatures&) { p.set (ParamID::osc1Shape, 0.5f); }, { ParamID::osc1Shape });
        add ("L4", "Sierra + pulso", Wave::Saw, [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Pulse); p.set (ParamID::osc2Fine, 5.0f); p.set (ParamID::oscMix, 0.4f); },
             { ParamID::oscMix, ParamID::osc2Shape });
        add ("L5", "Super Saw", Wave::SuperSaw, [] (SynthPatch& p, const ReferenceFeatures&) { p.set (ParamID::osc1Shape, 0.2f); }, { ParamID::osc1Shape });
        add ("L6", "Triangulo con FM", Wave::Triangle, [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2Wave, (float) Wave::Sine); p.set (ParamID::osc2Semi, 0); p.set (ParamID::osc2Fine, 0); p.set (ParamID::fmAmount, 0.25f); },
             { ParamID::fmAmount });
        add ("L7", "Sync", Wave::Sync, [] (SynthPatch& p, const ReferenceFeatures&) { p.set (ParamID::osc1Shape, 0.3f); }, { ParamID::osc1Shape });
        add ("L8", "Sierra + sub + ruido", Wave::Saw, [] (SynthPatch& p, const ReferenceFeatures& f)
             { p.set (ParamID::subOn, 1); p.set (ParamID::subLevel, 0.2f); p.set (ParamID::noiseOn, 1); p.set (ParamID::noise, f.noiseDb > -20.0 ? 0.15f : 0.04f); },
             { ParamID::subLevel, ParamID::noise });
        // FORGE 2.0: familias inarmónicas usando la FM nativa del motor.
        // No se añade todavía un nuevo DSP; los patches siguen siendo compatibles.
        add ("L9", "Campana FM inarmonica", Wave::Sine, [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Sine);
               p.set (ParamID::osc2Semi, 6); p.set (ParamID::osc2Fine, 17.0f);
               p.set (ParamID::oscMix, 0.0f); p.set (ParamID::fmAmount, 0.42f);
               p.set (ParamID::cutoff, 18000.0f); p.set (ParamID::filterEnv, 0.0f); },
             { ParamID::fmAmount, ParamID::osc2Semi, ParamID::osc2Fine });
        add ("L10", "Campana FM octava", Wave::Sine, [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Sine);
               p.set (ParamID::osc2Semi, 19); p.set (ParamID::osc2Fine, 0.0f);
               p.set (ParamID::oscMix, 0.0f); p.set (ParamID::fmAmount, 0.36f);
               p.set (ParamID::cutoff, 18000.0f); p.set (ParamID::filterEnv, 0.0f); },
             { ParamID::fmAmount, ParamID::osc2Semi, ParamID::osc2Fine });
        add ("L11", "Campana FM percusiva", Wave::Sine, [] (SynthPatch& p, const ReferenceFeatures& f)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Sine);
               p.set (ParamID::osc2Semi, 6); p.set (ParamID::osc2Fine, 17.0f);
               p.set (ParamID::oscMix, 0.0f); p.set (ParamID::fmAmount, 0.18f);
               p.set (ParamID::fltA, 0.001f); p.set (ParamID::fltD, (float) juce::jlimit (0.02, 1.5, f.decayS * 0.5));
               p.set (ParamID::fltS, 0.0f); p.setMod (ModSrc::Env2, ModDst::Fm, 0.6f);
               p.set (ParamID::cutoff, 18000.0f); p.set (ParamID::filterEnv, 0.0f); },
             { ParamID::fmAmount, ParamID::osc2Semi, ParamID::osc2Fine, ParamID::fltD,
               matrixID (ModSrc::Env2, ModDst::Fm) });
        add ("L12", "Campana FM grave", Wave::Sine, [] (SynthPatch& p, const ReferenceFeatures&)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Sine);
               p.set (ParamID::osc2Semi, -5); p.set (ParamID::osc2Fine, 17.0f);
               p.set (ParamID::oscMix, 0.0f); p.set (ParamID::fmAmount, 0.5f);
               p.set (ParamID::cutoff, 18000.0f); p.set (ParamID::filterEnv, 0.0f); },
             { ParamID::fmAmount, ParamID::osc2Semi, ParamID::osc2Fine });
        // 0.3.1: campana de tres operadores (op 3 -> osc 1 con relación inarmónica, índice que cae con la Env 3)
        add ("L13", "Campana FM 3 operadores", Wave::Sine, [] (SynthPatch& p, const ReferenceFeatures& f)
             { p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) Wave::Sine);
               p.set (ParamID::osc2Semi, 12); p.set (ParamID::osc2Fine, 0.0f);
               p.set (ParamID::oscMix, 0.0f); p.set (ParamID::fmAmount, 0.2f);
               p.set (ParamID::op3On, 1); p.set (ParamID::op3Target, 0); p.set (ParamID::op3Ratio, 2.76f);
               p.set (ParamID::op3Amount, 0.15f); p.set (ParamID::op3Env, 0.4f);
               p.set (ParamID::env3A, 0.001f); p.set (ParamID::env3D, (float) juce::jlimit (0.05, 2.0, f.decayS * 0.4)); p.set (ParamID::env3S, 0.0f);
               p.set (ParamID::cutoff, 18000.0f); p.set (ParamID::filterEnv, 0.0f); },
             { ParamID::fmAmount, ParamID::op3Ratio, ParamID::op3Amount, ParamID::op3Env, ParamID::env3D });
        return a;
    }

    // Genes comunes, con rango alrededor de la estimación
    inline std::vector<Gene> genesFor (const Architecture& arch, const SynthPatch& p, bool bass)
    {
        std::vector<Gene> g { geneAround (p, ParamID::ampA, 0.15f), geneAround (p, ParamID::ampD, 0.25f), geneAround (p, ParamID::ampS, 0.3f),
                              geneAround (p, ParamID::ampCurve, 0.4f) };
        if (bass)
        {
            if (p.getMod (ModSrc::Env2, ModDst::Pitch) != 0.0f)
            { g.push_back (geneAround (p, matrixID (ModSrc::Env2, ModDst::Pitch), 0.15f)); g.push_back (geneAround (p, ParamID::fltD, 0.25f)); g.push_back (geneAround (p, ParamID::fltCurve, 0.3f)); }
            if (p.getMod (ModSrc::Env3, ModDst::Pitch) != 0.0f)
            { g.push_back (geneAround (p, matrixID (ModSrc::Env3, ModDst::Pitch), 0.08f)); g.push_back (geneAround (p, ParamID::env3A, 0.2f)); g.push_back (geneAround (p, ParamID::env3Curve, 0.4f)); }
        }
        else
        {
            g.push_back (geneAround (p, ParamID::cutoff, 0.3f));
            g.push_back (geneAround (p, ParamID::resonance, 0.3f));
            g.push_back (geneAround (p, ParamID::filterEnv, 0.3f));
            g.push_back (geneAround (p, ParamID::fltA, 0.25f));
            g.push_back (geneAround (p, ParamID::ampR, 0.2f));
            // la velocidad del vibrato queda como se midió (la pérdida de tono no la distingue bien); se ajustan
            // su profundidad y su entrada
            if (p.get (ParamID::lfoPitch) > 0.5f) { g.push_back (geneAround (p, ParamID::lfoPitch, 0.2f)); g.push_back (geneAround (p, ParamID::lfoFade, 0.3f)); }
        }
        for (auto& e : arch.genes) g.push_back (geneAround (p, e.id, 0.35f));
        return g;
    }

    // ---------- la búsqueda ----------
    struct SearchSettings
    {
        juce::int64 seed = 1;
        int candidates = 8;                 // cuántos entregar (8-16)
        int evaluationsPerArchitecture = 56;   // presupuesto por semilla de la búsqueda local (= cmaBudget + polishBudget)
        int cmaBudget = 44;                 // evaluaciones de CMA-ES por semilla
        int polishBudget = 12;              // pulido local después de CMA-ES
        int retrieve = 24;                  // vecinos que se piden al índice (se evalúan todos)
        int retrievedSeeds = 6;             // cuántos de ellos pasan a optimizarse
        enum class Optimizer { CmaThenLocal, Local } optimizer = Optimizer::Local;   // elegido con "Forge optimizadores" (docs/0.4.0/P3-INVERSE-FORGE.md)
        const Dataset* dataset = nullptr;   // base de patches (nullptr = sólo arquitecturas)
        int datasetSplitMask = 0b110;       // particiones del índice que se usan (entrenamiento + validación)
        double complexityPenalty = 0.01;    // REGULARIZACIÓN: pérdida extra por módulo activo
        double timeLimitS = 120.0;
    };

    // Diagnóstico de una búsqueda (SEARCH_DIAGNOSTICS)
    struct SearchDiagnostics
    {
        struct SeedLog { juce::String label, source; double initialLoss = 0, finalLoss = 0; };
        std::vector<SeedLog> seeds;
        int evaluations = 0, retrieved = 0;
        juce::String indexCategory;
        double bestInitialLoss = 1.0e9, bestFinalLoss = 1.0e9, seconds = 0;
        juce::String summary() const
        {
            return juce::String (evaluations) + " evaluaciones, " + juce::String (seconds, 1) + " s; indice: " + juce::String (retrieved) + " vecinos ("
                 + indexCategory + "); perdida inicial " + juce::String (bestInitialLoss, 2) + " -> final " + juce::String (bestFinalLoss, 2);
        }
    };

    class Searcher
    {
    public:
        explicit Searcher (const ReferenceFeatures& f) : ref (f), eval (f) {}

        std::atomic<bool> cancel { false };
        std::atomic<float> progress { 0.0f };
        int evaluations = 0;

        SearchDiagnostics diagnostics;

        // Resultados progresivos (NEBULA-CORE-050): se llama desde el hilo de la búsqueda cada vez que una
        // semilla empieza a optimizarse (provisional = true, fidelidad sin medir) y cuando termina (medida
        // completa). El mismo id identifica al candidato en los dos avisos. Nada se inventa: cada aviso es
        // un patch real del motor en ese momento de la búsqueda.
        std::function<void (const PatchCandidate&, bool provisional)> onCandidate;
        // Objetivo de la búsqueda (opcional): recibe el patch y su pérdida rápida contra la referencia y
        // devuelve lo que se minimiza. Sin él, se minimiza la pérdida (recrear lo más fiel posible).
        std::function<double (const SynthPatch&, double referenceLoss)> objective;

        std::vector<PatchCandidate> run (const SearchSettings& st)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            diagnostics = {};
            const bool bass = ref.soundClass == SoundClass::Bass || (ref.pitched && ref.f0 < 110.0);
            auto archs = bass ? bassArchitectures() : leadArchitectures();
            settings = &st;

            // ---- 1. Semillas: estimación por arquitectura + vecinos del índice ----
            struct Seed { SynthPatch patch; std::vector<Gene> genes; juce::String id, source; double loss = 1.0e9; };
            std::vector<Seed> seeds;
            for (int ai = 0; ai < (int) archs.size() && ! cancel.load(); ++ai)
            {
                auto p = archs[(size_t) ai].estimate (ref);
                p.origin = "forge"; p.reference = ref.name; p.architecture = archs[(size_t) ai].label; p.seed = st.seed;
                seeds.push_back ({ p, genesFor (archs[(size_t) ai], p, bass), archs[(size_t) ai].key, "estimacion" });
            }
            if (st.dataset != nullptr && ! st.dataset->entries.empty())
            {
                const double f0 = ref.pitched ? 440.0 * std::exp2 ((ref.note - 69) / 12.0) : 0.0;
                const auto fp = fingerprint (ref.tuned, ref.sampleRate, f0, ref.releaseStartS);
                int cat = -1;
                const auto hits = st.dataset->query (fp, st.retrieve, st.datasetSplitMask, 0.04f, &cat);
                diagnostics.retrieved = (int) hits.size();
                diagnostics.indexCategory = cat >= 0 ? categoryName ((Category) cat) : "-";
                std::vector<Seed> fromIndex;
                for (auto& h : hits)
                {
                    if (cancel.load()) break;
                    const auto& e = st.dataset->entries[(size_t) h.index];
                    auto p = e.patch();
                    p.origin = "forge"; p.reference = ref.name; p.seed = st.seed;
                    p.architecture = juce::String ("Indice ") + categoryName (e.category);
                    Seed sd { p, datasetGenes (*st.dataset, e.category, p), juce::String ("D") + juce::String ((int) e.category), "indice" };
                    sd.loss = loss (p);
                    fromIndex.push_back (std::move (sd));
                }
                std::sort (fromIndex.begin(), fromIndex.end(), [] (auto& a, auto& b) { return a.loss < b.loss; });
                for (int i = 0; i < (int) fromIndex.size() && i < st.retrievedSeeds; ++i) seeds.push_back (fromIndex[(size_t) i]);
            }
            for (auto& sd : seeds) if (sd.loss >= 1.0e8 && ! cancel.load()) sd.loss = loss (sd.patch);
            std::stable_sort (seeds.begin(), seeds.end(), [] (auto& a, auto& b) { return a.loss < b.loss; });
            for (auto& sd : seeds) diagnostics.bestInitialLoss = juce::jmin (diagnostics.bestInitialLoss, sd.loss);
            {
                // Diversidad desde el principio: primero la mejor semilla de cada estructura, después el resto
                std::vector<Seed> first, rest;
                juce::StringArray keys;
                for (auto& sd : seeds)
                {
                    const auto k = structureKey (sd.patch);
                    if (keys.contains (k)) rest.push_back (sd); else { keys.add (k); first.push_back (sd); }
                }
                seeds = first;
                seeds.insert (seeds.end(), rest.begin(), rest.end());
            }

            // ---- 2. Optimización: CMA-ES y pulido local (las mejores semillas, hasta llenar el pedido + 2) ----
            std::vector<PatchCandidate> pool;
            const int toOptimize = juce::jmin ((int) seeds.size(), st.candidates + 2);
            int dCount = 0;
            for (int i = 0; i < toOptimize && ! cancel.load(); ++i)
            {
                auto& sd = seeds[(size_t) i];
                const auto id = sd.source == "indice" ? sd.id + "." + juce::String (dCount + 1) : sd.id + "-1";
                if (onCandidate)
                {
                    auto pre = finish (sd.patch, sd.loss, id, 0, {});
                    pre.source = sd.source; pre.initialPatch = sd.patch; pre.initialLoss = sd.loss;
                    pre.fidelity.overall = -1.0f;
                    onCandidate (pre, true);
                }
                juce::Random rng (st.seed * 7919 + i);
                std::pair<SynthPatch, double> best { sd.patch, sd.loss };
                if (st.optimizer == SearchSettings::Optimizer::CmaThenLocal)
                {
                    best = cma (sd.patch, sd.genes, st.seed * 104729 + i, st.cmaBudget);
                    if (sd.loss < best.second) best = { sd.patch, sd.loss };
                    auto polished = refine (best.first, sd.genes, rng, st.polishBudget, t0, st.timeLimitS);
                    if (polished.second < best.second) best = polished;
                }
                else
                    best = refine (sd.patch, sd.genes, rng, st.evaluationsPerArchitecture, t0, st.timeLimitS);
                if (sd.source == "indice") ++dCount;
                auto c = finish (best.first, best.second, id, 0, {});
                c.initialPatch = sd.patch; c.initialLoss = sd.loss; c.source = sd.source;
                pool.push_back (c);
                if (onCandidate && ! cancel.load())
                {
                    auto shown = c;
                    measureFully (shown);
                    shown.fidelity.loss = best.second;   // lo que minimizó la búsqueda
                    onCandidate (shown, false);
                }
                diagnostics.seeds.push_back ({ id, sd.source, sd.loss, best.second });
                diagnostics.bestFinalLoss = juce::jmin (diagnostics.bestFinalLoss, best.second);
                progress = 0.9f * (float) (i + 1) / (float) toOptimize;
                if ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 > st.timeLimitS) break;
            }

            // ---- 3. Exploración si faltan candidatos ----
            std::sort (pool.begin(), pool.end(), [] (auto& a, auto& b) { return a.fidelity.loss < b.fidelity.loss; });
            juce::Random rng (st.seed * 104729 + 17);
            int variant = 2;
            const size_t bases = pool.size();
            int explored = 0;
            while (bases > 0 && explored < 3 * st.candidates && ! cancel.load()
                   && (int) select (pool, st.candidates).size() < st.candidates
                   && (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 < st.timeLimitS)
            {
                ++explored;
                const auto parent = pool[(size_t) (variant - 2) % std::min<size_t> (bases, 3)];
                auto child = mutate (parent.patch, rng, 0.35f);
                auto archIt = std::find_if (archs.begin(), archs.end(), [&] (auto& a) { return a.label == parent.patch.architecture; });
                auto genes = genesFor (archIt != archs.end() ? *archIt : archs.front(), child, bass);
                auto best = refine (child, genes, rng, st.polishBudget * 2, t0, st.timeLimitS);
                auto c = finish (best.first, best.second, parent.id.upToFirstOccurrenceOf ("-", false, false) + "-" + juce::String (variant), 1, parent.id);
                c.initialPatch = child; c.initialLoss = best.second; c.source = "exploracion";
                pool.push_back (c);
                ++variant;
            }

            // ---- 4. Selección y medición completa ----
            auto chosen = select (pool, st.candidates);
            for (auto& c : chosen)
            {
                if (cancel.load()) break;
                measureFully (c);
                // fidelidad del punto de partida (para verificar que optimizar mejora: AT004)
                c.initialFidelity = eval.score (eval.render (renderer, c.initialPatch)).overall;
            }
            std::stable_sort (chosen.begin(), chosen.end(), [] (const PatchCandidate& a, const PatchCandidate& b)
            {
                if (std::abs (a.fidelity.overall - b.fidelity.overall) < 0.02f) return a.complexity < b.complexity;
                return a.fidelity.overall > b.fidelity.overall;
            });
            diagnostics.evaluations = evaluations;
            diagnostics.seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            progress = 1.0f;
            return chosen;
        }

        // Variación musical alrededor de un patch (para EXPLORAR / MUTAR): mueve los genes continuos
        static SynthPatch mutate (const SynthPatch& base, juce::Random& rng, float amount)
        {
            auto p = base;
            static const juce::StringArray ids { ParamID::ampA, ParamID::ampD, ParamID::ampS, ParamID::ampCurve, ParamID::ampR, ParamID::cutoff, ParamID::resonance,
                                                 ParamID::filterEnv, ParamID::fltD, ParamID::fltCurve, ParamID::drive, ParamID::fmAmount, ParamID::oscMix, ParamID::osc1Shape };
            for (auto& id : ids)
            {
                const float u = rng.nextFloat() * 2.0f - 1.0f;
                p.setNorm (id, p.getNorm (id) + u * amount * 0.5f);
            }
            for (int d : { ModDst::Pitch, ModDst::Noise, ModDst::Fm })
                if (p.getMod (ModSrc::Env2, d) != 0.0f) p.setMod (ModSrc::Env2, d, p.getMod (ModSrc::Env2, d) * (1.0f + (rng.nextFloat() * 2.0f - 1.0f) * amount * 0.3f));
            return p;
        }

        PatchCandidate measure (const SynthPatch& p, const juce::String& id)
        {
            auto c = finish (p, 0.0, id, 0, {});
            measureFully (c);
            return c;
        }

        const ReferenceFeatures& ref;
        Evaluator eval;
        PatchRenderer renderer;
        const SearchSettings* settings = nullptr;

    private:
        // Pérdida rápida + regularización por complejidad
        double loss (const SynthPatch& q)
        {
            ++evaluations;
            int cx = 0; describe (q, &cx);
            const double l = eval.fastLoss (eval.render (renderer, q, nullptr, true)) + (settings != nullptr ? settings->complexityPenalty : 0.01) * cx;
            return objective ? objective (q, l) : l;
        }

        // CMA-ES separable sobre los genes (espacio normalizado, caja = rango de cada gen)
        std::pair<SynthPatch, double> cma (const SynthPatch& start, const std::vector<Gene>& genes, juce::int64 seed, int budget)
        {
            std::vector<double> x0, lo, hi;
            for (auto& g : genes) { x0.push_back (start.getNorm (g.id)); lo.push_back (g.lo); hi.push_back (g.hi); }
            auto apply = [&] (const std::vector<double>& x) { auto q = start; for (size_t i = 0; i < genes.size(); ++i) q.setNorm (genes[i].id, (float) x[i]); return q; };
            const auto r = cmaesMinimize ([&] (const std::vector<double>& x) { return loss (apply (x)); }, x0, lo, hi, 0.08, budget, seed, &cancel);
            if (r.best.empty()) return { start, 1.0e9 };
            return { apply (r.best), r.bestValue };
        }

        // Genes de un patch del índice: los parámetros que varían en su categoría (máximo 16, los de más rango)
        static std::vector<Gene> datasetGenes (const Dataset& d, Category c, const SynthPatch& p)
        {
            std::vector<Gene> g;
            for (int idx : d.variedParams (c))
            {
                const auto& info = synthParams()[(size_t) idx];
                if (info.discrete) continue;   // ondas, octavas y encendidos quedan como los trae el vecino
                g.push_back (geneAround (p, info.id, 0.2f));
                if (g.size() >= 16) break;
            }
            return g;
        }

        std::pair<SynthPatch, double> refine (SynthPatch p, const std::vector<Gene>& genes, juce::Random& rng, int budget, double t0, double limitS)
        {
            auto evalP = [&] (const SynthPatch& q) { return loss (q); };
            double best = evalP (p);
            float step = 0.12f;
            int used = 1;
            std::vector<size_t> order (genes.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            while (used < budget && step > 0.01f && ! cancel.load())
            {
                // orden sorteado por la semilla (determinista)
                for (size_t i = order.size(); i > 1; --i) std::swap (order[i - 1], order[(size_t) rng.nextInt ((int) i)]);
                bool improved = false;
                for (size_t oi = 0; oi < order.size() && used < budget && ! cancel.load(); ++oi)
                {
                    const auto& g = genes[order[oi]];
                    for (float dir : { 1.0f, -1.0f })
                    {
                        auto q = p;
                        q.setNorm (g.id, juce::jlimit (g.lo, g.hi, p.getNorm (g.id) + dir * step));
                        if (std::abs (q.getNorm (g.id) - p.getNorm (g.id)) < 1.0e-5f) continue;
                        const double l = evalP (q); ++used;
                        if (l < best - 1.0e-4) { p = q; best = l; improved = true; break; }
                        if (used >= budget) break;
                    }
                    if ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 > limitS) return { p, best };
                }
                if (! improved) step *= 0.5f;
            }
            return { p, best };
        }

        // Candidato con su búsqueda (pérdida rápida); la fidelidad completa se mide sólo para los elegidos
        PatchCandidate finish (const SynthPatch& p, double fastLoss, const juce::String& id, int gen, const juce::String& parent)
        {
            PatchCandidate c;
            c.patch = p; c.id = id; c.generation = gen; c.parentId = parent;
            c.fidelity.loss = fastLoss;
            c.structure = describe (p, &c.complexity);
            c.patch.name = "FORGE " + id;
            return c;
        }

    public:
        // Mide la fidelidad completa y el costo de CPU (render a la frecuencia original)
        void measureFully (PatchCandidate& c)
        {
            double secs = 0;
            auto audio = eval.render (renderer, c.patch, &secs);
            c.fidelity = eval.score (audio);
            c.cpuPercent = 100.0 * secs / juce::jmax (1.0e-3, eval.mono.getNumSamples() / ref.sampleRate);
        }

    private:
        // Diversidad explícita: primero la mejor receta de cada estructura distinta (dentro de un margen de
        // pérdida respecto de la mejor), luego se completa con las mejores restantes que no sean casi-duplicados.
        // Orden: pérdida con un pequeño recargo por complejidad (orden estricto, apto para std::sort).
        static std::vector<PatchCandidate> select (std::vector<PatchCandidate> pool, int count)
        {
            auto rank = [] (const PatchCandidate& c) { return c.fidelity.loss * (1.0 + 0.01 * c.complexity); };
            pool.erase (std::remove_if (pool.begin(), pool.end(), [] (const PatchCandidate& c)
                        { return c.fidelity.silent || c.fidelity.loss >= 1.0e5 || ! std::isfinite (c.fidelity.loss); }), pool.end());
            std::stable_sort (pool.begin(), pool.end(), [&] (const PatchCandidate& a, const PatchCandidate& b) { return rank (a) < rank (b); });
            std::vector<PatchCandidate> out;
            if (pool.empty()) return out;
            auto paramDistance = [] (const SynthPatch& a, const SynthPatch& b)
            {
                double d = 0; int n = 0;
                for (auto& info : synthParams()) { d += std::abs (a.getNorm (info.id) - b.getNorm (info.id)); ++n; }
                return d / juce::jmax (1, n);
            };
            const double margin = pool.front().fidelity.loss * 2.0 + 0.1;
            std::vector<bool> taken (pool.size(), false);
            juce::StringArray keys;
            // 1) una por estructura
            for (size_t i = 0; i < pool.size() && (int) out.size() < count; ++i)
            {
                if (pool[i].fidelity.loss > margin) break;
                const auto key = structureKey (pool[i].patch);
                if (keys.contains (key)) continue;
                keys.add (key);
                out.push_back (pool[i]); taken[i] = true;
            }
            // 2) completar con las mejores que no sean casi-duplicados
            for (size_t i = 0; i < pool.size() && (int) out.size() < count; ++i)
            {
                if (taken[i]) continue;
                bool dup = false;
                // media sobre todos los parámetros de síntesis (la mayoría iguales): 0.004 ~ un par de perillas casi iguales
                for (auto& o : out) if (paramDistance (pool[i].patch, o.patch) < 0.004) { dup = true; break; }
                if (! dup) { out.push_back (pool[i]); taken[i] = true; }
            }
            std::stable_sort (out.begin(), out.end(), [&] (const PatchCandidate& a, const PatchCandidate& b) { return rank (a) < rank (b); });
            return out;
        }
    };
}
