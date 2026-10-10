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
// Filtro.
//  · Limpio  : SVF lineal (Butterworth en 24 dB). Transparente: ganancia 1 en la
//              banda que pasa y, totalmente abierto, deja la señal intacta.
//  · Moog    : escalera de 4 polos tipo Moog (ZDF) con saturación suave y
//              compensación de graves (no se "adelgaza" al subir la resonancia).
//  · Moog +  : la misma escalera empujada más fuerte: más color y grasa.
class NebFilter
{
public:
    enum Character { Clean = 0, Moog = 1, MoogHot = 2 };

    void reset() { for (auto& s : st) s = {}; for (auto& s : lad) s = 0.0; openMix = target; }

    void set (int kind, int slope, int character, float cutoffHz, float resonance, double sr)
    {
        type = kind;
        twoStages = slope == 1;
        charMode = juce::jlimit (0, 2, character);
        reso = juce::jlimit (0.0f, 1.0f, resonance);
        const double fc = juce::jlimit (16.0, sr * 0.45, (double) cutoffHz);
        g = std::tan (juce::MathConstants<double>::pi * fc / sr);
        k = 1.0 / qFor (reso);
        kFirst = twoStages ? 1.0 / 0.5412 : k;    // Butterworth de 4º orden: Q 0.54 y 1.31
        if (twoStages && reso < 0.02f) k = 1.0 / 1.3066;
        G = g / (1.0 + g);
        kLad = 3.96 * std::pow ((double) reso, 0.9);

        // Totalmente abierto (pasa bajos, sin resonancia): sin filtro = transparencia total
        const bool open = type == FilterKind::LowPass && cutoffHz > 17500.0f && reso < 0.05f;
        target = open ? 1.0f : 0.0f;
    }

    float process (float x)
    {
        float y = charMode == Clean ? processSvf (x) : processLadder (x);
        openMix += (target - openMix) * 0.002f;
        if (openMix > 1.0e-4f) y += (x - y) * openMix;
        return y;
    }

    // Respuesta en frecuencia (para dibujar la curva)
    static std::complex<double> response (int kind, int slope, float cutoffHz, float resonance, float hz, int character = 0)
    {
        const std::complex<double> s (0.0, hz / cutoffHz);
        if (character != Clean)
        {
            const double kl = 3.96 * std::pow ((double) juce::jlimit (0.0f, 1.0f, resonance), 0.9);
            const auto p = 1.0 / (1.0 + s);
            const auto u = 1.0 / (1.0 + kl * p * p * p * p);
            const auto y1 = u * p, y2 = y1 * p, y3 = y2 * p, y4 = y3 * p;
            return u * ladderMix (kind, slope == 1, std::complex<double> (1.0), y1 / u, y2 / u, y3 / u, y4 / u) * ladderComp (kind, kl);
        }
        const double q = qFor (resonance);
        auto one = [&] (double kk) -> std::complex<double>
        {
            const std::complex<double> den = s * s + kk * s + 1.0;
            switch (kind)
            {
                case FilterKind::HighPass: return s * s / den;
                case FilterKind::BandPass: return kk * s / den;
                case FilterKind::Notch:    return (s * s + 1.0) / den;
                default:                   return 1.0 / den;
            }
        };
        if (slope == 1)
            return one (1.0 / 0.5412) * one (resonance < 0.02f ? 1.0 / 1.3066 : 1.0 / q);
        return one (1.0 / q);
    }

private:
    static double qFor (float r) { return 0.707 * std::pow (14.0, (double) juce::jlimit (0.0f, 1.0f, r)); }

    template <typename T>
    static T ladderMix (int kind, bool fourPole, T x, T y1, T y2, T y3, T y4)
    {
        switch (kind)
        {
            case FilterKind::HighPass: return fourPole ? x - 4.0 * y1 + 6.0 * y2 - 4.0 * y3 + y4 : x - 2.0 * y1 + y2;
            case FilterKind::BandPass: return fourPole ? 4.0 * (y2 - 2.0 * y3 + y4) : 2.0 * (y1 - y2);
            case FilterKind::Notch:    return fourPole ? x - 4.0 * y1 + 6.0 * y2 - 4.0 * y3 + 2.0 * y4 : x - 2.0 * y1 + 2.0 * y2;
            default:                   return fourPole ? y4 : y2;
        }
    }

    // Compensa la pérdida de graves de la escalera al subir la resonancia
    static double ladderComp (int kind, double kl)
    {
        return (kind == FilterKind::LowPass || kind == FilterKind::BandPass || kind == FilterKind::Notch) ? 1.0 + kl : 1.0;
    }

    float processLadder (float xin)
    {
        const double drv = charMode == MoogHot ? 2.6 : 1.15;
        const double x = xin;
        // Solución sin retardo de la realimentación (lineal) y luego saturación suave
        const double S = G * G * G * (1.0 - G) * lad[0] + G * G * (1.0 - G) * lad[1] + G * (1.0 - G) * lad[2] + (1.0 - G) * lad[3];
        const double G4 = G * G * G * G;
        const double y4est = (G4 * x + S) / (1.0 + kLad * G4);
        double u = x - kLad * y4est;
        u = std::tanh (u * drv) / drv;

        double y[4], in = u;
        for (int i = 0; i < 4; ++i)
        {
            const double v = (in - lad[i]) * G;
            y[i] = v + lad[i];
            lad[i] = y[i] + v;
            if (charMode == MoogHot && i < 3) y[i] = std::tanh (y[i] * 1.2) / 1.2;   // un poco de "transistor" entre etapas
            in = y[i];
        }
        const double out = ladderMix (type, twoStages, u, y[0], y[1], y[2], y[3]) * ladderComp (type, kLad);
        return (float) (charMode == MoogHot ? out * 1.12 : out);
    }

    float processSvf (float x)
    {
        if (twoStages)
        {
            x = stage (st[0], x, kFirst);
            return stage (st[1], x, k);
        }
        return stage (st[0], x, k);
    }

    struct State { double ic1 = 0.0, ic2 = 0.0; };

    float stage (State& s, float x, double kk)
    {
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
    std::array<double, 4> lad {};
    double g = 0.1, k = 1.4, kFirst = 1.4, G = 0.09, kLad = 0.0;
    float reso = 0.0f, openMix = 0.0f, target = 0.0f;
    int type = 0, charMode = 0;
    bool twoStages = false;
};
