#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Parameters.h"
#include "SampleData.h"
#include "Wavetable.h"
#include "DspParts.h"
#include "ResynthModel.h"
#include "Dna.h"
#include "Transform.h"

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Datos compartidos entre todas las voces (los actualiza el procesador)
struct VoiceContext
{
    VoiceContext (const ParamPointers& p, const SampleSource& s, const WavetableBank& t) : params (p), samples (s), tables (t) {}

    const ParamPointers& params;
    const SampleSource& samples;
    const WavetableBank& tables;

    // Reloj (al inicio del bloque actual)
    double blockTime = 0.0, blockBeats = 0.0, bpm = 120.0;

    // Controladores globales
    std::atomic<float> modWheel { 0.0f }, pressure { 0.0f };

    // Región elegida del sample (0..1) y corrección de nota raíz (semitonos)
    std::atomic<float> regionStart { 0.0f }, regionEnd { 1.0f }, rootOffset { 0.0f };

    float lastNote = -1.0f;   // para el glide

    // v3
    SharedHolder<ResynthModel> models;                 // modelo del motor de resíntesis
    std::atomic<bool> abOriginal { false };            // A/B: tocar el sample original
    std::atomic<float> abGain { 1.0f };
    std::atomic<float> masterBend { 0.0f };            // MPE: pitch bend del canal maestro

    // Azar reproducible: cada nota arranca con una semilla derivada de la semilla de la instancia,
    // del número de nota-on (desde el último prepare/reset) y de la nota. Así la misma configuración
    // y la misma secuencia MIDI dan exactamente el mismo audio, y cada golpe sigue siendo distinto.
    // "Legacy" (proyectos de antes de esta versión) conserva el azar libre de siempre.
    std::atomic<bool> seededRandom { true };
    std::atomic<juce::uint32> renderSeed { 0x4E424C41u };
    std::atomic<juce::uint64> noteOnCount { 0 };

    static juce::int64 noteSeed (juce::uint32 seed, juce::uint64 count, int note)
    {
        // splitmix64
        juce::uint64 z = ((juce::uint64) seed << 32) ^ (count * 0x9E3779B97F4A7C15ull) ^ (juce::uint64) (note & 0x7f);
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (juce::int64) (z ^ (z >> 31));
    }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int maxUnison = 7;
    static constexpr int controlRate = 16;

