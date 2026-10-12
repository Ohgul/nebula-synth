#pragma once
#include "ResynthModel.h"
#include "Parameters.h"

// ===========================================================================
// ADN del sonido recreado.
//
// El modelo base (lo que sale de RECREAR o se guarda en el proyecto) nunca se toca. Los genes "estructurales"
// (brillo, brillo en el tiempo, pares/impares, cantidad de armónicos, formantes) generan una variante del
// modelo reconstruyendo cada ciclo de la tabla armónica en el dominio de los armónicos; los genes "en tiempo
// real" los aplica ResynthPlayer mientras suena (ver DnaRealtime).
// ===========================================================================
namespace Dna
{
    struct Structural
    {
        float tilt = 0.0f, tiltEvolve = 0.0f, oddEven = 0.0f, harmonics = 1.0f, formant = 0.0f;
        float evolve = 1.0f;   // 0 = timbre fijo (el promedio), 1 = como el original, 2 = cambios exagerados
        float cosmos = 0.0f, abyss = 0.0f;   // transformación (ver Transform.h)
        bool neutral() const
        {
            return std::abs (tilt) < 0.01f && std::abs (tiltEvolve) < 0.01f && std::abs (oddEven) < 0.001f
                && harmonics > 0.999f && std::abs (formant) < 0.01f && std::abs (evolve - 1.0f) < 1.0e-3f
                && cosmos < 1.0e-3f && abyss < 1.0e-3f;
        }
        bool operator== (const Structural& o) const
        {
            return tilt == o.tilt && tiltEvolve == o.tiltEvolve && oddEven == o.oddEven && harmonics == o.harmonics
                && formant == o.formant && evolve == o.evolve && cosmos == o.cosmos && abyss == o.abyss;
        }
        bool operator!= (const Structural& o) const { return ! (*this == o); }
    };

    inline Structural readStructural (const ParamPointers& p)
    {
        Structural s;
        s.tilt = p[P::dnaTilt]; s.tiltEvolve = p[P::dnaTiltEvolve]; s.oddEven = p[P::dnaOddEven];
        s.harmonics = p[P::dnaHarmonics]; s.formant = p[P::dnaFormant]; s.evolve = p[P::dnaEvolve];
        if (std::abs (s.evolve - 1.0f) < 1.0e-4f) s.evolve = 1.0f;
        // la transformación se cuantiza a pasos de 1 % (cada paso reconstruye la tabla)
        const float t = std::round (p[P::transform] * 100.0f) / 100.0f;
        const float keep = 1.0f - juce::jlimit (0.0f, 1.0f, std::round (p[P::identity] * 100.0f) / 100.0f);   // Identidad conserva el original
        s.cosmos = juce::jmax (0.0f, t) * keep; s.abyss = juce::jmax (0.0f, -t) * keep;
        return s;
    }

    // Ganancia del armónico k (frecuencia relativa "ratio" para parciales) en la posición temporal pos (0..1)
    inline float harmonicGain (const Structural& s, double ratio, int k, float pos)
    {
        const double tilt = s.tilt + s.tiltEvolve * pos;   // dB por octava respecto a la fundamental
        double g = std::pow (10.0, tilt * std::log2 (juce::jmax (1.0e-3, ratio)) / 20.0);
        if (k > 0 && (k % 2) == 0) g *= 1.0 + s.oddEven;   // -1 = solo impares (hueco), +1 = pares al doble
        if (s.harmonics < 0.999f)
        {
            const double K = 1.0 + std::pow ((double) s.harmonics, 2.0) * 255.0;   // armónico de corte
            g /= 1.0 + std::pow (ratio / K, 8.0);
        }
        return (float) g;
    }

