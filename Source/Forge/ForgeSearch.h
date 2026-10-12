#pragma once
#include "ReferenceFeatures.h"
#include "PatchRenderer.h"
#include "../Similarity.h"

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
            return colorDb / 3.0 + envDb / 3.0 + pitchTerm / 30.0 + atk / 6.0;
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

        // Tono rápido para la búsqueda: YIN directo sobre la señal filtrada y diezmada a 32·f0 (sólo interesa
        // la fundamental, entre una octava abajo y dos arriba de la del cuerpo), cada 25 ms
        static std::vector<double> pitchTrack (const std::vector<double>& x, double sr, double f0)
        {
            std::vector<double> out;
            if (f0 <= 0.0) return out;
            const double srD = juce::jmin (sr, 32.0 * f0);
            // filtro de 4 polos (dos biquads) a 12·f0 y remuestreo lineal
            juce::IIRCoefficients c = juce::IIRCoefficients::makeLowPass (sr, juce::jmin (sr * 0.45, 12.0 * f0), 0.54);
            juce::IIRCoefficients c2 = juce::IIRCoefficients::makeLowPass (sr, juce::jmin (sr * 0.45, 12.0 * f0), 1.31);
            juce::IIRFilter a, b; a.setCoefficients (c); b.setCoefficients (c2);
            std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i) y[i] = b.processSingleSampleRaw (a.processSingleSampleRaw ((float) x[i]));
            const double step = sr / srD;
            std::vector<double> d ((size_t) (x.size() / step) + 1);
            for (size_t i = 0; i < d.size(); ++i)
            {
                const double pos = i * step; const size_t i0 = (size_t) pos; const double fr = pos - (double) i0;
                d[i] = i0 + 1 < y.size() ? y[i0] * (1.0 - fr) + y[i0 + 1] * fr : 0.0;
            }
            const int tmin = juce::jmax (2, (int) (srD / (4.0 * f0))), tmax = (int) (srD / (0.5 * f0)), W = 2 * tmax;
            const int hop = juce::jmax (1, (int) (0.025 * srD));
            std::vector<double> df ((size_t) tmax + 2);
            for (int start = 0; start + W + tmax < (int) d.size(); start += hop)
            {
                double energy = 0; for (int j = 0; j < W; ++j) energy += d[(size_t) (start + j)] * d[(size_t) (start + j)];
                if (energy < 1.0e-10) { out.push_back (0.0); continue; }
                double run = 0; int found = -1;
                df[0] = 1.0;
                for (int t = 1; t <= tmax; ++t)
                {
                    double acc = 0;
                    for (int j = 0; j < W; ++j) { const double e = d[(size_t) (start + j)] - d[(size_t) (start + j + t)]; acc += e * e; }
                    run += acc;
                    df[(size_t) t] = run > 0 ? acc * t / run : 1.0;
                }
                for (int t = tmin; t < tmax; ++t)
                    if (df[(size_t) t] < 0.15) { while (t + 1 < tmax && df[(size_t) t + 1] < df[(size_t) t]) ++t; found = t; break; }
                if (found < 0) { out.push_back (0.0); continue; }
                const double l = df[(size_t) found - 1], m = df[(size_t) found], r = df[(size_t) found + 1];
                const double den = l - 2 * m + r;
                const double tau = found + (std::abs (den) > 1e-12 ? 0.5 * (l - r) / den : 0.0);
                out.push_back (srD / tau);
            }
            return out;
        }

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
        int evaluationsPerArchitecture = 36;
        double timeLimitS = 120.0;
    };

    class Searcher
    {
    public:
        explicit Searcher (const ReferenceFeatures& f) : ref (f), eval (f) {}

        std::atomic<bool> cancel { false };
        std::atomic<float> progress { 0.0f };
        int evaluations = 0;

        std::vector<PatchCandidate> run (const SearchSettings& st)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const bool bass = ref.soundClass == SoundClass::Bass || (ref.pitched && ref.f0 < 110.0);
            auto archs = bass ? bassArchitectures() : leadArchitectures();
            std::vector<PatchCandidate> pool;
            const int total = (int) archs.size();
            for (int ai = 0; ai < total && ! cancel.load(); ++ai)
            {
                const auto& arch = archs[(size_t) ai];
                auto p = arch.estimate (ref);
                p.origin = "forge"; p.reference = ref.name; p.architecture = arch.label; p.seed = st.seed;
                auto genes = genesFor (arch, p, bass);
                juce::Random rng (st.seed * 7919 + ai);
                auto best = refine (p, genes, rng, st.evaluationsPerArchitecture, t0, st.timeLimitS);
                pool.push_back (finish (best.first, best.second, arch.key + "-1", 0, {}));
                progress = (float) (ai + 1) / (float) (total + 1);
                if ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 > st.timeLimitS) break;
            }
            // Exploración: variaciones de los mejores hasta llenar el pedido (con otra semilla cada una)
            std::sort (pool.begin(), pool.end(), [] (auto& a, auto& b) { return a.fidelity.loss < b.fidelity.loss; });
            juce::Random rng (st.seed * 104729 + 17);
            int variant = 2;
            const size_t bases = pool.size();
            while ((int) pool.size() < st.candidates + 2 && bases > 0 && ! cancel.load()
                   && (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 < st.timeLimitS)
            {
                const auto& parent = pool[(size_t) (variant - 2) % std::min<size_t> (bases, 3)];
                auto child = mutate (parent.patch, rng, 0.25f);
                auto archIt = std::find_if (archs.begin(), archs.end(), [&] (auto& a) { return a.label == parent.patch.architecture; });
                auto genes = genesFor (archIt != archs.end() ? *archIt : archs.front(), child, bass);
                auto best = refine (child, genes, rng, st.evaluationsPerArchitecture / 2, t0, st.timeLimitS);
                pool.push_back (finish (best.first, best.second, parent.id.upToFirstOccurrenceOf ("-", false, false) + "-" + juce::String (variant), 1, parent.id));
                ++variant;
            }
            auto chosen = select (pool, st.candidates);
            for (auto& c : chosen) { if (cancel.load()) break; measureFully (c); }
            // orden final por la fidelidad medida (a parecida, el más simple primero)
            std::stable_sort (chosen.begin(), chosen.end(), [] (const PatchCandidate& a, const PatchCandidate& b)
            {
                if (std::abs (a.fidelity.overall - b.fidelity.overall) < 0.02f) return a.complexity < b.complexity;
                return a.fidelity.overall > b.fidelity.overall;
            });
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

    private:
        std::pair<SynthPatch, double> refine (SynthPatch p, const std::vector<Gene>& genes, juce::Random& rng, int budget, double t0, double limitS)
        {
            auto evalP = [&] (const SynthPatch& q) { ++evaluations; return eval.fastLoss (eval.render (renderer, q, nullptr, true)); };
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
        // Los mejores, sin casi-duplicados; a fidelidad parecida (5 %) primero el más simple
        static std::vector<PatchCandidate> select (std::vector<PatchCandidate> pool, int count)
        {
            std::sort (pool.begin(), pool.end(), [] (const PatchCandidate& a, const PatchCandidate& b)
            {
                const double la = a.fidelity.loss, lb = b.fidelity.loss;
                if (std::abs (la - lb) < 0.05 * juce::jmin (la, lb)) return a.complexity < b.complexity;
                return la < lb;
            });
            std::vector<PatchCandidate> out;
            for (auto& c : pool)
            {
                if (c.fidelity.silent || c.fidelity.loss >= 1.0e5) continue;
                bool dup = false;
                for (auto& o : out)
                {
                    double d = 0; int n = 0;
                    for (auto& info : synthParams()) { d += std::abs (c.patch.getNorm (info.id) - o.patch.getNorm (info.id)); ++n; }
                    if (d / n < 0.004 && c.patch.architecture == o.patch.architecture) { dup = true; break; }
                }
                if (! dup) out.push_back (c);
                if ((int) out.size() >= count) break;
            }
            return out;
        }
    };
}
