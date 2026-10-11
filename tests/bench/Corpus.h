#pragma once
// ===========================================================================
// Banco de pruebas de Nebula — Corpus reproducible
//
// Todos los sonidos se generan por código con semillas fijas (48 kHz), así que
// cualquier máquina obtiene exactamente las mismas muestras. Cada sonido puede
// generarse en "variantes" (otra semilla de ruido, desafinado, decaimiento más
// corto, evolución congelada, fases aleatorias) para validar las métricas.
// ===========================================================================
#include <limits>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <functional>
#include <vector>

namespace Bench
{
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846, k2Pi = 2.0 * kPi;

inline double midiHz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

// Variante de un sonido (lo que la validación de métricas cambia a propósito)
struct Variant
{
    int seed = 1;              // semilla del ruido
    double cents = 0.0;        // desafinado global
    double decay = 1.0;        // multiplica los tiempos de decaimiento
    double freezeAfter = -1.0; // s: a partir de aquí la evolución espectral se congela
    int phaseSeed = 0;         // 0 = fases fijas; otra = fases de armónicos aleatorias
};

struct Sound
{
    juce::String name, category;   // categoría: tonal / inarmonico / percusivo / pad / transitorio
    double nominalNote = -1.0;     // nota MIDI real (-1 = sin nota)
    bool pitched = false;
    juce::AudioBuffer<float> audio;        // lo que se le da a Nebula
    juce::AudioBuffer<float> transient;    // verdad de terreno: sólo el transitorio (mono, puede estar vacío)
    juce::AudioBuffer<float> tonal;        // verdad de terreno: sólo la parte tonal (mono, puede estar vacío)
    double trueAttackMs = -1.0;            // duración real del transitorio (si se conoce)
    juce::String notes;                    // qué pone a prueba
};

// ---------- utilidades de síntesis ----------
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double f, double q)  { set (f, q, 0); }
    void highpass (double f, double q) { set (f, q, 1); }
    void bandpass (double f, double q) { set (f, q, 2); }
    void set (double f, double q, int type)
    {
        f = juce::jlimit (10.0, kSr * 0.49, f);
        const double w = k2Pi * f / kSr, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        double B0, B1, B2;
        if (type == 0)      { B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; }
        else if (type == 1) { B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; }
        else                { B0 = al; B1 = 0; B2 = -al; }
        const double A0 = 1 + al;
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = -2 * c / A0; a2 = (1 - al) / A0;
    }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Sierra con PolyBLEP (sin aliasing audible)
struct BlepSaw
{
    double ph = 0.0;
    static double blep (double t, double dt)
    {
        if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    double next (double hz)
    {
        const double dt = hz / kSr;
        double v = 2.0 * ph - 1.0 - blep (ph, dt);
        ph += dt; if (ph >= 1.0) ph -= 1.0;
        return v;
    }
};

inline double smoothstep (double x) { x = juce::jlimit (0.0, 1.0, x); return x * x * (3 - 2 * x); }

inline double noise (juce::Random& r) { return r.nextDouble() * 2.0 - 1.0; }

// Fase inicial del armónico k (0 si la variante no pide fases aleatorias)
inline double phaseOf (const Variant& v, int k)
{
    if (v.phaseSeed == 0) return 0.0;
    juce::Random r (v.phaseSeed * 7919 + k * 104729);
    return r.nextDouble() * k2Pi;
}

inline juce::AudioBuffer<float> mono (double seconds) { juce::AudioBuffer<float> b (1, (int) (seconds * kSr)); b.clear(); return b; }

// ===========================================================================
// Sonidos
// ===========================================================================
inline Sound pluck (const Variant& v)
{
    Sound s; s.name = "pluck_sierra"; s.category = "tonal"; s.nominalNote = 48; s.pitched = true;
    s.notes = "Cuerda pulsada: ataque de 2 ms, los agudos decaen antes que los graves";
    s.audio = mono (1.6);
    juce::Random r (v.seed);
    Biquad lp; lp.lowpass (3000, 0.7);
    const double f = midiHz (48 + v.cents / 100.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        double x = 0.0;
        for (int k = 1; k < 80 && k * f < 20000; ++k)
            x += std::sin (k2Pi * k * f * t + phaseOf (v, k)) / k * std::exp (-t * (1.5 + 0.35 * k) / v.decay);
        const double nz = lp.process (noise (r)) * 0.5 * std::exp (-t * 60.0 / v.decay);
        s.audio.setSample (0, i, (float) (0.32 * (x * juce::jmin (1.0, t / 0.002) + nz)));
    }
    return s;
}

inline Sound padVibrato (const Variant& v)
{
    Sound s; s.name = "pad_vibrato"; s.category = "tonal"; s.nominalNote = 55; s.pitched = true;
    s.notes = "Onda cuadrada con vibrato de 20 ct a 5.5 Hz; ataque 0.3 s";
    s.audio = mono (2.5);
    const double f0 = midiHz (55 + v.cents / 100.0);
    double ph = 0.0;
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double f = f0 * std::exp2 (20.0 / 1200.0 * std::sin (k2Pi * 5.5 * t));
        ph += f / kSr;
        double x = 0.0;
        for (int k = 1; k < 60 && k * f < 20000; k += 2) x += std::sin (k2Pi * k * ph + phaseOf (v, k)) / k;
        const double env = smoothstep (t / 0.3) * (t > 2.1 ? juce::jmax (0.0, (2.5 - t) / (0.4 * v.decay)) : 1.0);
        s.audio.setSample (0, i, (float) (0.33 * x * juce::jmin (1.0, env)));
    }
    return s;
}

