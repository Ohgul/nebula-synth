#pragma once
#include <juce_dsp/juce_dsp.h>
#include "SampleData.h"

// ===========================================================================
// "Limpiar": separa partes de un audio que es un conjunto de sonidos.
// Todo se hace sobre el espectro (STFT); no hay modelos entrenados.
// ===========================================================================
namespace Cleaner
{
    enum Kind { MainNote, Tonal, Hits, Denoise, Lows, Mids, Highs, count };

    inline const juce::StringArray& names()
    {
        static const juce::StringArray n { "Nota principal", "Parte tonal", "Solo golpes", "Sin ruido", "Graves", "Medios", "Agudos" };
        return n;
    }
    inline const juce::StringArray& tips()
    {
        static const juce::StringArray n {
            "Sigue la nota mas fuerte y se queda solo con ella y sus armonicos (quita acompanamiento y fondo)",
            "Lo que tiene tono y se sostiene: quita golpes, clics y transientes",
            "Solo los golpes y transientes: quita lo que se sostiene",
            "Quita el ruido de fondo constante (soplido, hum, ambiente)",
            "Solo la parte grave (menos de 200 Hz): bombo, bajo",
            "Solo los medios (200 Hz a 4 kHz): voz, cuerpo de instrumentos",
            "Solo los agudos (mas de 4 kHz): platillos, aire, brillo" };
        return n;
    }

    class Job : private juce::Thread
    {
    public:
        Job() : juce::Thread ("Nebula limpiar") {}
        ~Job() override { stopThread (5000); }

        void start (std::shared_ptr<const SampleData> src)
        {
            stopThread (5000);
            source = std::move (src);
            for (auto& r : results) std::atomic_store (&r, std::shared_ptr<const SampleData>());
            progress = 0.0f;
            startThread (juce::Thread::Priority::low);
        }
        void cancel()           { stopThread (5000); }
        bool isRunning() const  { return isThreadRunning(); }
        std::shared_ptr<const SampleData> result (int k) const { return std::atomic_load (&results[(size_t) k]); }
        std::shared_ptr<const SampleData> input() const { return source; }
        std::atomic<float> progress { 0.0f };

        // Versión síncrona (para pruebas): procesa una opción
        static std::shared_ptr<const SampleData> processOne (const SampleData& s, int kind)
        {
            Job j;
            j.source = std::shared_ptr<const SampleData> (&s, [] (const SampleData*) {});
            j.analyse();
            return j.render (kind);
        }

    private:
        static constexpr int order = 12, N = 1 << order, hop = N / 4, bins = N / 2 + 1;

        void run() override
        {
            analyse();
            const int order_[] { Tonal, Hits, Denoise, MainNote, Lows, Mids, Highs };
            int done = 0;
            for (int k : order_)
            {
                if (threadShouldExit()) return;
                auto r = render (k);
                std::atomic_store (&results[(size_t) k], r);
                progress = 0.3f + 0.7f * (float) ++done / (float) count;
            }
            progress = 1.0f;
        }

