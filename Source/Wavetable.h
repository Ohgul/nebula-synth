#pragma once
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include "SampleData.h"

// ===========================================================================
// Tablas de ondas: varios "cuadros" de un ciclo cada uno, con versiones
// filtradas por octava (mipmaps) para que no haya aliasing en notas agudas.
// ===========================================================================
class Wavetable
{
public:
    static constexpr int frameSize = 2048;
    static constexpr int numMips = 10;       // mip m conserva 1024 >> m armónicos
    static constexpr int maxFrames = 256;   // la resíntesis adaptativa puede usar más de 64 cuadros

    juce::String name;

    int numFrames() const { return frames; }

    // Construye la tabla a partir de ciclos crudos (cada uno de frameSize muestras)
    static std::shared_ptr<Wavetable> fromCycles (const std::vector<std::vector<float>>& cycles, const juce::String& name)
    {
        auto t = std::make_shared<Wavetable>();
        t->name = name;
        t->frames = juce::jlimit (1, maxFrames, (int) cycles.size());
        t->data.assign ((size_t) (t->frames * numMips * frameSize), 0.0f);
        t->raw.assign ((size_t) (t->frames * frameSize), 0.0f);

        juce::dsp::FFT fft (11);   // 2048
        std::vector<float> spec ((size_t) frameSize * 2), work ((size_t) frameSize * 2);

        // Normalización común a toda la tabla
        float peak = 1.0e-6f;
        for (int f = 0; f < t->frames; ++f)
            for (auto v : cycles[(size_t) f]) peak = juce::jmax (peak, std::abs (v));

        for (int f = 0; f < t->frames; ++f)
        {
            const auto& c = cycles[(size_t) f];
            std::fill (spec.begin(), spec.end(), 0.0f);
            for (int i = 0; i < frameSize; ++i)
            {
                spec[(size_t) i] = c[(size_t) i] / peak;
                t->raw[(size_t) (f * frameSize + i)] = spec[(size_t) i];
            }
            fft.performRealOnlyForwardTransform (spec.data(), true);
            spec[0] = spec[1] = 0.0f;     // sin componente DC

            for (int m = 0; m < numMips; ++m)
            {
                const int keep = (frameSize / 2) >> m;
                std::copy (spec.begin(), spec.end(), work.begin());
                for (int h = keep + 1; h <= frameSize / 2; ++h)
                    work[(size_t) (2 * h)] = work[(size_t) (2 * h + 1)] = 0.0f;
                fft.performRealOnlyInverseTransform (work.data());
                float* dst = t->ptr (f, m);
                for (int i = 0; i < frameSize; ++i) dst[i] = work[(size_t) i];
            }
        }
        return t;
    }

    // Lee la tabla: fase [0,1), posición [0,1], mip según la frecuencia
    float read (double phase, float position, int mip) const
    {
        const float fpos = position * (float) (frames - 1);
        const int f0 = juce::jlimit (0, frames - 1, (int) fpos);
        const int f1 = juce::jmin (frames - 1, f0 + 1);
        const float ff = fpos - (float) f0;

        const double x = phase * frameSize;
        const int i0 = (int) x & (frameSize - 1);
        const int i1 = (i0 + 1) & (frameSize - 1);
        const float fx = (float) (x - std::floor (x));

        const float* a = ptr (f0, mip);
        const float* b = ptr (f1, mip);
        const float va = a[i0] + (a[i1] - a[i0]) * fx;
        const float vb = b[i0] + (b[i1] - b[i0]) * fx;
        return va + (vb - va) * ff;
    }

    static int mipFor (double freqHz, double sampleRate)
    {
        const double maxHarm = (sampleRate * 0.45) / juce::jmax (1.0, freqHz);
        int m = 0;
        while (m < numMips - 1 && ((frameSize / 2) >> m) > maxHarm) ++m;
        return m;
    }

    // Ciclo sin filtrar (para dibujarlo en pantalla)
    const float* rawFrame (int f) const { return raw.data() + (size_t) (juce::jlimit (0, frames - 1, f) * frameSize); }