inline Sound voice (const Variant& v)
{
    Sound s; s.name = "voz_a"; s.category = "tonal"; s.nominalNote = 57; s.pitched = true;
    s.notes = "Vocal 'a' con formantes 800/1200 Hz, vibrato leve y aliento";
    s.audio = mono (1.8);
    juce::Random r (v.seed);
    Biquad br; br.bandpass (2500, 0.8);
    const double f0 = midiHz (57 + v.cents / 100.0);
    double ph = 0.0;
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double f = f0 * (1.0 + 0.004 * std::sin (k2Pi * 5.0 * t));
        ph += f / kSr;
        double x = 0.0;
        for (int h = 1; h < 40 && h * f < 20000; ++h)
        {
            const double fh = h * f;
            x += (1.0 / h) * (1.0 + 4.0 * std::exp (-std::pow ((fh - 800.0) / 150.0, 2)) + 3.0 * std::exp (-std::pow ((fh - 1200.0) / 200.0, 2)))
                 * std::sin (k2Pi * h * ph + phaseOf (v, h));
        }
        const double env = juce::jmin (1.0, t / 0.05) * (t > 1.6 ? juce::jmax (0.0, (1.8 - t) / (0.2 * v.decay)) : 1.0);
        s.audio.setSample (0, i, (float) (0.14 * x * env + 0.02 * br.process (noise (r)) * env));
    }
    return s;
}

// Piano sintético: armónicos estirados (inarmonicidad B), martillo como transitorio separado
inline Sound piano (const Variant& v)
{
    Sound s; s.name = "piano_sint"; s.category = "tonal"; s.nominalNote = 60; s.pitched = true;
    s.notes = "Armonicos estirados (B=0.0004) + golpe de martillo de ~28 ms (verdad de terreno separada)";
    s.audio = mono (3.0); s.transient = mono (3.0); s.tonal = mono (3.0);
    s.trueAttackMs = 28.0;
    juce::Random r (v.seed);
    Biquad bp; bp.bandpass (2500, 0.7);
    Biquad th; th.lowpass (150, 0.7);
    const double f = midiHz (60 + v.cents / 100.0), B = 0.0004;
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        double x = 0.0;
        for (int k = 1; k <= 40; ++k)
        {
            const double fk = k * f * std::sqrt (1.0 + B * k * k);
            if (fk > 20000) break;
            const double tau = 2.5 / (1.0 + 0.15 * k) * v.decay;
            x += std::pow (k, -1.2) * std::exp (-t / tau) * std::sin (k2Pi * fk * t + phaseOf (v, k));
        }
        x *= 0.3 * juce::jmin (1.0, t / 0.001);
        const double e = std::exp (-t / 0.006);
        const double tr = 0.25 * e * (bp.process (noise (r)) * 1.5 + th.process (noise (r)) * 3.0);
        s.tonal.setSample (0, i, (float) x);
        s.transient.setSample (0, i, (float) tr);
        s.audio.setSample (0, i, (float) (x + tr));
    }
    return s;
}