    explicit SynthVoice (VoiceContext& c) : ctx (c), params (c.params) {}

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        declickCoef = (float) std::exp (-1.0 / (0.003 * sampleRate));
        for (auto* e : { &ampEnv, &env2, &env3 }) e->setSampleRate (sampleRate);
        for (auto& f : filters) f.reset();
        rsPlayer.prepare (sampleRate);
        tf.prepare (sampleRate);
    }

    // Memoria para la Calidad más alta (4x): cambiar la Calidad mientras suena no reasigna en el hilo de audio
    void reserveFor (double maxSampleRate) { rsPlayer.reserve (maxSampleRate); tf.reserve (maxSampleRate); }

    // Deja la voz como recién creada (lo usa reset() en modo de azar reproducible)
    void resetState()
    {
        clearCurrentNote();
        ampEnv.reset(); env2.reset(); env3.reset();
        for (auto& f : filters) f = NebFilter();     // también su mezcla "abierto" (reset() la deja en el último valor)
        rsPlayer = ResynthPlayer();                  // prepare() se llama justo después
        for (auto& o : osc1) o = NebOsc();
        for (auto& o : osc2) o = NebOsc();
        sub = NebOsc();
        lfo1 = NebLfo(); lfo2 = NebLfo();
        tick = 0; legatoPending = false;
        velocity = 1.0f; wheel = 0.0f; polyPressure = 0.0f; targetNote = currentNote = 60.0f;
        env2Val = env3Val = 0.0f;
        volTarget = 1.0f; volSm = 1.0f;
        playhead = 0.0; grainTimer = 0;
        for (auto& g : grains) g.active = false;
        for (auto& g : cloud) g.active = false;
        cloudTimer = 0; newestGrain = -1; noiseState = 0.0f;
        timbre = 0.0f; noteDrift = 0.0f; driftA = driftB = 0.0f;
        chaosVal = chaosTarget = chaosTimer = 0.0f;
        abPos = 0.0;
        realTail = false; tailGain = 0.0f;
        lastL = lastR = declickL = declickR = 0.0f;
        tf.reset (1); tfSemis = 0.0f; noteCounter = 0;
    }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SynthSound*> (s) != nullptr; }

    void startNote (int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel) override
    {
        const int mode = params.i (P::polyMode);
        const bool legato = legatoPending && ampEnv.isActive() && mode != PolyMode::Poly;
        legatoPending = false;

        realTail = false;
        // semilla propia de Cosmos/Abismo (no consume números del azar de la voz: lo demás suena igual)
        juce::int64 tfSeed = (juce::int64) midiNote * 7919 + (juce::int64) (++noteCounter) * 104729;
        if (ctx.seededRandom.load())
        {
            const auto ns = VoiceContext::noteSeed (ctx.renderSeed.load(), ctx.noteOnCount.fetch_add (1), midiNote);
            random.setSeed (ns);
            tfSeed = (juce::int64) (ns ^ 0x5DEECE66Dull);
            if (! legato)
            {
                // la deriva analógica arranca en un punto de su recorrido normal (antes venía de la historia de la voz)
                driftA = (random.nextFloat() * 2.0f - 1.0f) * 0.055f;
                driftB = (random.nextFloat() * 2.0f - 1.0f) * 0.055f;
            }
        }

        velocity = vel;
        targetNote = (float) midiNote;
        pitchWheelMoved (pitchWheel);

        if (params[P::glide] > 0.0005f)
        {
            if (! legato)
                currentNote = ctx.lastNote >= 0.0f ? ctx.lastNote : targetNote;
            // en legato seguimos desde donde va el tono
        }
        else
            currentNote = targetNote;
        ctx.lastNote = targetNote;

        if (! legato)
        {
            if (! ampEnv.isActive())
            {
                for (auto& f : filters) f.reset();
                for (int u = 0; u < maxUnison; ++u)
                {
                    osc1[(size_t) u].reset (random, u > 0);
                    osc2[(size_t) u].reset (random, true);
                }
                sub.reset (random, false);
            }
            lfo1.noteOn (random);
            lfo2.noteOn (random);
            sample = ctx.samples.get();
            rsPlayer.start (ctx.models.get(), random);
            rsPlayer.seedMorph (tfSeed ^ 0x2545F491);
            tf.reset (tfSeed);
            tfSemis = 0.0f;
            noteDrift = random.nextFloat() * 2.0f - 1.0f;
            abPos = 0.0;
            playhead = 0.0;
            grainTimer = 0;
            for (auto& g : grains) g.active = false;
            for (auto& g : cloud) g.active = false;
            cloudTimer = 0;
            newestGrain = -1;
        }

        // En Legato la envolvente sigue; en Mono se reinicia desde donde va
        if (! legato || mode == PolyMode::Mono)
        {
            updateEnvelopes();
            ampEnv.noteOn (legato);
            env2.noteOn (legato);
            env3.noteOn (legato);
        }
        tick = 0;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            // Resíntesis con cola real: suena la cola del sample (la envolvente de suelta no la recorta)
            if (resynth && rsPlayer.hasRealTail() && ! ctx.abOriginal.load())
            {
                rsPlayer.release();
                realTail = true;
                tailGain = ampEnv.current();
                env2.noteOff(); env3.noteOff();
                return;
            }
            ampEnv.noteOff(); env2.noteOff(); env3.noteOff();
            return;
        }
        realTail = false;
        // En mono/legato "robar" la voz es en realidad un cambio de nota: no cortamos
        if (params.i (P::polyMode) != PolyMode::Poly && ampEnv.isActive() && isKeyDown())
        {
            legatoPending = true;
            clearCurrentNote();
            return;
        }
        // Corte inmediato (robo de voz): la señal saltaba de golpe a cero y hacía clic. Se guarda el último valor
        // y se deja caer en ~3 ms sumado a la nota nueva, así la onda no da un salto.
        if (ampEnv.isActive()) { declickL = lastL; declickR = lastR; }
        ampEnv.reset(); env2.reset(); env3.reset();
        clearCurrentNote();
    }

    void pitchWheelMoved (int value) override { wheel = (value - 8192) / 8192.0f; }
    void controllerMoved (int cc, int value) override { if (cc == 74) timbre = value / 127.0f; }   // MPE: "timbre"
    void aftertouchChanged (int value) override { polyPressure = value / 127.0f; }
    void channelPressureChanged (int value) override { polyPressure = value / 127.0f; }

    bool isSounding() const { return ampEnv.isActive(); }

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! ampEnv.isActive())
            return;

        readBlockParams();
        float* outL = out.getWritePointer (0);
        float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

        // A/B: el sample original, afinado a la nota, al mismo volumen que lo recreado
        if (ctx.abOriginal.load() && sample != nullptr)
        {
            const double ratio = std::exp2 ((targetNote - (sample->rootNote + ctx.rootOffset.load())) / 12.0) * sample->sampleRate / sr;
            const int len = sample->length();
            const double a = ctx.regionStart.load() * (len - 1), b = ctx.regionEnd.load() * (len - 1);
            const float gain = ctx.abGain.load() * velGain * 0.3f;
            for (int i = 0; i < num; ++i)
            {
                const float e = ampEnv.next();
                const double idx = a + abPos;
                float v = 0.0f;
                if (idx < b)
                {
                    const int i0 = (int) idx; const float fr = (float) (idx - i0);
                    v = sample->data()[i0] + (sample->data()[juce::jmin (len - 1, i0 + 1)] - sample->data()[i0]) * fr;
                }
                abPos += ratio;
                const float o = v * e * gain;
                if (outR != nullptr) { outL[start + i] += o; outR[start + i] += o; } else outL[start + i] += o;
                if (! ampEnv.isActive() || idx >= b + sample->sampleRate * 0.05) { ampEnv.reset(); clearCurrentNote(); break; }
            }
            return;
        }

        for (int i = 0; i < num; ++i)
        {
            if (tick-- <= 0)
            {
                tick = controlRate - 1;
                controlUpdate (start + i);
            }

            env2Val = env2.next();
            env3Val = env3.next();

            float L = 0.0f, R = 0.0f;
            generate (L, R);
            tf.process (L, R);   // Cosmos / Abismo (no hace nada en el centro)

            // Filtros
            if (f2On)
            {
                if (parallel)
                {
                    const float l1 = f1On ? filters[0].process (L) : L, r1 = f1On ? filters[1].process (R) : R;
                    const float l2 = filters[2].process (L), r2 = filters[3].process (R);
                    L = 0.5f * (l1 + l2); R = 0.5f * (r1 + r2);
                }
                else
                {
                    L = filters[2].process (f1On ? filters[0].process (L) : L);
                    R = filters[3].process (f1On ? filters[1].process (R) : R);
                }
            }
            else if (f1On)
            {
                L = filters[0].process (L);
                R = filters[1].process (R);
            }

            if (drive > 0.001f)
            {
                L = std::tanh (L * driveGain) * driveNorm;
                R = std::tanh (R * driveGain) * driveNorm;
            }

            volSm += (volTarget - volSm) * 0.01f;
            if (realTail && ! rsPlayer.hasRealTail()) { realTail = false; ampEnv.noteOff(); }   // el modelo cambió
            const float amp = (realTail ? tailGain : ampEnv.next()) * velGain * 0.3f * volSm;
            L *= amp * gainL;
            R *= amp * gainR;
            if (declickL != 0.0f || declickR != 0.0f)
            {
                L += declickL; R += declickR;
                declickL *= declickCoef; declickR *= declickCoef;
                if (std::abs (declickL) + std::abs (declickR) < 1.0e-6f) declickL = declickR = 0.0f;
            }
            lastL = L; lastR = R;

            if (outR != nullptr) { outL[start + i] += L; outR[start + i] += R; }
            else                 { outL[start + i] += 0.5f * (L + R); }

            if (realTail ? rsPlayer.finished() : ! ampEnv.isActive())
            {
                if (realTail) { realTail = false; ampEnv.reset(); env2.reset(); env3.reset(); }
                clearCurrentNote();
                for (auto& f : filters) f.reset();
                legatoPending = false;
                break;
            }
        }
    }

