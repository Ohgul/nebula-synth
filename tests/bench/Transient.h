#pragma once
// ===========================================================================
// Banco de pruebas — Viabilidad del residuo de transitorio (Paso 0, punto 7)
//
// Con sonidos cuya parte tonal y cuyo transitorio se conocen por separado, se
// estima el transitorio con cuatro métodos y se compara contra la verdad:
//   A. Resta en el tiempo: original - recreación de Nebula (fases del motor actual).
//   B. Resta de magnitudes por bin con la fase del original, usando la recreación.
//   C. Resta en el tiempo de un modelo armónico con FASE MEDIDA del original
//      (lo que permitiría un motor que arranque sus osciladores con esa fase).
//   D. Resta de magnitudes usando ese modelo armónico (C sin usar la fase).
// Medidas: SDR del transitorio estimado (dB, mayor = mejor) dentro de la
// ventana del ataque, y fuga tonal (energía del residuo después del ataque
// respecto al original, dB, menor = mejor: si es alta, la capa duplicaría el sonido).
// ===========================================================================
#include "Metrics.h"

namespace Bench
{
using Vec = std::vector<double>;

// STFT de magnitud/fase y resíntesis con suma de ventanas normalizada
struct Stft
{
    int order = 9, N = 512, hop = 128;
    std::vector<std::vector<std::complex<double>>> frames;
    int length = 0;

    void analyse (const Vec& x)
    {
        length = (int) x.size();
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2);
        frames.clear();
        for (int c = -N; c < length; c += hop)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i) { const int k = c + i; if (k >= 0 && k < length) buf[(size_t) i] = (float) (x[(size_t) k] * win (i)); }
            fft.performRealOnlyForwardTransform (buf.data());
            std::vector<std::complex<double>> f ((size_t) N / 2 + 1);
            for (int k = 0; k <= N / 2; ++k) f[(size_t) k] = { buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)] };
            frames.push_back (std::move (f));
        }
    }
    double win (int i) const { return 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N); }
    Vec synthesise() const
    {
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2);
        Vec y ((size_t) length, 0.0), wsum ((size_t) length, 0.0);
        int c = -N;
        for (auto& f : frames)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int k = 0; k <= N / 2; ++k) { buf[(size_t) (2 * k)] = (float) f[(size_t) k].real(); buf[(size_t) (2 * k + 1)] = (float) f[(size_t) k].imag(); }
            fft.performRealOnlyInverseTransform (buf.data());
            for (int i = 0; i < N; ++i)
            {
                const int k = c + i;
                if (k < 0 || k >= length) continue;
                const double w = win (i);
                y[(size_t) k] += buf[(size_t) i] * w;
                wsum[(size_t) k] += w * w;
            }
            c += hop;
        }
        for (size_t i = 0; i < y.size(); ++i) y[i] = wsum[i] > 1e-9 ? y[i] / wsum[i] : 0.0;
        return y;
    }
};

// Resta de magnitudes: R = X · max(0, 1 - |Y| / |X|)  (fase del original)
inline Vec magnitudeSubtract (const Vec& x, const Vec& y)
{
    Stft sx, sy; sx.analyse (x); sy.analyse (y);
    for (size_t f = 0; f < sx.frames.size() && f < sy.frames.size(); ++f)
        for (size_t k = 0; k < sx.frames[f].size(); ++k)
        {
            const double mx = std::abs (sx.frames[f][k]), my = std::abs (sy.frames[f][k]);
            const double g = mx > 1e-12 ? juce::jmax (0.0, 1.0 - my / mx) : 0.0;
            sx.frames[f][k] *= g;
        }
    return sx.synthesise();
}

