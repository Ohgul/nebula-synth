#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace Forge
{
    // Tono rápido para la búsqueda: YIN directo sobre la señal filtrada y diezmada a 32·f0 (sólo interesa
    // la fundamental, entre una octava abajo y dos arriba de la del cuerpo), cada 25 ms
    inline std::vector<double> fastPitchTrack (const std::vector<double>& x, double sr, double f0, double hopS = 0.025)
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
        const int hop = juce::jmax (1, (int) (hopS * srD));
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
}
