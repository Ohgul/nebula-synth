#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// ===========================================================================
// COSMOS (+) y ABISMO (-): dos transformaciones opuestas del sonido de cada voz.
//
// Lo estructural vive en dos lugares:
//   - Dna::makeVariant  -> el espectro del modelo recreado (Cosmos: expansión con ecos de octava y quinta,
//                          difuminado y fases abiertas; Abismo: fragmentación del espectro en el tiempo,
//                          pliegue de armónicos y deformación de niveles).
//   - ResynthPlayer     -> cómo se recorre el modelo (Cosmos: más lento, deriva de posición y una capa
//                          fantasma una octava arriba abierta en estéreo; Abismo: saltos en el tiempo).
// Esta clase hace lo que funciona para cualquier motor (osciladores, sample o resíntesis):
//   Cosmos: deriva de afinación lenta, difusión estéreo modulada, respiración espectral y textura de aire.
//   Abismo: caos de afinación por pasos, modulación en anillo inarmónica, distorsión asimétrica con
//           pliegue, fragmentación temporal (repeticiones y reversa), reducción de resolución.
// Todo el azar sale de una semilla por nota: el mismo proyecto suena igual en cada render.
// Protección: control automático de nivel, limitador suave, bloqueo de continua y reinicio si aparece
// un valor no finito. "Identidad" mezcla la señal sin transformar; "Salida" ajusta el nivel final.
// ===========================================================================
namespace Transform
{
    struct Amounts
    {
        float cosmos = 0.0f, abyss = 0.0f, identity = 0.0f, outGain = 1.0f;
        bool active() const { return cosmos > 1.0e-3f || abyss > 1.0e-3f; }
    };

    inline Amounts make (float transform, float identity, float outDb)
    {
        Amounts a;
        if (std::abs (transform) < 1.0e-3f) return a;
        a.cosmos = juce::jlimit (0.0f, 1.0f, transform);
        a.abyss = juce::jlimit (0.0f, 1.0f, -transform);
        a.identity = juce::jlimit (0.0f, 1.0f, identity);
        a.outGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 6.0f, outDb));
        return a;
    }
}

