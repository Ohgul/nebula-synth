#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include <array>
#include "Wavetable.h"

// ===========================================================================
// Modelo de resíntesis: lo que Nebula aprende de un sample para reconstruirlo.
//  · Parte armónica: tabla de ondas, un cuadro por momento (síntesis aditiva).
//  · Parciales inarmónicos: frecuencia relativa + volumen en el tiempo (campanas, metales, golpes).
//  · Ruido: 24 bandas con su volumen en el tiempo (soplido, golpe, roce).
//  · Curva de tono (vibrato, glides) y ancho estéreo.
// Todos los niveles están en las mismas unidades: el cuadro más fuerte de la tabla vale 1.
// ===========================================================================
struct ResynthModel
{
    static constexpr int numBands = 24;
    static constexpr int maxPartials = 48;

    juce::String name;
    float rootNote = 60.0f;
    std::vector<float> times;             // segundos desde el ataque, por cuadro
    bool sustained = false;               // al mantener la nota se queda en el último cuadro
    float harmonicLevel = 1.0f;           // nivel de la tabla (relativo)
    std::shared_ptr<const Wavetable> harmonic;
    struct Partial { float ratio = 1.0f; std::vector<float> amp, ratioCurve; };   // ratioCurve: la frecuencia también se mueve (bombos, glides)
    std::vector<Partial> partials;
    std::vector<std::array<float, numBands>> noise;
    std::vector<float> pitchCents;
    float width = 0.0f;
    // Curvas densas (para frases y ataques): volumen de la parte armónica y tono, en cents
    float envHop = 0.005f, pitchHop = 0.01f;
    std::vector<float> harmEnv, pitchCurve;

    static float curveAt (const std::vector<float>& c, float hop, double t)
    {
        if (c.empty()) return 0.0f;
        const double x = juce::jmax (0.0, t / hop);
        const int i0 = juce::jmin ((int) c.size() - 1, (int) x), i1 = juce::jmin ((int) c.size() - 1, i0 + 1);
        const float w = (float) (x - std::floor (x));
        return c[(size_t) i0] + (c[(size_t) i1] - c[(size_t) i0]) * w;
    }

    int frames() const { return (int) times.size(); }

    static const std::array<float, numBands + 1>& bandEdges()
    {
        static const auto e = []
        {
            std::array<float, numBands + 1> r {};
            for (int b = 0; b <= numBands; ++b) r[(size_t) b] = 60.0f * std::pow (16000.0f / 60.0f, (float) b / numBands);
            return r;
        }();
        return e;
    }

    // Cuadro (fraccional) que corresponde a un tiempo
    float frameAt (double t) const
    {
        const int n = frames();
        // Se compara en float, igual que la búsqueda: con double, un tiempo justo antes del último
        // frame podía redondear a ese frame y upper_bound devolvía end() (lectura fuera del vector).
        const float tf = (float) t;
        if (n < 2 || ! (tf > times.front())) return 0.0f;   // también si t no es un número
        if (tf >= times.back()) return (float) (n - 1);
        const auto it = std::upper_bound (times.begin(), times.end(), tf);
        const int i1 = (int) (it - times.begin()), i0 = i1 - 1;
        const float span = juce::jmax (1.0e-6f, times[(size_t) i1] - times[(size_t) i0]);
        return (float) i0 + ((float) t - times[(size_t) i0]) / span;
    }

    // ---- Guardar / cargar (dentro del proyecto) ----
    juce::String toBase64() const
    {
        juce::MemoryOutputStream mo;
        mo.writeInt (3);   // versión del formato
        mo.writeString (name);
        mo.writeFloat (rootNote);
        mo.writeBool (sustained);
        mo.writeFloat (harmonicLevel);
        mo.writeFloat (width);
        mo.writeInt ((int) times.size());
        for (auto t : times) mo.writeFloat (t);
        for (size_t i = 0; i < times.size(); ++i) mo.writeFloat (i < pitchCents.size() ? pitchCents[i] : 0.0f);
        mo.writeInt ((int) partials.size());
        for (auto& p : partials)
        {
            mo.writeFloat (p.ratio);
            for (size_t i = 0; i < times.size(); ++i) mo.writeFloat (i < p.amp.size() ? p.amp[i] : 0.0f);
            for (size_t i = 0; i < times.size(); ++i) mo.writeFloat (i < p.ratioCurve.size() ? p.ratioCurve[i] : p.ratio);
        }
        mo.writeInt ((int) noise.size());
        for (auto& f : noise) for (auto v : f) mo.writeFloat (v);
        mo.writeString (harmonic != nullptr ? harmonic->toBase64() : juce::String());
        mo.writeFloat (envHop); mo.writeInt ((int) harmEnv.size()); for (auto v : harmEnv) mo.writeFloat (v);
        mo.writeFloat (pitchHop); mo.writeInt ((int) pitchCurve.size()); for (auto v : pitchCurve) mo.writeFloat (v);
        return mo.getMemoryBlock().toBase64Encoding();
    }