inline Sound bowed (const Variant& v)
{
    Sound s; s.name = "cuerda_frotada"; s.category = "tonal"; s.nominalNote = 50; s.pitched = true;
    s.notes = "Ataque lento de 400 ms (no es transitorio), brillo que crece, vibrato despues de 1 s";
    s.trueAttackMs = 0.0;   // sin transitorio: el ataque es envolvente
    s.audio = mono (3.0);
    juce::Random r (v.seed);
    Biquad bp; bp.bandpass (1500, 1.0);
    const double f0 = midiHz (50 + v.cents / 100.0);
    double ph = 0.0;
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double a = smoothstep (t / 0.4);
        const double vib = t > 1.0 ? 15.0 * std::sin (k2Pi * 5.0 * t) * smoothstep ((t - 1.0) / 0.5) : 0.0;
        const double f = f0 * std::exp2 (vib / 1200.0);
        ph += f / kSr;
        const double brightT = (v.freezeAfter >= 0 ? juce::jmin (t, v.freezeAfter) : t);
        const double bright = 3.0 + 12.0 * smoothstep (brightT / 0.8);
        double x = 0.0;
        for (int k = 1; k < 70 && k * f < 20000; ++k) x += std::sin (k2Pi * k * ph + phaseOf (v, k)) / k * std::exp (-k / bright);
        const double rel = t > 2.6 ? juce::jmax (0.0, (3.0 - t) / (0.4 * v.decay)) : 1.0;
        s.audio.setSample (0, i, (float) ((0.3 * x + 0.015 * bp.process (noise (r))) * a * rel));
    }
    return s;
}

inline Sound bell (const Variant& v)
{
    Sound s; s.name = "campana"; s.category = "inarmonico"; s.nominalNote = 64; s.pitched = true;
    s.notes = "Parciales de campana (0.5, 1, 1.2, 1.5, 2, 2.5, 2.61, 3, 4, 5.33) con decaimientos distintos";
    s.audio = mono (3.0);
    juce::Random r (v.seed);
    const double f = midiHz (64 + v.cents / 100.0);
    const double ratio[] { 0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 2.61, 3.0, 4.0, 5.33 };
    const double amp[]   { 0.5, 1.0, 0.6, 0.4, 0.7, 0.3, 0.25, 0.2, 0.15, 0.1 };
    const double tau[]   { 2.5, 1.8, 1.2, 1.0, 0.9, 0.5, 0.45, 0.35, 0.25, 0.15 };
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        double x = 0.0;
        for (int k = 0; k < 10; ++k) x += amp[k] * std::exp (-t / (tau[k] * v.decay)) * std::sin (k2Pi * f * ratio[k] * t + phaseOf (v, k + 1));
        const double click = noise (r) * std::exp (-t / 0.0008);
        s.audio.setSample (0, i, (float) (0.22 * x * juce::jmin (1.0, t / 0.001) + 0.15 * click));
    }
    return s;
}