        // ---- Paso 1: espectrograma de magnitudes + nota principal por cuadro ----
        void analyse()
        {
            const int n = source->length();
            frames = n / hop + 4;
            mags.assign ((size_t) frames * bins, 0.0f);
            f0.assign ((size_t) frames, 0.0f);
            window.resize ((size_t) N);
            for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);

            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) N * 2);
            const float* x = source->data();
            for (int f = 0; f < frames; ++f)
            {
                if (threadShouldExit()) return;
                frameSpectrum (fft, buf, x, n, f);
                float* m = &mags[(size_t) f * bins];
                for (int b = 0; b < bins; ++b) m[b] = std::hypot (buf[(size_t) (2 * b)], buf[(size_t) (2 * b + 1)]);
                f0[(size_t) f] = estimateF0 (m);
                // Si en este cuadro no se encontró la nota (un golpe la tapa), se sostiene la anterior hasta ~150 ms
                if (f0[(size_t) f] <= 0.0f && f > 0 && f0[(size_t) f - 1] > 0.0f && holdFrames < (int) (0.15 * source->sampleRate / hop))
                { f0[(size_t) f] = f0[(size_t) f - 1]; ++holdFrames; }
                else if (f0[(size_t) f] > 0.0f) holdFrames = 0;
                if ((f & 63) == 0) progress = 0.3f * (float) f / (float) frames;
            }
        }

        void frameSpectrum (juce::dsp::FFT& fft, std::vector<float>& buf, const float* x, int n, int f) const
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const int start = f * hop - N / 2;
            for (int i = 0; i < N; ++i)
            {
                const int k = start + i;
                if (k >= 0 && k < n) buf[(size_t) i] = x[k] * window[(size_t) i];
            }
            fft.performRealOnlyForwardTransform (buf.data(), true);
        }

        // Nota principal por suma de armónicos sobre el espectro (rápido)
        float estimateF0 (const float* m) const
        {
            const double sr = source->sampleRate, binHz = sr / N;
            double energy = 1e-12;
            for (int b = 1; b < bins; ++b) energy += m[b];
            float bestHz = 0.0f; double best = 0.0;
            for (double note = 28.0; note <= 90.0; note += 0.125)
            {
                const double hz = 440.0 * std::exp2 ((note - 69.0) / 12.0);
                double s = 0.0;
                for (int k = 1; k <= 10; ++k)
                {
                    const double pos = k * hz / binHz;
                    if (pos >= bins - 2) break;
                    const int b = (int) pos;
                    const float fr = (float) (pos - b);
                    s += std::sqrt (m[b] * (1.0f - fr) + m[b + 1] * fr) * std::pow (0.86, k - 1);
                }
                // La fundamental tiene que existir (evita escoger una octava abajo)
                const double pos1 = hz / binHz;
                const int b1 = (int) pos1;
                if (b1 + 1 < bins && m[b1] + m[b1 + 1] < 4.0 * energy / bins) continue;
                if (s > best) { best = s; bestHz = (float) hz; }
            }
            // ¿Es de verdad una nota? Que los armónicos se lleven buena parte de la energía
            if (bestHz > 0.0f)
            {
                double harm = 0.0;
                for (int k = 1; k <= 20; ++k)
                {
                    const int b = juce::roundToInt (k * bestHz / binHz);
                    if (b >= bins - 1) break;
                    harm += m[b - 1] + m[b] + m[b + 1];
                }
                if (harm < 0.15 * energy) return 0.0f;
            }
            return bestHz;
        }

        // ---- Paso 2: máscara por opción y reconstrucción ----
        std::shared_ptr<const SampleData> render (int kind)
        {
            const int n = source->length();
            const double sr = source->sampleRate, binHz = sr / N;
            std::vector<float> gain ((size_t) frames * bins, 0.0f);

            switch (kind)
            {
                case Tonal: case Hits:
                {
                    // HPSS: mediana en el tiempo (lo sostenido) contra mediana en frecuencia (los golpes)
                    if (hp.empty()) computeHpss();
                    for (size_t i = 0; i < gain.size(); ++i)
                    {
                        const float h = hp[i].first, p = hp[i].second;
                        const float denom = h * h + p * p + 1e-20f;
                        gain[i] = kind == Tonal ? h * h / denom : p * p / denom;
                    }
                    break;
                }
                case Denoise:
                {
                    // Piso de ruido por banda: percentil 15 en el tiempo
                    std::vector<float> floorMag ((size_t) bins);
                    std::vector<float> col ((size_t) frames);
                    for (int b = 0; b < bins; ++b)
                    {
                        for (int f = 0; f < frames; ++f) col[(size_t) f] = mags[(size_t) f * bins + b];
                        auto nth = col.begin() + (long) (frames * 15 / 100);
                        std::nth_element (col.begin(), nth, col.end());
                        floorMag[(size_t) b] = *nth;
                    }
                    for (int b = 0; b < bins; ++b)
                    {
                        float smooth = 1.0f;
                        for (int f = 0; f < frames; ++f)
                        {
                            const float m = mags[(size_t) f * bins + b] + 1e-12f;
                            const float ratio = floorMag[(size_t) b] * 2.0f / m;
                            float g = juce::jlimit (0.03f, 1.0f, 1.0f - ratio * ratio);
                            smooth = g > smooth ? g : smooth + (g - smooth) * 0.35f;   // sube rápido, baja suave
                            gain[(size_t) f * bins + b] = smooth;
                        }
                    }
                    break;
                }
                case MainNote:
                {
                    for (int f = 0; f < frames; ++f)
                    {
                        // f0 suavizado con los vecinos (mediana de 3)
                        float a = f0[(size_t) juce::jmax (0, f - 1)], b0 = f0[(size_t) f], c = f0[(size_t) juce::jmin (frames - 1, f + 1)];
                        float hz = std::max (std::min (a, b0), std::min (std::max (a, b0), c));
                        if (hz <= 0.0f) hz = b0;
                        float* g = &gain[(size_t) f * bins];
                        for (int b = 0; b < bins; ++b) g[b] = 0.015f;
                        if (hz <= 0.0f) continue;
                        const float* m = &mags[(size_t) f * bins];
                        for (int k = 1; k * hz < juce::jmin (12000.0, sr * 0.45); ++k)
                        {
                            const double centre = k * hz / binHz;
                            // Sólo los armónicos que de verdad están (sobresalen de lo que tienen alrededor)
                            {
                                const int cb = juce::roundToInt (centre);
                                if (cb + 8 >= bins) break;
                                float here = juce::jmax (m[cb - 1], m[cb], m[cb + 1]);
                                std::array<float, 6> around { m[cb - 8], m[cb - 6], m[cb - 5], m[cb + 5], m[cb + 6], m[cb + 8] };
                                std::nth_element (around.begin(), around.begin() + 3, around.end());
                                if (k > 1 && here < 2.5f * around[3]) continue;
                            }
                            const double bw = juce::jmax (1.3, 0.012 * k * hz / binHz);
                            for (int b = (int) (centre - 3 * bw); b <= (int) (centre + 3 * bw); ++b)
                                if (b > 0 && b < bins)
                                {
                                    const double d = (b - centre) / bw;
                                    g[b] = juce::jmax (g[b], (float) std::exp (-0.5 * d * d));
                                }
                        }
                    }
                    break;
                }
                default:
                {
                    for (int b = 0; b < bins; ++b)
                    {
                        const double hz = b * binHz;
                        auto lp = [] (double f, double fc) { return 1.0 / std::sqrt (1.0 + std::pow (f / fc, 8.0)); };
                        auto hpf = [] (double f, double fc) { return 1.0 / std::sqrt (1.0 + std::pow (fc / juce::jmax (1.0, f), 8.0)); };
                        const float g = (float) (kind == Lows ? lp (hz, 200.0) : kind == Highs ? hpf (hz, 4000.0) : hpf (hz, 200.0) * lp (hz, 4000.0));
                        for (int f = 0; f < frames; ++f) gain[(size_t) f * bins + b] = g;
                    }
                    break;
                }
            }

            // Reconstrucción por suma superpuesta
            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) N * 2);
            juce::AudioBuffer<float> out (1, n);
            out.clear();
            std::vector<float> norm ((size_t) n, 0.0f);
            const float* x = source->data();
            float* y = out.getWritePointer (0);
            for (int f = 0; f < frames; ++f)
            {
                if (threadShouldExit()) return nullptr;
                frameSpectrum (fft, buf, x, n, f);
                const float* g = &gain[(size_t) f * bins];
                for (int b = 0; b < bins; ++b) { buf[(size_t) (2 * b)] *= g[b]; buf[(size_t) (2 * b + 1)] *= g[b]; }
                fft.performRealOnlyInverseTransform (buf.data());
                const int start = f * hop - N / 2;
                for (int i = 0; i < N; ++i)
                {
                    const int k = start + i;
                    if (k < 0 || k >= n) continue;
                    y[k] += buf[(size_t) i] * window[(size_t) i];
                    norm[(size_t) k] += window[(size_t) i] * window[(size_t) i];
                }
            }
            for (int i = 0; i < n; ++i) y[i] = norm[(size_t) i] > 1e-6f ? y[i] / norm[(size_t) i] : 0.0f;

            if (out.getMagnitude (0, 0, n) < 1.0e-5f) return nullptr;   // no quedó nada
            return SampleTools::fromBuffer (out, sr, source->name + " - " + names()[kind], juce::File());   // sin ruta: se guarda dentro del proyecto
        }

        void computeHpss()
        {
            constexpr int L = 17;
            hp.assign ((size_t) frames * bins, { 0.0f, 0.0f });
            std::vector<float> w (L);
            for (int f = 0; f < frames; ++f)
                for (int b = 0; b < bins; ++b)
                {
                    // en el tiempo
                    int c = 0;
                    for (int j = f - L / 2; j <= f + L / 2; ++j) w[(size_t) c++] = (j >= 0 && j < frames) ? mags[(size_t) j * bins + b] : 0.0f;
                    std::nth_element (w.begin(), w.begin() + L / 2, w.end());
                    const float h = w[L / 2];
                    // en frecuencia
                    c = 0;
                    for (int j = b - L / 2; j <= b + L / 2; ++j) w[(size_t) c++] = (j >= 0 && j < bins) ? mags[(size_t) f * bins + j] : 0.0f;
                    std::nth_element (w.begin(), w.begin() + L / 2, w.end());
                    hp[(size_t) f * bins + b] = { h, w[L / 2] };
                }
        }

        std::shared_ptr<const SampleData> source;
        std::array<std::shared_ptr<const SampleData>, count> results;
        std::vector<float> mags, f0, window;
        std::vector<std::pair<float, float>> hp;
        int frames = 0, holdFrames = 0;
    };
}
