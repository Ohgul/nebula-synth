#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "SoundProfile.h"
#include "Wavetable.h"

// ===========================================================================
// Exportar para arrastrar a Ableton: notas de una frase (MIDI), audio y la onda.
// Los archivos se guardan en ~/Music/Nebula/Exportados.
// ===========================================================================
namespace Exporter
{
    inline juce::File folder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Nebula").getChildFile ("Exportados");
        f.createDirectory();
        return f;
    }

    struct Note { int pitch; double start, length; int velocity; };

    // Notas de la sección elegida (frases o una sola nota), con su tiempo real
    inline std::vector<Note> transcribe (const SampleData& smp, float rs, float re)
    {
        std::vector<Note> notes;
        const int full = smp.length();
        const int a0 = juce::jlimit (0, juce::jmax (0, full - 2), (int) (rs * (float) full));
        const int a1 = juce::jlimit (a0 + 2, full, (int) (re * (float) full));
        const float* x = smp.data() + a0;
        const int n = a1 - a0;
        float peak = 1.0e-6f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (x[i]));
        const auto c = SoundProfiler::pitchContour (x, n, smp.sampleRate, peak * 0.04f);
        float maxW = 1.0e-9f;
        for (auto w : c.weight) maxW = juce::jmax (maxW, w);

        // Afinación del audio (cents comunes) para redondear bien cada nota
        std::vector<std::pair<float, float>> frac;
        for (size_t i = 0; i < c.note.size(); ++i) if (c.note[i] > 0.0f) frac.push_back ({ c.note[i] - std::round (c.note[i]), c.weight[i] });
        const float tune = frac.empty() ? 0.0f : SoundProfiler::weightedMedian (frac);

        int cur = -1; size_t startI = 0; float wMax = 0.0f, prevW = 0.0f;
        auto close = [&] (size_t endI)
        {
            const double len = (endI - startI) * c.hop;
            if (cur >= 0 && len >= 0.06)
                notes.push_back ({ cur, startI * c.hop, len, juce::jlimit (30, 127, (int) (40 + 87 * std::sqrt (wMax / maxW))) });
        };
        for (size_t i = 0; i < c.note.size(); ++i)
        {
            const int nt = c.note[i] > 0.0f ? (int) std::round (c.note[i] - tune) : -1;
            const bool reattack = c.weight[i] > prevW * 2.0f && prevW > 0.0f && c.weight[i] > maxW * 0.1f;   // golpe nuevo en la misma nota
            if (nt != cur || reattack)
            {
                close (i);
                cur = nt; startI = i; wMax = 0.0f;
            }
            wMax = juce::jmax (wMax, c.weight[i]);
            prevW = c.weight[i];
        }
        close (c.note.size());
        return notes;
    }

    inline juce::File writeMidi (const std::vector<Note>& notes, double bpm, const juce::String& name)
    {
        constexpr int ppq = 960;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jmax (30.0, bpm))), 0);
        auto ticks = [&] (double secs) { return secs * bpm / 60.0 * ppq; };
        for (auto& n : notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, n.pitch), (juce::uint8) n.velocity), ticks (n.start));
            seq.addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, n.pitch)), ticks (n.start + n.length));
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        mf.addTrack (seq);
        auto f = folder().getChildFile (juce::File::createLegalFileName (name) + " - notas.mid");
        f.deleteFile();
        if (auto os = f.createOutputStream()) mf.writeTo (*os);
        return f;
    }

    inline juce::File writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::String& fileName)
    {
        auto f = folder().getChildFile (juce::File::createLegalFileName (fileName));
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        if (os != nullptr)
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), sr, (unsigned) b.getNumChannels(), 24, {}, 0)))
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return f;
    }

    // Tabla de ondas como WAV de ciclos seguidos de 2048 muestras (la abren Serum, Vital y Wavetable de Ableton)
    inline juce::File writeWavetable (const Wavetable& t)
    {
        juce::AudioBuffer<float> b (1, t.numFrames() * Wavetable::frameSize);
        for (int f = 0; f < t.numFrames(); ++f)
            b.copyFrom (0, f * Wavetable::frameSize, t.rawFrame (f), Wavetable::frameSize);
        const float mag = b.getMagnitude (0, 0, b.getNumSamples());
        if (mag > 0.0f) b.applyGain (0.95f / mag);
        return writeWav (b, 48000.0, (t.name.isNotEmpty() ? t.name : juce::String ("Nebula")) + " - onda.wav");
    }
}