    // Cosmos / Abismo sobre el espectro de un ciclo (armónico k = bin k). Azar con semillas fijas: siempre igual.
    inline void transformSpectrum (std::vector<double>& amp, std::vector<double>& ph, int frame, int frames, float cosmos, float abyss)
    {
        const int nb = (int) amp.size();
        if (cosmos > 0.0f)
        {
            // expansión: eco del espectro una octava (k <- k/2) y una quinta (k <- 2k/3) arriba, y difuminado
            std::vector<double> e (amp);
            for (int k = 2; k < nb; ++k)
            {
                if (k % 2 == 0) e[(size_t) k] += cosmos * 0.55 * amp[(size_t) (k / 2)];
                if (k % 3 == 0) e[(size_t) k] += cosmos * 0.35 * amp[(size_t) (2 * k / 3)];
            }
            const double w = 0.3 * cosmos;
            for (int k = 2; k < nb - 1; ++k) amp[(size_t) k] = (1.0 - 2.0 * w) * e[(size_t) k] + w * (e[(size_t) k - 1] + e[(size_t) k + 1]);
            // fases abiertas: la forma de onda pierde el pico (más suave, más "pad")
            juce::Random r (0xC05305);
            for (int k = 2; k < nb; ++k) ph[(size_t) k] += cosmos * juce::MathConstants<double>::pi * (r.nextDouble() * 2.0 - 1.0);
        }
        if (abyss > 0.0f)
        {
            // fragmentación: bandas de armónicos que se apagan; el patrón cambia cada octavo de la nota
            const int segment = juce::jmax (1, frames / 8);
            juce::Random r (0xAB1550 + (frame / segment) * 7919);
            for (int k = 2; k < nb;)
            {
                const int width = 2 + r.nextInt (5);
                const bool off = r.nextFloat() < 0.55f * abyss;
                for (int j = k; j < juce::jmin (nb, k + width); ++j) if (off) amp[(size_t) j] *= 1.0 - abyss;
                k += width;
            }
            // pliegue: lo que pasa del armónico K vuelve reflejado hacia abajo (aspereza metálica)
            const int K = 4 + (int) (60.0f * (1.0f - abyss));
            for (int k = K + 1; k < juce::jmin (nb, 2 * K); ++k) amp[(size_t) (2 * K - k)] += abyss * amp[(size_t) k];
            // deformación: niveles al azar ±12 dB · abismo (fijos por armónico)
            juce::Random g (0xDEF0A);
            for (int k = 1; k < nb; ++k) amp[(size_t) k] *= std::pow (10.0, abyss * 12.0 * (g.nextDouble() * 2.0 - 1.0) / 20.0);
        }
    }