    static std::shared_ptr<ResynthModel> fromBase64 (const juce::String& s)
    {
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (s)) return nullptr;
        juce::MemoryInputStream in (mb, false);
        if (in.readInt() != 3) return nullptr;
        auto m = std::make_shared<ResynthModel>();
        m->name = in.readString();
        m->rootNote = in.readFloat();
        m->sustained = in.readBool();
        m->harmonicLevel = in.readFloat();
        m->width = in.readFloat();
        const int n = in.readInt();
        if (n < 2 || n > 4096) return nullptr;
        m->times.resize ((size_t) n);
        for (auto& t : m->times) t = in.readFloat();
        m->pitchCents.resize ((size_t) n);
        for (auto& c : m->pitchCents) c = in.readFloat();
        const int np = in.readInt();
        if (np < 0 || np > maxPartials) return nullptr;
        m->partials.resize ((size_t) np);
        for (auto& p : m->partials)
        {
            p.ratio = in.readFloat();
            p.amp.resize ((size_t) n); for (auto& a : p.amp) a = in.readFloat();
            p.ratioCurve.resize ((size_t) n); for (auto& a : p.ratioCurve) a = in.readFloat();
        }
        const int nn = in.readInt();
        if (nn < 0 || nn > 4096) return nullptr;
        m->noise.resize ((size_t) nn);
        for (auto& f : m->noise) for (auto& v : f) v = in.readFloat();
        const auto table = in.readString();
        if (table.isNotEmpty()) m->harmonic = Wavetable::fromBase64 (table, m->name);
        m->envHop = in.readFloat();
        const int ne = in.readInt();
        if (ne < 0 || ne > 200000) return nullptr;
        m->harmEnv.resize ((size_t) ne); for (auto& v : m->harmEnv) v = in.readFloat();
        m->pitchHop = in.readFloat();
        const int npc = in.readInt();
        if (npc < 0 || npc > 200000) return nullptr;
        m->pitchCurve.resize ((size_t) npc); for (auto& v : m->pitchCurve) v = in.readFloat();
        return m;
    }
};

// Contenedor seguro entre hilos (la interfaz cambia el modelo, las voces lo leen)
template <typename T>
class SharedHolder
{
public:
    std::shared_ptr<const T> get() const { return std::atomic_load (&current); }
    void set (std::shared_ptr<const T> v)
    {
        auto old = std::atomic_exchange (&current, std::move (v));
        retired.push_back (std::move (old));
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (auto& p) { return p == nullptr || p.use_count() == 1; }), retired.end());
    }
private:
    std::shared_ptr<const T> current;
    std::vector<std::shared_ptr<const T>> retired;
};

