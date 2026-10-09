#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SampleData.h"

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Oscilador con anti-aliasing PolyBLEP (sierra y cuadrada suenan limpias en agudos)
struct BlepOscillator
{
    double phase = 0.0, increment = 0.0;
    int wave = 2; // 0 seno, 1 triángulo, 2 sierra, 3 cuadrada

    void setFrequency (double hz, double sampleRate) { increment = juce::jlimit (0.0, 0.5, hz / sampleRate); }

    static float polyBlep (double t, double dt)
    {
        if (t < dt)        { t /= dt;               return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt)  { t = (t - 1.0) / dt;    return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    // pm = modulación de fase (para FM), en ciclos
    float next (double pm = 0.0)
    {
        double t = phase + pm;
        t -= std::floor (t);
        float v = 0.0f;

        switch (wave)
        {
            case 0:  v = (float) std::sin (juce::MathConstants<double>::twoPi * t); break;
            case 1:  v = (float) (1.0 - 4.0 * std::abs (t - 0.5)); break;
            case 2:  v = (float) (2.0 * t - 1.0) - polyBlep (t, increment); break;
            default:
            {
                v = t < 0.5 ? 1.0f : -1.0f;
                v += polyBlep (t, increment);
                v -= polyBlep (std::fmod (t + 0.5, 1.0), increment);
                break;
            }
        }

        phase += increment;
        if (phase >= 1.0) phase -= 1.0;
        return v;
    }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    SynthVoice (const ParamPointers& p, const SampleSource& s) : params (p), sampleSource (s) {}

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        ampEnv.setSampleRate (sampleRate);
        fltEnv.setSampleRate (sampleRate);
    }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SynthSound*> (s) != nullptr; }

    void startNote (int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel) override
    {
        noteNumber = midiNote;
        velocity = vel;
        pitchWheelMoved (pitchWheel);

        // Reiniciamos fases solo si la voz estaba en silencio (evita clics al robar voces)
        if (! ampEnv.isActive())
        {
            filter.reset();
            osc1.phase = 0.0;
            osc2.phase = random.nextDouble();
            sub.phase = 0.0;
        }
        lfoPhase = 0.0;
        sampleCounter = 0; // fuerza actualizar frecuencia y filtro en la primera muestra

        // Modo sample: tomamos el sample actual y reiniciamos la lectura
        sample = sampleSource.get();
        playhead = 0.0;
        grainTimer = 0;
        for (auto& g : grains) g.active = false;

        updateEnvelopes();
        ampEnv.noteOn();
        fltEnv.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            fltEnv.noteOff();
        }
        else
        {
            ampEnv.reset();
            fltEnv.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int value) override { bendSemis = 2.0 * (value - 8192) / 8192.0; }
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;

        updateEnvelopes();

        osc1.wave = (int) params.osc1Wave->load();
        osc2.wave = (int) params.osc2Wave->load();
        sub.wave  = 0;

        const float mix      = params.oscMix->load();
        const float subLvl   = params.subLevel->load();
        const float baseCut  = params.cutoff->load();
        const float envAmt   = params.filterEnv->load();
        const float drive    = params.drive->load();
        const float lfoRate  = params.lfoRate->load();
        const float lfoFlt   = params.lfoFilter->load();
        const float lfoPitch = params.lfoPitch->load() / 100.0f; // en semitonos

        const float fm       = params.fmAmount->load();
        const float noiseLvl = params.noise->load();

        const int fType = (int) params.filterType->load();
        if (fType != currentFilterType)
        {
            currentFilterType = fType;
            filter.setType (fType == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                          : fType == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                       : juce::dsp::StateVariableTPTFilterType::lowpass);
        }

        const float q = 0.707f * std::pow (14.0f, params.resonance->load());
        filter.setResonance (q);

        const bool samplerMode = params.engine->load() > 0.5f && sample != nullptr && sample->length() > 4;
        if (samplerMode) prepareSampleBlock();

        const double note1 = samplerMode ? noteNumber + bendSemis
                                         : noteNumber + 12.0 * params.osc1Octave->load() + bendSemis;
        const double note2 = noteNumber + params.osc2Semi->load() + params.osc2Fine->load() / 100.0 + bendSemis;

        const float driveGain = 1.0f + drive * 9.0f;
        const float driveNorm = 1.0f / std::tanh (driveGain);
        const float velGain   = 0.35f + 0.65f * velocity;
        const float maxCut    = (float) juce::jmin (20000.0, sr * 0.45);
        const double lfoInc   = lfoRate / sr;

        for (int i = 0; i < num; ++i)
        {
            const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase);
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;

            const float fEnv = fltEnv.getNextSample();

            // Actualizamos frecuencias cada 16 muestras (más que suficiente y ahorra CPU)
            if ((sampleCounter++ & 15) == 0)
            {
                const double vib = lfo * lfoPitch;
                osc1.setFrequency (juce::MidiMessage::getMidiNoteInHertz (0) * std::pow (2.0, (note1 + vib) / 12.0), sr);
                osc2.setFrequency (juce::MidiMessage::getMidiNoteInHertz (0) * std::pow (2.0, (note2 + vib) / 12.0), sr);
                sub.setFrequency  (juce::MidiMessage::getMidiNoteInHertz (0) * std::pow (2.0, (note1 - 12.0 + vib) / 12.0), sr);

                const float octaves = envAmt * 6.0f * fEnv + lfoFlt * 3.0f * lfo;
                filter.setCutoffFrequency (juce::jlimit (20.0f, maxCut, baseCut * std::pow (2.0f, octaves)));
            }

