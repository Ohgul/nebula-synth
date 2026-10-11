#pragma once
#include "SoundProfile.h"
#include "ResynthModel.h"

// ===========================================================================
// Construye el modelo de resíntesis a partir de la sección elegida del sample.
// ===========================================================================
namespace ResynthBuilder
{
    inline std::shared_ptr<ResynthModel> build (const SampleData& smp, float rs, float re, float rootOffset, SoundProfile& prof)
    {
        ResynthData rd;
        prof = SoundProfiler::analyse (smp, rs, re, rootOffset, &rd, 64, 30.0);
        if (! prof.valid || rd.times.size() < 2) return nullptr;

        const double sr = smp.sampleRate;
        const int full = smp.length();
        const int a0 = juce::jlimit (0, juce::jmax (0, full - 2), (int) (rs * (float) full));
        const int a1 = juce::jlimit (a0 + 2, full, (int) (re * (float) full));
        const float* x = smp.data() + a0;
        const int n = a1 - a0;
        const int onset = (int) (prof.onsetS * sr);
        float regionPeak = 1.0e-6f;
        for (int i = 0; i < n; ++i) regionPeak = juce::jmax (regionPeak, std::abs (x[i]));

        auto m = std::make_shared<ResynthModel>();
        m->name = smp.name;
        m->rootNote = prof.pitched ? prof.note : (smp.hasPitch ? smp.rootNote + rootOffset : 60.0f);
        m->times = rd.times;
        m->pitchCents = rd.pitchCents;
        m->sustained = prof.sustained;
        m->width = smp.width;
        const int frames = (int) rd.times.size();
        const double rootHz = 440.0 * std::exp2 ((m->rootNote - 69.0) / 12.0);

        // ---- Parte armónica: cuadros normalizados (rms 1) + curva densa de volumen ----
        const bool useHarm = prof.pitched && ! prof.inharmonic && rd.cycles.size() >= 2;
        float g = 1.0f;   // escala global: el original llega a ~1 de pico
        if (useHarm)
        {
            std::vector<float> rmsF ((size_t) frames, 0.0f);
            auto cycles = rd.cycles;
            for (size_t f = 0; f < cycles.size(); ++f)
            {
                double e = 0.0;
                for (auto v : cycles[f]) e += (double) v * v;
                const float r = (float) std::sqrt (e / (double) cycles[f].size());
                rmsF[f] = r;
                if (r > 1.0e-9f) for (auto& v : cycles[f]) v /= r;
            }
            float peakNorm = 1.0e-9f;
            for (auto& c : cycles) for (auto v : c) peakNorm = juce::jmax (peakNorm, std::abs (v));
            m->harmonic = Wavetable::fromCycles (cycles, smp.name);
            g = 1.0f / juce::jmax (1.0e-6f, regionPeak);
            m->harmonicLevel = peakNorm * g;   // la tabla queda con rms 1/peakNorm

            // Volumen de la parte armónica cada 5 ms: rms del sonido × fracción armónica del cuadro.
            // La ventana dura un número entero de periodos (>= 10 ms): con menos de un ciclo (bajos de 40 Hz),
            // el rms oscilaba con la forma de onda y modulaba el sonido (bandas laterales audibles).
            const double periodS = 1.0 / rootHz;
            const int hop = (int) (sr * m->envHop);
            const int win = (int) std::round (sr * periodS * std::ceil (0.010 / periodS));
            for (double t = 0.0; t <= rd.times.back() + 0.001; t += m->envHop)
            {
                const int c = onset + (int) (t * sr);
                double s = 0.0; int cnt = 0;
                for (int k = c - win / 2; k < c + win / 2; ++k) if (k >= 0 && k < n) { s += (double) x[k] * x[k]; ++cnt; }
                const float total = (float) std::sqrt (s / juce::jmax (1, cnt));
                // fracción armónica interpolada entre cuadros
                const float fr = [&] {
                    int f = 0;
                    while (f < frames - 1 && rd.times[(size_t) f + 1] < (float) t) ++f;
                    const int f2 = juce::jmin (frames - 1, f + 1);
                    const float span = juce::jmax (1.0e-6f, rd.times[(size_t) f2] - rd.times[(size_t) f]);
                    const float w = juce::jlimit (0.0f, 1.0f, ((float) t - rd.times[(size_t) f]) / span);
                    const float h1 = rmsF[(size_t) f], h2 = rmsF[(size_t) f2];
                    const float n1 = rd.noiseRms[(size_t) f], n2 = rd.noiseRms[(size_t) f2];
                    const float hh = h1 + (h2 - h1) * w, nn = n1 + (n2 - n1) * w;
                    return hh / std::sqrt (hh * hh + nn * nn + 1.0e-20f);
                }();
                m->harmEnv.push_back (total * fr * (float) std::sqrt (2.0) / 1.41421356f);   // rms del sonido armónico
            }
            juce::ignoreUnused (hop);
        }
        else g = 1.0f / juce::jmax (1.0e-6f, regionPeak);

        // ---- Curva densa de tono (vibratos, glides, melodías) ----
        if (prof.pitched)
        {
            const auto contour = SoundProfiler::pitchContourAuto (x + onset, n - onset, sr, regionPeak * 0.02f);
            m->pitchHop = (float) contour.hop;
            float last = 0.0f;
            for (auto nt : contour.note)
            {
                if (nt > 0.0f) last = juce::jlimit (-2400.0f, 2400.0f, (nt - m->rootNote) * 100.0f);
                m->pitchCurve.push_back (last);
            }
            // Sin melodía (una sola nota) se suaviza para no meter temblores de la detección
            if (! prof.melodic && m->pitchCurve.size() > 4)
            {
                auto c = m->pitchCurve;
                for (size_t i = 2; i + 2 < c.size(); ++i)
                    m->pitchCurve[i] = (c[i - 2] + c[i - 1] + c[i] + c[i + 1] + c[i + 2]) / 5.0f;
                // si casi no se mueve, mejor plano (la nota queda exacta)
                double var = 0.0; for (auto v : m->pitchCurve) var += v * v;
                if (std::sqrt (var / m->pitchCurve.size()) < 4.0) m->pitchCurve.assign (m->pitchCurve.size(), 0.0f);
            }
        }

        // ---- STFT por cuadro (2048) para el ruido y los parciales ----
        constexpr int order = 11, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), buf ((size_t) N * 2);
        double sumW = 0.0, sumW2 = 0.0;
        for (int i = 0; i < N; ++i)
        {
            win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            sumW += win[(size_t) i]; sumW2 += (double) win[(size_t) i] * win[(size_t) i];
        }
        const double binHz = sr / N;
        auto spectrumAt = [&] (double t, std::vector<float>& mag)
        {
            const int c = onset + (int) (t * sr);
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i) { const int k = c - N / 2 + i; if (k >= 0 && k < n) buf[(size_t) i] = x[k] * win[(size_t) i]; }
            fft.performRealOnlyForwardTransform (buf.data(), true);
            mag.resize ((size_t) N / 2 + 1);
            for (int b = 0; b <= N / 2; ++b) mag[(size_t) b] = std::hypot (buf[(size_t) (2 * b)], buf[(size_t) (2 * b + 1)]);
        };

        // ---- Parciales (sonidos inarmónicos o sin nota) ----
        std::vector<int> partialBins;
        std::vector<std::vector<int>> partialTrack;
        if (! useHarm)
        {
            std::vector<float> maxSpec ((size_t) N / 2 + 1, 0.0f), mag;
            for (int f = 0; f < frames; ++f)
            {
                spectrumAt (rd.times[(size_t) f], mag);
                for (size_t b = 0; b < mag.size(); ++b) maxSpec[b] = juce::jmax (maxSpec[b], mag[b]);
            }
            const float top = *std::max_element (maxSpec.begin(), maxSpec.end());
            std::vector<std::pair<float, int>> peaks;
            for (int b = 3; b < N / 2 - 3; ++b)
            {
                const float v = maxSpec[(size_t) b];
                if (v < top * 1.0e-3f || v < maxSpec[(size_t) b - 1] || v < maxSpec[(size_t) b + 1]) continue;
                // que sobresalga de lo que tiene alrededor (si no, es ruido)
                std::vector<float> around;
                for (int k = b - 16; k <= b + 16; ++k) if (k > 0 && k < N / 2 && std::abs (k - b) > 2) around.push_back (maxSpec[(size_t) k]);
                std::nth_element (around.begin(), around.begin() + (long) around.size() / 2, around.end());
                if (v < 4.0f * around[around.size() / 2]) continue;
                if (b * binHz < 30.0 || b * binHz > juce::jmin (18000.0, sr * 0.45)) continue;
                peaks.push_back ({ v, b });
            }
            std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            for (auto& [v, b] : peaks)
            {
                bool close = false;
                for (int pb : partialBins) if (std::abs (pb - b) < 3) close = true;
                if (close) continue;
                partialBins.push_back (b);
                if ((int) partialBins.size() >= (prof.pitched ? ResynthModel::maxPartials : 32)) break;
            }
            // Espectros de todos los cuadros (para seguir cada parcial en el tiempo)
            std::vector<std::vector<float>> specs ((size_t) frames);
            for (int f = 0; f < frames; ++f) spectrumAt (rd.times[(size_t) f], specs[(size_t) f]);
            auto fineBin = [&] (const std::vector<float>& s, int b)
            {
                const float a = s[(size_t) b - 1], c0 = s[(size_t) b], c = s[(size_t) b + 1];
                const float den = a - 2.0f * c0 + c;
                return b + (std::abs (den) > 1.0e-12f ? 0.5 * (a - c) / den : 0.0);
            };
            for (int b : partialBins)
            {
                ResynthModel::Partial p;
                p.ratio = (float) (fineBin (maxSpec, b) * binHz / rootHz);
                p.amp.assign ((size_t) frames, 0.0f);
                p.ratioCurve.assign ((size_t) frames, p.ratio);
                // Arranca en el cuadro donde el parcial es más fuerte y lo sigue hacia adelante y hacia atrás
                int best = 0; float bestV = 0.0f;
                for (int f = 0; f < frames; ++f) { const float v = specs[(size_t) f][(size_t) b]; if (v > bestV) { bestV = v; best = f; } }
                auto track = [&] (int from, int to, int stepDir)
                {
                    double bin = b;
                    for (int f = from; f != to; f += stepDir)
                    {
                        const auto& s = specs[(size_t) f];
                        const int span = juce::jmax (3, (int) (bin * 0.08));
                        int arg = juce::jlimit (2, N / 2 - 2, (int) std::round (bin));
                        for (int k = juce::jmax (2, (int) bin - span); k <= juce::jmin (N / 2 - 2, (int) bin + span); ++k)
                            if (s[(size_t) k] > s[(size_t) arg]) arg = k;
                        bin = fineBin (s, arg);
                        p.ratioCurve[(size_t) f] = (float) (bin * binHz / rootHz);
                    }
                };
                track (best, frames, 1);
                track (best - 1, -1, -1);
                m->partials.push_back (std::move (p));
            }
            // Bins de cada parcial por cuadro (para medir su volumen y quitarlo del ruido)
            partialTrack.assign (m->partials.size(), std::vector<int> ((size_t) frames, 0));
            for (size_t k = 0; k < m->partials.size(); ++k)
                for (int f = 0; f < frames; ++f)
                    partialTrack[k][(size_t) f] = juce::roundToInt (m->partials[k].ratioCurve[(size_t) f] * rootHz / binHz);
        }

        // ---- Ruido por bandas (y volumen de cada parcial) ----
        const auto& edges = ResynthModel::bandEdges();
        m->noise.assign ((size_t) frames, {});
        std::vector<float> mag;
        std::vector<char> excluded ((size_t) N / 2 + 1, 0);
        for (int f = 0; f < frames; ++f)
        {
            spectrumAt (rd.times[(size_t) f], mag);
            std::fill (excluded.begin(), excluded.end(), 0);
            if (useHarm)
            {
                const double hz = f < (int) rd.localHz.size() ? rd.localHz[(size_t) f] : rootHz;
                for (int k = 1; k * hz < sr * 0.45; ++k)
                {
                    const int c = juce::roundToInt (k * hz / binHz);
                    for (int d = -2; d <= 2; ++d) if (c + d > 0 && c + d <= N / 2) excluded[(size_t) (c + d)] = 1;
                }
            }
            for (size_t k = 0; k < partialTrack.size(); ++k)
            {
                const int c = partialTrack[k][(size_t) f];
                float pk = 0.0f;
                for (int d = -2; d <= 2; ++d) if (c + d > 0 && c + d <= N / 2) { pk = juce::jmax (pk, mag[(size_t) (c + d)]); excluded[(size_t) (c + d)] = 1; }
                m->partials[k].amp[(size_t) f] = (float) (2.0 * pk / sumW) * g;
            }
            double totalNoise = 0.0;
            for (int b = 0; b < ResynthModel::numBands; ++b)
            {
                const int lo = juce::jmax (1, (int) (edges[(size_t) b] / binHz)), hi = juce::jmin (N / 2, (int) (edges[(size_t) b + 1] / binHz));
                double e = 0.0; int used = 0, all = 0;
                for (int k = lo; k <= hi; ++k) { ++all; if (! excluded[(size_t) k]) { e += (double) mag[(size_t) k] * mag[(size_t) k]; ++used; } }
                // En sonidos con tono (p. ej. 808), extrapolar los bins excluidos
                // inflaba el ruido de fondo donde predominan los parciales.
                // Conservamos la correccion para material predominantemente aperiódico.
                const double compensation = (! prof.pitched && used > 0) ? (double) all / used : 1.0;
                const double p = used > 0 ? (2.0 * e / ((double) N * sumW2)) * compensation : 0.0;
                m->noise[(size_t) f][(size_t) b] = (float) std::sqrt (p);
                totalNoise += p;
            }
            // Con parte armónica, el total de ruido lo manda el análisis sincronizado (más fiable)
            if (useHarm && totalNoise > 0.0)
            {
                const double want = f < (int) rd.noiseRms.size() ? rd.noiseRms[(size_t) f] : 0.0;
                const float k = (float) (want / std::sqrt (totalNoise));
                for (auto& v : m->noise[(size_t) f]) v *= k;
            }
            for (auto& v : m->noise[(size_t) f]) v *= g;
        }
        // Ruido despreciable: fuera (ahorra CPU)
        if (useHarm && prof.noiseRatio < 0.01f) m->noise.clear();

        // ---- Cabeza original para bajos (808, sub) ----
        // El golpe de un 808 (click + caída de tono de ~12 semitonos en pocos ms) es más rápido de lo que el
        // análisis por ciclos puede seguir con notas de 30-60 Hz. Se guardan los primeros 40 ms del original y
        // la resíntesis entra con un cruce de 10 ms en el que la fase de la tabla se alinea con la del original
        // (así el cruce no cancela). Funciona igual para 808 limpios y saturados.
        if (useHarm && rootHz < 110.0 && m->harmonic != nullptr)
        {
            const int pre = (int) (0.002 * sr), head = (int) (0.040 * sr), xf = (int) (0.010 * sr);
            const int s0 = juce::jmax (0, onset - pre);
            const int len = juce::jmin (n - s0, (onset - s0) + head + xf);
            const int xfStart = (onset - s0) + head;
            if (len > xfStart + xf / 2)
            {
                m->transient.assign ((size_t) len, 0.0f);
                for (int i = 0; i < len; ++i)
                {
                    const float fi = i < (onset - s0) && (onset - s0) > 0 ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) (onset - s0)) : 1.0f;
                    const float fo = i >= xfStart ? juce::jmax (0.0f, 1.0f - (float) (i - xfStart) / (float) xf) : 1.0f;
                    m->transient[(size_t) i] = x[s0 + i] * fi * fo * g;
                }
                m->transientRate = (float) sr;
                m->headStart = (float) xfStart;
                m->headXf = (float) xf;
                // fase de la fundamental del original en el inicio del cruce (cos(2π·fase) en la tabla)
                const double tS = (double) (s0 + xfStart - onset) / sr;
                const double cents = ResynthModel::curveAt (m->pitchCurve, m->pitchHop, tS);
                const double f = rootHz * std::exp2 (cents / 1200.0);
                const int half = (int) (sr / f);
                double c = 0.0, sn = 0.0;
                for (int k = -half; k <= half; ++k)
                {
                    const int i = s0 + xfStart + k;
                    if (i < 0 || i >= n) continue;
                    const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * k / (half + 1));
                    const double ang = juce::MathConstants<double>::twoPi * f * k / sr;
                    c += w * x[i] * std::cos (ang); sn += w * x[i] * std::sin (ang);
                }
                double ph = std::atan2 (-sn, c) / juce::MathConstants<double>::twoPi;
                m->headPhase = (float) (ph - std::floor (ph));
            }
        }
        return m;
    }
}