inline Sound gong (const Variant& v)
{
    Sound s; s.name = "gong_metal"; s.category = "inarmonico"; s.nominalNote = -1; s.pitched = false;
    s.notes = "16 parciales inarmonicos con batidos y 'florecimiento' de agudos en 0.5 s";
    s.audio = mono (3.5);
    juce::Random structure (77);   // la estructura es fija; la variante sólo cambia el ruido
    struct P { double hz, beat, amp, tau, bloom; };
    std::vector<P> ps;
    for (int k = 0; k < 16; ++k)
        ps.push_back ({ 110.0 * (1.0 + 8.0 * structure.nextDouble()), 0.3 + 1.2 * structure.nextDouble(),
                        0.3 + 0.7 * structure.nextDouble(), 1.0 + 3.0 * structure.nextDouble(), structure.nextDouble() });
    juce::Random r (v.seed);
    Biquad lp; lp.lowpass (400, 0.7);
    const double c = std::exp2 (v.cents / 1200.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double bt = v.freezeAfter >= 0 ? juce::jmin (t, v.freezeAfter) : t;
        double x = 0.0;
        for (size_t k = 0; k < ps.size(); ++k)
        {
            const auto& p = ps[k];
            const double grow = 1.0 - p.bloom * std::exp (-bt / 0.5);
            x += p.amp * grow * std::exp (-t / (p.tau * v.decay)) * 0.5
                 * (std::sin (k2Pi * p.hz * c * t + phaseOf (v, (int) k + 1)) + std::sin (k2Pi * (p.hz * c + p.beat) * t));
        }
        const double thump = lp.process (noise (r)) * std::exp (-t / 0.02) * 2.0;
        s.audio.setSample (0, i, (float) (0.09 * x * juce::jmin (1.0, t / 0.002) + 0.2 * thump));
    }
    return s;
}

inline Sound kick808 (const Variant& v)
{
    Sound s; s.name = "bombo_808"; s.category = "percusivo"; s.nominalNote = -1; s.pitched = false;
    s.notes = "Barrido de tono 165 -> 45 Hz en ~30 ms, decaimiento largo";
    s.audio = mono (1.2);
    double ph = 0.0;
    const double c = std::exp2 (v.cents / 1200.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double f = (45.0 + 120.0 * std::exp (-t * 30.0)) * c;
        ph += f / kSr;
        s.audio.setSample (0, i, (float) (0.8 * std::sin (k2Pi * ph) * std::exp (-t * 2.5 / v.decay) * juce::jmin (1.0, t / 0.001)));
    }
    return s;
}

inline Sound snare (const Variant& v)
{
    Sound s; s.name = "caja"; s.category = "percusivo"; s.nominalNote = -1; s.pitched = false;
    s.notes = "Modos de 185/330 Hz + bordonera (ruido) 150 ms + golpe de baqueta 2 ms";
    s.audio = mono (0.8);
    juce::Random r (v.seed);
    Biquad hp; hp.highpass (1200, 0.7);
    Biquad bp; bp.bandpass (5000, 0.6);
    const double c = std::exp2 (v.cents / 1200.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double tone = (std::sin (k2Pi * 185 * c * t) + 0.6 * std::sin (k2Pi * 330 * c * t)) * std::exp (-t / (0.08 * v.decay));
        const double n = noise (r);
        const double wires = (hp.process (n) * 0.7 + bp.process (n) * 0.6) * std::exp (-t / (0.15 * v.decay));
        const double stick = n * std::exp (-t / 0.0005);
        s.audio.setSample (0, i, (float) (0.35 * tone + 0.45 * wires + 0.3 * stick));
    }
    return s;
}

inline Sound noiseHit (const Variant& v)
{
    Sound s; s.name = "golpe_ruido"; s.category = "percusivo"; s.nominalNote = -1; s.pitched = false;
    s.notes = "Ruido filtrado (paso bajo de un polo) con decaimiento de 120 ms";
    s.audio = mono (0.6);
    juce::Random r (v.seed);
    double lp = 0.0;
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        lp += 0.3 * (noise (r) - lp);
        s.audio.setSample (0, i, (float) (1.2 * lp * std::exp (-t / (0.12 * v.decay))));
    }
    return s;
}

inline Sound hihat (const Variant& v)
{
    Sound s; s.name = "hihat"; s.category = "percusivo"; s.nominalNote = -1; s.pitched = false;
    s.notes = "Ruido agudo (7-10 kHz) con decaimiento de 40 ms";
    s.audio = mono (0.4);
    juce::Random r (v.seed);
    Biquad hp; hp.highpass (7000, 0.7);
    Biquad bp; bp.bandpass (10000, 1.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double n = noise (r);
        s.audio.setSample (0, i, (float) ((0.6 * hp.process (n) + 0.5 * bp.process (n)) * std::exp (-t / (0.04 * v.decay))));
    }
    return s;
}

