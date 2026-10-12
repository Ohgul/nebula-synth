#pragma once
#include "Metrics.h"

// ===========================================================================
// "% de parecido" honesto.
//
// Sale de las métricas validadas del banco de pruebas (Metrics.h), no de un ajuste a mano:
//   - Color:      distancia log-mel de 32 bandas promediada ±250 ms (dB). Ruido de fondo de la
//                 métrica (cambios inaudibles): <= 0.45 dB. Un estante de agudos de -3 dB: ~0.6-1.5 dB.
//   - Envolvente: error RMS de la curva de sonoridad (dB). Fondo <= 0.26 dB; decaimiento x0.8: ~1.3 dB.
//   - Tono:       diferencia de la nota central y de la curva cuadro a cuadro (cents).
// Cada aspecto se pasa a porcentaje con una caída exponencial suave: 6 dB de error de color o de
// envolvente dejan un 37 %, 2 dB un 72 %, 0.45 dB (lo inaudible) un 93 %; 50 cents dejan un 37 %.
// No se recorta nada abajo ni se redondea hacia arriba: un 100 % sólo sale de dos señales idénticas.
// El total es la media geométrica ponderada (color 0.5, envolvente 0.3, tono 0.2): un aspecto muy
// malo arrastra el total (con una media normal se escondería).
// ===========================================================================
struct SimilarityReport
{
    float overall = -1.0f, color = -1.0f, envelope = -1.0f, pitch = -1.0f;   // 0..1 (-1 = sin dato)
    double colorDb = Bench::kNaN, envelopeDb = Bench::kNaN, pitchCents = Bench::kNaN;
    bool valid() const { return overall >= 0.0f; }
};

namespace Similarity
{
    inline float fromError (double err, double scale) { return std::isfinite (err) ? (float) std::exp (-juce::jmax (0.0, err) / scale) : -1.0f; }

    inline SimilarityReport fromScores (const Bench::Scores& s, bool pitched)
    {
        SimilarityReport r;
        r.colorDb = s.colorDb; r.envelopeDb = s.envErrDb;
        r.color = fromError (s.colorDb, 6.0);
        r.envelope = fromError (s.envErrDb, 6.0);
        if (pitched && std::isfinite (s.pitchCentreCents))
        {
            r.pitchCents = std::abs (s.pitchCentreCents) + 0.5 * (std::isfinite (s.pitchMedCents) ? s.pitchMedCents : 0.0);
            r.pitch = fromError (r.pitchCents, 50.0);
        }
        double logSum = 0.0, wSum = 0.0;
        auto add = [&] (float v, double w) { if (v >= 0.0f) { logSum += w * std::log (juce::jmax (1.0e-4f, v)); wSum += w; } };
        add (r.color, 0.5); add (r.envelope, 0.3); add (r.pitch, 0.2);
        r.overall = wSum > 0.0 ? (float) std::exp (logSum / wSum) : -1.0f;
        return r;
    }

    // Compara la referencia (el sample) con lo que suena (mismo sr). expectedCents: diferencia de afinación
    // esperada (la nota tocada no siempre es exactamente la del sample).
    inline SimilarityReport measure (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& test, double sr,
                                     bool pitched, double expectedCents, double lowestHz)
    {
        if (ref.getNumSamples() < (int) (0.05 * sr) || test.getNumSamples() < (int) (0.05 * sr)) return {};
        const auto s = Bench::compare (ref, test, sr, pitched, expectedCents, lowestHz);
        return fromScores (s, pitched);
    }

    inline juce::String describe (const SimilarityReport& r)
    {
        if (! r.valid()) return {};
        auto pc = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        juce::String s = "color " + pc (r.color) + "   envolvente " + pc (r.envelope);
        if (r.pitch >= 0.0f) s << "   tono " << pc (r.pitch);
        return s;
    }
}
