#pragma once
// ===========================================================================
// Banco de pruebas de Nebula — Métricas objetivas
//
// Ninguna métrica es un porcentaje. Cada una tiene unidad física (LU, dB,
// cents, ms o una correlación de -1 a 1) y se valida con controles: cambios
// que no deberían oírse (la métrica no debe moverse) y cambios que sí se oyen
// (la métrica debe moverse claramente). Ver BenchTest.cpp, sección "validación".
//
// Definiciones (todas a 48 kHz):
//  - Sonoridad: ITU-R BS.1770-4 (ponderación K, bloques de 400 ms, compuertas
//    absoluta -70 LUFS y relativa -10 LU). Curva corta: ventanas de 50 ms cada 10 ms.
//  - Espectrograma log-mel: FFT 2048 Hann, salto 10 ms, 64 bandas de 40 Hz a
//    16 kHz, potencia en dB. Se compara tras igualar la sonoridad integrada y
//    alinear el ataque. Celdas consideradas: las que superan (máximo - 60 dB).
//  - Zonas: ataque = primeros 100 ms desde el inicio; cola = desde la última vez
//    que la curva de sonoridad está a menos de 12 dB del máximo; cuerpo = entre ambas.
//  - Tono: YIN (umbral 0.15), ventana 2048, salto 10 ms, 50-1500 Hz.
//  - Evolución: flujo espectral (suma de subidas por banda, dB) y centroide mel
//    por cuadro; se correlacionan las trayectorias de referencia y prueba.
//  - Ataque: energía de los primeros 10 ms respecto a los primeros 100 ms (dB) y,
//    como dato secundario, tiempo de 10 % a 50 % del máximo de la envolvente de 2 ms.
//  - Color: distancia log-mel (32 bandas) con cada banda promediada ±250 ms (ignora batidos y
//    el grano del ruido, que cambian con la fase/semilla sin cambiar el sonido).
//  - Estéreo: correlación L/R por banda (graves <300 Hz, medios, agudos >3 kHz).
//  - Clic: pico de la segunda derivada alrededor del evento contra su RMS en
//    los alrededores (dB). Ruido de fondo de un pad sin eventos: ~10-14 dB.
// ===========================================================================
#include <limits>
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace Bench
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline std::vector<double> toMono (const juce::AudioBuffer<float>& b)
{
    std::vector<double> m ((size_t) b.getNumSamples(), 0.0);
    const int nc = b.getNumChannels();
    for (int ch = 0; ch < nc; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] += b.getSample (ch, i) / (double) nc;
    return m;
}

inline double median (std::vector<double> v)
{
    if (v.empty()) return kNaN;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}
inline double percentile (std::vector<double> v, double p)
{
    if (v.empty()) return kNaN;
    std::sort (v.begin(), v.end());
    return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (double) (v.size() - 1)))];
}
inline double pearson (const std::vector<double>& a, const std::vector<double>& b)
{
    const size_t n = juce::jmin (a.size(), b.size());
    if (n < 3) return kNaN;
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= (double) n; mb /= (double) n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    if (saa <= 1e-20 || sbb <= 1e-20) return kNaN;
    return sab / std::sqrt (saa * sbb);
}
inline double db (double p) { return 10.0 * std::log10 (juce::jmax (1e-20, p)); }

// ---------------- Sonoridad BS.1770 ----------------
struct KFilter
{
    // Coeficientes oficiales a 48 kHz
    double s[4] {};
    double t[4] {};
    double process (double x)
    {
        const double b0 = 1.53512485958697, b1 = -2.69169618940638, b2 = 1.19839281085285, a1 = -1.69065929318241, a2 = 0.73248077421585;
        const double y = b0 * x + b1 * s[0] + b2 * s[1] - a1 * s[2] - a2 * s[3];
        s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y;
        const double c0 = 1.0, c1 = -2.0, c2 = 1.0, d1 = -1.99004745483398, d2 = 0.99007225036621;
        const double z = c0 * y + c1 * t[0] + c2 * t[1] - d1 * t[2] - d2 * t[3];
        t[1] = t[0]; t[0] = y; t[3] = t[2]; t[2] = z;
        return z;
    }
};