// Pad de 12 s: tres sierras desafinadas con un filtro que se mueve todo el tiempo
inline Sound padEvolving (const Variant& v)
{
    Sound s; s.name = "pad_evolutivo_12s"; s.category = "pad"; s.nominalNote = 48; s.pitched = true;
    s.notes = "12 s: filtro 250->4000->600->1500 Hz; la evolucion espectral no se detiene";
    s.audio = mono (12.0);
    BlepSaw saw[3];
    for (int k = 0; k < 3; ++k) saw[k].ph = 0.31 * k + (v.phaseSeed ? 0.1 * v.phaseSeed : 0.0);
    Biquad lp1, lp2;
    const double f0 = midiHz (48 + v.cents / 100.0);
    const double det[] { -7.0, 0.0, 7.0 };
    auto cutoffAt = [] (double t)
    {
        auto seg = [] (double a, double b, double x) { return a * std::pow (b / a, smoothstep (x)); };
        if (t < 4.0) return seg (250.0, 4000.0, t / 4.0);
        if (t < 8.0) return seg (4000.0, 600.0, (t - 4.0) / 4.0);
        return seg (600.0, 1500.0, (t - 8.0) / 4.0);
    };
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        if ((i & 15) == 0)
        {
            const double ft = v.freezeAfter >= 0 ? juce::jmin (t, v.freezeAfter) : t;
            const double fc = cutoffAt (ft);
            lp1.lowpass (fc, 0.9); lp2.lowpass (fc, 0.6);
        }
        double x = 0.0;
        for (int k = 0; k < 3; ++k) x += saw[k].next (f0 * std::exp2 (det[k] / 1200.0));
        x = lp2.process (lp1.process (x / 3.0));
        const double env = smoothstep (t / 0.8) * (t > 10.5 ? juce::jmax (0.0, (12.0 - t) / (1.5 * v.decay)) : 1.0);
        s.audio.setSample (0, i, (float) (0.6 * x * env));
    }
    return s;
}

// Pad vocal de 8 s: los formantes pasan de 'a' a 'o' a 'i' y vuelven
inline Sound padVocal (const Variant& v)
{
    Sound s; s.name = "pad_vocal_8s"; s.category = "pad"; s.nominalNote = 45; s.pitched = true;
    s.notes = "8 s: formantes a -> o -> i -> a con aliento; prueba evolucion lenta del color";
    s.audio = mono (8.0);
    juce::Random r (v.seed);
    BlepSaw src;
    Biquad f1, f2, f3, breath;
    breath.bandpass (3000, 0.5);
    const double f0 = midiHz (45 + v.cents / 100.0);
    struct V { double a, b, c; };
    const V vow[] { { 800, 1200, 2500 }, { 500, 900, 2400 }, { 300, 2300, 3000 }, { 800, 1200, 2500 } };
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        if ((i & 15) == 0)
        {
            const double ft = v.freezeAfter >= 0 ? juce::jmin (t, v.freezeAfter) : t;
            const double pos = juce::jlimit (0.0, 2.999, ft / 8.0 * 3.0);
            const int k = (int) pos; const double w = smoothstep (pos - k);
            auto mix = [&] (double a, double b) { return a + (b - a) * w; };
            f1.bandpass (mix (vow[k].a, vow[k + 1].a), 6.0);
            f2.bandpass (mix (vow[k].b, vow[k + 1].b), 8.0);
            f3.bandpass (mix (vow[k].c, vow[k + 1].c), 10.0);
        }
        const double e = src.next (f0 * (1.0 + 0.003 * std::sin (k2Pi * 4.5 * t)));
        const double x = 1.0 * f1.process (e) + 0.7 * f2.process (e) + 0.35 * f3.process (e) + 0.03 * breath.process (noise (r));
        const double env = smoothstep (t / 0.6) * (t > 7.0 ? juce::jmax (0.0, (8.0 - t) / (1.0 * v.decay)) : 1.0);
        s.audio.setSample (0, i, (float) (0.9 * x * env));
    }
    return s;
}

