#pragma once
#include <juce_dsp/juce_dsp.h>
#include <map>
#include "SampleData.h"
#include "Parameters.h"

// ===========================================================================
// Análisis profundo de un sonido de referencia y su resíntesis.
//
//  · Tono exacto (con cents) o, si es una frase, su tonalidad.
//  · Timbre: armónicos, brillo, ruido, si es inarmónico.
//  · Envolvente (ataque, caída, sostenido, suelta) y vibrato.
//  · Resíntesis: una tabla de ondas que cambia en el tiempo (cada cuadro es
//    el espectro armónico del sonido en ese momento) + ruido con su envolvente.
// ===========================================================================
struct SoundProfile
{
    bool valid = false;

    // Tono
    bool pitched = false, melodic = false;
    float note = 60.0f;          // nota MIDI con decimales (los decimales son cents)
    float f0 = 261.63f;
    float tuningCents = 0.0f;    // desafinación respecto a A440
    int keyTonic = -1;           // 0 = C ... 11 = B (sólo frases melódicas)
    bool keyMinor = false;

    // Timbre
    std::array<float, 16> harmonicsDb {};   // relativo al más fuerte
    int strongHarmonics = 0;                // armónicos a menos de 30 dB del más fuerte
    float evenOddRatio = 1.0f;              // energía armónicos pares / impares (sin la fundamental)
    float slopeDb = -6.0f;                  // dB por octava de caída de los armónicos
    float brightnessHz = 0.0f, brightnessEndHz = 0.0f;
    float noiseRatio = 0.0f, noiseAttackRatio = 0.0f, noiseBrightnessHz = 0.0f;
    bool inharmonic = false;

    // Envolvente
    float attackS = 0.01f, decayS = 0.3f, sustainLevel = 1.0f, releaseS = 0.2f;
    bool sustained = true;
    float durationS = 0.0f, scanS = 1.0f, onsetS = 0.0f;

    // Vibrato
    float vibratoHz = 0.0f, vibratoCents = 0.0f;

    // Parecido de la recreación (0..1), lo llena el procesador
    float similarity = -1.0f;
};

struct ResynthData
{
    std::vector<std::vector<float>> cycles;   // cuadros de un ciclo (Wavetable::frameSize)
    std::vector<float> times, pitchCents, noiseRms, localHz;   // por cuadro
    float peakCycle = 1.0f;
    float scanCurve = 0.3f;                   // curva de la Env 3 que recorre la tabla
    float noiseAmount = 0.0f;                 // cantidad de la matriz Env 2 -> Ruido
    float noiseColor = 1.0f;
    float nA = 0.005f, nD = 0.2f, nS = 0.0f, nR = 0.2f;   // envolvente del ruido (Env 2)
};

namespace SoundProfiler
{
    inline const char* noteNames (int pc)
    {
        static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return n[((pc % 12) + 12) % 12];
    }
    inline juce::String noteName (float midi)
    {
        const int n = juce::roundToInt (midi);
        return juce::String (noteNames (n)) + juce::String (n / 12 - 1);
    }
    inline juce::String noteWithCents (float midi)
    {
        const int n = juce::roundToInt (midi);
        const int c = juce::roundToInt ((midi - (float) n) * 100.0f);
        return noteName (midi) + (c == 0 ? juce::String() : (c > 0 ? " +" : " ") + juce::String (c) + " ct");
    }

    // ---- Envolvente RMS (cuadros de 5 ms) ----
    struct Envelope { std::vector<float> lin; double hop = 0.005; float peak = 0.0f; int peakIdx = 0; };

    inline Envelope rmsEnvelope (const float* x, int n, double sr)
    {
        Envelope e;
        const int hop = juce::jmax (1, (int) (sr * e.hop)), win = hop * 2;
        for (int i = 0; i + 1 < n; i += hop)
        {
            double s = 0.0; int c = 0;
            for (int k = i - win / 2; k < i + win / 2; ++k) if (k >= 0 && k < n) { s += (double) x[k] * x[k]; ++c; }
            const float r = (float) std::sqrt (s / juce::jmax (1, c));
            if (r > e.peak) { e.peak = r; e.peakIdx = (int) e.lin.size(); }
            e.lin.push_back (r);
        }
        return e;
    }