// Potencia ponderada K (suma de canales) por muestra
inline std::vector<double> kPower (const juce::AudioBuffer<float>& b)
{
    std::vector<double> p ((size_t) b.getNumSamples(), 0.0);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        KFilter k;
        for (int i = 0; i < b.getNumSamples(); ++i) { const double z = k.process (b.getSample (ch, i)); p[(size_t) i] += z * z; }
    }
    // Un sonido mono se cuenta como si sonara igual en ambos canales (así mono y estéreo se comparan bien)
    if (b.getNumChannels() == 1) for (auto& v : p) v *= 2.0;
    return p;
}

inline double integratedLufs (const juce::AudioBuffer<float>& b, double sr)
{
    const auto p = kPower (b);
    const int blk = (int) (0.4 * sr), hop = (int) (0.1 * sr);
    std::vector<double> z;
    for (int s = 0; s + blk <= (int) p.size(); s += hop)
    {
        double acc = 0; for (int i = s; i < s + blk; ++i) acc += p[(size_t) i];
        z.push_back (acc / blk);
    }
    if (z.empty())   // sonido más corto que un bloque: usa todo
    {
        double acc = 0; for (auto v : p) acc += v;
        z.push_back (acc / (double) std::max<size_t> (1, p.size()));
    }
    auto lufs = [] (double ms) { return -0.691 + db (ms); };
    double sum = 0; int cnt = 0;
    for (auto v : z) if (lufs (v) > -70.0) { sum += v; ++cnt; }
    if (cnt == 0) return -120.0;
    const double rel = lufs (sum / cnt) - 10.0;
    sum = 0; cnt = 0;
    for (auto v : z) if (lufs (v) > -70.0 && lufs (v) > rel) { sum += v; ++cnt; }
    return cnt > 0 ? lufs (sum / cnt) : -120.0;
}

// Curva de sonoridad corta (dB), ventanas de 50 ms cada 10 ms (centrada en el cuadro)
inline std::vector<double> loudnessCurve (const juce::AudioBuffer<float>& b, double sr)
{
    const auto p = kPower (b);
    const int hop = (int) (0.01 * sr), win = (int) (0.05 * sr);
    std::vector<double> out;
    for (int c = 0; c < (int) p.size(); c += hop)
    {
        double acc = 0; int n = 0;
        for (int i = c - win / 2; i < c + win / 2; ++i) if (i >= 0 && i < (int) p.size()) { acc += p[(size_t) i]; ++n; }
        out.push_back (-0.691 + db (acc / juce::jmax (1, n)));
    }
    return out;
}

// ---------------- Espectrograma log-mel ----------------
struct MelSpec
{
    int bands = 64;
    std::vector<std::vector<double>> db;   // [cuadro][banda]
    std::vector<double> centroid;          // banda media ponderada por potencia
    double maxDb = -200;
};

inline double hzToMel (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); }
inline double melToHz (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); }

inline MelSpec melSpectrogram (const std::vector<double>& x, double sr, int bands = 64, int fftOrder = 11, double hopS = 0.01)
{
    const int N = 1 << fftOrder, hop = (int) (hopS * sr);
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
    // filtros triangulares
    const double m0 = hzToMel (40.0), m1 = hzToMel (16000.0);
    std::vector<double> edges ((size_t) bands + 2);
    for (int b = 0; b < bands + 2; ++b) edges[(size_t) b] = melToHz (m0 + (m1 - m0) * b / (bands + 1)) * N / sr;
    MelSpec ms; ms.bands = bands;
    for (int c = 0; c < (int) x.size(); c += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) { const int k = c - N / 2 + i; if (k >= 0 && k < (int) x.size()) buf[(size_t) i] = (float) x[(size_t) k] * win[(size_t) i]; }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        std::vector<double> frame ((size_t) bands, 0.0);
        double cw = 0, cs = 0;
        for (int b = 0; b < bands; ++b)
        {
            const double lo = edges[(size_t) b], mid = edges[(size_t) b + 1], hi = edges[(size_t) b + 2];
            double acc = 0, wsum = 0;
            for (int k = (int) std::floor (lo); k <= (int) std::ceil (hi) && k <= N / 2; ++k)
            {
                if (k < 1) continue;
                const double w = k < mid ? (k - lo) / juce::jmax (1e-9, mid - lo) : (hi - k) / juce::jmax (1e-9, hi - mid);
                if (w <= 0) continue;
                const double mag = buf[(size_t) k];
                acc += w * mag * mag; wsum += w;
            }
            const double pw = wsum > 0 ? acc / wsum : 0.0;
            frame[(size_t) b] = db (pw / ((double) N * N) + 1e-14);
            cw += pw * b; cs += pw;
            ms.maxDb = juce::jmax (ms.maxDb, frame[(size_t) b]);
        }
        ms.db.push_back (std::move (frame));
        ms.centroid.push_back (cs > 0 ? cw / cs : 0.0);
    }
    return ms;
}