inline Sound padStereo (const Variant& v)
{
    Sound s; s.name = "pad_estereo_4s"; s.category = "pad"; s.nominalNote = 48; s.pitched = true;
    s.notes = "Estereo real: izquierda y derecha con desafinados distintos (imagen ancha)";
    s.audio = juce::AudioBuffer<float> (2, (int) (4.0 * kSr));
    const double f = midiHz (48 + v.cents / 100.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        double l = 0, rr = 0;
        for (int k = 1; k < 30 && k * f < 20000; ++k)
        {
            const double d = 0.002 * (k % 3);
            l  += std::sin (k2Pi * k * f * (1.0 + d) * t + phaseOf (v, k)) / k;
            rr += std::sin (k2Pi * k * f * (1.0 - d) * t + phaseOf (v, k) + 0.7 * k) / k;
        }
        const double env = smoothstep (t / 0.4) * (t > 3.6 ? juce::jmax (0.0, (4.0 - t) / (0.4 * v.decay)) : 1.0);
        s.audio.setSample (0, i, (float) (0.22 * l * env));
        s.audio.setSample (1, i, (float) (0.22 * rr * env));
    }
    return s;
}

// Tono + transitorio de duración conocida (0, 2, 10, 50, 150, 400 ms)
inline Sound transientCase (double ms, const Variant& v)
{
    Sound s; s.name = "transitorio_" + juce::String ((int) ms) + "ms"; s.category = "transitorio";
    s.nominalNote = 60; s.pitched = true; s.trueAttackMs = ms;
    s.notes = "Tono C4 + ruido de " + juce::String ((int) ms) + " ms (verdad de terreno separada)";
    s.audio = mono (1.5); s.transient = mono (1.5); s.tonal = mono (1.5);
    juce::Random r (v.seed);
    Biquad bp; bp.bandpass (3000, 0.5);
    const double f = midiHz (60 + v.cents / 100.0);
    const double tau = ms / 1000.0 / 4.6;   // -40 dB al final de la duración
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        double x = 0.0;
        for (int k = 1; k <= 20 && k * f < 20000; ++k) x += std::pow (k, -1.5) * std::sin (k2Pi * k * f * t + phaseOf (v, k));
        x *= 0.35 * std::exp (-t / (3.0 * v.decay)) * juce::jmin (1.0, t / 0.001);
        const double tr = ms > 0.0 ? 0.5 * bp.process (noise (r)) * std::exp (-t / tau) : 0.0;
        s.tonal.setSample (0, i, (float) x);
        s.transient.setSample (0, i, (float) tr);
        s.audio.setSample (0, i, (float) (x + tr));
    }
    return s;
}