    // Variante del modelo con los genes estructurales aplicados (mismo número de cuadros y tiempos: se puede
    // cambiar en una nota que ya suena). Devuelve el mismo puntero si los genes son neutros.
    inline std::shared_ptr<const ResynthModel> makeVariant (const std::shared_ptr<const ResynthModel>& base, const Structural& s)
    {
        if (base == nullptr || s.neutral()) return base;
        auto m = std::make_shared<ResynthModel> (*base);
        const double total = base->times.empty() ? 1.0 : juce::jmax (1.0e-3, (double) base->times.back());

        if (base->harmonic != nullptr)
        {
            const int frames = base->harmonic->numFrames(), fs = Wavetable::frameSize;
            juce::dsp::FFT fft (11);
            std::vector<float> spec ((size_t) fs * 2);
            std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> ((size_t) fs));
            float newPeak = 1.0e-9f;
            const double r = std::exp2 (s.formant / 12.0);
            const int nb = fs / 2 + 1;
            // 1ª pasada: espectro de cada ciclo (amplitud y fase) y su rms
            std::vector<std::vector<double>> amps ((size_t) frames, std::vector<double> ((size_t) nb)), phs = amps;
            std::vector<double> rmsIns ((size_t) frames, 0.0);
            for (int f = 0; f < frames; ++f)
            {
                const float* raw = base->harmonic->rawFrame (f);
                double rmsIn = 0.0;
                std::fill (spec.begin(), spec.end(), 0.0f);
                for (int i = 0; i < fs; ++i) { spec[(size_t) i] = raw[i]; rmsIn += (double) raw[i] * raw[i]; }
                rmsIns[(size_t) f] = std::sqrt (rmsIn / fs);
                fft.performRealOnlyForwardTransform (spec.data(), true);
                for (int k = 0; k < nb; ++k)
                {
                    amps[(size_t) f][(size_t) k] = std::hypot (spec[(size_t) (2 * k)], spec[(size_t) (2 * k + 1)]);
                    phs[(size_t) f][(size_t) k] = std::atan2 (spec[(size_t) (2 * k + 1)], spec[(size_t) (2 * k)]);
                }
            }
            // Evolución: cada ciclo se acerca (evolve < 1) o se aleja (evolve > 1) del timbre promedio, en dB.
            // El nivel de cada momento no cambia (lo lleva la envolvente), sólo cuánto cambia el color.
            if (std::abs (s.evolve - 1.0f) > 1.0e-3f && frames > 1)
            {
                std::vector<double> mean ((size_t) nb, 0.0);
                std::vector<double> floorDb ((size_t) frames);
                for (int f = 0; f < frames; ++f)
                {
                    double mx = 1.0e-12; for (auto a : amps[(size_t) f]) mx = std::max (mx, a);
                    floorDb[(size_t) f] = 20.0 * std::log10 (mx) - 100.0;
                    for (int k = 1; k < nb; ++k) mean[(size_t) k] += std::max (floorDb[(size_t) f], 20.0 * std::log10 (std::max (1.0e-12, amps[(size_t) f][(size_t) k])));
                }
                for (auto& v : mean) v /= frames;
                for (int f = 0; f < frames; ++f)
                    for (int k = 1; k < nb; ++k)
                    {
                        const double d = std::max (floorDb[(size_t) f], 20.0 * std::log10 (std::max (1.0e-12, amps[(size_t) f][(size_t) k])));
                        amps[(size_t) f][(size_t) k] = std::pow (10.0, juce::jlimit (-140.0, 40.0, mean[(size_t) k] + (double) s.evolve * (d - mean[(size_t) k])) / 20.0);
                    }
            }
            for (int f = 0; f < frames; ++f)
            {
                const double rmsIn = rmsIns[(size_t) f];
                const float pos = (float) ((f < (int) base->times.size() ? base->times[(size_t) f] : 0.0) / total);
                auto& amp = amps[(size_t) f];
                auto& ph = phs[(size_t) f];
                if (s.cosmos > 0.0f || s.abyss > 0.0f) transformSpectrum (amp, ph, f, frames, s.cosmos, s.abyss);
                for (int k = 1; k < fs / 2; ++k)
                {
                    double a = amp[(size_t) k];
                    if (std::abs (s.formant) > 0.01f)
                    {
                        // la envolvente espectral se lee desplazada: el armónico k toma el nivel que tenía k/r
                        const double src = k / r;
                        if (src < 1.0) a = amp[1] * src;
                        else { const int k0 = (int) src; const double w = src - k0; a = amp[(size_t) juce::jmin (fs / 2, k0)] * (1.0 - w) + amp[(size_t) juce::jmin (fs / 2, k0 + 1)] * w; }
                    }
                    a *= harmonicGain (s, (double) k, k, pos);
                    spec[(size_t) (2 * k)] = (float) (a * std::cos (ph[(size_t) k]));
                    spec[(size_t) (2 * k + 1)] = (float) (a * std::sin (ph[(size_t) k]));
                }
                spec[0] = spec[1] = 0.0f;
                spec[(size_t) fs] = spec[(size_t) fs + 1] = 0.0f;
                fft.performRealOnlyInverseTransform (spec.data());
                // mismo rms que el ciclo original: el volumen lo sigue llevando la envolvente armónica
                double rmsOut = 0.0;
                for (int i = 0; i < fs; ++i) rmsOut += (double) spec[(size_t) i] * spec[(size_t) i];
                rmsOut = std::sqrt (rmsOut / fs);
                const float gn = rmsOut > 1.0e-12 ? (float) (rmsIn / rmsOut) : 0.0f;
                for (int i = 0; i < fs; ++i) { cycles[(size_t) f][(size_t) i] = spec[(size_t) i] * gn; newPeak = juce::jmax (newPeak, std::abs (cycles[(size_t) f][(size_t) i])); }
            }
            m->harmonic = Wavetable::fromCycles (cycles, base->name);
            // la tabla se normaliza a su pico: se compensa para que la salida tenga la misma escala que el ciclo
            m->harmonicLevel = base->harmonicLevel * newPeak;
        }

        // Ruido: misma evolución del color (la forma entre bandas), conservando el nivel de cada cuadro
        if (std::abs (s.evolve - 1.0f) > 1.0e-3f && m->noise.size() > 1)
        {
            const int nbands = (int) m->noise[0].size();
            std::vector<double> mean ((size_t) nbands, 0.0);
            std::vector<double> tot (m->noise.size(), 0.0);
            for (size_t f = 0; f < m->noise.size(); ++f)
            {
                for (auto a : m->noise[f]) tot[f] += (double) a * a;
                tot[f] = std::sqrt (tot[f]);
                for (int b = 0; b < nbands; ++b) mean[(size_t) b] += std::log (std::max (1.0e-9, m->noise[f][(size_t) b] / std::max (1.0e-12, tot[f])));
            }
            for (auto& v : mean) v /= (double) m->noise.size();
            for (size_t f = 0; f < m->noise.size(); ++f)
            {
                if (tot[f] < 1.0e-12) continue;
                double tot2 = 0.0;
                std::vector<double> nv ((size_t) nbands);
                for (int b = 0; b < nbands; ++b)
                {
                    const double l = std::log (std::max (1.0e-9, m->noise[f][(size_t) b] / tot[f]));
                    nv[(size_t) b] = std::exp (juce::jlimit (-30.0, 5.0, mean[(size_t) b] + (double) s.evolve * (l - mean[(size_t) b])));
                    tot2 += nv[(size_t) b] * nv[(size_t) b];
                }
                const double g = tot[f] / std::max (1.0e-12, std::sqrt (tot2));
                for (int b = 0; b < nbands; ++b) m->noise[f][(size_t) b] = (float) (nv[(size_t) b] * g);
            }
        }