// ---------------- Tono (YIN) ----------------
inline std::vector<double> yinTrack (const std::vector<double>& x, double sr, double hopS = 0.01, double fmin = 50.0)
{
    // Para bajos (808) se usa una ventana más larga, que necesita al menos 2 periodos de la nota más grave
    const bool low = fmin < 50.0;
    const int W = low ? 4096 : 2048, hop = (int) (hopS * sr);
    const int tauMin = (int) (sr / 1500.0), tauMax = juce::jmin (W - 2, (int) (sr / fmin));
    std::vector<double> out, d ((size_t) tauMax + 2);
    // Diferencia de YIN calculada con FFT: d(τ) = Ea + Eb(τ) - 2·cruce(τ)
    const int order = low ? 14 : 13, N = 1 << order;   // N >= W + (W + tauMax)
    juce::dsp::FFT fft (order);
    std::vector<float> fa ((size_t) N * 2), fb ((size_t) N * 2);
    std::vector<double> pre ((size_t) (W + tauMax + 1));
    for (int c = 0; c < (int) x.size(); c += hop)
    {
        const int s0 = c - W / 2;
        if (s0 < 0 || s0 + W + tauMax >= (int) x.size()) { out.push_back (0.0); continue; }
        double energy = 0; for (int i = 0; i < W; ++i) energy += x[(size_t) (s0 + i)] * x[(size_t) (s0 + i)];
        if (energy < 1e-8) { out.push_back (0.0); continue; }
        std::fill (fa.begin(), fa.end(), 0.0f); std::fill (fb.begin(), fb.end(), 0.0f);
        for (int i = 0; i < W; ++i) fa[(size_t) i] = (float) x[(size_t) (s0 + i)];
        for (int i = 0; i < W + tauMax; ++i) fb[(size_t) i] = (float) x[(size_t) (s0 + i)];
        pre[0] = 0; for (int i = 0; i < W + tauMax; ++i) pre[(size_t) i + 1] = pre[(size_t) i] + (double) fb[(size_t) i] * fb[(size_t) i];
        fft.performRealOnlyForwardTransform (fa.data());
        fft.performRealOnlyForwardTransform (fb.data());
        for (int k = 0; k <= N / 2; ++k)   // cruce = IFFT(conj(A)·B)
        {
            const float ar = fa[(size_t) (2 * k)], ai = fa[(size_t) (2 * k + 1)], br = fb[(size_t) (2 * k)], bi = fb[(size_t) (2 * k + 1)];
            fb[(size_t) (2 * k)] = ar * br + ai * bi;
            fb[(size_t) (2 * k + 1)] = ar * bi - ai * br;
        }
        fft.performRealOnlyInverseTransform (fb.data());
        d[0] = 1.0;
        double run = 0.0; int found = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const double eb = pre[(size_t) (tau + W)] - pre[(size_t) tau];
            const double acc = juce::jmax (0.0, energy + eb - 2.0 * (double) fb[(size_t) tau]);
            run += acc;
            d[(size_t) tau] = acc * tau / juce::jmax (1e-20, run);
        }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < 0.15) { while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau; found = tau; break; }
        if (found < 0) { out.push_back (0.0); continue; }
        const double a = d[(size_t) found - 1], b = d[(size_t) found], cc = d[(size_t) found + 1];
        const double den = a - 2 * b + cc;
        const double tf = found + (std::abs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0);
        out.push_back (sr / tf);
    }
    return out;
}

