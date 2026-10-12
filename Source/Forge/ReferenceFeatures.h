#pragma once
#include "../Metrics.h"

// ===========================================================================
// FORGE · ReferenceFeatures (ANALYZE)
//
// Lo que se puede medir de un audio de referencia para elegir y ajustar una receta de síntesis:
// tono y su confianza, curva de tono (caída inicial, glide final, vibrato), envolvente de amplitud
// (ataque, decaimiento, sostenido), estructura armónica del cuerpo, ruido fuera de los armónicos,
// brillo en el tiempo, golpe inicial y ancho estéreo. Cada dato lleva su confianza; lo que no se puede
// inferir con fiabilidad se informa en "warnings" en vez de inventarse.
// ===========================================================================
namespace Forge
{
    enum class SoundClass { Tonal, Bass, Percussive, Inharmonic, Noisy, Evolving, Unknown };

    inline juce::String className (SoundClass c)
    {
        switch (c)
        {
            case SoundClass::Tonal:      return "tonal";
            case SoundClass::Bass:       return "bajo / 808";
            case SoundClass::Percussive: return "percusivo tonal";
            case SoundClass::Inharmonic: return "inarmonico";
            case SoundClass::Noisy:      return "ruido";
            case SoundClass::Evolving:   return "evolutivo";
            default:                     return "desconocido";
        }
    }

    struct ReferenceFeatures
    {
        double sampleRate = 48000.0, lengthS = 0.0;
        juce::String name;
        juce::AudioBuffer<float> audio;          // referencia recortada (desde el inicio del sonido), estéreo
        juce::AudioBuffer<float> tuned;          // la misma, llevada a la afinación exacta de "note" (para comparar)

        // Tono
        bool pitched = false;
        double pitchConfidence = 0.0;            // fracción de cuadros audibles con tono detectado (0..1)
        double f0 = 0.0, rootExact = 60.0;       // Hz y nota MIDI exacta del cuerpo
        int note = 60;                           // tecla con la que se toca/compara
        std::vector<double> pitchCents;          // por cuadro de 10 ms, respecto al cuerpo (NaN = sin tono)
        double pitchStartCents = 0.0;            // primeros 30 ms respecto al cuerpo (caída de un 808: positivo)
        double pitchDropTimeS = 0.0;             // tiempo hasta quedar a menos de 1/e de la caída
        double pitchEndCents = 0.0;              // último 20 % de la nota respecto al cuerpo (glide: negativo/positivo)
        double glideStartS = 0.0, glideTimeS = 0.0;
        double vibratoHz = 0.0, vibratoCents = 0.0, vibratoStartS = 0.0;

        // Amplitud (curva de sonoridad en dB, 10 ms)
        std::vector<double> loudness;
        double attackS = 0.0;                    // del inicio al máximo
        double decayS = 0.0;                     // del máximo a -20 dB (o al final)
        double sustainDb = -60.0;                // nivel del cuerpo respecto al máximo
        double peakS = 0.0;
        double releaseStartS = -1.0;             // sonidos sostenidos: dónde empieza a apagarse (-1 = decae solo)
        double releaseS = 0.0;                   // de ahí a -40 dB

        // Timbre
        std::array<double, 24> harmonicsDb {};   // cuerpo: armónicos 1..24 en dB respecto al mayor
        double noiseDb = -60.0;                  // energía fuera de los armónicos (dB de la fracción)
        double centroidBody = 0.0, centroidAttack = 0.0, centroidEnd = 0.0;   // centroide en bandas mel (0..31)
        double earlyDb = 0.0;                    // energía de los primeros 10 ms respecto a los primeros 100 ms
        double sideMidDb = -200.0;               // ancho estéreo
        double subFractionDb = -60.0;            // energía bajo 120 Hz

        SoundClass soundClass = SoundClass::Unknown;
        juce::StringArray warnings;              // lo que no se pudo inferir con fiabilidad