class TransformVoice
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        frag.assign ((size_t) (sr * 0.6) + 4, {});
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < numAp; ++s)
                ap[(size_t) c][(size_t) s].buf.assign ((size_t) (sr * 0.05) + 4, 0.0f);
        dcCoef = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 12.0 / sr);
        agcCoef = (float) std::exp (-1.0 / (0.08 * sr));
        reset (1);
    }

    // Al empezar cada nota (la semilla viene de la semilla de la nota: render reproducible)
    void reset (juce::int64 seed)
    {
        rng.setSeed (seed);
        if (dirty)   // los búferes sólo se limpian si se usaron (al empezar cada nota, en el hilo de audio)
        {
            for (auto& ch : ap) for (auto& a : ch) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
            std::fill (frag.begin(), frag.end(), std::array<float, 2> {});
            dirty = false;
        }
        fragW = 0; fragMode = false; fragLeft = (int) (0.05 * sr); fragRead = 0.0; fragLen = 1; fragDir = 1; fragFadeIn = 0;
        lfo = { rng.nextDouble(), rng.nextDouble(), rng.nextDouble(), rng.nextDouble() };
        chaosTarget = chaosNow = 0.0f; chaosTimer = 0.0f;
        static const float ratios[] { 1.4142f, 1.618f, 2.718f * 0.5f, 3.1416f * 0.5f, 1.7321f, 2.2361f };
        ringRatio = ratios[rng.nextInt (6)];
        ringPhase = 0.0; noteHz = 220.0;
        dcInL = dcInR = dcOutL = dcOutR = 0.0f;
        lpL = lpR = 0.0f; tiltL = tiltR = 0.0f;
        agcIn = agcOut = 1.0e-4f;
        crushHold = 0; crushL = crushR = 0.0f;
        airState = 0.0f; envFollow = 0.0f;
        agcGain = 1.0f;
        active = false;
    }

    // Una vez por bloque de control (16 muestras). Devuelve el desvío de afinación en semitonos.
    float control (const Transform::Amounts& a, double hz, int samples)
    {
        amt = a;
        active = a.active();
        noteHz = hz;
        if (! active) return 0.0f;
        const double dt = samples / sr;
        // LFOs lentos (fase inicial distinta en cada nota)
        static constexpr double rates[] { 0.09, 0.23, 0.13, 0.071 };
        for (size_t i = 0; i < lfo.size(); ++i) { lfo[i] += rates[i] * dt; lfo[i] -= std::floor (lfo[i]); }
        float semis = 0.0f;
        if (a.cosmos > 0.0f)
            semis += a.cosmos * 0.07f * (float) (std::sin (juce::MathConstants<double>::twoPi * lfo[0]) + 0.5 * std::sin (juce::MathConstants<double>::twoPi * lfo[1]));
        if (a.abyss > 0.0f)
        {
            // caos por pasos: saltos de afinación cada 40-250 ms (más seguidos y grandes con más Abismo)
            if ((chaosTimer -= (float) dt) <= 0.0f)
            {
                chaosTimer = (0.04f + 0.21f * rng.nextFloat()) * (1.0f - 0.6f * a.abyss);
                const float r = rng.nextFloat();
                const float range = a.abyss * a.abyss * 12.0f;
                chaosTarget = r < 0.45f ? 0.0f : r < 0.6f ? -range : r < 0.7f ? range * 0.58f : (rng.nextFloat() * 2.0f - 1.0f) * range;
            }
            chaosNow += (chaosTarget - chaosNow) * (float) juce::jmin (1.0, dt / 0.012);
            semis += chaosNow;
        }
        return semis * (1.0f - a.identity);
    }

    void process (float& L, float& R)
    {
        if (! active) return;
        dirty = true;
        const float dryL = L, dryR = R;
        const float c = amt.cosmos, a = amt.abyss;
        float l = L, r = R;

        // nivel de entrada (para que la transformación no cambie el volumen de golpe)
        agcIn = agcCoef * agcIn + (1.0f - agcCoef) * (l * l + r * r);

        if (c > 0.0f)
        {
            // Respiración espectral: el balance graves/agudos se mece lentamente
            const float sway = c * 0.45f * (float) std::sin (juce::MathConstants<double>::twoPi * lfo[2]);
            lpL += 0.08f * (l - lpL); lpR += 0.08f * (r - lpR);
            l = lpL * (1.0f - sway) + (l - lpL) * (1.0f + sway);
            r = lpR * (1.0f - sway) + (r - lpR) * (1.0f + sway);
            // Difusión estéreo: allpass encadenados con retardos distintos en cada lado y modulados
            const float g = 0.35f + 0.35f * c;
            const float mod = (float) std::sin (juce::MathConstants<double>::twoPi * lfo[3]);
            // (se difunde el centro; los lados nuevos salen de la diferencia entre las dos difusiones,
            //  así el resultado sigue sonando bien en mono)
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            float wl = mid, wr = mid;
            for (int s = 0; s < numAp; ++s)
            {
                wl = ap[0][(size_t) s].process (wl, g, apTime (0, s, mod));
                wr = ap[1][(size_t) s].process (wr, g, apTime (1, s, -mod));
            }
            const float w = 0.5f * c;
            const float newMid = mid * (1.0f - w) + 0.5f * (wl + wr) * w;
            float newSide = side * (1.0f + 0.3f * c) + 0.5f * (wl - wr) * w * 0.6f;
            // Textura de aire: ruido suave que sigue la envolvente del sonido
            envFollow += 0.001f * (std::abs (dryL) + std::abs (dryR) - envFollow);
            airState += 0.25f * ((rng.nextFloat() * 2.0f - 1.0f) - airState);
            const float air = (rng.nextFloat() * 2.0f - 1.0f - airState) * envFollow * 0.05f * c * c;
            newSide += air * 0.5f;
            l = newMid + newSide + air * 0.5f; r = newMid - newSide + air * 0.5f;
        }

        if (a > 0.0f)
        {
            // Fragmentación temporal: se graba siempre; a veces se repite un trozo reciente (o al revés)
            fragment (l, r, a);
            // Anillo inarmónico (proporción fija por nota)
            const float ring = juce::jlimit (0.0f, 1.0f, (a - 0.15f) / 0.6f) * 0.6f;
            if (ring > 0.0f)
            {
                ringPhase += noteHz * ringRatio / sr;
                ringPhase -= std::floor (ringPhase);
                const float m = (float) std::sin (juce::MathConstants<double>::twoPi * ringPhase);
                l = l * (1.0f - ring) + l * m * ring * 1.4f;
                r = r * (1.0f - ring) + r * m * ring * 1.4f;
            }
            // Distorsión asimétrica controlada (+ pliegue a partir de la mitad)
            const float drive = 1.0f + 18.0f * a * a;
            const float bias = 0.25f * a;
            auto shape = [&] (float x)
            {
                float y = std::tanh (drive * x + bias) - std::tanh (bias);
                if (a > 0.5f) y = std::sin (y * juce::MathConstants<float>::halfPi * (1.0f + 3.0f * (a - 0.5f)));
                return y;
            };
            l = shape (l); r = shape (r);
            // Reducción de resolución (bits y frecuencia) en el último tramo
            if (a > 0.6f)
            {
                const int hold = 1 + (int) ((a - 0.6f) * 15.0f);
                if (--crushHold <= 0) { crushHold = hold; crushL = l; crushR = r; }
                const float steps = std::exp2 (12.0f - 16.0f * (a - 0.6f));
                l = std::round (crushL * steps) / steps; r = std::round (crushR * steps) / steps;
            }
        }

        // Bloqueo de continua
        const float ol = l - dcInL + dcCoef * dcOutL; dcInL = l; dcOutL = ol;
        const float orr = r - dcInR + dcCoef * dcOutR; dcInR = r; dcOutR = orr;
        l = ol; r = orr;

        // Control de nivel: la salida sigue el nivel de la entrada (Abismo puede subir hasta +3 dB)
        agcOut = agcCoef * agcOut + (1.0f - agcCoef) * (l * l + r * r);
        const float target = std::sqrt ((agcIn + 1.0e-9f) / (agcOut + 1.0e-9f)) * (1.0f + 0.4f * a);
        agcGain += (juce::jlimit (0.05f, 4.0f, target) - agcGain) * 0.002f;
        l *= agcGain; r *= agcGain;

        // Identidad: cuánto de la señal original se conserva
        const float id = amt.identity;
        l = l * (1.0f - id) + dryL * id;
        r = r * (1.0f - id) + dryR * id;
        l *= amt.outGain; r *= amt.outGain;

        // Protección: valores no finitos -> silencio y estado limpio; picos -> limitador suave
        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            const auto keep = amt;
            reset (rng.nextInt64());
            amt = keep; active = true;
            l = r = 0.0f;
        }
        L = softLimit (l); R = softLimit (r);
    }

    static float softLimit (float x)
    {
        const float ax = std::abs (x);
        if (ax <= 0.8f) return x;
        return std::copysign (0.8f + 0.2f * std::tanh ((ax - 0.8f) / 0.2f), x);   // nunca pasa de 1
    }