// ---------------- Envolvente fina y alineación ----------------
inline std::vector<double> rmsEnvelope (const std::vector<double>& x, double sr, double winS = 0.002, double hopS = 0.001)
{
    const int hop = juce::jmax (1, (int) (hopS * sr)), win = juce::jmax (1, (int) (winS * sr));
    std::vector<double> e;
    for (int c = 0; c < (int) x.size(); c += hop)
    {
        double acc = 0; int n = 0;
        for (int i = c - win / 2; i < c + win / 2; ++i) if (i >= 0 && i < (int) x.size()) { acc += x[(size_t) i] * x[(size_t) i]; ++n; }
        e.push_back (std::sqrt (acc / juce::jmax (1, n)));
    }
    return e;
}

// Desfase (muestras) que hay que aplicar a "test" para alinearlo con "ref" (envolvente, ±maxS)
inline int alignLag (const std::vector<double>& ref, const std::vector<double>& test, double sr, double maxS = 0.05)
{
    // Se correlaciona la "fuerza de ataque" (subidas de la envolvente en dB), no la envolvente:
    // así un sonido estable no tiene un desfase ambiguo. Empates: gana el desfase más pequeño.
    auto onsetStrength = [sr] (const std::vector<double>& x)
    {
        const auto e = rmsEnvelope (x, sr, 0.002, 0.0005);
        double mx = 1e-9; for (auto v : e) mx = juce::jmax (mx, v);
        std::vector<double> o (e.size(), 0.0);
        double prev = -120.0;
        for (size_t i = 0; i < e.size(); ++i)
        {
            const double d = 20.0 * std::log10 (juce::jmax (e[i], mx * 1e-4));
            o[i] = i == 0 ? 0.0 : juce::jmax (0.0, d - prev);
            prev = d;
        }
        if (! o.empty()) o[0] = juce::jmax (0.0, 20.0 * std::log10 (juce::jmax (e[0], mx * 1e-4) / (mx * 1e-4)));   // arranque desde silencio
        return o;
    };
    const auto er = onsetStrength (ref), et = onsetStrength (test);
    const int span = juce::jmin ((int) er.size(), (int) (0.4 / 0.0005));
    const int maxL = (int) (maxS / 0.0005);
    double best = 0.0; int bestL = 0;
    for (int a = 0; a <= 2 * maxL; ++a)
    {
        const int L = (a & 1) ? (a + 1) / 2 : -(a / 2);   // 0, 1, -1, 2, -2 ...
        double acc = 0;
        for (int i = 0; i < span; ++i) { const int j = i + L; if (j >= 0 && j < (int) et.size()) acc += er[(size_t) i] * et[(size_t) j]; }
        if (acc > best * (1.0 + 1e-6) + 1e-12) { best = acc; bestL = L; }
    }
    return (int) std::round (bestL * 0.0005 * sr);   // test[i + lag] corresponde a ref[i]
}

inline juce::AudioBuffer<float> shifted (const juce::AudioBuffer<float>& b, int lag, int length)
{
    juce::AudioBuffer<float> o (b.getNumChannels(), length);
    o.clear();
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < length; ++i) { const int j = i + lag; if (j >= 0 && j < b.getNumSamples()) o.setSample (ch, i, b.getSample (ch, j)); }
    return o;
}

