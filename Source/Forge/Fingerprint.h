#pragma once
#include "../Metrics.h"
#include "FastPitch.h"

// ===========================================================================
// FORGE · SONIC_FINGERPRINT
//
// Un vector fijo de 69 números que resume un sonido para buscar patches parecidos. Todo va normalizado a
// rangos parecidos (aprox. -1..1) y no depende del volumen (todo en dB relativos al máximo). Lo que depende
// de la nota se expresa respecto a la fundamental (armónicos, brillo relativo a f0, curva de tono en cents):
// así un patch medido en C2 se puede encontrar con una referencia en F1. El espectro absoluto (mel) también
// está, con poco peso, para sonidos sin tono.
//
// Bloques (y su peso por defecto):
//   A  envolvente: dB respecto al máximo en 12 instantes (0 ms .. 2.5 s)            12
//   B  armónicos 1..16 del cuerpo, dB respecto al mayor                              16
//   C  brillo relativo a f0 (log2 del centroide / f0) en 8 instantes                  8
//   D  espectro mel absoluto del cuerpo (16 bandas, dB respecto al total)            16
//   E  curva de tono en cents respecto al cuerpo, 8 instantes                         8
//   F  escalares: ruido, confianza de tono, vibrato (profundidad, frecuencia),
//      sostenido, decaimiento, ataque, suelta, ancho estéreo                           9
// No identifica grabaciones: sólo describe cómo suena (NO_DIRECT_RECORDING_IDENTIFICATION).
// ===========================================================================
namespace Forge
{
    constexpr int fingerprintVersion = 1;
    constexpr int fingerprintDims = 69;
    using FingerprintVec = std::array<float, fingerprintDims>;

    enum class FpBlock { Envelope, Harmonics, Brightness, Mel, Pitch, Scalars };
    inline std::pair<int, int> blockRange (FpBlock b)
    {
        switch (b)
        {
            case FpBlock::Envelope:   return { 0, 12 };
            case FpBlock::Harmonics:  return { 12, 28 };
            case FpBlock::Brightness: return { 28, 36 };
            case FpBlock::Mel:        return { 36, 52 };
            case FpBlock::Pitch:      return { 52, 60 };
            default:                  return { 60, 69 };
        }
    }