// ---- Bajos 808 (prioridad crítica): sub con caída de tono, click, saturación, glide ----
// tipo: 0 limpio, 1 saturado, 2 glide de G1 a C1 a los 0.6 s, 3 largo (4 s) distorsionado con cola
inline Sound bass808 (int type, const Variant& v)
{
    const char* names[] { "808_limpio", "808_saturado", "808_glide", "808_largo_dist" };
    const double notes[] { 29.0, 31.0, 31.0, 26.0 };   // F1, G1, G1->C1, D1
    Sound s; s.name = names[type]; s.category = "808"; s.nominalNote = notes[type]; s.pitched = true;
    s.notes = type == 0 ? "Sub senoidal F1 (43.7 Hz), caida de tono +12 st en 40 ms, click, decaimiento 2.5 s"
            : type == 1 ? "Igual pero saturado (tanh x4): armonicos impares sobre el sub, G1"
            : type == 2 ? "G1 con glide a C1 entre 0.6 y 0.8 s (como en un patron de trap)"
                        : "D1 de 4 s con distorsion suave, vibrato leve y cola larga";
    const double len = type == 3 ? 4.0 : 2.5;
    s.audio = mono (len); s.transient = mono (len); s.tonal = mono (len);
    s.trueAttackMs = 5.0;
    juce::Random r (v.seed);
    Biquad hp; hp.highpass (1500, 0.7);
    double ph = 0.0;
    const double c = v.cents / 100.0;
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        double note = notes[type] + c;
        if (type == 2) note = 31.0 + c - 7.0 * smoothstep ((t - 0.6) / 0.2);
        if (type == 3) note += 0.15 * std::sin (k2Pi * 4.0 * t) * smoothstep ((t - 1.0) / 1.0);
        const double drop = 12.0 * std::exp (-t / 0.012);           // caída de tono típica del 808
        ph += midiHz (note + drop) / kSr;
        double x = std::sin (k2Pi * (ph + phaseOf (v, 1) / k2Pi));
        const double env = std::exp (-t / ((type == 3 ? 1.6 : 0.9) * v.decay)) * juce::jmin (1.0, t / 0.002);
        x *= env;
        if (type == 1) x = std::tanh (4.0 * x) / std::tanh (4.0);
        if (type == 3) x = std::tanh (2.2 * x) / std::tanh (2.2);
        const double click = hp.process (noise (r)) * std::exp (-t / 0.0012) * 0.6;
        s.tonal.setSample (0, i, (float) (0.75 * x));
        s.transient.setSample (0, i, (float) click);
        s.audio.setSample (0, i, (float) (0.75 * x + click));
    }
    return s;
}

inline Sound lead (const Variant& v)
{
    Sound s; s.name = "lead_sierra"; s.category = "tonal"; s.nominalNote = 69; s.pitched = true;
    s.notes = "Lead de sierras desafinadas (A4) con vibrato tardio y filtro que se abre";
    s.audio = mono (2.0);
    BlepSaw a, b; b.ph = 0.37;
    Biquad lp;
    const double f0 = midiHz (69 + v.cents / 100.0);
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
    {
        const double t = i / kSr;
        const double vib = 25.0 * std::sin (k2Pi * 5.5 * t) * smoothstep ((t - 0.5) / 0.4);
        const double f = f0 * std::exp2 (vib / 1200.0);
        if ((i & 15) == 0) lp.lowpass (900.0 + 4000.0 * smoothstep (t / (0.35 * v.decay)), 1.2);
        const double x = lp.process (0.5 * (a.next (f * std::exp2 (6.0 / 1200.0)) + b.next (f * std::exp2 (-6.0 / 1200.0))));
        const double env = juce::jmin (1.0, t / 0.01) * (t > 1.7 ? juce::jmax (0.0, (2.0 - t) / 0.3) : 1.0);
        s.audio.setSample (0, i, (float) (0.45 * x * env));
    }
    return s;
}

using Maker = std::function<Sound (const Variant&)>;

struct Entry { juce::String name; Maker make; };

inline std::vector<Entry> corpus()
{
    std::vector<Entry> c {
        { "pluck_sierra",      pluck },
        { "lead_sierra",       lead },
        { "808_limpio",        [] (const Variant& v) { return bass808 (0, v); } },
        { "808_saturado",      [] (const Variant& v) { return bass808 (1, v); } },
        { "808_glide",         [] (const Variant& v) { return bass808 (2, v); } },
        { "808_largo_dist",    [] (const Variant& v) { return bass808 (3, v); } },
        { "pad_vibrato",       padVibrato },
        { "voz_a",             voice },
        { "piano_sint",        piano },
        { "cuerda_frotada",    bowed },
        { "campana",           bell },
        { "gong_metal",        gong },
        { "bombo_808",         kick808 },
        { "caja",              snare },
        { "golpe_ruido",       noiseHit },
        { "hihat",             hihat },
        { "pad_evolutivo_12s", padEvolving },
        { "pad_vocal_8s",      padVocal },
        { "pad_estereo_4s",    padStereo },
    };
    for (double ms : { 0.0, 2.0, 10.0, 50.0, 150.0, 400.0 })
        c.push_back ({ "transitorio_" + juce::String ((int) ms) + "ms", [ms] (const Variant& v) { return transientCase (ms, v); } });
    return c;
}
} // namespace Bench