private:
    static constexpr int numAp = 3;
    struct Allpass
    {
        std::vector<float> buf; int pos = 0;
        float process (float x, float g, float delaySamples)
        {
            const int n = (int) buf.size();
            const float rp = (float) pos - delaySamples;
            const float fp = rp < 0 ? rp + (float) n : rp;
            const int i0 = (int) fp; const float fr = fp - (float) i0;
            const float d = buf[(size_t) i0 % (size_t) n] * (1.0f - fr) + buf[(size_t) (i0 + 1) % (size_t) n] * fr;
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            pos = (pos + 1) % n;
            return d - g * v;
        }
    };
    float apTime (int ch, int stage, float mod) const
    {
        static constexpr float ms[2][numAp] { { 7.1f, 11.3f, 17.9f }, { 8.3f, 12.7f, 19.1f } };
        return (float) ((ms[ch][stage] + 0.6f * mod) * 0.001 * sr);
    }

    void fragment (float& l, float& r, float a)
    {
        const int n = (int) frag.size();
        frag[(size_t) fragW] = { l, r };
        fragW = (fragW + 1) % n;
        if (--fragLeft <= 0)
        {
            // nuevo tramo: seguir normal o repetir un trozo (2-6 veces, a veces al revés)
            if (! fragMode && rng.nextFloat() < 0.75f * a)
            {
                fragMode = true;
                fragLen = juce::jmax (64, (int) ((0.03 + 0.12 * rng.nextDouble()) * sr));
                fragDir = rng.nextFloat() < 0.35f * a + 0.1f ? -1 : 1;
                fragStart = (fragW - fragLen - 1 + n) % n;
                fragRead = 0.0;
                fragLeft = fragLen * (2 + rng.nextInt (5));
            }
            else
            {
                fragMode = false;
                fragLeft = (int) ((0.05 + 0.25 * rng.nextDouble()) * sr * (1.0 - 0.6 * a));
            }
            fragFadeIn = (int) (0.003 * sr);
        }
        if (fragMode)
        {
            const int k = (int) fragRead;
            const int idx = (fragStart + (fragDir > 0 ? k : fragLen - 1 - k) + n) % n;
            // ventana corta en cada vuelta del trozo para que no haga clic
            const int edge = juce::jmin (k, fragLen - 1 - k);
            const float w = juce::jmin (1.0f, (float) edge / (float) juce::jmax (1, (int) (0.002 * sr)));
            const float fl = frag[(size_t) idx][0] * w, fr = frag[(size_t) idx][1] * w;
            fragRead += 1.0; if (fragRead >= fragLen) fragRead = 0.0;
            const float x = fragFadeIn > 0 ? 1.0f - (float) fragFadeIn / (float) (0.003 * sr) : 1.0f;
            l = l * (1.0f - x) + fl * x; r = r * (1.0f - x) + fr * x;
        }
        else if (fragFadeIn > 0)
        {
            const float x = (float) fragFadeIn / (float) (0.003 * sr);   // vuelve del trozo al sonido normal
            const int idx = (fragStart + (int) fragRead + n) % n;
            l = l * (1.0f - x) + frag[(size_t) idx][0] * x; r = r * (1.0f - x) + frag[(size_t) idx][1] * x;
        }
        if (fragFadeIn > 0) --fragFadeIn;
    }

    double sr = 44100.0, noteHz = 220.0, ringPhase = 0.0, fragRead = 0.0;
    juce::Random rng;
    Transform::Amounts amt;
    bool active = false, dirty = true;
    std::array<double, 4> lfo {};
    std::array<std::array<Allpass, numAp>, 2> ap;
    std::vector<std::array<float, 2>> frag;
    int fragW = 0, fragLeft = 0, fragLen = 1, fragDir = 1, fragStart = 0, fragFadeIn = 0;
    bool fragMode = false;
    float chaosTarget = 0.0f, chaosNow = 0.0f, chaosTimer = 0.0f, ringRatio = 1.4142f;
    float dcCoef = 0.999f, dcInL = 0, dcInR = 0, dcOutL = 0, dcOutR = 0;
    float lpL = 0, lpR = 0, tiltL = 0, tiltR = 0;
    float agcCoef = 0.999f, agcIn = 1.0e-4f, agcOut = 1.0e-4f, agcGain = 1.0f;
    int crushHold = 0; float crushL = 0, crushR = 0;
    float airState = 0, envFollow = 0;
};
