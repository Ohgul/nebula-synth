#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Parameters.h"
#include "SampleData.h"
#include "Wavetable.h"
#include "DspParts.h"

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
        for (auto* e : { &ampEnv, &env2, &env3 }) e->setSampleRate (sampleRate);
        for (auto& f : filters) f.reset();
    }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SynthSound*> (s) != nullptr; }

    void startNote (int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel) override
    {
        const int mode = params.i (P::polyMode);
        const bool legato = legatoPending && ampEnv.isActive() && mode != PolyMode::Poly;
        legatoPending = false;

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
            playhead = 0.0;
            grainTimer = 0;
            for (auto& g : grains) g.active = false;
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
            ampEnv.noteOff(); env2.noteOff(); env3.noteOff();
            return;
        }
        // En mono/legato "robar" la voz es en realidad un cambio de nota: no cortamos
        if (params.i (P::polyMode) != PolyMode::Poly && ampEnv.isActive() && isKeyDown())
        {
            legatoPending = true;
            clearCurrentNote();
            return;
        }
        ampEnv.reset(); env2.reset(); env3.reset();
        clearCurrentNote();
    }

    void pitchWheelMoved (int value) override { wheel = (value - 8192) / 8192.0f; }
    void controllerMoved (int, int) override {}
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

            // Filtros
            if (f2On)
            {
                if (parallel)
                {
                    const float l1 = filters[0].process (L), r1 = filters[1].process (R);
                    const float l2 = filters[2].process (L), r2 = filters[3].process (R);
                    L = 0.5f * (l1 + l2); R = 0.5f * (r1 + r2);
                }
                else
                {
                    L = filters[2].process (filters[0].process (L));
                    R = filters[3].process (filters[1].process (R));
                }
            }
            else
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
            const float amp = ampEnv.next() * velGain * 0.3f * volSm;
            L *= amp * gainL;
            R *= amp * gainR;

            if (outR != nullptr) { outL[start + i] += L; outR[start + i] += R; }
            else                 { outL[start + i] += 0.5f * (L + R); }

            if (! ampEnv.isActive())
            {
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
        sampler = params.i (P::engine) == 1 && sample != nullptr && sample->length() > 4;

        wave1 = params.i (P::osc1Wave);  wave2 = params.i (P::osc2Wave);
        table1 = ctx.tables.get (params.i (P::osc1Table));
        table2 = ctx.tables.get (params.i (P::osc2Table));
        unison = sampler ? 1 : juce::jlimit (1, maxUnison, params.i (P::uniVoices));
        f2On = params[P::f2On] > 0.5f;
        parallel = params.i (P::filterRouting) == 1;
        drive = params[P::drive];
        driveGain = 1.0f + drive * 9.0f;
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
        const float l1 = runLfo (lfo1, P::lfoRate, P::lfoShape, P::lfoSync, P::lfoRetrig, P::lfoFade, sampleIndex);
        const float l2 = runLfo (lfo2, P::lfo2Rate, P::lfo2Shape, P::lfo2Sync, P::lfo2Retrig, P::lfo2Fade, sampleIndex);

        // ---- Matriz ----
        const float src[ModSrc::count] { env2Val, env3Val, l1, l2, velocity, ctx.modWheel.load(),
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

        const float bend = wheel * (float) params.i (P::bendRange);
        const float vib = l1 * params[P::lfoPitch] / 100.0f;
        const float pitch = currentNote + bend + vib + m[ModDst::Pitch] * 12.0f;

        shape1   = juce::jlimit (0.0f, 1.0f, params[P::osc1Shape] + m[ModDst::Shape1]);
        shape2   = juce::jlimit (0.0f, 1.0f, params[P::osc2Shape] + m[ModDst::Shape2]);
        mix      = juce::jlimit (0.0f, 1.0f, params[P::oscMix] + m[ModDst::Mix]);
        fm       = juce::jlimit (0.0f, 1.0f, params[P::fmAmount] + m[ModDst::Fm]);
        subLvl   = params[P::subLevel];
        noiseLvl = params[P::noise];

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
        const float c1 = params[P::cutoff] * std::exp2 (params[P::filterEnv] * 6.0f * env2Val + params[P::lfoFilter] * 3.0f * l1 + m[ModDst::Cutoff1] * 5.0f);
        const float r1 = juce::jlimit (0.0f, 1.0f, params[P::resonance] + m[ModDst::Reso1]);
        const int t1 = params.i (P::filterType), s1 = params.i (P::filterSlope), ch1 = params.i (P::filterChar);
        filters[0].set (t1, s1, ch1, c1, r1, sr);
        filters[1].set (t1, s1, ch1, c1, r1, sr);
        if (f2On)
        {
            const float c2 = params[P::f2Cutoff] * std::exp2 (params[P::f2Env] * 6.0f * env2Val + m[ModDst::Cutoff2] * 5.0f);
            const int t2 = params.i (P::f2Type), s2 = params.i (P::f2Slope), ch2 = params.i (P::f2Char);
            filters[2].set (t2, s2, ch2, c2, params[P::f2Reso], sr);
            filters[3].set (t2, s2, ch2, c2, params[P::f2Reso], sr);
        }

        // ---- Volumen y paneo ----
        volTarget = juce::jlimit (0.0f, 2.0f, 1.0f + m[ModDst::Volume]);
        const float pan = juce::jlimit (-1.0f, 1.0f, m[ModDst::Pan]);
        gainL = juce::jmin (1.0f, 1.0f - pan);
        gainR = juce::jmin (1.0f, 1.0f + pan);
    }

    static double noteHz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

    void generate (float& L, float& R)
    {
        float centre = 0.0f;
        if (sampler)
        {
            const double ratio = std::exp2 ((samplePitch - (sample->rootNote + ctx.rootOffset.load()) + smpTune) / 12.0) * sample->sampleRate / sr;
            centre = 2.0f * ((tempoMode || smpTexture > 0.01f) ? nextGranular (ratio) : nextSample (ratio));
        }
        else
        {
            const Wavetable* tb1 = table1.get();
            const Wavetable* tb2 = table2.get();
            const double fmDepth = (double) fm * fm * 1.5;
            for (int u = 0; u < unison; ++u)
            {
                const float o2 = osc2[(size_t) u].next (wave2, shape2, tb2);
                const float o1 = osc1[(size_t) u].next (wave1, shape1, tb1, fmDepth * o2);
                const float s = (o1 * (1.0f - mix) + o2 * mix) * uniNorm;
                L += s * panL[(size_t) u] * 1.41421f;
                R += s * panR[(size_t) u] * 1.41421f;
            }
        }

        centre += sub.next (Wave::Sine, 0.0f, nullptr) * subLvl;
        L += centre; R += centre;

        if (noiseLvl > 0.0f)
        {
            const float n = (random.nextFloat() * 2.0f - 1.0f) * noiseLvl * 1.2f;
            L = L * (1.0f - noiseLvl) + n;
            R = R * (1.0f - noiseLvl) + n;
        }
        L *= 0.5f; R *= 0.5f;
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
    int newestGrain = -1;
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