private:
    // ------------------------------------------------------------------
    void readBlockParams()
    {
        updateEnvelopes();
        if (sample == nullptr) sample = ctx.samples.get();
        engine = params.i (P::engine);
        sampler = engine == Engine::Sample && sample != nullptr && sample->length() > 4;
        resynth = engine == Engine::Resynth && rsPlayer.hasModel();
        if (resynth)
        {
            // ADN: genes en tiempo real y, si cambió la variante del modelo (genes estructurales), cambiarla en vivo
            const auto d = Dna::readRealtime (params);
            {
                ResynthPlayer::Morph mo;
                const auto ta = Transform::make (params[P::transform], params[P::identity], params[P::transformOut]);
                const float keep = 1.0f - ta.identity;
                mo.speedMul = 1.0f - 0.6f * ta.cosmos * keep;
                mo.wander = 0.12f * ta.cosmos * keep;
                mo.ghost = 0.6f * ta.cosmos * keep;
                mo.jumps = 7.0f * ta.abyss * ta.abyss * keep;
                rsPlayer.setMorph (mo);
            }
            ResynthPlayer::Genes g;
            g.pitchDepth = d.pitchDepth; g.attack = d.attack; g.release = d.release; g.transient = d.transient;
            g.noiseTilt = d.noiseTilt; g.rough = d.rough; g.density = d.density; g.freeze = d.freeze;
            rsPlayer.setGenes (g);
            auto latest = ctx.models.get();
            if (latest.get() != rsPlayer.current()) rsPlayer.swapModel (std::move (latest));
        }
        tfAmounts = Transform::make (params[P::transform], params[P::identity], params[P::transformOut]);
        osc1On = params[P::osc1On] > 0.5f; osc2On = params[P::osc2On] > 0.5f;
        subOn = params[P::subOn] > 0.5f;   noiseOn = params[P::noiseOn] > 0.5f;
        f1On = params[P::f1On] > 0.5f;

        wave1 = params.i (P::osc1Wave);  wave2 = params.i (P::osc2Wave);
        table1 = ctx.tables.get (params.i (P::osc1Table));
        table2 = ctx.tables.get (params.i (P::osc2Table));
        unison = (sampler || resynth || params[P::uniOn] < 0.5f) ? 1 : juce::jlimit (1, maxUnison, params.i (P::uniVoices));
        f2On = params[P::f2On] > 0.5f;
        parallel = params.i (P::filterRouting) == 1;
        // Locura: también empuja la saturación
        const float chaosAmt = params[P::macroChaos];
        drive = juce::jlimit (0.0f, 1.0f, (params[P::driveOn] > 0.5f ? params[P::drive] : 0.0f) + 0.5f * chaosAmt);
        driveGain = 1.0f + 12.0f * std::pow (drive, 0.75f);   // responde desde el principio de la perilla
        driveNorm = 1.0f / std::tanh (driveGain);
        const float vs = params[P::velSens];
        velGain = 1.0f - vs + vs * velocity;
        if (sampler) prepareSampleBlock();
    }

    void updateEnvelopes()
    {
        const float ts = params[P::modTime];
        ampEnv.setParams ({ params[P::ampA], params[P::ampD], params[P::ampS], params[P::ampR], params[P::ampCurve], false }, ts);
        env2.setParams ({ params[P::fltA], params[P::fltD], params[P::fltS], params[P::fltR], params[P::fltCurve], params.i (P::fltMode) == 1 }, ts);
        env3.setParams ({ params[P::env3A], params[P::env3D], params[P::env3S], params[P::env3R], params[P::env3Curve], params.i (P::env3Mode) == 1 }, ts);
    }

    float runLfo (NebLfo& lfo, P::Id rateId, P::Id shapeId, P::Id syncId, P::Id retrigId, P::Id fadeId, int sampleIndex)
    {
        const int syncIdx = params.i (syncId);
        double cps = syncIdx > 0 ? (ctx.bpm / 60.0) / syncBeats (syncIdx) : (double) params[rateId];
        cps /= juce::jmax (0.05f, params[P::modTime]);

        double freePhase = -1.0;
        if (params[retrigId] < 0.5f)
        {
            const double dt = sampleIndex / sr;
            freePhase = syncIdx > 0 ? (ctx.blockBeats + dt * ctx.bpm / 60.0) / syncBeats (syncIdx)
                                    : (ctx.blockTime + dt) * cps;
        }
        return lfo.next (params.i (shapeId), cps * controlRate / sr, freePhase, params[fadeId], sr / controlRate, random);
    }

    void controlUpdate (int sampleIndex)
    {
        // Un LFO apagado no mueve nada (ni sus perillas directas ni la matriz)
        const float l1 = params[P::lfoOn]  > 0.5f ? runLfo (lfo1, P::lfoRate, P::lfoShape, P::lfoSync, P::lfoRetrig, P::lfoFade, sampleIndex) : 0.0f;
        const float l2 = params[P::lfo2On] > 0.5f ? runLfo (lfo2, P::lfo2Rate, P::lfo2Shape, P::lfo2Sync, P::lfo2Retrig, P::lfo2Fade, sampleIndex) : 0.0f;

        // ---- Macros, locura y deriva analógica ----
        const float move = params[P::macroMove], chaos = params[P::macroChaos], body = params[P::macroBody];
        const float bright = params[P::macroBright], analog = params[P::analog];
        {
            const float dt = (float) controlRate / (float) sr;
            // caminatas aleatorias lentas (deriva) y rápidas (locura)
            driftA += (random.nextFloat() * 2.0f - 1.0f) * dt * 3.0f - driftA * dt * 0.5f;
            driftB += (random.nextFloat() * 2.0f - 1.0f) * dt * 3.0f - driftB * dt * 0.5f;
            chaosTarget = (chaosTimer -= dt) <= 0.0f ? (chaosTimer = 0.04f + 0.25f * random.nextFloat(), random.nextFloat() * 2.0f - 1.0f) : chaosTarget;
            chaosVal += (chaosTarget - chaosVal) * juce::jmin (1.0f, dt * 25.0f);
        }
        const bool mpe = params[P::mpeOn] > 0.5f;
        const float modWheelSrc = mpe ? juce::jmax (timbre, ctx.modWheel.load()) : ctx.modWheel.load();

        // ---- Matriz ----
        const float src[ModSrc::count] { env2Val, env3Val, l1, l2, velocity, modWheelSrc,
                                         juce::jmax (polyPressure, ctx.pressure.load()), (targetNote - 60.0f) / 24.0f };
        const float amount = params[P::modAmount];
        float m[ModDst::count] {};
        for (int s = 0; s < ModSrc::count; ++s)
        {
            if (std::abs (src[s]) < 1.0e-6f) continue;
            for (int d = 0; d < ModDst::count; ++d)
                m[d] += src[s] * params.mod (s, d);
        }
        for (auto& x : m) x *= amount;

        // ---- Glide ----
        const float glide = params[P::glide];
        if (glide > 0.0005f)
        {
            const float k = 1.0f - std::exp (-(float) controlRate / (glide * 0.35f * (float) sr));
            currentNote += (targetNote - currentNote) * k;
        }
        else currentNote = targetNote;

        // MPE: cada nota en su canal dobla hasta 48 semitonos; el canal 1 dobla a todas
        const bool memberChannel = mpe && ! isPlayingChannel (1);
        const float bend = wheel * (memberChannel ? 48.0f : (float) params.i (P::bendRange))
                         + (mpe ? ctx.masterBend.load() * (float) params.i (P::bendRange) : 0.0f);
        const float vib = l1 * params[P::lfoPitch] / 100.0f * (1.0f + 2.0f * move) + l2 * move * 0.15f;
        const float drift = analog * (0.08f * driftA + 0.05f * noteDrift);
        // Cosmos/Abismo: deriva lenta o caos por pasos de la afinación (0 si no se transforma)
        tfSemis = tf.control (tfAmounts, noteHz (currentNote), controlRate);
        const float pitch = currentNote + bend + vib + m[ModDst::Pitch] * 12.0f + drift + chaos * chaos * 0.8f * chaosVal + tfSemis;

        shape1   = juce::jlimit (0.0f, 1.0f, params[P::osc1Shape] + m[ModDst::Shape1]);
        shape2   = juce::jlimit (0.0f, 1.0f, params[P::osc2Shape] + m[ModDst::Shape2]);
        mix      = juce::jlimit (0.0f, 1.0f, params[P::oscMix] + m[ModDst::Mix]);
        fm       = juce::jlimit (0.0f, 1.0f, params[P::fmAmount] + m[ModDst::Fm] + 0.45f * chaos * (0.6f + 0.4f * chaosVal));
        subLvl   = subOn ? juce::jlimit (0.0f, 1.0f, params[P::subLevel] * (1.0f + juce::jmin (0.0f, body)) + juce::jmax (0.0f, body) * 0.5f) : 0.0f;
        noiseLvl = noiseOn ? params[P::noise] : 0.0f;
        noiseMod = noiseOn ? juce::jlimit (0.0f, 1.0f, m[ModDst::Noise]) : 0.0f;
        {
            const float col = params[P::noiseColor];
            if (col > 0.995f) noiseLp = 1.0f;
            else
            {
                const double fc = 200.0 * std::pow (100.0, (double) col);
                noiseLp = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * fc / sr));
                noiseGain = noiseGainFor (noiseLp);
            }
        }

        // ---- Frecuencias (con unísono) ----
        const float detune = params[P::uniDetune] * 0.6f;   // semitonos a cada lado
        const float width = params[P::uniWidth];
        const float octave = sampler ? 0.0f : 12.0f * (float) params.i (P::osc1Octave);
        const double note1 = pitch + octave;
        const double note2 = pitch + params[P::osc2Semi] + params[P::osc2Fine] / 100.0f;
        for (int u = 0; u < unison; ++u)
        {
            const float spread = unison > 1 ? (2.0f * (float) u / (float) (unison - 1) - 1.0f) : 0.0f;
            const float off = spread * detune;
            osc1[(size_t) u].setFrequency (noteHz (note1 + off), sr);
            osc2[(size_t) u].setFrequency (noteHz (note2 + off), sr);
            const float pan = spread * width;
            panL[(size_t) u] = std::sqrt (0.5f * (1.0f - pan));
            panR[(size_t) u] = std::sqrt (0.5f * (1.0f + pan));
        }
        uniNorm = unison > 1 ? 1.4f / std::sqrt ((float) unison) : 1.0f;
        sub.setFrequency (noteHz (pitch + octave - 12.0), sr);
        samplePitch = pitch;

        // ---- Filtros ----
        // Macros sobre el corte: Brillo lo abre/cierra, Movimiento lo mece, Locura lo sacude
        const float macroOct = bright * 2.5f + move * 0.8f * l2 + chaos * 2.0f * chaosVal + analog * 0.15f * driftB;
        const float c1 = params[P::cutoff] * std::exp2 (params[P::filterEnv] * 6.0f * env2Val + params[P::lfoFilter] * 3.0f * l1 * (1.0f + move)
                                                       + m[ModDst::Cutoff1] * 5.0f + macroOct);
        const float r1 = juce::jlimit (0.0f, 1.0f, params[P::resonance] + m[ModDst::Reso1]);
        const int t1 = params.i (P::filterType), s1 = params.i (P::filterSlope), ch1 = params.i (P::filterChar);
        filters[0].set (t1, s1, ch1, c1, r1, sr);
        filters[1].set (t1, s1, ch1, c1, r1, sr);
        if (f2On)
        {
            const float c2 = params[P::f2Cutoff] * std::exp2 (params[P::f2Env] * 6.0f * env2Val + m[ModDst::Cutoff2] * 5.0f + macroOct);
            const int t2 = params.i (P::f2Type), s2 = params.i (P::f2Slope), ch2 = params.i (P::f2Char);
            filters[2].set (t2, s2, ch2, c2, params[P::f2Reso], sr);
            filters[3].set (t2, s2, ch2, c2, params[P::f2Reso], sr);
        }

        // ---- Resíntesis ----
        if (resynth)
            rsPlayer.control (noteHz (pitch), params[P::rsSpeed], params[P::rsHarm], params[P::rsPartials], params[P::rsNoise],
                              params[P::rsStretch], params[P::rsPitch] > 0.5f, controlRate);

        // ---- Volumen y paneo ----
        volTarget = juce::jlimit (0.0f, 2.0f, 1.0f + m[ModDst::Volume]);
        const float pan = juce::jlimit (-1.0f, 1.0f, m[ModDst::Pan]);
        gainL = juce::jmin (1.0f, 1.0f - pan);
        gainR = juce::jmin (1.0f, 1.0f + pan);
    }

    // Ganancia para que el ruido filtrado conserve el mismo volumen (rms) que el blanco
    static float noiseGainFor (float a) { return std::sqrt ((2.0f - a) / juce::jmax (1.0e-4f, a)); }

    static double noteHz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

    void generate (float& L, float& R)
    {
        float centre = 0.0f;
        if (resynth)
        {
            rsPlayer.next (L, R, params[P::rsTone], params[P::rsWidth] * (ctx.models.get() != nullptr ? juce::jmax (0.3f, ctx.models.get()->width * 2.0f) : 1.0f), random);
            L *= 2.0f; R *= 2.0f;   // misma escala que un oscilador (después se multiplica por 0.5)
        }
        else if (sampler)
        {
            const double ratio = std::exp2 ((samplePitch - (sample->rootNote + ctx.rootOffset.load()) + smpTune) / 12.0) * sample->sampleRate / sr;
            const float main = 2.0f * ((tempoMode || smpTexture > 0.01f) ? nextGranular (ratio) : nextSample (ratio));
            if (grainAmt > 0.001f)
            {
                // Capa de granos encima del sonido: "Cantidad" mezcla y hace más densa la nube
                float cl = 0.0f, cr = 0.0f;
                nextCloud (ratio, cl, cr);
                centre = main * (1.0f - 0.65f * grainAmt);
                L += 2.0f * grainAmt * cl;
                R += 2.0f * grainAmt * cr;
            }
            else centre = main;
        }
        else
        {
            const Wavetable* tb1 = table1.get();
            const Wavetable* tb2 = table2.get();
            const double fmDepth = 1.5 * std::pow ((double) fm, 1.15);   // la FM se nota desde el principio de la perilla
            const float w1 = osc1On ? (osc2On ? 1.0f - mix : 1.0f) : 0.0f;
            const float w2 = osc2On ? (osc1On ? mix : 1.0f) : 0.0f;
            for (int u = 0; u < unison; ++u)
            {
                const float o2 = osc2[(size_t) u].next (wave2, shape2, tb2);
                const float o1 = osc1[(size_t) u].next (wave1, shape1, tb1, fmDepth * o2);
                const float s = (o1 * w1 + o2 * w2) * uniNorm;
                L += s * panL[(size_t) u] * 1.41421f;
                R += s * panR[(size_t) u] * 1.41421f;
            }
        }

        centre += sub.next (Wave::Sine, 0.0f, nullptr) * subLvl;
        L += centre; R += centre;

        if (noiseLvl > 0.0f || noiseMod > 0.0f)
        {
            float n = random.nextFloat() * 2.0f - 1.0f;
            if (noiseLp < 0.999f) { noiseState += noiseLp * (n - noiseState); n = noiseState * noiseGain; }   // color del ruido
            const float base = n * noiseLvl * 1.2f;
            L = L * (1.0f - noiseLvl) + base + n * noiseMod * 1.2f;   // la parte modulada se suma sin bajar el resto
            R = R * (1.0f - noiseLvl) + base + n * noiseMod * 1.2f;
        }
        L *= 0.5f; R *= 0.5f;
    }

    // ------------------------------------------------------------------
    // Nube de granos (Sample → Granos: Cantidad y Tipo)
    struct CloudGrain { bool active = false; double pos = 0.0, inc = 1.0; int age = 0, length = 1; float gl = 0.7f, gr = 0.7f; };

    void nextCloud (double ratio, float& outL, float& outR)
    {
        const double rate = 8.0 + 70.0 * grainAmt * grainAmt;   // granos por segundo
        const int baseLen = juce::jmax (32, grainLenOut);
        if (--cloudTimer <= 0)
        {
            cloudTimer = juce::jmax (1, (int) (sr / rate * (0.5 + random.nextDouble())));
            for (auto& g : cloud)
            {
                if (g.active) continue;
                const double srcRate = sample->sampleRate;
                double pos = playhead, inc = ratio, pan = (random.nextFloat() * 2.0f - 1.0f) * 0.3f * grainAmt;
                int len = baseLen;
                switch (grainType)
                {
                    case GrainType::Sharp:   pos += (random.nextDouble() * 2.0 - 1.0) * 0.005 * srcRate; break;
                    case GrainType::Scatter:
                        pos = random.nextDouble() * regionLen;
                        inc = ratio * std::exp2 ((random.nextDouble() * 2.0 - 1.0) * 0.25 / 12.0);
                        pan = (random.nextFloat() * 2.0f - 1.0f) * 0.95f;
                        len = juce::jmax (32, (int) (baseLen * (0.5 + random.nextDouble())));
                        break;
                    case GrainType::Reverse: inc = -ratio; pos += baseLen * ratio + (random.nextDouble() * 2.0 - 1.0) * 0.015 * srcRate; break;
                    case GrainType::Octave:  inc = ratio * (random.nextFloat() < 0.75f ? 2.0 : 1.5); pos += (random.nextDouble() * 2.0 - 1.0) * 0.01 * srcRate; break;
                    default:                 pos += (random.nextDouble() * 2.0 - 1.0) * 0.015 * srcRate; break;
                }
                if (smpLoop) { pos = std::fmod (pos, regionLen); if (pos < 0.0) pos += regionLen; }
                g = { true, juce::jmax (0.0, pos), inc, 0, len,
                      std::sqrt (0.5f * (1.0f - (float) pan)), std::sqrt (0.5f * (1.0f + (float) pan)) };
                break;
            }
        }

        const float overlap = (float) juce::jmax (1.0, rate * baseLen / sr);
        const float norm = 1.0f / std::sqrt (overlap);
        for (auto& g : cloud)
        {
            if (! g.active) continue;
            const float t = (float) g.age / (float) g.length;
            float w;
            if (grainType == GrainType::Sharp)
                w = (t < 0.03f ? t / 0.03f : std::exp (-6.0f * (t - 0.03f)));
            else
                w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * t);
            const float v = readRegion (g.pos) * w * norm * 1.41421f;
            outL += v * g.gl; outR += v * g.gr;
            g.pos += g.inc;
            if (++g.age >= g.length) g.active = false;
        }
    }

    // ------------------------------------------------------------------
    // Reproducción de sample (dentro de la región elegida)
    struct Grain { bool active = false; double pos = 0.0, inc = 1.0; int age = 0, length = 1; };

    void prepareSampleBlock()
    {
        const int len = sample->length();
        const float rs = juce::jlimit (0.0f, 0.99f, ctx.regionStart.load());
        const float re = juce::jlimit (rs + 0.005f, 1.0f, ctx.regionEnd.load());
        const double regionA = rs * (len - 1), regionB = re * (len - 1);
        smpStartPos  = juce::jlimit (regionA, regionB - 2.0, regionA + (double) params[P::smpStart] * (regionB - regionA));
        regionLen    = juce::jmax (2.0, regionB - smpStartPos);
        regionEndPos = regionB;
        smpTune      = params[P::smpTune];
        smpReverse   = params[P::smpReverse] > 0.5f;
        smpLoop      = params[P::smpLoop] > 0.5f;
        smpTexture   = params[P::smpTexture];
        grainLenOut  = juce::jmax (16, (int) (params[P::smpGrain] * sr));
        grainAmt     = params[P::grainsOn] > 0.5f ? params[P::smpGrainAmt] : 0.0f;
        grainType    = params.i (P::smpGrainType);

        // Modo Tempo: la sección dura N tiempos al BPM del proyecto
        tempoMode = params.i (P::smpPlayMode) == 1;
        if (tempoMode)
        {
            const double regionSeconds = (regionB - regionA) / sample->sampleRate;
            double beats = beatsFor (params.i (P::smpBeats));
            if (beats <= 0.0)   // Auto: la potencia de 2 más cercana
            {
                const double raw = juce::jmax (0.25, regionSeconds * ctx.bpm / 60.0);
                beats = std::exp2 (std::round (std::log2 (raw)));
            }
            const double targetSeconds = beats * 60.0 / juce::jmax (20.0, ctx.bpm);
            tempoSpeed = (regionB - regionA) / (targetSeconds * sr);   // muestras del sample por muestra de salida
            grainLenOut = juce::jmax (grainLenOut, (int) (0.06 * sr));  // granos más largos = más limpio
        }
    }

    float readRegion (double p) const
    {
        if (smpLoop) { p = std::fmod (p, regionLen); if (p < 0.0) p += regionLen; }
        else if (p < 0.0 || p >= regionLen) return 0.0f;

        const double idx = smpReverse ? regionEndPos - p : smpStartPos + p;
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

    // Busca, cerca de `nominal`, el punto cuya forma de onda más se parece a la que
    // está sonando ahora en `continuation` (correlación cruzada, búsqueda gruesa y luego fina).
    double alignGrain (double nominal, double continuation) const
    {
        const float* d = sample->data();
        const int n = sample->length();
        const int tol = juce::jmax (8, (int) (sample->sampleRate * 0.008));   // ±8 ms
        const int len = juce::jmax (32, (int) (sample->sampleRate * 0.006));  // ventana de 6 ms
        auto srcIndex = [this] (double p) { return smpReverse ? regionEndPos - p : smpStartPos + p; };
        const int c0 = (int) srcIndex (continuation);
        const int dir = smpReverse ? -1 : 1;
        auto score = [&] (int offset)
        {
            const int s0 = (int) srcIndex (nominal + offset);
            double acc = 0.0, e = 1.0e-9;
            for (int k = 0; k < len; k += 2)
            {
                const int a = s0 + dir * k, b = c0 + dir * k;
                if (a < 0 || a >= n || b < 0 || b >= n) break;
                acc += d[a] * d[b];
                e += d[a] * d[a];
            }
            return acc / std::sqrt (e);
        };
        int best = 0; double bestScore = -1.0e30;
        for (int o = -tol; o <= tol; o += 4)
            if (nominal + o >= 0.0) { const double sc = score (o); if (sc > bestScore) { bestScore = sc; best = o; } }
        const int coarse = best;
        for (int o = coarse - 3; o <= coarse + 3; ++o)
            if (nominal + o >= 0.0) { const double sc = score (o); if (sc > bestScore) { bestScore = sc; best = o; } }
        return juce::jmax (0.0, nominal + best);
    }

    float nextGranular (double ratio)
    {
        const double timeSpeed = tempoMode ? tempoSpeed : (sample->sampleRate / sr) * (1.0 - 0.9 * smpTexture);
        const double grainSrcLen = grainLenOut * ratio;

        const bool finished = tempoMode && ! smpLoop && playhead >= regionLen;
        if (! finished && --grainTimer <= 0)
        {
            grainTimer = juce::jmax (1, grainLenOut / 4);
            for (int gi = 0; gi < (int) grains.size(); ++gi)
            {
                auto& g = grains[(size_t) gi];
                if (g.active) continue;
                const double jitter = (random.nextDouble() * 2.0 - 1.0) * smpTexture * juce::jmin (regionLen * 0.15, sample->sampleRate * 0.3);
                const double detune = (random.nextDouble() * 2.0 - 1.0) * smpTexture * 0.012;
                double start = juce::jmax (0.0, playhead + jitter);
                // Modo Tempo: el nuevo grano se alinea con la forma de onda del anterior (WSOLA)
                // para que no haya cancelaciones de fase ("sonido hueco / metálico").
                if (tempoMode && smpTexture < 0.5f && newestGrain >= 0 && grains[(size_t) newestGrain].active)
                    start = alignGrain (start, grains[(size_t) newestGrain].pos);
                g = { true, start, ratio * (1.0 + detune), 0, grainLenOut };
                newestGrain = gi;
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
        if (tempoMode)
        {
            if (smpLoop && playhead >= regionLen) playhead -= regionLen;   // sin loop: termina y se calla
        }
        else if (playhead + grainSrcLen >= regionLen)
            playhead = smpLoop ? 0.0 : juce::jmax (0.0, regionLen - grainSrcLen - 1.0);
        return out * 0.5f;
    }

    // ------------------------------------------------------------------
    VoiceContext& ctx;
    const ParamPointers& params;
    double sr = 44100.0;

    std::array<NebOsc, maxUnison> osc1, osc2;
    NebOsc sub;
    std::array<NebFilter, 4> filters;   // F1 L/R, F2 L/R
    NebEnv ampEnv, env2, env3;
    NebLfo lfo1, lfo2;
    juce::Random random;

    std::shared_ptr<const Wavetable> table1, table2;
    std::shared_ptr<const SampleData> sample;

    int wave1 = 2, wave2 = 3, unison = 1, tick = 0;
    bool sampler = false, f2On = false, parallel = false, legatoPending = false;
    float velocity = 1.0f, wheel = 0.0f, polyPressure = 0.0f, targetNote = 60.0f, currentNote = 60.0f;
    float env2Val = 0.0f, env3Val = 0.0f;
    float shape1 = 0.5f, shape2 = 0.5f, mix = 0.5f, fm = 0.0f, subLvl = 0.0f, noiseLvl = 0.0f, uniNorm = 1.0f;
    float drive = 0.0f, driveGain = 1.0f, driveNorm = 1.0f, velGain = 1.0f;
    float volTarget = 1.0f, volSm = 1.0f, gainL = 1.0f, gainR = 1.0f, samplePitch = 60.0f;
    std::array<float, maxUnison> panL {}, panR {};

    double playhead = 0.0, smpStartPos = 0.0, regionLen = 1.0, regionEndPos = 1.0;
    float smpTune = 0.0f, smpTexture = 0.0f;
    bool smpReverse = false, smpLoop = false, tempoMode = false;
    double tempoSpeed = 1.0;
    int grainLenOut = 1000, grainTimer = 0;
    std::array<Grain, 8> grains;
    std::array<CloudGrain, 32> cloud;
    float noiseMod = 0.0f, noiseLp = 1.0f, noiseGain = 1.0f, noiseState = 0.0f;
    int cloudTimer = 0, grainType = 0;
    float grainAmt = 0.0f;
    int newestGrain = -1;

    // v3
    ResynthPlayer rsPlayer;
    TransformVoice tf;
    Transform::Amounts tfAmounts;
    float tfSemis = 0.0f;
    juce::int64 noteCounter = 0;
    int engine = 0;
    bool resynth = false, osc1On = true, osc2On = true, subOn = true, noiseOn = true, f1On = true;
    float timbre = 0.0f, noteDrift = 0.0f, driftA = 0.0f, driftB = 0.0f;
    float chaosVal = 0.0f, chaosTarget = 0.0f, chaosTimer = 0.0f;
    double abPos = 0.0;
    bool realTail = false;     // sonando la cola real del modelo (tras soltar la tecla)
    float lastL = 0.0f, lastR = 0.0f, declickL = 0.0f, declickR = 0.0f, declickCoef = 0.993f;
    float tailGain = 0.0f;
};

// ---------------------------------------------------------------------------
// Sintetizador: Poli/Mono/Legato, número de voces y pila de notas en mono
class NebulaSynth : public juce::Synthesiser
{
public:
    explicit NebulaSynth (VoiceContext& c) : ctx (c) {}

    void noteOn (int channel, int note, float vel) override
    {
        held.removeAllInstancesOf (note);
        held.add (note);
        lastVelocity = vel;
        juce::Synthesiser::noteOn (channel, note, vel);
    }

    void noteOff (int channel, int note, float vel, bool allowTailOff) override
    {
        held.removeAllInstancesOf (note);
        if (mode() != PolyMode::Poly && ! held.isEmpty())
        {
            // Mono: si sueltas la nota que suena y hay otra apretada, regresa a esa
            bool sounding = false;
            for (auto* v : voices) if (v->getCurrentlyPlayingNote() == note) sounding = true;
            if (sounding)
            {
                juce::Synthesiser::noteOn (channel, held.getLast(), lastVelocity);
                return;
            }
        }
        juce::Synthesiser::noteOff (channel, note, vel, allowTailOff);
    }

    void handlePitchWheel (int channel, int value) override
    {
        // MPE: el canal 1 es el maestro y dobla todas las notas
        if (ctx.params[P::mpeOn] > 0.5f && channel == 1) { ctx.masterBend = (value - 8192) / 8192.0f; return; }
        juce::Synthesiser::handlePitchWheel (channel, value);
    }

    void handleController (int channel, int cc, int value) override
    {
        if (cc == 1) ctx.modWheel = value / 127.0f;
        juce::Synthesiser::handleController (channel, cc, value);
    }

    void handleChannelPressure (int channel, int value) override
    {
        ctx.pressure = value / 127.0f;
        juce::Synthesiser::handleChannelPressure (channel, value);
    }

    int activeVoiceCount() const
    {
        int n = 0;
        for (auto* v : voices) if (auto* sv = dynamic_cast<SynthVoice*> (v); sv != nullptr && sv->isSounding()) ++n;
        return n;
    }

protected:
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* sound, int channel, int note, bool steal) const override
    {
        if (mode() != PolyMode::Poly)
            return voices[0];

        const int limit = voiceLimit();
        for (int i = 0; i < limit; ++i)
            if (! voices[i]->isVoiceActive() && ! isSounding (voices[i]) && voices[i]->canPlaySound (sound))
                return voices[i];
        for (int i = 0; i < limit; ++i)
            if (! voices[i]->isVoiceActive() && voices[i]->canPlaySound (sound))
                return voices[i];

        return steal ? findVoiceToSteal (sound, channel, note) : nullptr;
    }

    juce::SynthesiserVoice* findVoiceToSteal (juce::SynthesiserSound*, int, int) const override
    {
        const int limit = voiceLimit();
        juce::SynthesiserVoice* oldestReleased = nullptr;
        juce::SynthesiserVoice* oldest = nullptr;
        for (int i = 0; i < limit; ++i)
        {
            auto* v = voices[i];
            if (! v->isKeyDown() && (oldestReleased == nullptr || v->wasStartedBefore (*oldestReleased))) oldestReleased = v;
            if (oldest == nullptr || v->wasStartedBefore (*oldest)) oldest = v;
        }
        return oldestReleased != nullptr ? oldestReleased : oldest;
    }

private:
    static bool isSounding (juce::SynthesiserVoice* v)
    {
        auto* sv = dynamic_cast<SynthVoice*> (v);
        return sv != nullptr && sv->isSounding();
    }
    int voiceLimit() const { return juce::jmin (voices.size(), juce::jlimit (1, 16, ctx.params.i (P::voiceCount))); }
    int mode() const { return ctx.params.i (P::polyMode); }

    VoiceContext& ctx;
    juce::Array<int> held;
    float lastVelocity = 0.8f;
};