    // ---- Contorno de tono (cada 10 ms) ----
    struct Contour { std::vector<float> note, weight; double hop = 0.01; };

    inline Contour pitchContour (const float* x, int n, double sr, float minRms)
    {
        Contour c;
        // Como mucho ~400 cuadros (los samples largos se miran más espaciado)
        c.hop = juce::jmax (0.01, n / sr / 400.0);
        const int hop = (int) (sr * c.hop), win = (int) (sr * 0.06);
        for (int i = 0; i + win < n; i += hop)
        {
            double s = 0.0;
            for (int k = 0; k < win; k += 2) s += (double) x[i + k] * x[i + k];
            const float r = (float) std::sqrt (s / (win / 2));
            float note = -1.0f;
            if (r > minRms)
            {
                const float hz = SampleTools::detectPitch (x + i, win, sr);
                if (hz > 30.0f && hz < 2500.0f) note = 69.0f + 12.0f * std::log2 (hz / 440.0f);
            }
            c.note.push_back (note);
            c.weight.push_back (r);
        }
        return c;
    }

    inline float weightedMedian (std::vector<std::pair<float, float>> v)   // (valor, peso)
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        double total = 0.0; for (auto& p : v) total += p.second;
        double acc = 0.0;
        for (auto& p : v) { acc += p.second; if (acc >= total * 0.5) return p.first; }
        return v.back().first;
    }

    // ---- Tonalidad (Krumhansl-Kessler) ----
    inline void detectKey (const std::array<double, 12>& chroma, int& tonic, bool& minor)
    {
        static const double maj[] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int root)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + root) % 12)]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = chroma[(size_t) ((i + root) % 12)] - mx, b = prof[i] - my;
                sxy += a * b; sxx += a * a; syy += b * b;
            }
            return sxy / std::sqrt (sxx * syy + 1e-12);
        };
        double best = -2.0;
        for (int r = 0; r < 12; ++r)
            for (int m = 0; m < 2; ++m)
            {
                const double c = corr (m == 0 ? maj : mnr, r);
                if (c > best) { best = c; tonic = r; minor = m == 1; }
            }
    }

    // ---- Ajuste ADSR a una curva de nivel (lineal, normalizada al pico) ----
    inline void fitAdsr (const std::vector<float>& lv, double hop, float& a, float& d, float& s, bool& sustained, int& peakIdx)
    {
        const int n = (int) lv.size();
        peakIdx = 0;
        for (int i = 0; i < n; ++i) if (lv[(size_t) i] > lv[(size_t) peakIdx]) peakIdx = i;
        int start = 0;
        while (start < peakIdx && lv[(size_t) start] < 0.1f) ++start;
        int at90 = start;
        while (at90 < peakIdx && lv[(size_t) at90] < 0.9f) ++at90;
        a = (float) juce::jmax (0.001, (at90 - start) * hop);

        // Sostenido: nivel típico entre el 55 % y el 80 % del sonido
        std::vector<float> mid;
        for (int i = peakIdx + (n - peakIdx) * 55 / 100; i < peakIdx + (n - peakIdx) * 80 / 100 && i < n; ++i) mid.push_back (lv[(size_t) i]);
        float sus = 0.0f;
        if (! mid.empty()) { std::nth_element (mid.begin(), mid.begin() + (long) mid.size() / 2, mid.end()); sus = mid[mid.size() / 2]; }
        sustained = sus > 0.25f;
        s = sustained ? juce::jlimit (0.0f, 1.0f, sus) : 0.0f;

        const float target = s + 0.37f * (1.0f - s);   // una "constante de tiempo"
        int i = peakIdx;
        while (i < n - 1 && lv[(size_t) i] > target) ++i;
        // Sin sostenido, la caída completa dura unas 4 constantes de tiempo
        d = (float) juce::jlimit (0.01, 5.0, (i - peakIdx) * hop * (sustained ? 2.5 : 4.0));
    }

    // =======================================================================
    // Análisis completo + datos para la resíntesis
    // =======================================================================
    inline SoundProfile analyse (const SampleData& smp, float regionStart, float regionEnd, float rootOffset,
                                 ResynthData* resynth = nullptr, int numFrames = 64, double maxScan = 5.0)
    {
        SoundProfile p;
        const int full = smp.length();
        const int a0 = juce::jlimit (0, juce::jmax (0, full - 2), (int) (regionStart * (float) full));
        const int a1 = juce::jlimit (a0 + 2, full, (int) (regionEnd * (float) full));
        const float* x = smp.data() + a0;
        const int n = a1 - a0;
        const double sr = smp.sampleRate;
        if (n < (int) (sr * 0.03)) return p;

        // ---- Envolvente y comienzo ----
        auto env = rmsEnvelope (x, n, sr);
        if (env.peak < 1.0e-5f) return p;
        std::vector<float> lv (env.lin.size());
        for (size_t i = 0; i < lv.size(); ++i) lv[i] = env.lin[i] / env.peak;
        int onsetIdx = 0;
        while (onsetIdx < env.peakIdx && lv[(size_t) onsetIdx] < 0.03f) ++onsetIdx;
        int endIdx = (int) lv.size() - 1;
        while (endIdx > env.peakIdx && lv[(size_t) endIdx] < 0.003f) --endIdx;
        p.onsetS = (float) (onsetIdx * env.hop);
        p.durationS = (float) ((endIdx - onsetIdx) * env.hop);

        std::vector<float> lvFromOnset (lv.begin() + onsetIdx, lv.begin() + endIdx + 1);
        int pk = 0;
        fitAdsr (lvFromOnset, env.hop, p.attackS, p.decayS, p.sustainLevel, p.sustained, pk);

        // Suelta: en sonidos sostenidos, la cola final del sample
        double scanEndS = p.durationS;
        if (p.sustained)
        {
            int rel = (int) lvFromOnset.size() - 1;
            while (rel > pk && lvFromOnset[(size_t) rel] < 0.7f * p.sustainLevel) --rel;
            const double tail = ((int) lvFromOnset.size() - 1 - rel) * env.hop;
            p.releaseS = (float) juce::jlimit (0.03, 4.0, tail * 0.6);
            scanEndS = rel * env.hop;
        }
        else p.releaseS = 0.25f;
        p.scanS = (float) juce::jlimit (0.05, maxScan, scanEndS);

        // ---- Tono ----
        const auto contour = pitchContour (x + (int) (p.onsetS * sr), n - (int) (p.onsetS * sr), sr, env.peak * 0.06f);
        std::vector<std::pair<float, float>> notes;
        double activeW = 0.0, pitchedW = 0.0;
        for (size_t i = 0; i < contour.note.size(); ++i)
        {
            if (contour.weight[i] < env.peak * 0.06f) continue;
            activeW += contour.weight[i];
            if (contour.note[i] > 0.0f) { notes.push_back ({ contour.note[i], contour.weight[i] }); pitchedW += contour.weight[i]; }
        }
        float rootGuess = smp.rootNote;
        if (activeW > 0.0 && pitchedW / activeW > 0.4 && notes.size() >= 3)
        {
            const float med = weightedMedian (notes);
            double stableW = 0.0;
            std::vector<std::pair<float, float>> stable;
            for (auto& v : notes) if (std::abs (v.first - med) < 0.5f) { stableW += v.second; stable.push_back (v); }

            // ¿Es una frase? Sólo si hay al menos 2 notas distintas que duran de verdad (>= 80 ms cada una)
            bool phrase = false;
            float modeNote = med;
            {
                std::map<int, double> durByNote;   // segundos por nota redondeada
                for (size_t i = 0; i < contour.note.size(); ++i)
                    if (contour.note[i] > 0.0f && contour.weight[i] >= env.peak * 0.06f)
                        durByNote[juce::roundToInt (contour.note[i])] += contour.hop;
                int distinct = 0; double best = 0.0;
                for (auto& [nt, d] : durByNote)
                {
                    if (d >= 0.08) ++distinct;
                    if (d > best) { best = d; modeNote = (float) nt; }
                }
                phrase = distinct >= 2 && p.durationS >= 0.8f && stableW / pitchedW < 0.6;
            }

            if (! phrase)
            {
                // Una sola nota: la que más dura (con sus cents)
                std::vector<std::pair<float, float>> near;
                for (auto& v : notes) if (std::abs (v.first - modeNote) < 0.5f) near.push_back (v);
                p.pitched = true;
                p.note = stableW / pitchedW >= 0.6 ? weightedMedian (stable) : (near.empty() ? med : weightedMedian (near));
            }
            else
            {
                // Frase melódica: tonalidad por croma
                p.pitched = true;
                p.melodic = true;
                std::array<double, 12> chroma {};
                std::vector<std::pair<float, float>> fractions;
                for (auto& v : notes)
                {
                    const int r = juce::roundToInt (v.first);
                    chroma[(size_t) (((r % 12) + 12) % 12)] += v.second;
                    fractions.push_back ({ v.first - (float) r, v.second });
                }
                detectKey (chroma, p.keyTonic, p.keyMinor);
                const float tune = weightedMedian (fractions);
                // Raíz: la tónica en la octava más cercana a la nota típica de la frase
                float best = 0.0f, bestD = 1e9f;
                for (int oct = 0; oct < 10; ++oct)
                {
                    const float cand = (float) (12 * oct + p.keyTonic);
                    if (std::abs (cand - med) < bestD) { bestD = std::abs (cand - med); best = cand; }
                }
                p.note = best + tune;
            }
            p.tuningCents = (p.note - (float) juce::roundToInt (p.note)) * 100.0f;

            // Vibrato (sólo notas sostenidas)
            if (! p.melodic && contour.note.size() > 40)
            {
                std::vector<float> c;
                for (size_t i = contour.note.size() / 6; i < contour.note.size(); ++i)
                    if (contour.note[i] > 0.0f && std::abs (contour.note[i] - p.note) < 1.0f) c.push_back ((contour.note[i] - p.note) * 100.0f);
                if (c.size() > 30)
                {
                    // Quitar la deriva lenta (media móvil de 250 ms)
                    std::vector<float> d (c.size());
                    for (size_t i = 0; i < c.size(); ++i)
                    {
                        double s = 0; int k = 0;
                        for (int j = (int) i - 12; j <= (int) i + 12; ++j) if (j >= 0 && j < (int) c.size()) { s += c[(size_t) j]; ++k; }
                        d[i] = c[i] - (float) (s / k);
                    }
                    double var = 0; int cross = 0;
                    for (size_t i = 0; i < d.size(); ++i) { var += d[i] * d[i]; if (i > 0 && (d[i - 1] < 0) != (d[i] < 0)) ++cross; }
                    const float sd = (float) std::sqrt (var / d.size());
                    const float rate = (float) (cross / 2.0 / (d.size() * contour.hop));
                    if (sd > 6.0f && rate > 2.5f && rate < 9.0f)
                    {
                        p.vibratoHz = rate;
                        p.vibratoCents = juce::jmin (100.0f, sd * 1.41f);
                    }
                }
            }
        }
        else
        {
            p.pitched = false;
            p.note = rootGuess;
        }
        // Si el usuario corrigió la nota a mano, manda su corrección
        if (std::abs (rootOffset) > 0.01f) { p.note = smp.rootNote + rootOffset; p.pitched = true; p.melodic = false; }
        p.f0 = 440.0f * std::exp2 ((p.note - 69.0f) / 12.0f);

        // ---- Análisis armónico por cuadros (y resíntesis) ----
        constexpr int order = 13, L = 1 << order, periods = 4, binsPerHarm = periods;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) L * 2), win ((size_t) L);
        double sumW = 0.0, sumW2 = 0.0;
        for (int i = 0; i < L; ++i)
        {
            win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) L);
            sumW += win[(size_t) i]; sumW2 += win[(size_t) i] * win[(size_t) i];
        }

        constexpr float scanCurve = 0.3f;                    // curva de la Env 3 que recorre la tabla
        const float scanP = std::exp2 (-scanCurve * 2.0f);  // cuadros más densos al principio (el ataque)
        const double f0Global = p.pitched ? 440.0 * std::exp2 ((p.note - 69.0) / 12.0) : 110.0;
        const int frames = juce::jlimit (2, 64, numFrames);

        std::array<double, 16> harmAcc {};
        double evenE = 0, oddE = 0, wTot = 0, brightAcc = 0, brightW = 0, noiseAcc = 0, harmAccE = 0;
        double noiseBrightAcc = 0, noiseBrightW = 0, flatAcc = 0, flatW = 0, inhAcc = 0, inhW = 0;
        std::vector<float> noiseRms ((size_t) frames, 0.0f), frameTimes ((size_t) frames, 0.0f), harmRmsF ((size_t) frames, 0.0f);
        std::vector<float> centroidF ((size_t) frames, 0.0f);
        if (resynth != nullptr) resynth->cycles.clear();

        for (int f = 0; f < frames; ++f)
        {
            const double u = (double) f / (frames - 1);
            const double t = p.onsetS + p.scanS * std::pow (u, 1.0 / scanP);
            frameTimes[(size_t) f] = (float) (t - p.onsetS);

            // f0 local (sigue el vibrato y pequeñas derivas)
            double f0 = f0Global;
            if (p.pitched && ! p.melodic)
            {
                const int ci = juce::jlimit (0, juce::jmax (0, (int) contour.note.size() - 1), (int) ((t - p.onsetS) / contour.hop));
                if (! contour.note.empty() && contour.note[(size_t) ci] > 0.0f && std::abs (contour.note[(size_t) ci] - p.note) < 1.0f)
                    f0 = 440.0 * std::exp2 ((contour.note[(size_t) ci] - 69.0) / 12.0);
            }
            if (p.melodic)
            {
                const int ci = juce::jlimit (0, juce::jmax (0, (int) contour.note.size() - 1), (int) ((t - p.onsetS) / contour.hop));
                if (! contour.note.empty() && contour.note[(size_t) ci] > 0.0f) f0 = 440.0 * std::exp2 ((contour.note[(size_t) ci] - 69.0) / 12.0);
            }

            if (resynth != nullptr)
            {
                resynth->localHz.push_back ((float) f0);
                resynth->pitchCents.push_back (p.pitched ? (float) juce::jlimit (-2400.0, 2400.0, 1200.0 * std::log2 (f0 / f0Global)) : 0.0f);
            }

            // Re-muestrea 4 periodos a L puntos: el armónico k cae justo en el bin 4k
            const double period = sr / f0;
            const double centre = t * sr;
            const double step = period * periods / L;
            for (int i = 0; i < L; ++i)
            {
                const double pos = centre + (i - L / 2) * step;
                const int i0 = (int) std::floor (pos);
                const float fr = (float) (pos - i0);
                auto at = [&] (int k) { return (k >= 0 && k < n) ? x[k] : 0.0f; };
                const float y0 = at (i0 - 1), y1 = at (i0), y2 = at (i0 + 1), y3 = at (i0 + 2);
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                buf[(size_t) i] = (((c3 * fr + c2) * fr + c1) * fr + y1) * win[(size_t) i];
            }
            std::fill (buf.begin() + L, buf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (buf.data(), true);
            auto re = [&] (int b) { return buf[(size_t) (2 * b)]; };
            auto im = [&] (int b) { return buf[(size_t) (2 * b + 1)]; };
            auto pw = [&] (int b) { return (double) re (b) * re (b) + (double) im (b) * im (b); };

            const int maxK = juce::jmin (1023, (int) (juce::jmin (sr * 0.45, 18000.0) / f0), L / (2 * binsPerHarm) - 1);
            double H = 0.0, N = 0.0, cAcc = 0.0, cW = 0.0, nCent = 0.0, nW = 0.0;
            std::vector<double> mags ((size_t) maxK + 1, 0.0), phs ((size_t) maxK + 1, 0.0);
            double logSum = 0.0, linSum = 0.0; int resBins = 0;
            double offAcc = 0.0, offW = 0.0;   // ¿los picos caen justo en los armónicos o corridos? (inarmonía)
            for (int k = 1; k <= maxK; ++k)
            {
                const int b = k * binsPerHarm;
                const double hp = pw (b - 1) + pw (b) + pw (b + 1);
                const double np = pw (b + 2) * binsPerHarm;
                H += hp; N += np;
                mags[(size_t) k] = std::sqrt (pw (b)) * 2.0 / sumW;
                phs[(size_t) k] = std::atan2 (im (b), re (b));
                const double hz = k * f0;
                cAcc += hz * hp; cW += hp;
                nCent += (hz + 0.5 * f0) * np; nW += np;
                logSum += std::log (pw (b + 2) + 1e-20); linSum += pw (b + 2); ++resBins;
            }
            // Inarmonía: picos del espectro que no caen en la rejilla de armónicos (campana, metal)
            {
                double maxP = 0.0;
                for (int b = 2; b < maxK * binsPerHarm; ++b) maxP = juce::jmax (maxP, pw (b));
                for (int b = 2; b < maxK * binsPerHarm; ++b)
                {
                    const double pb = pw (b);
                    if (pb < maxP * 1.0e-4 || pb < pw (b - 1) || pb < pw (b + 1)) continue;
                    if (pb < 4.0 * juce::jmin (pw (b - 2), pw (b + 2)) && pb < maxP * 0.01) continue;
                    const double a = std::sqrt (pw (b - 1)), c0 = std::sqrt (pb), c = std::sqrt (pw (b + 1));
                    const double den = a - 2.0 * c0 + c;
                    const double peakBin = b + (std::abs (den) > 1e-20 ? 0.5 * (a - c) / den : 0.0);
                    const double h = peakBin / binsPerHarm;
                    const double dev = std::abs (h - std::round (h));
                    offW += pb;
                    if (dev > 0.15 || h < 0.75) offAcc += pb;
                }
            }
            const double scale = 2.0 / ((double) L * sumW2);   // Parseval → potencia
            H *= scale; N *= scale;
            const double flat = resBins > 0 ? std::exp (logSum / resBins) / (linSum / resBins + 1e-20) : 1.0;

            noiseRms[(size_t) f] = (float) std::sqrt (N);
            harmRmsF[(size_t) f] = (float) std::sqrt (H);
            centroidF[(size_t) f] = (float) (cW > 0 ? cAcc / cW : 0.0);
            const double wF = std::sqrt (H + N);
            noiseAcc += N; harmAccE += H;
            if (f < frames / 8) p.noiseAttackRatio = juce::jmax (p.noiseAttackRatio, (float) (N / (N + H + 1e-20)));
            if (nW > 0) { noiseBrightAcc += nCent / nW * N; noiseBrightW += N; }
            flatAcc += flat * N; flatW += N;
            inhAcc += offAcc; inhW += offW;

            // Estadísticas de armónicos (ponderadas por energía)
            const double maxMag = *std::max_element (mags.begin(), mags.end()) + 1e-20;
            for (int k = 1; k <= juce::jmin (16, maxK); ++k)
                harmAcc[(size_t) k - 1] += wF * (mags[(size_t) k] / maxMag);
            for (int k = 2; k <= maxK; ++k)
                (k % 2 == 0 ? evenE : oddE) += wF * mags[(size_t) k] * mags[(size_t) k] / (maxMag * maxMag);
            if (cW > 0) { brightAcc += wF * cAcc / cW; brightW += wF; }
            wTot += wF;

            // Ciclo resintetizado: suma de armónicos con su fase relativa a la fundamental
            if (resynth != nullptr)
            {
                constexpr int fs = 2048;
                std::vector<float> spec ((size_t) fs * 2, 0.0f);
                const double ph1 = phs[1];
                for (int k = 1; k <= juce::jmin (maxK, fs / 2 - 1); ++k)
                {
                    if (mags[(size_t) k] < maxMag * 1.0e-3) continue;   // -60 dB: fuera
                    const double ph = phs[(size_t) k] - k * ph1;
                    // amplitud a / fase ph → coeficientes para la FFT inversa real de JUCE (escala fs/2)
                    spec[(size_t) (2 * k)]     = (float) (mags[(size_t) k] * std::cos (ph) * fs * 0.5);
                    spec[(size_t) (2 * k + 1)] = (float) (mags[(size_t) k] * std::sin (ph) * fs * 0.5);
                }
                juce::dsp::FFT inv (11);
                inv.performRealOnlyInverseTransform (spec.data());
                std::vector<float> cyc ((size_t) fs);
                for (int i = 0; i < fs; ++i) cyc[(size_t) i] = spec[(size_t) i];
                resynth->cycles.push_back (std::move (cyc));
            }
        }

        // ---- Resumen del timbre ----
        if (wTot > 0)
        {
            const double mx = *std::max_element (harmAcc.begin(), harmAcc.end()) + 1e-20;
            int strong = 0;
            for (int k = 0; k < 16; ++k)
            {
                p.harmonicsDb[(size_t) k] = (float) juce::jmax (-80.0, 20.0 * std::log10 (harmAcc[(size_t) k] / mx + 1e-9));
                if (p.harmonicsDb[(size_t) k] > -30.0f) ++strong;
            }
            p.strongHarmonics = strong;
            p.evenOddRatio = (float) (evenE / (oddE + 1e-12));
            p.brightnessHz = (float) (brightAcc / juce::jmax (1e-12, brightW));
            // Pendiente: ajuste lineal de dB contra log2(k) en los primeros 16
            double sx = 0, sy = 0, sxx = 0, sxy = 0; int cnt = 0;
            for (int k = 1; k <= 16; ++k)
                if (p.harmonicsDb[(size_t) k - 1] > -60.0f)
                {
                    const double lx = std::log2 ((double) k), ly = p.harmonicsDb[(size_t) k - 1];
                    sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly; ++cnt;
                }
            if (cnt > 2) p.slopeDb = (float) ((cnt * sxy - sx * sy) / (cnt * sxx - sx * sx + 1e-12));
        }
        p.noiseRatio = (float) (noiseAcc / (noiseAcc + harmAccE + 1e-20));
        p.noiseBrightnessHz = (float) (noiseBrightW > 0 ? noiseBrightAcc / noiseBrightW : 0.0);
        const double flatness = flatW > 0 ? flatAcc / flatW : 1.0;
        const double offCentre = inhW > 0 ? inhAcc / inhW : 0.0;
        p.inharmonic = p.pitched && ! p.melodic && ((p.noiseRatio > 0.25f && flatness < 0.12) || offCentre > 0.05);
        {
            float lastC = 0.0f; double lastW = 0;
            for (int f = frames * 2 / 3; f < frames; ++f) { lastC += centroidF[(size_t) f] * harmRmsF[(size_t) f]; lastW += harmRmsF[(size_t) f]; }
            p.brightnessEndHz = lastW > 0 ? (float) (lastC / lastW) : p.brightnessHz;
        }

        // ---- Ruido: cuánto, de qué color y con qué envolvente ----
        if (resynth != nullptr)
        {
            float peakCycle = 1.0e-9f;
            for (auto& c : resynth->cycles) for (auto v : c) peakCycle = juce::jmax (peakCycle, std::abs (v));
            resynth->peakCycle = peakCycle;
            resynth->times = frameTimes;
            resynth->noiseRms = noiseRms;
            const float nMax = *std::max_element (noiseRms.begin(), noiseRms.end());
            const bool useNoise = p.noiseRatio > 0.03f && ! p.inharmonic && nMax > 0.0f;
            constexpr float whiteRms = 1.2f * 0.57735f;   // ruido del sinte con cantidad 1
            resynth->noiseAmount = useNoise ? juce::jlimit (0.0f, 1.0f, (nMax / peakCycle) / whiteRms) : 0.0f;
            const float fc = juce::jlimit (200.0f, 20000.0f, p.noiseBrightnessHz * 1.3f);
            resynth->noiseColor = fc > 15000.0f ? 1.0f : std::log (fc / 200.0f) / std::log (100.0f);
            resynth->scanCurve = scanCurve;

            if (useNoise)
            {
                // Remuestrea la envolvente del ruido a pasos de 5 ms para ajustar el ADSR
                std::vector<float> nl;
                const double hop = 0.005;
                for (double tt = 0.0; tt <= p.scanS; tt += hop)
                {
                    int f = 0;
                    while (f < frames - 1 && frameTimes[(size_t) f + 1] < tt) ++f;
                    const int g = juce::jmin (frames - 1, f + 1);
                    const float span = juce::jmax (1.0e-6f, frameTimes[(size_t) g] - frameTimes[(size_t) f]);
                    const float w = juce::jlimit (0.0f, 1.0f, ((float) tt - frameTimes[(size_t) f]) / span);
                    nl.push_back ((noiseRms[(size_t) f] * (1.0f - w) + noiseRms[(size_t) g] * w) / nMax);
                }
                bool sus = false; int pkI = 0;
                fitAdsr (nl, hop, resynth->nA, resynth->nD, resynth->nS, sus, pkI);
                resynth->nA = juce::jmax (0.001f, resynth->nA);
                resynth->nR = p.releaseS;
            }
        }

        p.valid = true;
        return p;
    }
}