// ===========================================================================
// Reproductor por voz
// ===========================================================================
class ResynthPlayer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto& e = ResynthModel::bandEdges();
        for (int b = 0; b < ResynthModel::numBands; ++b)
        {
            const double lo = e[(size_t) b], hi = e[(size_t) b + 1];
            const double fc = std::sqrt (lo * hi), bw = hi - lo;
            const double q = fc / bw;
            bands[(size_t) b].g = std::tan (juce::MathConstants<double>::pi * juce::jmin (fc, sr * 0.45) / sr);
            bands[(size_t) b].k = 1.0 / q;
            // Ganancia para que la banda (ruido blanco de rms 1) salga con rms 1
            const double enbw = juce::MathConstants<double>::halfPi * bw;
            bands[(size_t) b].norm = (float) (1.0 / std::sqrt (juce::jmin (1.0, enbw / (sr * 0.5))));
        }
        haas.assign ((size_t) (sr * 0.02) + 2, 0.0f);
    }

    void start (std::shared_ptr<const ResynthModel> m, juce::Random& r)
    {
        model = std::move (m);
        t = 0.0;
        phase = 0.0;
        for (auto& p : ph) p = { 1.0, 0.0 };
        for (size_t k = 0; k < ph.size(); ++k)
        {
            const double a = r.nextDouble() * juce::MathConstants<double>::twoPi;
            ph[k] = { std::cos (a), std::sin (a) };
        }
        for (auto& b : bands) b.s1 = b.s2 = 0.0;
        std::fill (haas.begin(), haas.end(), 0.0f);
        tiltState = 0.0f;
        harmGain = harmTarget = harmStep = 0.0f;
        for (auto& a : partAmp) a = 0.0f;
        for (auto& a : bandAmp) a = 0.0f;
    }

    bool hasModel() const { return model != nullptr && model->frames() >= 2; }

    // Se llama cada "controlRate" muestras: avanza el tiempo y calcula los objetivos
    void control (double noteHz, float speed, float harm, float partials, float noiseAmt, float stretch, bool followPitch, int samples)
    {
        if (! hasModel()) return;
        const auto& m = *model;
        double tt = t;
        if (m.sustained) tt = juce::jmin (tt, (double) m.times.back());
        frame = m.frameAt (tt);
        const int f0 = (int) frame, f1 = juce::jmin (m.frames() - 1, f0 + 1);
        const float w = frame - (float) f0;
        auto lerp = [w] (float a, float b) { return a + (b - a) * w; };

        float cents = 0.0f;
        if (followPitch)
        {
            if (! m.pitchCurve.empty()) cents = ResynthModel::curveAt (m.pitchCurve, m.pitchHop, tt);
            else if (! m.pitchCents.empty()) cents = lerp (m.pitchCents[(size_t) f0], m.pitchCents[(size_t) f1]);
        }
        hz = noteHz * std::exp2 (cents / 1200.0);
        const float env = m.harmEnv.empty() ? 1.0f : ResynthModel::curveAt (m.harmEnv, m.envHop, tt);
        harmTarget = harm * m.harmonicLevel * env;
        harmStep = (harmTarget - harmGain) / (float) juce::jmax (1, samples);
        position = m.frames() > 1 ? frame / (float) (m.frames() - 1) : 0.0f;

        rampLen = juce::jmax (1, samples);
        const double nyq = sr * 0.45;
        const int np = juce::jmin ((int) m.partials.size(), ResynthModel::maxPartials);
        numPartials = np;
        const float rootRatioScale = (float) (noteHz / (440.0 * std::exp2 ((m.rootNote - 69.0) / 12.0)));
        juce::ignoreUnused (rootRatioScale);
        for (int k = 0; k < np; ++k)
        {
            const auto& p = m.partials[(size_t) k];
            const double baseRatio = p.ratioCurve.empty() ? p.ratio : lerp (p.ratioCurve[(size_t) f0], p.ratioCurve[(size_t) f1]);
            const double ratio = std::pow (baseRatio, 1.0 + 0.15 * stretch);
            const double f = hz * ratio;
            const float target = f < nyq ? partials * lerp (p.amp[(size_t) f0], p.amp[(size_t) f1]) : 0.0f;
            partStep[(size_t) k] = (target - partAmp[(size_t) k]) / (float) rampLen;
            const double om = juce::MathConstants<double>::twoPi * juce::jmin (f, nyq) / sr;
            rot[(size_t) k] = { std::cos (om), std::sin (om) };
        }
        for (int b = 0; b < ResynthModel::numBands; ++b)
        {
            float target = 0.0f;
            if (! m.noise.empty())
            {
                const int nf0 = juce::jmin ((int) m.noise.size() - 1, f0), nf1 = juce::jmin ((int) m.noise.size() - 1, f1);
                target = noiseAmt * lerp (m.noise[(size_t) nf0][(size_t) b], m.noise[(size_t) nf1][(size_t) b]) * bands[(size_t) b].norm;
            }
            bandStep[(size_t) b] = (target - bandAmp[(size_t) b]) / (float) rampLen;
        }
        t += speed * samples / sr;
        // Renormaliza los fasores (evita que la amplitud se desvíe con el tiempo)
        for (int k = 0; k < np; ++k)
        {
            const double mag = std::sqrt (ph[(size_t) k].first * ph[(size_t) k].first + ph[(size_t) k].second * ph[(size_t) k].second);
            if (mag > 0.0) { ph[(size_t) k].first /= mag; ph[(size_t) k].second /= mag; }
        }
    }

    void next (float& L, float& R, float tone, float width, juce::Random& rng)
    {
        if (! hasModel()) return;
        const auto& m = *model;
        float centre = 0.0f;

        // Parte armónica (tabla)
        if (m.harmonic != nullptr && (harmGain > 0.0f || harmTarget > 0.0f))
        {
            harmGain += harmStep;
            const float v = m.harmonic->read (phase, position, Wavetable::mipFor (hz, sr));
            phase += hz / sr;
            if (phase >= 1.0) phase -= std::floor (phase);
            centre += v * harmGain;
        }

        // Parciales inarmónicos (osciladores seno por rotación)
        float pl = 0.0f, pr = 0.0f;
        for (int k = 0; k < numPartials; ++k)
        {
            auto& p = ph[(size_t) k];
            const auto& w = rot[(size_t) k];
            const double c = p.first * w.first - p.second * w.second;
            const double s = p.first * w.second + p.second * w.first;
            p = { c, s };
            partAmp[(size_t) k] += partStep[(size_t) k];
            const float v = (float) s * partAmp[(size_t) k];
            // un poco de abertura estéreo: los parciales se alternan a cada lado
            const float side = (k & 1) ? 0.35f * width : -0.35f * width;
            pl += v * (1.0f - side); pr += v * (1.0f + side);
        }

        // Ruido por bandas
        float nz = 0.0f;
        const float white = (rng.nextFloat() * 2.0f - 1.0f) * 1.7320508f;   // rms 1
        for (int b = 0; b < ResynthModel::numBands; ++b)
        {
            auto& f = bands[(size_t) b];
            bandAmp[(size_t) b] += bandStep[(size_t) b];
            if (bandAmp[(size_t) b] <= 1.0e-7f) { continue; }
            const double a1 = 1.0 / (1.0 + f.g * (f.g + f.k));
            const double a2 = f.g * a1, a3 = f.g * a2;
            const double v3 = white - f.s2;
            const double v1 = a1 * f.s1 + a2 * v3;
            const double v2 = f.s2 + a2 * f.s1 + a3 * v3;
            f.s1 = 2.0 * v1 - f.s1;
            f.s2 = 2.0 * v2 - f.s2;
            nz += (float) (v1 * f.k) * bandAmp[(size_t) b];
        }
        // Ancho: el ruido de la derecha llega unos ms después (Haas)
        const int hl = (int) haas.size();
        haas[(size_t) haasPos] = nz;
        // El ancho que llega puede pasar de 1 (ancho del modelo × 2 × "Resintesis Ancho"); el retardo
        // Haas se satura en su máximo (20 ms) para no leer fuera del búfer. Con ancho <= 1 no cambia nada.
        const int delay = juce::jlimit (1, hl - 2, (int) (juce::jmin (1.0f, width) * (float) (hl - 2)));
        const float nzR = haas[(size_t) ((haasPos - delay + hl) % hl)];
        haasPos = (haasPos + 1) % hl;

        float l = centre + pl + nz, r = centre + pr + (width > 0.01f ? nzR : nz);

        // Tono: inclina el brillo (+ más agudos, - más oscuro)
        tiltState += 0.12f * (0.5f * (l + r) - tiltState);
        if (std::abs (tone) > 0.01f)
        {
            const float lowpart = tiltState;
            if (tone > 0.0f) { l += tone * (l - lowpart) * 1.5f; r += tone * (r - lowpart) * 1.5f; }
            else             { const float k = -tone; l = l * (1.0f - k) + lowpart * k; r = r * (1.0f - k) + lowpart * k; }
        }
        L += l; R += r;
    }

private:
    struct Band { double g = 0.1, k = 1.0, s1 = 0.0, s2 = 0.0; float norm = 1.0f; };

    std::shared_ptr<const ResynthModel> model;
    double sr = 44100.0, t = 0.0, phase = 0.0, hz = 220.0;
    float frame = 0.0f, position = 0.0f, harmGain = 0.0f, harmTarget = 0.0f, harmStep = 0.0f, tiltState = 0.0f;
    int rampLen = 16, numPartials = 0, haasPos = 0;
    std::array<std::pair<double, double>, ResynthModel::maxPartials> ph {}, rot {};
    std::array<float, ResynthModel::maxPartials> partAmp {}, partStep {};
    std::array<Band, ResynthModel::numBands> bands {};
    std::array<float, ResynthModel::numBands> bandAmp {}, bandStep {};
    std::vector<float> haas;
};