// Modelo armónico con amplitud y fase medidas cuadro a cuadro (acepta armónicos estirados)
inline Vec harmonicModel (const Vec& x, double sr, double f0, int frameN = 1024, int hop = 128, bool prominence = true)
{
    const int Z = 8192;
    juce::dsp::FFT fftZ (13);
    std::vector<float> buf ((size_t) Z * 2);
    Vec w ((size_t) frameN);
    double wsumA = 0;
    for (int i = 0; i < frameN; ++i) { w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / frameN); wsumA += w[(size_t) i]; }
    Vec y (x.size(), 0.0), norm (x.size(), 0.0);
    for (int c = -frameN / 2; c < (int) x.size(); c += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        double energy = 0;
        for (int i = 0; i < frameN; ++i) { const int k = c + i; if (k >= 0 && k < (int) x.size()) { buf[(size_t) i] = (float) (x[(size_t) k] * w[(size_t) i]); energy += x[(size_t) k] * x[(size_t) k]; } }
        if (energy > 1e-12)
        {
            fftZ.performFrequencyOnlyForwardTransform (buf.data());
            const double binHz = sr / Z;
            double top = 0; for (int k = 1; k < Z / 2; ++k) top = juce::jmax (top, (double) buf[(size_t) k]);
            double prev = 0.0, prev2 = 0.0;
            for (int h = 1; h < 200; ++h)
            {
                // el siguiente armónico: a un paso parecido al anterior (admite armónicos estirados)
                const double step = h <= 2 ? f0 : juce::jlimit (0.8 * f0, 1.4 * f0, prev - prev2);
                const double expect = h == 1 ? f0 : prev + step;
                if (expect > juce::jmin (18000.0, sr * 0.45)) break;
                const int lo = juce::jmax (1, (int) ((expect - 0.3 * f0) / binHz)), hi = juce::jmin (Z / 2 - 2, (int) ((expect + 0.3 * f0) / binHz));
                int arg = lo; for (int k = lo; k <= hi; ++k) if (buf[(size_t) k] > buf[(size_t) arg]) arg = k;
                const double a = std::log (buf[(size_t) arg - 1] + 1e-12), b = std::log (buf[(size_t) arg] + 1e-12), cc = std::log (buf[(size_t) arg + 1] + 1e-12);
                const double den = a - 2 * b + cc;
                const double fk = (arg + (std::abs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0)) * binHz;
                prev2 = prev; prev = fk;
                if (buf[(size_t) arg] < top * 1e-3) continue;   // por debajo de -60 dB: no se modela
                if (prominence)
                {
                    // que sobresalga del entorno (si no, es ruido y no un armónico)
                    std::vector<float> around;
                    const int lobe = 2 * Z / frameN;                    // medio lóbulo principal de la ventana
                    const int span = juce::jmax (2 * lobe, (int) (0.5 * f0 / binHz));
                    for (int k = arg - span; k <= arg + span; ++k) if (k > 0 && k < Z / 2 && std::abs (k - arg) > lobe) around.push_back (buf[(size_t) k]);
                    if (around.empty()) continue;
                    std::nth_element (around.begin(), around.begin() + (long) around.size() / 2, around.end());
                    if (buf[(size_t) arg] < 4.0f * around[around.size() / 2]) continue;
                }
                // amplitud y fase complejas por proyección
                const double om = 2.0 * juce::MathConstants<double>::pi * fk / sr;
                double re = 0, im = 0;
                for (int i = 0; i < frameN; ++i)
                {
                    const int k = c + i; if (k < 0 || k >= (int) x.size()) continue;
                    re += w[(size_t) i] * x[(size_t) k] * std::cos (om * i);
                    im -= w[(size_t) i] * x[(size_t) k] * std::sin (om * i);
                }
                const double A = 2.0 * std::hypot (re, im) / wsumA, ph = std::atan2 (im, re);
                for (int i = 0; i < frameN; ++i)
                {
                    const int k = c + i; if (k < 0 || k >= (int) x.size()) continue;
                    y[(size_t) k] += w[(size_t) i] * A * std::cos (om * i + ph);
                }
            }
        }
        for (int i = 0; i < frameN; ++i) { const int k = c + i; if (k >= 0 && k < (int) x.size()) norm[(size_t) k] += w[(size_t) i]; }
    }
    for (size_t i = 0; i < y.size(); ++i) y[i] = norm[i] > 1e-9 ? y[i] / norm[i] : 0.0;
    return y;
}

inline double sdrDb (const Vec& truth, const Vec& est, int a, int b)
{
    double s = 0, e = 0;
    for (int i = a; i < b && i < (int) truth.size() && i < (int) est.size(); ++i) { s += truth[(size_t) i] * truth[(size_t) i]; const double d = truth[(size_t) i] - est[(size_t) i]; e += d * d; }
    if (s < 1e-14) return kNaN;
    return db (s / juce::jmax (1e-20, e));
}
inline double energyRatioDb (const Vec& num, const Vec& den, int a, int b)
{
    double n = 0, d = 0;
    for (int i = a; i < b && i < (int) num.size() && i < (int) den.size(); ++i) { n += num[(size_t) i] * num[(size_t) i]; d += den[(size_t) i] * den[(size_t) i]; }
    return db (n / juce::jmax (1e-20, d));
}

// ---- Detectores de la duración del transitorio ----
// D1: el flujo espectral vuelve al nivel del cuerpo (2× su mediana)
inline double detectByFlux (const Vec& x, double sr)
{
    const auto m = melSpectrogram (x, sr, 40, 8, 0.001);   // FFT 256, salto 1 ms
    std::vector<double> flux (m.db.size(), 0.0);
    const double floorDb = m.maxDb - 60;
    for (size_t f = 1; f < m.db.size(); ++f)
        for (int b = 0; b < m.bands; ++b) flux[f] += juce::jmax (0.0, juce::jmax (m.db[f][(size_t) b], floorDb) - juce::jmax (m.db[f - 1][(size_t) b], floorDb));
    const auto sm = smoothCurve (flux, 2);
    int on = 0; double mx = 0; for (int f = 0; f < juce::jmin (100, (int) sm.size()); ++f) if (sm[(size_t) f] > mx) { mx = sm[(size_t) f]; on = f; }
    std::vector<double> body;
    for (int f = 600; f < juce::jmin (1000, (int) sm.size()); ++f) body.push_back (sm[(size_t) f]);
    const double thr = 2.0 * median (body);
    for (int f = on; f < (int) sm.size(); ++f)
    {
        bool quiet = true;
        for (int g = f; g < juce::jmin ((int) sm.size(), f + 10); ++g) if (sm[(size_t) g] > thr) quiet = false;
        if (quiet) return (double) (f - on);
    }
    return kNaN;
}
// D2: el residuo (método C) cae 20 dB por debajo del original y se mantiene 20 ms
inline double detectByResidual (const Vec& x, const Vec& residual, double sr, double thresholdDb = -20.0)
{
    const double k = std::pow (10.0, thresholdDb / 20.0);
    const auto ex = rmsEnvelope (x, sr, 0.002, 0.001), er = rmsEnvelope (residual, sr, 0.002, 0.001);
    for (int f = 0; f < (int) ex.size(); ++f)
    {
        bool quiet = true;
        for (int g = f; g < juce::jmin ((int) ex.size(), f + 20); ++g) if (er[(size_t) g] > ex[(size_t) g] * k) quiet = false;
        if (quiet) return (double) f;
    }
    return kNaN;
}
} // namespace Bench
