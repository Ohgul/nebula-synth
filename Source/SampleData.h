#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

// Un sample cargado (mezclado a mono) con su nota detectada.
struct SampleData
{
    juce::AudioBuffer<float> audio;  // 1 canal
    double sampleRate = 44100.0;
    float rootNote = 60.0f;          // nota MIDI (con decimales = cents)
    bool hasPitch = false;
    juce::String name;
    juce::File file;

    int length() const { return audio.getNumSamples(); }
    const float* data() const { return audio.getReadPointer (0); }
};

// Contenedor seguro entre hilos: la interfaz cambia el sample, las voces lo leen.
class SampleSource
{
public:
    std::shared_ptr<const SampleData> get() const { return std::atomic_load (&current); }

    void set (std::shared_ptr<const SampleData> s)
    {
        // El sample anterior se libera aquí (hilo de mensajes), nunca en el hilo de audio
        auto old = std::atomic_exchange (&current, std::move (s));
        retired.push_back (std::move (old));
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [] (auto& p) { return p == nullptr || p.use_count() == 1; }),
                       retired.end());
    }

private:
    std::shared_ptr<const SampleData> current;
    std::vector<std::shared_ptr<const SampleData>> retired;
};

namespace SampleTools
{
    // Detección de tono (algoritmo YIN simplificado). Devuelve Hz o 0 si no hay tono claro.
    inline float detectPitch (const float* x, int n, double sr)
    {
        const int minTau = juce::jmax (2, (int) (sr / 1500.0));
        const int maxTau = juce::jmin (n / 2 - 1, (int) (sr / 35.0));
        if (maxTau <= minTau + 2) return 0.0f;

        const int w = juce::jmin (n - maxTau, (int) (sr * 0.05));
        std::vector<float> d ((size_t) maxTau + 1, 0.0f);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double s = 0.0;
            for (int i = 0; i < w; ++i) { const double diff = x[i] - x[i + tau]; s += diff * diff; }
            d[(size_t) tau] = (float) s;
        }

        // Diferencia normalizada acumulada
        std::vector<float> cmnd (d.size(), 1.0f);
        double running = 0.0;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0 ? (float) (d[(size_t) tau] * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxTau && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) return 0.0f;

        // Interpolación parabólica
        const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
        return (float) (sr / (best + juce::jlimit (-1.0f, 1.0f, shift)));
    }

    // Busca el tono en varias ventanas cerca del inicio y toma la mediana
    inline void analysePitch (SampleData& s)
    {
        const float* x = s.data();
        const int n = s.length();
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (x[i]));
        if (peak <= 0.0f) return;

        int onset = 0;
        while (onset < n && std::abs (x[onset]) < peak * 0.05f) ++onset;

        const int win = (int) (s.sampleRate * 0.1);
        std::vector<float> notes;
        for (int k = 0; k < 8; ++k)
        {
            const int start = onset + (int) (s.sampleRate * (0.03 + 0.06 * k));
            if (start + win >= n) break;
            const float hz = detectPitch (x + start, win, s.sampleRate);
            if (hz > 30.0f && hz < 2000.0f)
                notes.push_back (69.0f + 12.0f * std::log2 (hz / 440.0f));
        }
        if (notes.size() < 2) return;

        std::sort (notes.begin(), notes.end());
        const float median = notes[notes.size() / 2];
        int agree = 0;
        for (auto v : notes) if (std::abs (v - median) < 0.5f) ++agree;
        if (agree * 2 >= (int) notes.size())
        {
            s.rootNote = median;
            s.hasPitch = true;
        }
    }

    inline std::shared_ptr<const SampleData> fromBuffer (const juce::AudioBuffer<float>& src, double sr,
                                                         const juce::String& name, const juce::File& file = {})
    {
        auto s = std::make_shared<SampleData>();
        const int n = src.getNumSamples();
        s->audio.setSize (1, n);
        s->audio.clear();
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            s->audio.addFrom (0, 0, src, ch, 0, n, 1.0f / (float) src.getNumChannels());

        // Normaliza a -1 dBFS para que todos los samples suenen parecido de fuerte
        const float mag = s->audio.getMagnitude (0, 0, n);
        if (mag > 1.0e-5f) s->audio.applyGain (0.89f / mag);

        s->sampleRate = sr;
        s->name = name;
        s->file = file;
        analysePitch (*s);
        return s;
    }

    inline std::shared_ptr<const SampleData> load (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "No pude leer ese archivo (usa WAV, AIFF, FLAC, MP3 u OGG).";
            return nullptr;
        }

        const auto maxLen = (juce::int64) (reader->sampleRate * 20.0); // hasta 20 s
        const int n = (int) juce::jmin (reader->lengthInSamples, maxLen);
        if (n < (int) (reader->sampleRate * 0.02))
        {
            error = "El archivo es demasiado corto.";
            return nullptr;
        }

        juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), n);
        reader->read (&buf, 0, n, 0, true, buf.getNumChannels() > 1);
        return fromBuffer (buf, reader->sampleRate, file.getFileNameWithoutExtension(), file);
    }
}
