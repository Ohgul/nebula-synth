#pragma once
#include <juce_dsp/juce_dsp.h>

// "Huella" de un sonido: cómo cambia su espectro y su volumen en el tiempo.
// Se usa para comparar un sample con lo que produce el sinte.
struct SoundFeatures
{
    static constexpr int numBands = 28;
    int frames = 0;
    std::vector<float> shape;  // frames * bands: forma espectral (dB relativos al total del cuadro)
    std::vector<float> env;    // frames: volumen en dB relativo al pico
    float pitchHz = 0.0f;

    bool isValid() const { return frames > 0; }
};

class SoundAnalyser
{
public:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;

    SoundAnalyser() : fft (fftOrder), window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false) {}

    // Cuadros más densos al principio (el ataque importa mucho para reconocer un sonido)
    static double frameTime (int f, int numFrames, double lengthSeconds)
    {
        const double u = (double) f / (double) (numFrames - 1);
        return lengthSeconds * std::pow (u, 1.6);
    }

    SoundFeatures analyse (const float* x, int n, double sr, double lengthSeconds, int numFrames = 48)
    {
        SoundFeatures out;
        out.frames = numFrames;
        out.shape.assign ((size_t) (numFrames * SoundFeatures::numBands), -60.0f);
        out.env.assign ((size_t) numFrames, -60.0f);

        // Límites de bandas en Hz (escala logarítmica)
        std::array<float, SoundFeatures::numBands + 1> edges;
        for (int b = 0; b <= SoundFeatures::numBands; ++b)
            edges[(size_t) b] = 50.0f * std::pow (15000.0f / 50.0f, (float) b / (float) SoundFeatures::numBands);

        std::vector<float> buf ((size_t) fftSize * 2);
        float maxEnv = -200.0f;

        for (int f = 0; f < numFrames; ++f)
        {
            const int centre = (int) (frameTime (f, numFrames, lengthSeconds) * sr);
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = centre - fftSize / 2 + i;
                if (idx >= 0 && idx < n) buf[(size_t) i] = x[idx];
            }
            window.multiplyWithWindowingTable (buf.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            std::array<double, SoundFeatures::numBands> bandPow {};
            double total = 1.0e-12;
            for (int k = 1; k < fftSize / 2; ++k)
            {
                const float hz = (float) (k * sr / fftSize);
                const double p = (double) buf[(size_t) k] * buf[(size_t) k];
                total += p;
                if (hz < edges.front() || hz >= edges.back()) continue;
                const int b = juce::jlimit (0, SoundFeatures::numBands - 1,
                                            (int) (std::log (hz / 50.0f) / std::log (15000.0f / 50.0f) * SoundFeatures::numBands));
                bandPow[(size_t) b] += p;
            }

            const float totalDb = (float) (10.0 * std::log10 (total));
            out.env[(size_t) f] = totalDb;
            maxEnv = juce::jmax (maxEnv, totalDb);
            for (int b = 0; b < SoundFeatures::numBands; ++b)
            {
                const float db = (float) (10.0 * std::log10 (bandPow[(size_t) b] + 1.0e-12)) - totalDb;
                out.shape[(size_t) (f * SoundFeatures::numBands + b)] = juce::jlimit (-60.0f, 0.0f, db);
            }
        }

        for (auto& e : out.env)
            e = juce::jlimit (-60.0f, 0.0f, e - maxEnv);

        return out;
    }

    // Distancia entre dos huellas (0 = idénticas). Aproximadamente en dB.
    static float distance (const SoundFeatures& target, const SoundFeatures& cand)
    {
        if (target.frames != cand.frames || target.frames == 0) return 1000.0f;

        double shapeSum = 0.0, weightSum = 0.0, envSum = 0.0;
        for (int f = 0; f < target.frames; ++f)
        {
            // Los cuadros fuertes del sample pesan más que los silencios
            const double w = juce::jlimit (0.0, 1.0, (target.env[(size_t) f] + 50.0) / 50.0);
            if (w > 0.0)
            {
                double d = 0.0;
                for (int b = 0; b < SoundFeatures::numBands; ++b)
                {
                    const size_t i = (size_t) (f * SoundFeatures::numBands + b);
                    d += std::abs (target.shape[i] - cand.shape[i]);
                }
                shapeSum += w * d / SoundFeatures::numBands;
                weightSum += w;
            }
            envSum += std::abs (target.env[(size_t) f] - cand.env[(size_t) f]);
        }

        const double shapeD = weightSum > 0.0 ? shapeSum / weightSum : 60.0;
        const double envD = envSum / target.frames;
        return (float) (shapeD + 0.5 * envD);
    }

    // Convierte la distancia a un "% de parecido" fácil de leer
    // (dos tomas del mismo patch dan ~2: las fases de los osciladores varían)
    static float similarity (float distance)
    {
        const float x = juce::jmax (0.0f, distance - 2.0f) / 6.0f;
        return 1.0f / (1.0f + x * x);
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
};
