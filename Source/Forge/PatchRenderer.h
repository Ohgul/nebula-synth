#pragma once
#include "SynthPatch.h"
#include "../SynthVoice.h"

// ===========================================================================
// FORGE · PatchRenderer
//
// Renderiza un SynthPatch fuera de línea con el motor real de Nebula (las mismas voces que el plugin),
// sin procesador, sin sample y sin efectos. Determinista: misma semilla + mismo patch + misma frecuencia
// de muestreo = las mismas muestras. Pensado para el hilo de búsqueda (no toca el audio en vivo).
// ===========================================================================
namespace Forge
{
    class PatchRenderer
    {
    public:
        PatchRenderer()
            : pointers ([this] (const juce::String& id) { return lookup (id); }),
              ctx (pointers, noSample, bank),
              synth (ctx)
        {
            for (auto& p : createParameterList())
            {
                ids.add (p->getParameterID());
                defaults.push_back (p->convertFrom0to1 (p->getDefaultValue()));
            }
            values.reset (new std::atomic<float>[defaults.size()]);
            for (size_t i = 0; i < defaults.size(); ++i) values[i] = defaults[i];
            resolved = true;
            pointers = ParamPointers ([this] (const juce::String& id) { return lookup (id); });
            synth.addSound (new SynthSound());
            for (int v = 0; v < 4; ++v) synth.addVoice (new SynthVoice (ctx));
            ctx.seededRandom = true;
        }

        // Fija todos los parámetros: el patch + todo lo que no es receta en neutro (motor Synth, sin
        // efectos, sin Cosmos/Abismo, sin ADN de resíntesis)
        void setPatch (const SynthPatch& p)
        {
            for (int i = 0; i < ids.size(); ++i)
            {
                const auto& id = ids[i];
                values[(size_t) i] = isSynthParam (id) ? p.get (id) : defaults[(size_t) i];
            }
            set (ParamID::engine, (float) Engine::Synth);
            set (ParamID::transform, 0.0f);
            for (auto* id : { ParamID::macroBright, ParamID::macroBody, ParamID::macroMove, ParamID::macroSpace, ParamID::macroChaos }) set (id, 0.0f);
        }

        // Una nota (o varias a la vez), soltadas en holdS; devuelve estéreo
        juce::AudioBuffer<float> render (const std::vector<int>& notes, double holdS, double totalS, double sampleRate,
                                         juce::uint32 seed = 0x4E42, float velocity = 100.0f / 127.0f)
        {
            prepare (sampleRate);
            synth.allNotesOff (0, false);
            for (int v = 0; v < synth.getNumVoices(); ++v)
                if (auto* sv = dynamic_cast<SynthVoice*> (synth.getVoice (v))) { sv->resetState(); sv->prepare (sampleRate); }
            ctx.lastNote = -1.0f;
            ctx.renderSeed = seed;
            ctx.noteOnCount = 0;

            const int total = juce::jmax (1, (int) (totalS * sampleRate)), hold = (int) (holdS * sampleRate);
            juce::AudioBuffer<float> out (2, total);
            out.clear();
            static constexpr int block = 256;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = juce::jmin (block, total - pos);
                juce::MidiBuffer midi;
                if (pos == 0) for (int note : notes) midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
                // (renderNextBlock con startSample = pos: las posiciones del MIDI son absolutas dentro del búfer)
                if (hold >= pos && hold < pos + n) for (int note : notes) midi.addEvent (juce::MidiMessage::noteOff (1, note), hold);
                ctx.blockTime = pos / sampleRate;
                synth.renderNextBlock (out, midi, pos, n);
            }
            return out;
        }

        juce::AudioBuffer<float> render (const SynthPatch& p, int note, double holdS, double totalS, double sampleRate, juce::uint32 seed = 0x4E42)
        {
            setPatch (p);
            return render (std::vector<int> { note }, holdS, totalS, sampleRate, seed);
        }

    private:
        void prepare (double sr)
        {
            if (sr == currentRate) return;
            currentRate = sr;
            synth.setCurrentPlaybackSampleRate (sr);
            for (int v = 0; v < synth.getNumVoices(); ++v)
                if (auto* sv = dynamic_cast<SynthVoice*> (synth.getVoice (v))) sv->prepare (sr);
        }
        void set (const char* id, float v) { const int i = ids.indexOf (id); if (i >= 0) values[(size_t) i] = v; }
        std::atomic<float>* lookup (const juce::String& id)
        {
            if (! resolved) return &dummy;
            const int i = ids.indexOf (id);
            return i >= 0 ? &values[(size_t) i] : &dummy;
        }

        juce::StringArray ids;
        std::vector<float> defaults;
        std::unique_ptr<std::atomic<float>[]> values;
        std::atomic<float> dummy { 0.0f };
        bool resolved = false;
        ParamPointers pointers;
        SampleSource noSample;
        WavetableBank bank;
        VoiceContext ctx;
        NebulaSynth synth;
        double currentRate = 0.0;
    };
}