    // f0Hz: fundamental conocida o estimada (0 = sin tono). releaseS: dónde se soltó la nota (-1 = no se sabe).
    inline FingerprintVec fingerprint (const juce::AudioBuffer<float>& stereo, double sr, double f0Hz, double releaseS = -1.0)
    {
        using namespace Bench;
        FingerprintVec v {};
        const auto x = toMono (stereo);
        if (x.empty()) return v;
        double peak = 0; for (auto s : x) peak = juce::jmax (peak, std::abs (s));
        if (peak < 1.0e-6) return v;
        const bool pitched = f0Hz > 0.0;
        const double f0 = pitched ? f0Hz : 200.0;

        // ---- A: envolvente ----
        const auto loud = loudnessCurve (stereo, sr);
        double lmax = -200; size_t pk = 0;
        for (size_t i = 0; i < loud.size(); ++i) if (loud[i] > lmax) { lmax = loud[i]; pk = i; }
        auto loudAt = [&] (double t) { const size_t i = (size_t) (t / 0.01); return i < loud.size() ? juce::jmax (-60.0, loud[i] - lmax) : -60.0; };
        const double times[] { 0.0, 0.005, 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64, 1.0, 1.5, 2.5 };
        for (int i = 0; i < 12; ++i) v[(size_t) i] = (float) (loudAt (times[i]) / 60.0 + 0.5);

        // ---- cuerpo: ventana alrededor del máximo + 25 % del resto ----
        const int n = (int) x.size();
        const int order = 13, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2);
        auto spectrumAt = [&] (int centre, int len, std::vector<double>& pw)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const int L = juce::jmin (len, N);
            for (int i = 0; i < L; ++i)
            {
                const int k = centre - L / 2 + i;
                if (k >= 0 && k < n) buf[(size_t) i] = (float) (x[(size_t) k] * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / L)));
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            pw.assign ((size_t) N / 2, 0.0);
            for (int k = 1; k < N / 2; ++k) pw[(size_t) k] = (double) buf[(size_t) k] * buf[(size_t) k];
        };
        const double lengthS = n / sr;
        const double bodyT = pk * 0.01 + 0.25 * juce::jmax (0.0, (releaseS > 0 ? juce::jmin (releaseS, lengthS) : lengthS) - pk * 0.01);
        std::vector<double> pw;
        spectrumAt ((int) (bodyT * sr), N, pw);

        // ---- B: armónicos ----
        double noiseOn = 0, noiseOff = 0;
        {
            std::array<double, 16> h {};
            const double binHz = sr / N;
            for (int k = 1; k < N / 2; ++k)
            {
                const double hz = k * binHz;
                if (hz > juce::jmin (16000.0, sr * 0.45)) break;
                const double hn = hz / f0; const int hi = (int) std::round (hn);
                const bool near = hi >= 1 && std::abs (hn - hi) * f0 < juce::jmax (2.5 * binHz, 0.04 * f0);
                if (near) { noiseOn += pw[(size_t) k]; if (hi <= 16) h[(size_t) hi - 1] += pw[(size_t) k]; }
                else noiseOff += pw[(size_t) k];
            }
            double mx = 1e-30; for (auto e : h) mx = juce::jmax (mx, e);
            for (int i = 0; i < 16; ++i) v[(size_t) (12 + i)] = pitched ? (float) (juce::jmax (-60.0, db (h[(size_t) i] / mx)) / 60.0 + 0.5) : 0.0f;
        }

        // ---- D: mel absoluto del cuerpo (16 bandas) ----
        {
            const double m0 = hzToMel (40.0), m1 = hzToMel (juce::jmin (16000.0, sr * 0.45));
            std::array<double, 16> band {};
            double tot = 1e-30;
            for (int k = 1; k < N / 2; ++k)
            {
                const double hz = k * sr / N;
                const double m = (hzToMel (hz) - m0) / (m1 - m0);
                if (m < 0 || m >= 1) continue;
                band[(size_t) juce::jlimit (0, 15, (int) (m * 16))] += pw[(size_t) k];
                tot += pw[(size_t) k];
            }
            for (int b = 0; b < 16; ++b) v[(size_t) (36 + b)] = (float) (juce::jmax (-60.0, db (band[(size_t) b] / tot)) / 60.0 + 0.5);
        }

        // ---- C: brillo relativo a f0 en el tiempo ----
        {
            const double ct[] { 0.01, 0.03, 0.06, 0.12, 0.25, 0.5, 1.0, 2.0 };
            std::vector<double> p2;
            for (int i = 0; i < 8; ++i)
            {
                const int c = (int) (ct[i] * sr);
                if (c >= n || loudAt (ct[i]) <= -59.0) { v[(size_t) (28 + i)] = 0.0f; continue; }
                spectrumAt (c, 2048, p2);
                double num = 0, den = 0;
                for (int k = 1; k < N / 2; ++k) { num += p2[(size_t) k] * k; den += p2[(size_t) k]; }
                const double cHz = den > 0 ? num / den * sr / N : f0;
                v[(size_t) (28 + i)] = (float) (std::log2 (juce::jmax (1.0, cHz) / f0) / 6.0);
            }
        }

        // ---- E: curva de tono ----
        double vibDepth = 0, vibRate = 0, conf = 0;
        if (pitched)
        {
            const auto tr = fastPitchTrack (x, sr, f0, 0.01);
            std::vector<double> cents (tr.size(), std::numeric_limits<double>::quiet_NaN());
            int voiced = 0, audible = 0;
            for (size_t i = 0; i < tr.size(); ++i)
            {
                const bool aud = i < loud.size() && loud[i] > lmax - 30.0;
                if (aud) ++audible;
                if (tr[i] > 0 && aud) { cents[i] = 1200.0 * std::log2 (tr[i] / f0); ++voiced; }
            }
            conf = audible > 0 ? (double) voiced / audible : 0.0;
            const double pt[] { 0.0, 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64 };
            for (int i = 0; i < 8; ++i)
            {
                // promedio de 3 cuadros alrededor del instante (el cuadro i cubre la ventana que empieza en i·10 ms)
                double acc = 0; int cnt = 0;
                for (int d = -1; d <= 1; ++d) { const int k = (int) (pt[i] / 0.01) + d; if (k >= 0 && k < (int) cents.size() && std::isfinite (cents[(size_t) k])) { acc += cents[(size_t) k]; ++cnt; } }
                v[(size_t) (52 + i)] = cnt > 0 ? (float) juce::jlimit (-2.0, 2.0, acc / cnt / 1200.0) : 0.0f;
            }
            // vibrato: desviación rápida (cuadro - media de ±6) y sus cruces
            std::vector<double> body;
            for (size_t i = cents.size() / 5; i < cents.size() * 4 / 5; ++i) if (std::isfinite (cents[i])) body.push_back (cents[i]);
            if (body.size() > 20)
            {
                const auto sm = smoothCurve (body, 6);
                double e = 0; int z = 0;
                for (size_t i = 1; i < body.size(); ++i) { const double a = body[i - 1] - sm[i - 1], b = body[i] - sm[i]; e += b * b; if ((a < 0) != (b < 0)) ++z; }
                vibDepth = std::sqrt (e / (double) body.size()) * 1.41;
                vibRate = z / 2.0 / (body.size() * 0.01);
            }
        }

        // ---- F: escalares ----
        {
            double sustain = -60;
            {
                std::vector<double> b;
                const size_t endI = releaseS > 0 ? juce::jmin (loud.size(), (size_t) (releaseS / 0.01)) : loud.size();
                for (size_t i = pk + (endI - pk) * 4 / 10; i < pk + (endI - pk) * 8 / 10 && i < loud.size(); ++i) b.push_back (loud[i] - lmax);
                if (! b.empty()) sustain = median (b);
            }
            size_t d20 = pk; while (d20 + 1 < loud.size() && loud[d20] > lmax - 20.0) ++d20;
            double rel = 0.0;
            if (releaseS > 0)
            {
                size_t r = (size_t) (releaseS / 0.01), e = r;
                const double lv = r < loud.size() ? loud[r] : -200;
                while (e + 1 < loud.size() && loud[e] > lv - 30.0) ++e;
                rel = (double) (e - r) * 0.01;
            }
            v[60] = (float) (pitched ? juce::jmax (-60.0, db (noiseOff / juce::jmax (1e-30, noiseOn + noiseOff))) / 60.0 + 1.0 : 1.0);
            v[61] = (float) conf;
            v[62] = (float) juce::jmin (1.0, vibDepth / 100.0);
            v[63] = (float) (vibDepth > 5.0 ? juce::jmin (1.0, vibRate / 10.0) : 0.0);
            v[64] = (float) (sustain / 60.0 + 0.5);
            v[65] = (float) (std::log2 (juce::jmax (0.01, (d20 - pk) * 0.01)) / 6.0);
            v[66] = (float) (std::log2 (juce::jmax (0.001, pk * 0.01)) / 10.0);
            v[67] = (float) (std::log2 (juce::jmax (0.01, rel)) / 6.0);
            const double sm = stereo.getNumChannels() > 1 ? stereoBands (stereo, sr).sideMidDb : -200.0;
            v[68] = (float) (juce::jlimit (-60.0, 0.0, std::isfinite (sm) ? sm : -60.0) / 60.0 + 0.5);
        }
        for (auto& e : v) if (! std::isfinite (e)) e = 0.0f;
        return v;
    }

    // ---------- distancia con pesos por clase ----------
    enum class Category { Bass, Eight08, Lead, Pluck, Pad, FmBell, Noise, count };

    inline const char* categoryName (Category c)
    {
        static const char* n[] { "BASS", "808", "LEAD", "PLUCK", "PAD", "FM_BELL", "NOISE_TEXTURE" };
        return n[juce::jlimit (0, (int) Category::count - 1, (int) c)];
    }

    using FpWeights = std::array<float, fingerprintDims>;

    // Pesos por bloque según qué importa en cada clase (CLASS_DEPENDENT_WEIGHTS)
    inline FpWeights weightsFor (Category c)
    {
        float wA = 1.5f, wB = 1.0f, wC = 1.0f, wD = 0.4f, wE = 1.0f, wF = 1.0f;
        switch (c)
        {
            case Category::Eight08: wE = 2.5f; wA = 2.0f; wB = 0.8f; break;
            case Category::Bass:    wB = 1.3f; wA = 1.5f; break;
            case Category::Lead:    wB = 1.5f; wC = 1.3f; break;
            case Category::Pluck:   wA = 2.0f; wC = 1.5f; break;
            case Category::Pad:     wA = 1.5f; wC = 1.5f; wD = 0.6f; break;
            case Category::FmBell:  wB = 0.8f; wD = 1.0f; wF = 1.3f; break;
            case Category::Noise:   wB = 0.0f; wE = 0.0f; wD = 1.6f; wA = 1.5f; break;
            default: break;
        }
        FpWeights w {};
        auto fill = [&] (FpBlock b, float x) { auto [a, z] = blockRange (b); for (int i = a; i < z; ++i) w[(size_t) i] = x; };
        fill (FpBlock::Envelope, wA); fill (FpBlock::Harmonics, wB); fill (FpBlock::Brightness, wC);
        fill (FpBlock::Mel, wD); fill (FpBlock::Pitch, wE); fill (FpBlock::Scalars, wF);
        return w;
    }

    inline float fpDistance (const FingerprintVec& a, const FingerprintVec& b, const FpWeights& w)
    {
        float d = 0;
        for (int i = 0; i < fingerprintDims; ++i) { const float e = a[(size_t) i] - b[(size_t) i]; d += w[(size_t) i] * e * e; }
        return std::sqrt (d);
    }
}