inline double attackTimeMs (const std::vector<double>& x, double sr)
{
    const auto e = rmsEnvelope (x, sr, 0.002, 0.0005);
    if (e.empty()) return kNaN;
    // máximo dentro del primer segundo (el ataque, no un crecimiento posterior)
    const int lim = juce::jmin ((int) e.size(), (int) (1.0 / 0.0005));
    double mx = 0; int at = 0;
    for (int i = 0; i < lim; ++i) if (e[(size_t) i] > mx) { mx = e[(size_t) i]; at = i; }
    if (mx <= 0) return kNaN;
    int i10 = -1, i90 = -1;
    // 10 % -> 50 %: la parte inicial del ataque (la cima puede moverse por batidos entre parciales)
    for (int i = 0; i <= at; ++i) { if (i10 < 0 && e[(size_t) i] >= 0.1 * mx) i10 = i; if (i90 < 0 && e[(size_t) i] >= 0.5 * mx) { i90 = i; break; } }
    if (i10 < 0 || i90 < 0) return kNaN;
    return (i90 - i10) * 0.5;
}

// ---------------- Estéreo por banda ----------------
struct StereoBands { double corr[3] { kNaN, kNaN, kNaN }; double sideMidDb = kNaN; };
inline StereoBands stereoBands (const juce::AudioBuffer<float>& b, double sr)
{
    StereoBands s;
    if (b.getNumChannels() < 2) return s;
    const int order = 11, N = 1 << order, hop = N / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> L ((size_t) N * 2), R ((size_t) N * 2);
    double lr[3] {}, ll[3] {}, rr[3] {}, mid = 0, side = 0;
    for (int c = 0; c + N <= b.getNumSamples(); c += hop)
    {
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const float w = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
            L[(size_t) i] = b.getSample (0, c + i) * w; R[(size_t) i] = b.getSample (1, c + i) * w;
            const double m = 0.5 * (b.getSample (0, c + i) + b.getSample (1, c + i)), sd = 0.5 * (b.getSample (0, c + i) - b.getSample (1, c + i));
            mid += m * m; side += sd * sd;
        }
        fft.performRealOnlyForwardTransform (L.data(), true);
        fft.performRealOnlyForwardTransform (R.data(), true);
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * sr / N;
            const int band = f < 300 ? 0 : f < 3000 ? 1 : 2;
            const double lre = L[(size_t) (2 * k)], lim = L[(size_t) (2 * k + 1)], rre = R[(size_t) (2 * k)], rim = R[(size_t) (2 * k + 1)];
            lr[band] += lre * rre + lim * rim;
            ll[band] += lre * lre + lim * lim;
            rr[band] += rre * rre + rim * rim;
        }
    }
    for (int k = 0; k < 3; ++k) if (ll[k] > 1e-12 && rr[k] > 1e-12) s.corr[k] = lr[k] / std::sqrt (ll[k] * rr[k]);
    if (mid > 1e-12) s.sideMidDb = db (side / mid);
    return s;
}

// ---------------- Clics ----------------
inline double clickScoreDb (const juce::AudioBuffer<float>& b, int at, double sr)
{
    const auto x = toMono (b);
    auto d2 = [&] (int i) { return (i >= 2 && i < (int) x.size()) ? x[(size_t) i] - 2 * x[(size_t) i - 1] + x[(size_t) i - 2] : 0.0; };
    const int pre = (int) (0.002 * sr), post = (int) (0.010 * sr);
    double pk = 0;
    for (int i = at - pre; i <= at + post; ++i) pk = juce::jmax (pk, std::abs (d2 (i)));
    double acc = 0; int n = 0;
    for (int i = at - (int) (0.06 * sr); i < at - (int) (0.01 * sr); ++i) if (i > 2) { acc += d2 (i) * d2 (i); ++n; }
    for (int i = at + (int) (0.02 * sr); i < at + (int) (0.07 * sr); ++i) if (i < (int) x.size()) { acc += d2 (i) * d2 (i); ++n; }
    const double rms = std::sqrt (acc / juce::jmax (1, n));
    return 20.0 * std::log10 (juce::jmax (1e-12, pk) / juce::jmax (1e-12, rms));
}