        juce::String summary() const
        {
            juce::String s;
            s << className (soundClass);
            if (pitched) s << ", " << juce::String (f0, 1) << " Hz (confianza " << juce::roundToInt (pitchConfidence * 100) << " %)";
            if (std::abs (pitchStartCents) > 50) s << ", cae " << juce::String (pitchStartCents / 100.0, 1) << " st al inicio";
            if (std::abs (pitchEndCents) > 50) s << ", glide " << juce::String (pitchEndCents / 100.0, 1) << " st";
            if (vibratoCents > 5) s << ", vibrato " << juce::String (vibratoHz, 1) << " Hz";
            s << ", ataque " << juce::roundToInt (attackS * 1000) << " ms, decae " << juce::String (decayS, 2) << " s";
            return s;
        }
    };

    inline ReferenceFeatures analyse (const juce::AudioBuffer<float>& in, double sr, const juce::String& name = {})
    {
        using namespace Bench;
        ReferenceFeatures f;
        f.sampleRate = sr; f.name = name;
        const int nch = juce::jmax (1, in.getNumChannels());

        // ---- recorte: desde el inicio del sonido (-50 dB del pico) hasta que queda por debajo de -70 dB ----
        float peak = 0.0f;
        for (int ch = 0; ch < in.getNumChannels(); ++ch) peak = juce::jmax (peak, in.getMagnitude (ch, 0, in.getNumSamples()));
        if (peak <= 1.0e-6f) { f.warnings.add ("La referencia esta en silencio"); return f; }
        auto absAt = [&] (int i) { float a = 0; for (int ch = 0; ch < in.getNumChannels(); ++ch) a = juce::jmax (a, std::abs (in.getSample (ch, i))); return a; };
        int start = 0; while (start < in.getNumSamples() - 1 && absAt (start) < peak * 0.003f) ++start;
        start = juce::jmax (0, start - (int) (0.002 * sr));
        int end = in.getNumSamples() - 1; while (end > start && absAt (end) < peak * 0.0003f) --end;
        const int len = juce::jlimit ((int) (0.05 * sr), (int) (12.0 * sr), end - start + 1);
        f.audio.setSize (2, len);
        f.audio.clear();
        for (int ch = 0; ch < 2; ++ch)
            f.audio.copyFrom (ch, 0, in, juce::jmin (ch, nch - 1), start, juce::jmin (len, in.getNumSamples() - start));
        f.lengthS = len / sr;
        const auto mono = toMono (f.audio);

        // ---- amplitud ----
        f.loudness = loudnessCurve (f.audio, sr);
        const int L = (int) f.loudness.size();
        int pk = 0; for (int i = 0; i < L; ++i) if (f.loudness[(size_t) i] > f.loudness[(size_t) pk]) pk = i;
        const double maxDb = f.loudness[(size_t) pk];
        f.peakS = pk * 0.01;
        f.attackS = f.peakS;
        int d20 = pk; while (d20 < L - 1 && f.loudness[(size_t) d20] > maxDb - 20.0) ++d20;
        f.decayS = (d20 - pk) * 0.01;
        {
            std::vector<double> body;
            for (int i = pk + (int) (0.4 * (L - pk)); i < pk + (int) (0.8 * (L - pk)); ++i) body.push_back (f.loudness[(size_t) i] - maxDb);
            f.sustainDb = body.empty() ? -60.0 : median (body);
        }
        if (f.sustainDb > -12.0)
        {
            // sostenido: la nota se "suelta" donde el nivel cae por última vez por debajo del cuerpo
            int r = L - 1; while (r > pk && f.loudness[(size_t) r] < maxDb + f.sustainDb - 3.0) --r;
            if (r < L - 5)
            {
                f.releaseStartS = r * 0.01;
                int e = r; while (e < L - 1 && f.loudness[(size_t) e] > maxDb + f.sustainDb - 40.0) ++e;
                f.releaseS = juce::jmax (0.02, (e - r) * 0.01);
            }
        }

        // ---- tono ----
        const auto lowCheck = bandFractionDb (mono, sr, 20.0, 120.0);
        f.subFractionDb = lowCheck;
        const auto track = yinTrack (mono, sr, 0.01, lowCheck > -6.0 ? 25.0 : 50.0);
        std::vector<double> voicedLog;
        int audible = 0, voiced = 0;
        for (size_t i = 0; i < track.size() && i < f.loudness.size(); ++i)
        {
            if (f.loudness[i] < maxDb - 30.0) continue;
            ++audible;
            if (track[i] > 0) { ++voiced; voicedLog.push_back (std::log2 (track[i])); }
        }
        f.pitchConfidence = audible > 0 ? (double) voiced / audible : 0.0;
        f.pitched = f.pitchConfidence > 0.5 && voicedLog.size() >= 5;
        if (f.pitched)
        {
            // el cuerpo: mediana de la mitad central de los cuadros con tono
            std::vector<double> mid (voicedLog.begin() + (long) (voicedLog.size() / 4), voicedLog.end() - (long) (voicedLog.size() / 4));
            f.f0 = std::exp2 (median (mid.empty() ? voicedLog : mid));
            f.rootExact = 69.0 + 12.0 * std::log2 (f.f0 / 440.0);
            f.note = juce::jlimit (12, 115, juce::roundToInt (f.rootExact));
            f.pitchCents.assign (track.size(), kNaN);
            for (size_t i = 0; i < track.size(); ++i)
                if (track[i] > 0 && i < f.loudness.size() && f.loudness[i] > maxDb - 40.0) f.pitchCents[i] = 1200.0 * std::log2 (track[i] / f.f0);
            auto medianRange = [&] (size_t a, size_t b)
            {
                std::vector<double> v; for (size_t i = a; i < b && i < f.pitchCents.size(); ++i) if (std::isfinite (f.pitchCents[i])) v.push_back (f.pitchCents[i]);
                return v.empty() ? 0.0 : median (v);
            };
            const size_t on = 0, n = f.pitchCents.size();
            f.pitchStartCents = medianRange (on, on + 4);
            // Los primeros milisegundos (la caída de un 808 dura ~10-50 ms) no los ve una ventana de YIN:
            // se miden por cruces por cero de la señal filtrada (frecuencia de cada ciclo). Sólo en graves: en
            // notas agudas el filtro deja pasar armónicos y los cruces no miden la fundamental.
            if (f.f0 < 150.0)
            {
                juce::IIRFilter lp; lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, juce::jmin (sr * 0.45, f.f0 * 6.0)));
                const int lim = juce::jmin (len, (int) (0.25 * sr));
                std::vector<double> crossT;
                float prev = 0.0f;
                for (int i = 0; i < lim; ++i)
                {
                    const float y = lp.processSingleSampleRaw ((float) mono[(size_t) i]);
                    if (i > 0 && (prev < 0.0f) != (y < 0.0f) && std::abs (y - prev) > 1.0e-9f) crossT.push_back ((i - 1 + prev / (prev - y)) / sr);
                    prev = y;
                }
                std::vector<std::pair<double, double>> inst;   // (tiempo, cents respecto al cuerpo)
                for (size_t i = 2; i < crossT.size(); ++i)
                {
                    const double hz = 1.0 / (crossT[i] - crossT[i - 2]);   // un ciclo completo
                    inst.push_back ({ 0.5 * (crossT[i] + crossT[i - 2]), 1200.0 * std::log2 (hz / f.f0) });
                }
                if (inst.size() >= 3)
                {
                    const double startC = 0.5 * (inst[0].second + inst[1].second);
                    if (std::abs (startC) > 30.0 && std::abs (startC) > std::abs (f.pitchStartCents))
                    {
                        f.pitchStartCents = startC;
                        f.pitchDropTimeS = 0.0;
                        for (auto& [t, c] : inst) if (std::abs (c) < std::abs (startC) * 0.37) { f.pitchDropTimeS = t; break; }
                        if (f.pitchDropTimeS <= 0.0) f.pitchDropTimeS = inst.back().first;
                    }
                }
            }
            if (std::abs (f.pitchStartCents) > 30.0)
            {
                // tiempo de la caída: hasta quedar a menos de 1/e de la desviación inicial
                for (size_t i = on; i < n; ++i)
                    if (std::isfinite (f.pitchCents[i]) && std::abs (f.pitchCents[i]) < std::abs (f.pitchStartCents) * 0.37) { f.pitchDropTimeS = (double) (i - on) * 0.01; break; }
            }
            f.pitchEndCents = medianRange ((size_t) (n * 0.8), n);
            if (std::abs (f.pitchEndCents) > 50.0)
            {
                // inicio del glide: último cuadro todavía a menos de 15 % del cambio final
                for (size_t i = n / 3; i < n; ++i)
                    if (std::isfinite (f.pitchCents[i]) && std::abs (f.pitchCents[i]) > std::abs (f.pitchEndCents) * 0.15) { f.glideStartS = (double) i * 0.01; break; }
                for (size_t i = (size_t) (f.glideStartS / 0.01); i < n; ++i)
                    if (std::isfinite (f.pitchCents[i]) && std::abs (f.pitchCents[i]) > std::abs (f.pitchEndCents) * 0.9) { f.glideTimeS = (double) i * 0.01 - f.glideStartS; break; }
            }
            // vibrato: periodo por autocorrelación de la desviación (sin tendencia) en el cuerpo; profundidad =
            // amplitud de la oscilación donde es más fuerte; inicio = cuándo aparece (vibrato tardío)
            {
                std::vector<double> body; size_t firstIdx = 0;
                for (size_t i = n / 6; i < n * 5 / 6; ++i) if (std::isfinite (f.pitchCents[i])) { if (body.empty()) firstIdx = i; body.push_back (f.pitchCents[i]); }
                if (body.size() > 40 && std::abs (f.pitchEndCents) < 50.0)
                {
                    const auto sm = smoothCurve (body, 12);
                    std::vector<double> res (body.size());
                    for (size_t i = 0; i < body.size(); ++i) res[i] = body[i] - sm[i];
                    double best = 0; int bestLag = 0;
                    for (int lag = 8; lag <= 50 && lag < (int) res.size() / 2; ++lag)   // 2 a 12.5 Hz
                    {
                        double num = 0, a = 0, b = 0;
                        for (size_t i = (size_t) lag; i < res.size(); ++i) { num += res[i] * res[i - (size_t) lag]; a += res[i] * res[i]; b += res[i - (size_t) lag] * res[i - (size_t) lag]; }
                        const double r = num / std::sqrt (juce::jmax (1e-12, a * b));
                        if (r > best) { best = r; bestLag = lag; }
                    }
                    // profundidad: RMS por tramos de 300 ms; el vibrato empieza donde supera la mitad del máximo
                    std::vector<double> segRms;
                    for (size_t i = 0; i + 30 <= res.size(); i += 30) { double e = 0; for (size_t j = i; j < i + 30; ++j) e += res[j] * res[j]; segRms.push_back (std::sqrt (e / 30.0)); }
                    double mx = 0; for (auto v : segRms) mx = juce::jmax (mx, v);
                    if (best > 0.4 && bestLag > 0 && mx * 1.41 > 5.0)
                    {
                        f.vibratoHz = 100.0 / bestLag;
                        // interpolación parabólica del pico de autocorrelación para afinar la frecuencia
                        f.vibratoCents = mx * 1.41;
                        for (size_t k = 0; k < segRms.size(); ++k) if (segRms[k] > 0.5 * mx) { f.vibratoStartS = (double) (firstIdx + k * 30) * 0.01; break; }
                    }
                }
            }
        }
        else
        {
            f.warnings.add (juce::String ("Tono poco fiable (") + juce::String (juce::roundToInt (f.pitchConfidence * 100)) + " % de los cuadros): no se infiere una curva de tono");
        }

        // ---- referencia afinada a la tecla (para comparar sin castigar unos cents de diferencia) ----
        f.tuned = f.audio;
        if (f.pitched)
        {
            const double ratio = std::exp2 ((f.note - f.rootExact) / 12.0);
            const int newLen = juce::jmax (2, (int) (len / ratio));
            juce::AudioBuffer<float> t (2, newLen);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = f.audio.getReadPointer (ch);
                auto at = [&] (int i) { return src[juce::jlimit (0, len - 1, i)]; };
                for (int i = 0; i < newLen; ++i)
                {
                    const double pos = i * ratio; const int i0 = (int) pos; const float fr = (float) (pos - i0);
                    const float y0 = at (i0 - 1), y1 = at (i0), y2 = at (i0 + 1), y3 = at (i0 + 2);
                    t.setSample (ch, i, y1 + 0.5f * fr * (y2 - y0 + fr * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + fr * (3.0f * (y1 - y2) + y3 - y0))));
                }
            }
            f.tuned = std::move (t);
        }

        // ---- timbre del cuerpo: armónicos y ruido ----
        {
            const int order = 14, N = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) N * 2, 0.0f);
            const int c = juce::jlimit (0, juce::jmax (0, len - N), (int) ((f.peakS + 0.25 * (f.lengthS - f.peakS)) * sr) - N / 2);
            for (int i = 0; i < N && c + i < len; ++i) buf[(size_t) i] = (float) (mono[(size_t) (c + i)] * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N)));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double mx = 1e-20, on = 0, off = 0;
            std::array<double, 24> h {};
            for (int k = 1; k < N / 2; ++k)
            {
                const double hz = k * sr / N, p = (double) buf[(size_t) k] * buf[(size_t) k];
                if (hz > 16000) break;
                if (f.pitched)
                {
                    const double hn = hz / f.f0; const int hi = (int) std::round (hn);
                    if (hi >= 1 && std::abs (hn - hi) * f.f0 < juce::jmax (3.0 * sr / N, 0.03 * f.f0)) { on += p; if (hi <= 24) h[(size_t) hi - 1] += p; }
                    else off += p;
                }
            }
            for (auto v : h) mx = juce::jmax (mx, v);
            for (size_t i = 0; i < 24; ++i) f.harmonicsDb[i] = db (h[i] / mx);
            f.noiseDb = f.pitched ? db (off / juce::jmax (1e-20, on + off)) : 0.0;
        }
        {
            auto centroid = [&] (double t0, double t1)
            {
                const auto x = std::vector<double> (mono.begin() + juce::jlimit (0, len, (int) (t0 * sr)), mono.begin() + juce::jlimit (0, len, (int) (t1 * sr)));
                const auto m = melSpectrogram (x, sr, 32);
                std::vector<double> c; for (auto v : m.centroid) if (std::isfinite (v)) c.push_back (v);
                return c.empty() ? 0.0 : median (c);
            };
            f.centroidAttack = centroid (0.0, juce::jmin (f.lengthS, 0.08));
            f.centroidBody = centroid (f.peakS, juce::jmin (f.lengthS, f.peakS + 0.5 * (f.lengthS - f.peakS)));
            f.centroidEnd = centroid (f.peakS + 0.6 * (f.lengthS - f.peakS), f.lengthS);
        }
        {
            double e10 = 0, e100 = 0;
            for (int i = 0; i < (int) (0.1 * sr) && i < len; ++i) { const double v = mono[(size_t) i] * mono[(size_t) i]; e100 += v; if (i < (int) (0.01 * sr)) e10 += v; }
            f.earlyDb = db (e10 / juce::jmax (1e-20, e100));
        }
        f.sideMidDb = stereoBands (f.audio, sr).sideMidDb;

        // ---- clase ----
        if (! f.pitched)
            f.soundClass = f.noiseDb > -3.0 || f.pitchConfidence < 0.2 ? SoundClass::Noisy : SoundClass::Inharmonic;
        else if (f.noiseDb > -6.0)
            f.soundClass = SoundClass::Inharmonic;
        else if (f.f0 < 110.0 && f.subFractionDb > -6.0)
            f.soundClass = SoundClass::Bass;
        else if (f.sustainDb < -25.0 && f.decayS < 1.5)
            f.soundClass = SoundClass::Percussive;
        else if (f.lengthS > 3.0 && std::abs (f.centroidEnd - f.centroidBody) > 3.0)
            f.soundClass = SoundClass::Evolving;
        else
            f.soundClass = SoundClass::Tonal;

        if (f.lengthS >= 12.0) f.warnings.add ("Se analizan los primeros 12 s");
        if (f.sideMidDb > -6.0) f.warnings.add ("Estereo ancho: las recetas de esta etapa son mono por voz");
        return f;
    }
}