            float s;
            if (samplerMode)
            {
                const double ratio = std::pow (2.0, (noteNumber + bendSemis + lfo * lfoPitch - sample->rootNote + smpTune) / 12.0)
                                     * sample->sampleRate / sr;
                s = 2.0f * (smpTexture > 0.01f ? nextGranular (ratio) : nextSample (ratio)) + sub.next() * subLvl;
            }
            else
            {
                // El oscilador 2 también modula la fase del 1 (FM)
                const float o2 = osc2.next();
                const float o1 = osc1.next (fm * fm * 1.5 * o2);
                s = o1 * (1.0f - mix) + o2 * mix + sub.next() * subLvl;
            }
            // Ruido: mezcla gradual (al 100 % solo queda ruido)
            if (noiseLvl > 0.0f)
                s = s * (1.0f - noiseLvl) + (random.nextFloat() * 2.0f - 1.0f) * noiseLvl * 1.2f;
            s *= 0.5f;

            s = filter.processSample (0, s);
            if (drive > 0.001f)
                s = std::tanh (s * driveGain) * driveNorm;

            s *= ampEnv.getNextSample() * velGain * 0.3f;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, start + i, s);

            if (! ampEnv.isActive())
            {
                clearCurrentNote();
                filter.reset();
                break;
            }
        }
    }

private:
    //==========================================================================
    // Reproducción de sample

    struct Grain
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;   // en muestras del sample (coordenada de la región)
        int age = 0, length = 1;
    };

    void prepareSampleBlock()
    {
        const int len = sample->length();
        smpStartPos = juce::jlimit (0.0, (double) len - 2.0, (double) params.smpStart->load() * (len - 1));
        regionLen   = juce::jmax (2.0, (double) len - 1.0 - smpStartPos);
        smpTune     = params.smpTune->load();
        smpReverse  = params.smpReverse->load() > 0.5f;
        smpLoop     = params.smpLoop->load() > 0.5f;
        smpTexture  = params.smpTexture->load();
        grainLenOut = juce::jmax (16, (int) (params.smpGrain->load() * sr));
    }

    // Lee el sample en una posición de la región (con interpolación cúbica)
    float readRegion (double p) const
    {
        if (smpLoop) { p = std::fmod (p, regionLen); if (p < 0.0) p += regionLen; }
        else if (p < 0.0 || p >= regionLen) return 0.0f;

        const double idx = smpReverse ? (sample->length() - 1) - (smpStartPos + p) : smpStartPos + p;
        const float* d = sample->data();
        const int n = sample->length();
        const int i1 = (int) std::floor (idx);
        const float t = (float) (idx - i1);
        auto at = [d, n] (int i) { return d[juce::jlimit (0, n - 1, i)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    float nextSample (double ratio)
    {
        const float v = readRegion (playhead);
        playhead += ratio;
        if (smpLoop && playhead >= regionLen) playhead -= regionLen;
        return v;
    }

    // Granular: el tiempo avanza más lento (textura) y el tono no depende de la velocidad
    float nextGranular (double ratio)
    {
        const double timeSpeed = (sample->sampleRate / sr) * (1.0 - 0.9 * smpTexture);
        const double grainSrcLen = grainLenOut * ratio;

        if (--grainTimer <= 0)
        {
            grainTimer = juce::jmax (1, grainLenOut / 4);
            for (auto& g : grains)
            {
                if (g.active) continue;
                const double jitter = (random.nextDouble() * 2.0 - 1.0) * smpTexture * juce::jmin (regionLen * 0.15, sample->sampleRate * 0.3);
                const double detune = (random.nextDouble() * 2.0 - 1.0) * smpTexture * 0.012;
                g.active = true;
                g.pos = juce::jmax (0.0, playhead + jitter);
                g.inc = ratio * (1.0 + detune);
                g.age = 0;
                g.length = grainLenOut;
                break;
            }
        }

        float out = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.length);
            out += readRegion (g.pos) * w;
            g.pos += g.inc;
            if (++g.age >= g.length) g.active = false;
        }

        playhead += timeSpeed;
        if (playhead + grainSrcLen >= regionLen)
            playhead = smpLoop ? 0.0 : juce::jmax (0.0, regionLen - grainSrcLen - 1.0); // sin loop: se "congela" al final

        return out * 0.5f; // 4 granos solapados con ventana Hann suman ~2
    }

    //==========================================================================
    void updateEnvelopes()
    {
        ampEnv.setParameters ({ params.ampA->load(), params.ampD->load(), params.ampS->load(), params.ampR->load() });
        fltEnv.setParameters ({ params.fltA->load(), params.fltD->load(), params.fltS->load(), params.fltR->load() });
    }

    const ParamPointers& params;
    const SampleSource& sampleSource;
    double sr = 44100.0;

    std::shared_ptr<const SampleData> sample;
    double playhead = 0.0, smpStartPos = 0.0, regionLen = 1.0;
    float smpTune = 0.0f, smpTexture = 0.0f;
    bool smpReverse = false, smpLoop = false;
    int grainLenOut = 1000, grainTimer = 0;
    std::array<Grain, 8> grains;

    BlepOscillator osc1, osc2, sub;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::ADSR ampEnv, fltEnv;

    juce::Random random;
    double lfoPhase = 0.0, bendSemis = 0.0;
    int noteNumber = 60;
    float velocity = 1.0f;
    unsigned int sampleCounter = 0;
    int currentFilterType = 0;
};