    // ---- Guardar / cargar (para el proyecto) ----
    juce::String toBase64() const
    {
        juce::MemoryOutputStream mo;
        mo.writeInt (frames);
        mo.write (raw.data(), raw.size() * sizeof (float));
        return mo.getMemoryBlock().toBase64Encoding();
    }

    static std::shared_ptr<Wavetable> fromBase64 (const juce::String& s, const juce::String& name)
    {
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (s) || mb.getSize() < 4) return nullptr;
        juce::MemoryInputStream mi (mb, false);
        const int n = mi.readInt();
        if (n < 1 || n > maxFrames || (int) mb.getSize() != 4 + n * frameSize * (int) sizeof (float)) return nullptr;
        std::vector<std::vector<float>> cycles ((size_t) n, std::vector<float> ((size_t) frameSize));
        for (auto& c : cycles) mi.read (c.data(), frameSize * (int) sizeof (float));
        return fromCycles (cycles, name);
    }

private:
    float* ptr (int f, int m)             { return data.data() + (size_t) ((f * numMips + m) * frameSize); }
    const float* ptr (int f, int m) const { return data.data() + (size_t) ((f * numMips + m) * frameSize); }

    int frames = 1;
    std::vector<float> data, raw;
};

// ---------------------------------------------------------------------------
namespace WavetableFactory
{
    // Suma de senos armónicos (vía FFT inversa: rápido)
    inline std::vector<float> additive (const std::function<float (int harmonic)>& amp, int maxHarm = 256)
    {
        constexpr int N = Wavetable::frameSize;
        juce::dsp::FFT fft (11);
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int h = 1; h <= juce::jmin (maxHarm, N / 2 - 1); ++h)
            buf[(size_t) (2 * h + 1)] = -amp (h) * (float) N * 0.5f;   // seno = parte imaginaria negativa
        fft.performRealOnlyInverseTransform (buf.data());
        return std::vector<float> (buf.begin(), buf.begin() + N);
    }

    // Seno → triángulo → sierra → cuadrada
    inline std::shared_ptr<Wavetable> basic()
    {
        constexpr int n = 24;
        auto shape = [] (int k, int h) -> float
        {
            const float sine = h == 1 ? 1.0f : 0.0f;
            const float tri  = (h % 2 == 1) ? (((h / 2) % 2 == 0) ? 1.0f : -1.0f) / (float) (h * h) * 0.81f * 1.23f : 0.0f;
            const float saw  = 0.6f / (float) h;
            const float sq   = (h % 2 == 1) ? 0.8f / (float) h : 0.0f;
            const float shapes[] { sine, tri, saw, sq };
            const float pos = (float) k / (n - 1) * 3.0f;
            const int i0 = juce::jmin (2, (int) pos);
            const float f = pos - (float) i0;
            return shapes[i0] + (shapes[i0 + 1] - shapes[i0]) * f;
        };
        std::vector<std::vector<float>> cycles;
        for (int k = 0; k < n; ++k)
            cycles.push_back (additive ([&] (int h) { return shape (k, h); }));
        return Wavetable::fromCycles (cycles, "Basica");
    }

    // De 1 a muchos armónicos, cada vez más brillante
    inline std::shared_ptr<Wavetable> harmonics()
    {
        constexpr int n = 32;
        std::vector<std::vector<float>> cycles;
        for (int k = 0; k < n; ++k)
        {
            const float count = 1.0f + std::pow ((float) k / (n - 1), 1.6f) * 60.0f;
            cycles.push_back (additive ([count] (int h)
            {
                if ((float) h > count + 1.0f) return 0.0f;
                const float edge = juce::jlimit (0.0f, 1.0f, count + 1.0f - (float) h);
                return edge / std::sqrt ((float) h);
            }, 64));
        }
        return Wavetable::fromCycles (cycles, "Armonicos");
    }

    // Vocales a → e → i → o → u (formantes)
    inline std::shared_ptr<Wavetable> vocal()
    {
        struct V { float f1, f2, f3; };
        const V vowels[] { { 800, 1200, 2500 }, { 400, 2000, 2550 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        constexpr int n = 25;
        const float fund = 110.0f;   // fundamental de referencia
        std::vector<std::vector<float>> cycles;
        for (int k = 0; k < n; ++k)
        {
            const float pos = (float) k / (n - 1) * 4.0f;
            const int i0 = juce::jmin (3, (int) pos);
            const float t = pos - (float) i0;
            const V v { vowels[i0].f1 + (vowels[i0 + 1].f1 - vowels[i0].f1) * t,
                        vowels[i0].f2 + (vowels[i0 + 1].f2 - vowels[i0].f2) * t,
                        vowels[i0].f3 + (vowels[i0 + 1].f3 - vowels[i0].f3) * t };
            cycles.push_back (additive ([&] (int h)
            {
                const float f = fund * (float) h;
                auto peak = [f] (float c, float w) { return std::exp (-juce::square ((f - c) / w)); };
                return (0.25f / std::sqrt ((float) h)) * (0.15f + peak (v.f1, 90.0f) + 0.7f * peak (v.f2, 120.0f) + 0.35f * peak (v.f3, 160.0f));
            }, 120));
        }
        return Wavetable::fromCycles (cycles, "Vocal");
    }

    // Formas digitales: escalones y pulsos que se van fragmentando
    inline std::shared_ptr<Wavetable> digital()
    {
        constexpr int n = 24;
        std::vector<std::vector<float>> cycles;
        for (int k = 0; k < n; ++k)
        {
            std::vector<float> c ((size_t) Wavetable::frameSize);
            const int steps = 2 + k;
            const float pw = 0.5f - 0.4f * (float) k / (n - 1);
            for (int i = 0; i < Wavetable::frameSize; ++i)
            {
                const float ph = (float) i / Wavetable::frameSize;
                const float saw = 2.0f * ph - 1.0f;
                const float stepped = std::floor (saw * (float) steps) / (float) steps;
                const float pulse = ph < pw ? 1.0f : -1.0f;
                c[(size_t) i] = 0.6f * stepped + 0.4f * pulse;
            }
            cycles.push_back (std::move (c));
        }
        return Wavetable::fromCycles (cycles, "Digital");
    }

    // ---- Tabla de usuario desde audio ("Hacer onda") ----
    // Corta la región elegida del sample en ciclos de una sola onda.
    inline std::shared_ptr<Wavetable> fromAudio (const float* x, int n, double sampleRate, int numFrames, float smoothing, const juce::String& name)
    {
        numFrames = juce::jlimit (1, Wavetable::maxFrames, numFrames);
        if (n < 64) return nullptr;

        // Periodo: por tono si lo hay; si no, una ventana fija
        float periodSamples = 0.0f;
        {
            std::vector<float> notes;
            const int win = juce::jmin (n, (int) (sampleRate * 0.1));
            for (int k = 0; k < 6; ++k)
            {
                const int start = (int) ((juce::int64) (n - win) * k / 6);
                const float hz = SampleTools::detectPitch (x + start, win, sampleRate);
                if (hz > 30.0f && hz < 2000.0f) notes.push_back (hz);
            }
            if (notes.size() >= 2)
            {
                std::sort (notes.begin(), notes.end());
                periodSamples = (float) (sampleRate / notes[notes.size() / 2]);
            }
        }
        const bool pitched = periodSamples > 0.0f;
        if (! pitched) periodSamples = (float) juce::jmin ((double) n / 2.0, sampleRate * 0.02);   // 20 ms

        std::vector<std::vector<float>> cycles;
        for (int f = 0; f < numFrames; ++f)
        {
            int start = (int) ((double) (n - (int) periodSamples - 2) * f / juce::jmax (1, numFrames - 1));
            start = juce::jlimit (0, juce::jmax (0, n - (int) periodSamples - 2), start);

            // Con tono: empieza en el cruce por cero ascendente más cercano
            if (pitched)
                for (int s = start; s < juce::jmin (n - (int) periodSamples - 2, start + (int) periodSamples); ++s)
                    if (x[s] <= 0.0f && x[s + 1] > 0.0f) { start = s; break; }

            std::vector<float> c ((size_t) Wavetable::frameSize);
            for (int i = 0; i < Wavetable::frameSize; ++i)
            {
                const double pos = start + (double) i / Wavetable::frameSize * periodSamples;
                const int i0 = juce::jlimit (0, n - 2, (int) pos);
                const float t = (float) (pos - i0);
                c[(size_t) i] = x[i0] + (x[i0 + 1] - x[i0]) * t;
            }

            // Sin tono: une el final con el principio para que el ciclo no "clickee"
            if (! pitched)
            {
                const int fadeLen = Wavetable::frameSize / 8;
                for (int i = 0; i < fadeLen; ++i)
                {
                    const float w = (float) i / fadeLen;
                    const size_t endIdx = (size_t) (Wavetable::frameSize - fadeLen + i);
                    c[endIdx] = c[endIdx] * (1.0f - w) + c[(size_t) i] * w;
                }
            }

            // Quita DC
            float mean = 0.0f;
            for (auto v : c) mean += v;
            mean /= (float) c.size();
            for (auto& v : c) v -= mean;
            cycles.push_back (std::move (c));
        }

        // Suavizado entre cuadros vecinos
        if (smoothing > 0.0f && cycles.size() > 2)
        {
            auto copy = cycles;
            for (size_t f = 1; f + 1 < cycles.size(); ++f)
                for (size_t i = 0; i < cycles[f].size(); ++i)
                    cycles[f][i] = copy[f][i] * (1.0f - smoothing) + 0.5f * smoothing * (copy[f - 1][i] + copy[f + 1][i]);
        }
        return Wavetable::fromCycles (cycles, name);
    }

    // Tabla de un solo ciclo dibujado a mano (puntos de -1 a 1)
    inline std::shared_ptr<Wavetable> fromDrawing (const std::vector<float>& points)
    {
        std::vector<float> c ((size_t) Wavetable::frameSize);
        const int n = (int) points.size();
        for (int i = 0; i < Wavetable::frameSize; ++i)
        {
            const float pos = (float) i / Wavetable::frameSize * (float) n;
            const int i0 = (int) pos % n, i1 = (i0 + 1) % n;
            const float t = pos - std::floor (pos);
            c[(size_t) i] = points[(size_t) i0] + (points[(size_t) i1] - points[(size_t) i0]) * t;
        }
        float mean = 0.0f;
        for (auto v : c) mean += v;
        mean /= (float) c.size();
        for (auto& v : c) v -= mean;
        return Wavetable::fromCycles ({ c }, "Dibujo");
    }
}

// Las 4 tablas incluidas + la del usuario (intercambiable en vivo de forma segura)
class WavetableBank
{
public:
    static constexpr int numBuiltIn = 4;
    static constexpr int userIndex = 4;

    WavetableBank() : builtIn (sharedBuiltIns())
    {
        setUser (builtIn[0]);
    }

    // Las tablas incluidas se calculan una sola vez y se comparten entre instancias
    static const std::array<std::shared_ptr<const Wavetable>, numBuiltIn>& sharedBuiltIns()
    {
        static const std::array<std::shared_ptr<const Wavetable>, numBuiltIn> t {
            WavetableFactory::basic(), WavetableFactory::harmonics(), WavetableFactory::vocal(), WavetableFactory::digital() };
        return t;
    }

    std::shared_ptr<const Wavetable> get (int index) const
    {
        if (index >= userIndex) return std::atomic_load (&user);
        return builtIn[(size_t) juce::jlimit (0, numBuiltIn - 1, index)];
    }

    void setUser (std::shared_ptr<const Wavetable> t)
    {
        auto old = std::atomic_exchange (&user, std::move (t));
        retired.push_back (std::move (old));
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (auto& p) { return p == nullptr || p.use_count() == 1; }), retired.end());
        ++userVersion;
    }

    bool hasCustomUser = false;
    std::atomic<int> userVersion { 0 };

private:
    const std::array<std::shared_ptr<const Wavetable>, numBuiltIn>& builtIn;
    std::shared_ptr<const Wavetable> user;
    std::vector<std::shared_ptr<const Wavetable>> retired;
};
