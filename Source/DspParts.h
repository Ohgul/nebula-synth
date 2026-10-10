#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Parameters.h"
#include "Wavetable.h"
#include <complex>

// ===========================================================================
// Piezas del motor: oscilador, envolvente, LFO y filtro.
// ===========================================================================

inline float polyBlep (double t, double dt)
{
    if (dt <= 0.0) return 0.0f;
    if (t < dt)        { t /= dt;            return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt)  { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Oscilador de Nebula: seno, triángulo, sierra, pulso (PWM), Super Saw,
// Sync y Wavetable. "shape" (0..1) cambia según el tipo:
//   Pulso → ancho del pulso · Super Saw → desafinación · Sync → relación
//   Wavetable → posición en la tabla
class NebOsc
{
public:
    static constexpr int superVoices = 7;

    void reset (juce::Random& r, bool randomPhase)
    {
        phase = randomPhase ? r.nextDouble() : 0.0;
        for (auto& p : sp) p = r.nextDouble();
        syncPhase = 0.0;
    }

    void setFrequency (double hz, double sr)
    {
        freq = hz;
        inc = juce::jlimit (0.0, 0.49, hz / sr);
        sampleRate = sr;
    }

    float next (int wave, float shape, const Wavetable* table, double pm = 0.0)
    {
        float v = 0.0f;
        double t = phase + pm;
        t -= std::floor (t);

        switch (wave)
        {
            case Wave::Sine:     v = (float) std::sin (juce::MathConstants<double>::twoPi * t); break;
            case Wave::Triangle: v = (float) (1.0 - 4.0 * std::abs (t - 0.5)); break;
            case Wave::Saw:      v = (float) (2.0 * t - 1.0) - polyBlep (t, inc); break;

            case Wave::Pulse:
            {
                const double pw = 0.05 + 0.9 * juce::jlimit (0.0f, 1.0f, shape);
                v = t < pw ? 1.0f : -1.0f;
                v += polyBlep (t, inc);
                double t2 = t - pw; if (t2 < 0.0) t2 += 1.0;
                v -= polyBlep (t2, inc);
                v -= (float) (2.0 * pw - 1.0);   // centra (sin DC)
                break;
            }

            case Wave::SuperSaw:
            {
                // 7 sierras: la central más las laterales desafinadas
                static const float spread[superVoices] { 0.0f, -1.0f, 1.0f, -0.62f, 0.62f, -0.28f, 0.28f };
                if (shape != ssShape)   // recalcular sólo cuando cambia el "detune" (ahorra CPU)
                {
                    ssShape = shape;
                    const float detune = std::pow (juce::jlimit (0.0f, 1.0f, shape), 1.5f) * 0.9f;   // semitonos máx.
                    for (int k = 0; k < superVoices; ++k) ssRatio[(size_t) k] = std::exp2 (spread[k] * detune / 12.0);
                }
                const float sideGain = 0.25f + 0.55f * juce::jlimit (0.0f, 1.0f, shape);
                float sum = 0.0f, norm = 0.0f;
                for (int k = 0; k < superVoices; ++k)
                {
                    const double di = juce::jmin (0.49, inc * ssRatio[(size_t) k]);
                    double tk = sp[(size_t) k] + pm;
                    tk -= std::floor (tk);
                    const float g = k == 0 ? 1.0f : sideGain;
                    sum += g * ((float) (2.0 * tk - 1.0) - polyBlep (tk, di));
                    norm += g;
                    sp[(size_t) k] += di;
                    if (sp[(size_t) k] >= 1.0) sp[(size_t) k] -= 1.0;
                }
                v = sum / norm * 1.6f;
                break;
            }

            case Wave::Sync:
            {
                // Sierra "esclava" reiniciada por el ciclo principal
                const double ratio = 1.0 + 7.0 * juce::jlimit (0.0f, 1.0f, shape);
                const double sInc = juce::jmin (0.49, inc * ratio);
                double st = syncPhase;
                v = (float) (2.0 * st - 1.0) - polyBlep (st, sInc);
                syncPhase += sInc;
                if (syncPhase >= 1.0) syncPhase -= 1.0;
                if (phase + inc >= 1.0)   // el maestro termina su ciclo: reinicio duro
                {
                    const double frac = (phase + inc - 1.0) / inc;
                    syncPhase = frac * sInc;
                }
                break;
            }

            case Wave::Table:
            {
                if (table != nullptr)
                    v = table->read (t, juce::jlimit (0.0f, 1.0f, shape), Wavetable::mipFor (freq, sampleRate));
                break;
            }

            default: break;
        }

        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return v;
    }

    double phase = 0.0;

private:
    double inc = 0.0, freq = 440.0, sampleRate = 44100.0, syncPhase = 0.0;
    std::array<double, superVoices> sp {};
    std::array<double, superVoices> ssRatio { 1, 1, 1, 1, 1, 1, 1 };
    float ssShape = -1.0f;
};

// ---------------------------------------------------------------------------
// Envolvente ADSR con curva y modo Loop
class NebEnv
{
public:
    struct Params { float a = 0.01f, d = 0.3f, s = 0.8f, r = 0.3f, curve = 0.0f; bool loop = false; };

    void setSampleRate (double sr) { sampleRate = sr; }
    void setParams (const Params& p, float timeScale)
    {
        prm = p;
        scale = juce::jmax (0.05f, timeScale);
    }

    void noteOn (bool fromCurrentLevel = true)
    {
        startLevel = fromCurrentLevel ? value : 0.0f;
        stage = Attack; t = 0.0f;
    }
    void noteOff()
    {
        if (stage == Idle) return;
        startLevel = value; stage = Release; t = 0.0f;
    }
    void reset() { stage = Idle; value = 0.0f; t = 0.0f; }
    bool isActive() const { return stage != Idle; }
    float current() const { return value; }

    float next()
    {
        const float dt = 1.0f / (float) sampleRate;
        switch (stage)
        {
            case Idle: value = 0.0f; break;
            case Attack:
            {
                const float len = prm.a * scale;
                t += dt;
                const float x = len > 0.0f ? juce::jmin (1.0f, t / len) : 1.0f;
                value = startLevel + (1.0f - startLevel) * shapeUp (x);
                if (x >= 1.0f) { stage = Decay; t = 0.0f; }
                break;
            }
            case Decay:
            {
                const float len = prm.d * scale;
                t += dt;
                const float x = len > 0.0f ? juce::jmin (1.0f, t / len) : 1.0f;
                value = 1.0f + (prm.s - 1.0f) * shapeDown (x);
                if (x >= 1.0f)
                {
                    if (prm.loop) { startLevel = value; stage = Attack; t = 0.0f; }
                    else          { stage = Sustain; }
                }
                break;
            }
            case Sustain: value = prm.s; break;
            case Release:
            {
                const float len = prm.r * scale;
                t += dt;
                const float x = len > 0.0f ? juce::jmin (1.0f, t / len) : 1.0f;
                value = startLevel * (1.0f - shapeDown (x));
                if (x >= 1.0f) { stage = Idle; value = 0.0f; }
                break;
            }
        }
        return value;
    }

    // Para dibujar la envolvente: valor en el tiempo t (s) con nota sostenida "hold" segundos
    static float shapeAt (const Params& p, float tSec, float holdSec)
    {
        auto up = [&] (float x) { return std::pow (x, std::exp2 (-p.curve * 2.0f)); };
        auto dn = [&] (float x) { return 1.0f - std::pow (1.0f - x, std::exp2 (p.curve * 2.0f)); };
        if (tSec < p.a) return up (tSec / juce::jmax (1.0e-4f, p.a));
        tSec -= p.a;
        if (tSec < p.d) return 1.0f + (p.s - 1.0f) * dn (tSec / juce::jmax (1.0e-4f, p.d));
        tSec -= p.d;
        const float sustainTime = juce::jmax (0.0f, holdSec - p.a - p.d);
        if (tSec < sustainTime) return p.s;
        tSec -= sustainTime;
        if (tSec < p.r) return p.s * (1.0f - dn (tSec / juce::jmax (1.0e-4f, p.r)));
        return 0.0f;
    }

private:
    enum Stage { Idle, Attack, Decay, Sustain, Release };

    // curva > 0: sube rápido (cóncava). curva < 0: arranca lento.
    float shapeUp (float x) const   { return std::pow (x, std::exp2 (-prm.curve * 2.0f)); }
    float shapeDown (float x) const { return 1.0f - std::pow (1.0f - x, std::exp2 (prm.curve * 2.0f)); }

    Params prm;
    double sampleRate = 44100.0;
    float scale = 1.0f, value = 0.0f, startLevel = 0.0f, t = 0.0f;
    Stage stage = Idle;
};

// ---------------------------------------------------------------------------
// LFO con formas, sincronía al tempo y entrada gradual
class NebLfo
{
public:
    void noteOn (juce::Random& r) { phase = 0.0; age = 0.0f; held = r.nextFloat() * 2.0f - 1.0f; target = r.nextFloat() * 2.0f - 1.0f; }

    // Devuelve el valor (-1..1). inc = ciclos por muestra. Si freePhase >= 0 se usa esa fase (LFO libre).
    float next (int shape, double inc, double freePhase, float fadeSec, double sampleRate, juce::Random& r)
    {
        double ph;
        if (freePhase >= 0.0)
        {
            ph = freePhase - std::floor (freePhase);
            if (ph < lastPhase) wrapped (r);
            lastPhase = ph;
        }
        else
        {
            phase += inc;
            if (phase >= 1.0) { phase -= 1.0; wrapped (r); }
            ph = phase;
        }

        float v = 0.0f;
        switch (shape)
        {
            case LfoShape::Sine:         v = (float) std::sin (juce::MathConstants<double>::twoPi * ph); break;
            case LfoShape::Triangle:     v = (float) (1.0 - 4.0 * std::abs (ph - 0.5)); v = -v; break;
            case LfoShape::Saw:          v = (float) (2.0 * ph - 1.0); break;
            case LfoShape::Square:       v = ph < 0.5 ? 1.0f : -1.0f; break;
            case LfoShape::Random:       v = held; break;
            case LfoShape::SmoothRandom:
            {
                const float x = (float) ph;
                const float s = x * x * (3.0f - 2.0f * x);
                v = held + (target - held) * s;
                break;
            }
            default: break;
        }

        age += (float) (1.0 / sampleRate);
        if (fadeSec > 0.001f && age < fadeSec) v *= age / fadeSec;
        return v;
    }

    // Forma de un ciclo para dibujar
    static float shapeAt (int shape, float ph)
    {
        switch (shape)
        {
            case LfoShape::Sine:     return std::sin (juce::MathConstants<float>::twoPi * ph);
            case LfoShape::Triangle: return -(1.0f - 4.0f * std::abs (ph - 0.5f));
            case LfoShape::Saw:      return 2.0f * ph - 1.0f;
            case LfoShape::Square:   return ph < 0.5f ? 1.0f : -1.0f;
            case LfoShape::Random:   { static const float steps[] { 0.6f, -0.3f, 0.9f, -0.8f, 0.1f, -0.5f, 0.4f, -0.1f }; return steps[juce::jlimit (0, 7, (int) (ph * 8.0f))]; }
            default:
            {
                static const float pts[] { 0.6f, -0.3f, 0.9f, -0.8f, 0.1f, -0.5f, 0.4f, -0.1f, 0.6f };
                const float x = ph * 8.0f; const int i = juce::jlimit (0, 7, (int) x); const float f = x - (float) i;
                const float s = f * f * (3.0f - 2.0f * f);
                return pts[i] + (pts[i + 1] - pts[i]) * s;
            }
        }
    }

private:
    void wrapped (juce::Random& r) { held = target; target = r.nextFloat() * 2.0f - 1.0f; }

    double phase = 0.0, lastPhase = 0.0;
    float age = 0.0f, held = 0.0f, target = 0.0f;
};

// ---------------------------------------------------------------------------
// Filtro de estado variable (Simper), 12 o 24 dB, con carácter
class NebFilter
{
public:
    void reset() { for (auto& s : st) s = {}; }

    void set (int kind, int slope, int character, float cutoffHz, float resonance, double sr)
    {
        type = kind;
        twoStages = slope == 1;
        charMode = character;
        const double fc = juce::jlimit (16.0, sr * 0.45, (double) cutoffHz);
        g = std::tan (juce::MathConstants<double>::pi * fc / sr);
        const double q = 0.707 * std::pow (14.0, juce::jlimit (0.0f, 1.0f, resonance));
        k = 1.0 / q;
        kFirst = twoStages ? 1.0 / 0.54 : k;   // 1ª etapa suave en 24 dB
    }

    float process (float x)
    {
        if (twoStages)
        {
            x = stage (st[0], x, kFirst);
            return stage (st[1], x, k);
        }
        return stage (st[0], x, k);
    }

    static std::complex<double> response (int kind, int slope, float cutoffHz, float resonance, float hz)
    {
        const double q = 0.707 * std::pow (14.0, juce::jlimit (0.0f, 1.0f, resonance));
        auto one = [&] (double kk) -> std::complex<double>
        {
            const std::complex<double> s (0.0, hz / cutoffHz);
            const std::complex<double> den = s * s + kk * s + 1.0;
            switch (kind)
            {
                case FilterKind::HighPass: return s * s / den;
                case FilterKind::BandPass: return kk * s / den;
                case FilterKind::Notch:    return (s * s + 1.0) / den;
                default:                   return 1.0 / den;
            }
        };
        auto h = one (1.0 / q);
        if (slope == 1) h *= one (1.0 / 0.54);
        return h;
    }

private:
    struct State { double ic1 = 0.0, ic2 = 0.0; };

    float stage (State& s, float x, double kk)
    {
        // Carácter: saturación suave a la entrada de cada etapa
        if (charMode == 1)      x = std::tanh (x * 1.4f) / 1.4f * 1.15f;
        else if (charMode == 2) x = std::tanh (x * 3.5f) / 1.6f;

        const double a1 = 1.0 / (1.0 + g * (g + kk));
        const double a2 = g * a1, a3 = g * a2;
        const double v3 = x - s.ic2;
        const double v1 = a1 * s.ic1 + a2 * v3;
        const double v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0 * v1 - s.ic1;
        s.ic2 = 2.0 * v2 - s.ic2;

        const double lp = v2, bp = v1, hp = x - kk * v1 - v2;
        switch (type)
        {
            case FilterKind::HighPass: return (float) hp;
            case FilterKind::BandPass: return (float) (bp * kk);
            case FilterKind::Notch:    return (float) (lp + hp);
            default:                   return (float) lp;
        }
    }

    std::array<State, 2> st;
    double g = 0.1, k = 1.4, kFirst = 1.4;
    int type = 0, charMode = 0;
    bool twoStages = false;
};