// Fracción de la energía (dB) entre lo y hi Hz, con un espectro promedio de todo el sonido
inline double bandFractionDb (const std::vector<double>& x, double sr, double lo, double hi)
{
    constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2);
    double inBand = 0, total = 0;
    // ventanas que empiezan media ventana antes del inicio: el ataque (click) cuenta con peso completo
    for (int c = -N / 2; c < (int) x.size(); c += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) { const int k = c + i; if (k >= 0 && k < (int) x.size()) buf[(size_t) i] = (float) (x[(size_t) k] * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N))); }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int k = 1; k < N / 2; ++k)
        {
            const double p = (double) buf[(size_t) k] * buf[(size_t) k], f = k * sr / N;
            total += p; if (f >= lo && f < hi) inBand += p;
        }
    }
    return db (inBand / juce::jmax (1e-30, total));
}

// ===========================================================================
// Comparación completa referencia vs. prueba
// ===========================================================================
struct Scores
{
    double lufsRef = kNaN, lufsDiff = kNaN;        // LU (prueba - referencia), antes de igualar
    double onsetLagMs = kNaN;                      // desfase del ataque
    double melAll = kNaN, melAtk = kNaN, melBody = kNaN, melTail = kNaN;   // dB medios por celda (10 ms)
    double colorDb = kNaN;                         // dB medios con el espectro promediado ±100 ms (color estable)
    double envErrDb = kNaN;                        // dB RMS de la curva de sonoridad
    double pitchMedCents = kNaN, pitchP95Cents = kNaN, voicedAgree = kNaN;   // curva de tono, cuadro a cuadro
    double pitchCentreCents = kNaN;               // diferencia de la nota central (mediana de toda la nota)
    double fluxCorr = kNaN, fluxRatio = kNaN;      // evolución espectral (correlación, movimiento prueba/ref)
    double centroidCorr = kNaN, centroidErr = kNaN;   // trayectoria de color (correlación, bandas mel)
    double attackRefMs = kNaN, attackTestMs = kNaN;
    double earlyRefDb = kNaN, earlyTestDb = kNaN;  // energía de los primeros 10 ms / primeros 100 ms
    double stereoCorrDiff = kNaN, sideMidDiffDb = kNaN;
    // Reparto de energía (dB de la fracción, prueba - referencia): subgrave (<120 Hz) y agudos (>1 kHz).
    // En un 808, "agudos" sube cuando la recreación añade ruido o soplo que el original no tiene.
    double subFracRefDb = kNaN, subDiffDb = kNaN, hfFracRefDb = kNaN, hfDiffDb = kNaN;
};

inline std::vector<double> smoothCurve (const std::vector<double>& v, int half)
{
    std::vector<double> o (v.size());
    for (int i = 0; i < (int) v.size(); ++i)
    {
        double acc = 0; int n = 0;
        for (int k = i - half; k <= i + half; ++k) if (k >= 0 && k < (int) v.size()) { acc += v[(size_t) k]; ++n; }
        o[(size_t) i] = acc / juce::jmax (1, n);
    }
    return o;
}

// Promedia cada banda en el tiempo (en potencia), ±half cuadros
inline MelSpec smoothMel (const MelSpec& m, int half)
{
    MelSpec o = m;
    for (int f = 0; f < (int) m.db.size(); ++f)
        for (int b = 0; b < m.bands; ++b)
        {
            double acc = 0; int n = 0;
            for (int k = f - half; k <= f + half; ++k) if (k >= 0 && k < (int) m.db.size()) { acc += std::pow (10.0, m.db[(size_t) k][(size_t) b] / 10.0); ++n; }
            o.db[(size_t) f][(size_t) b] = db (acc / juce::jmax (1, n));
        }
    return o;
}