        // Parciales inarmónicos: brillo y cantidad según su frecuencia relativa
        for (auto& p : m->partials)
            for (size_t f = 0; f < p.amp.size(); ++f)
            {
                const float pos = (float) ((f < base->times.size() ? base->times[f] : 0.0) / total);
                const double ratio = f < p.ratioCurve.size() ? p.ratioCurve[f] : p.ratio;
                p.amp[f] *= harmonicGain (s, ratio, 0, pos);
            }
        return m;
    }

    // Genes que actúan mientras suena (los lee la voz en cada bloque)
    struct Realtime
    {
        float pitchDepth = 1.0f, attack = 1.0f, release = 1.0f, transient = 1.0f;
        float noiseTilt = 0.0f, rough = 0.0f, density = 0.0f;
        bool freeze = false;
    };

    inline Realtime readRealtime (const ParamPointers& p)
    {
        Realtime r;
        r.pitchDepth = p[P::dnaPitchDepth]; r.attack = p[P::dnaAttack]; r.release = p[P::dnaRelease];
        r.transient = p[P::dnaTransient]; r.noiseTilt = p[P::dnaNoiseTilt];
        r.rough = p[P::dnaRough]; r.density = p[P::dnaDensity]; r.freeze = p.i (P::dnaHold) == 1;
        // los rangos con curva (ataque, cola) no devuelven 1 exacto al pasar por el valor normalizado:
        // se fija el neutro para que el ADN sin tocar suene idéntico a antes, bit a bit
        auto snap = [] (float& v, float n) { if (std::abs (v - n) < 1.0e-4f) v = n; };
        snap (r.pitchDepth, 1.0f); snap (r.attack, 1.0f); snap (r.release, 1.0f); snap (r.transient, 1.0f);
        snap (r.noiseTilt, 0.0f); snap (r.rough, 0.0f); snap (r.density, 0.0f);
        return r;
    }

    // ---- Grupos (para la interfaz, la mutación y copiar/pegar) ----
    struct GeneInfo { const char* id; const char* group; const char* name; };
    inline const std::vector<GeneInfo>& genes()
    {
        static const std::vector<GeneInfo> g {
            { ParamID::dnaPitchDepth, "A", "Tono" },        { ParamID::dnaFormant, "A", "Formantes" },
            { ParamID::dnaTilt, "B", "Brillo" },            { ParamID::dnaTiltEvolve, "B", "Brillo en el tiempo" },
            { ParamID::dnaOddEven, "B", "Pares/Impares" },  { ParamID::dnaHarmonics, "B", "Armonicos" },
            { ParamID::rsHarm, "C", "Capa armonica" },      { ParamID::rsPartials, "C", "Capa de parciales" },
            { ParamID::rsNoise, "C", "Capa de ruido" },     { ParamID::dnaNoiseTilt, "C", "Color del ruido" },
            { ParamID::dnaRough, "C", "Aspereza" },         { ParamID::dnaDensity, "C", "Densidad" },
            { ParamID::dnaAttack, "D", "Ataque" },          { ParamID::dnaTransient, "D", "Golpe" },
            { ParamID::rsSpeed, "D", "Velocidad" },         { ParamID::dnaRelease, "D", "Cola" },
            { ParamID::dnaEvolve, "E", "Evolucion" },       { ParamID::dnaHold, "E", "Al sostener" },
            { ParamID::rsStretch, "E", "Inarmonia" },       { ParamID::rsWidth, "E", "Ancho" },
            { ParamID::transform, "F", "Abismo / Cosmos" }, { ParamID::identity, "F", "Identidad" },
        };
        return g;
    }
    inline const juce::StringArray& groupNames()
    {
        static const juce::StringArray n { "A  Identidad tonal", "B  Estructura armonica", "C  Textura", "D  Tiempo", "E  Movimiento", "F  Transformacion" };
        return n;
    }
}