inline Scores compare (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& testIn, double sr, bool pitched, double expectedCents = 0.0, double lowestHz = 50.0)
{
    Scores s;
    const int len = ref.getNumSamples();
    const auto rm = toMono (ref);
    const int lag = alignLag (rm, toMono (testIn), sr);
    s.onsetLagMs = lag * 1000.0 / sr;
    auto test = shifted (testIn, lag, len);

    s.lufsRef = integratedLufs (ref, sr);
    const double lt = integratedLufs (test, sr);
    s.lufsDiff = lt - s.lufsRef;
    if (lt > -110.0) test.applyGain ((float) std::pow (10.0, (s.lufsRef - lt) / 20.0));
    const auto tm = toMono (test);

    // Curvas de sonoridad y zonas
    const auto cr = loudnessCurve (ref, sr), ct = loudnessCurve (test, sr);
    const double cmax = *std::max_element (cr.begin(), cr.end());
    int onset = 0; while (onset < (int) cr.size() && cr[(size_t) onset] < cmax - 40.0) ++onset;
    int tailStart = (int) cr.size() - 1; while (tailStart > 0 && cr[(size_t) tailStart] < cmax - 12.0) --tailStart;
    const int atkEnd = juce::jmin ((int) cr.size(), onset + 10);
    {
        double acc = 0; int n = 0;
        for (size_t i = 0; i < cr.size() && i < ct.size(); ++i)
            if (cr[i] > cmax - 40.0) { const double d = cr[i] - juce::jmax (ct[i], cmax - 60.0); acc += d * d; ++n; }
        s.envErrDb = n > 0 ? std::sqrt (acc / n) : kNaN;
    }

    // Espectrogramas mel
    const auto mr = melSpectrogram (rm, sr), mt = melSpectrogram (tm, sr);
    const double floorDb = mr.maxDb - 60.0;
    double za[4] {}; int zn[4] {};
    const size_t F = juce::jmin (mr.db.size(), mt.db.size());
    for (size_t f = 0; f < F; ++f)
    {
        const int zone = (int) f < onset ? -1 : (int) f < atkEnd ? 1 : (int) f < tailStart ? 2 : 3;
        if (zone < 0) continue;
        for (int b = 0; b < mr.bands; ++b)
        {
            const double a = juce::jmax (mr.db[f][(size_t) b], floorDb), c = juce::jmax (mt.db[f][(size_t) b], floorDb);
            if (a <= floorDb && c <= floorDb) continue;
            const double d = std::abs (a - c);
            za[0] += d; ++zn[0]; za[zone] += d; ++zn[zone];
        }
    }
    auto avg = [&] (int z) { return zn[z] > 0 ? za[z] / zn[z] : kNaN; };
    s.melAll = avg (0); s.melAtk = avg (1); s.melBody = avg (2); s.melTail = avg (3);
    {
        // Color estable: 32 bandas (cada banda junta más bins = menos varianza del ruido)
        // y cada banda promediada ±250 ms en potencia (los batidos y el grano del ruido se promedian)
        const auto r32 = melSpectrogram (rm, sr, 32), t32 = melSpectrogram (tm, sr, 32);
        const auto sr2 = smoothMel (r32, 25), st2 = smoothMel (t32, 25);
        const double fl = r32.maxDb - 60.0;
        double acc = 0; int n = 0;
        const size_t F2 = juce::jmin (sr2.db.size(), st2.db.size());
        for (size_t f = (size_t) onset; f < F2; ++f)
            for (int b = 0; b < 32; ++b)
            {
                const double a = juce::jmax (sr2.db[f][(size_t) b], fl), c = juce::jmax (st2.db[f][(size_t) b], fl);
                if (a <= fl && c <= fl) continue;
                acc += std::abs (a - c); ++n;
            }
        s.colorDb = n > 0 ? acc / n : kNaN;
    }
    // Energía temprana: dB de los primeros 10 ms respecto a los primeros 100 ms (tras el inicio)
    {
        auto early = [&] (const std::vector<double>& x)
        {
            const int o = onset * (int) (0.01 * sr);
            double e10 = 0, e100 = 0;
            for (int i = o; i < o + (int) (0.1 * sr) && i < (int) x.size(); ++i) { e100 += x[(size_t) i] * x[(size_t) i]; if (i < o + (int) (0.01 * sr)) e10 += x[(size_t) i] * x[(size_t) i]; }
            return db (e10 / juce::jmax (1e-20, e100));
        };
        s.earlyRefDb = early (rm); s.earlyTestDb = early (tm);
    }

    // Evolución: flujo y centroide en la zona audible
    {
        std::vector<double> fr, ft, cr2, ct2;
        for (size_t f = (size_t) juce::jmax (1, onset); f < F; ++f)
        {
            if (cr.size() > f && cr[f] < cmax - 30.0) continue;
            double a = 0, c = 0;
            for (int b = 0; b < mr.bands; ++b)
            {
                a += juce::jmax (0.0, juce::jmax (mr.db[f][(size_t) b], floorDb) - juce::jmax (mr.db[f - 1][(size_t) b], floorDb));
                c += juce::jmax (0.0, juce::jmax (mt.db[f][(size_t) b], floorDb) - juce::jmax (mt.db[f - 1][(size_t) b], floorDb));
            }
            fr.push_back (a); ft.push_back (c);
            cr2.push_back (mr.centroid[f]); ct2.push_back (mt.centroid[f]);
        }
        s.fluxCorr = pearson (smoothCurve (fr, 5), smoothCurve (ft, 5));
        const double sa = std::accumulate (fr.begin(), fr.end(), 0.0), sc = std::accumulate (ft.begin(), ft.end(), 0.0);
        s.fluxRatio = sa > 1e-9 ? sc / sa : kNaN;
        s.centroidCorr = pearson (cr2, ct2);
        double e = 0; for (size_t i = 0; i < cr2.size(); ++i) e += std::abs (cr2[i] - ct2[i]);
        s.centroidErr = cr2.empty() ? kNaN : e / (double) cr2.size();
    }

    // Tono
    if (pitched)
    {
        const auto pr = yinTrack (rm, sr, 0.01, lowestHz), pt = yinTrack (tm, sr, 0.01, lowestHz);
        std::vector<double> diffs; int voiced = 0, agree = 0;
        for (size_t f = 0; f < pr.size() && f < pt.size(); ++f)
        {
            if (f >= cr.size() || cr[f] < cmax - 30.0 || pr[f] <= 0) continue;
            ++voiced;
            if (pt[f] <= 0) continue;
            ++agree;
            double c = 1200.0 * std::log2 (pt[f] / pr[f]) - expectedCents;
            // errores de octava del detector no son error de afinación: se cuentan aparte
            if (std::abs (c) > 600.0) { --agree; continue; }
            diffs.push_back (std::abs (c));
        }
        {
            std::vector<double> a, b;
            for (size_t f = 0; f < pr.size() && f < pt.size(); ++f)
                if (f < cr.size() && cr[f] >= cmax - 30.0 && pr[f] > 0 && pt[f] > 0) { a.push_back (std::log2 (pr[f])); b.push_back (std::log2 (pt[f])); }
            if (! a.empty()) s.pitchCentreCents = 1200.0 * (median (b) - median (a)) - expectedCents;
        }
        s.pitchMedCents = median (diffs);
        s.pitchP95Cents = percentile (diffs, 0.95);
        s.voicedAgree = voiced > 0 ? (double) agree / voiced : kNaN;
    }

    // Por debajo de -60 dB de la energía total no se oye: ambas fracciones se limitan a -60 dB
    s.subFracRefDb = bandFractionDb (rm, sr, 0.0, 120.0);
    s.subDiffDb = juce::jmax (-60.0, bandFractionDb (tm, sr, 0.0, 120.0)) - juce::jmax (-60.0, s.subFracRefDb);
    s.hfFracRefDb = bandFractionDb (rm, sr, 1000.0, sr * 0.5);
    s.hfDiffDb = juce::jmax (-60.0, bandFractionDb (tm, sr, 1000.0, sr * 0.5)) - juce::jmax (-60.0, s.hfFracRefDb);

    s.attackRefMs = attackTimeMs (rm, sr);
    s.attackTestMs = attackTimeMs (tm, sr);

    if (ref.getNumChannels() >= 2)
    {
        const auto a = stereoBands (ref, sr), b = stereoBands (test, sr);
        double d = 0; int n = 0;
        for (int k = 0; k < 3; ++k) if (! std::isnan (a.corr[k]) && ! std::isnan (b.corr[k])) { d += std::abs (a.corr[k] - b.corr[k]); ++n; }
        s.stereoCorrDiff = n > 0 ? d / n : kNaN;
        s.sideMidDiffDb = b.sideMidDb - a.sideMidDb;
    }
    return s;
}
} // namespace Bench
