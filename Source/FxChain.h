#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"

// ===========================================================================
// Efectos de salida: Brillo/Cuerpo (macros) → EQ → Compresor → Chorus → Delay → Reverb → Ancho
// Cada efecto tiene su botón de encendido; apagado no gasta CPU.
// ===========================================================================
class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };
        for (auto* f : { &tiltLow, &tiltHigh, &eqLow, &eqMid, &eqHigh }) { f->prepare (spec); f->reset(); }
        chorusBuf.assign (2, std::vector<float> ((size_t) (sr * 0.05) + 4, 0.0f));
        delayBuf.assign (2, std::vector<float> ((size_t) (sr * 2.1) + 4, 0.0f));
        chorusPos = delayPos = 0;
        reverb.prepare (sr);
        compEnv = 0.0f;
        delayLp[0] = delayLp[1] = 0.0f;
        smoothDelay = -1.0;
    }

    // Vuelve al estado recién preparado (sin reservar memoria)
    void reset()
    {
        for (auto* f : { &tiltLow, &tiltHigh, &eqLow, &eqMid, &eqHigh }) f->reset();
        for (auto& b : chorusBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : delayBuf) std::fill (b.begin(), b.end(), 0.0f);
        chorusPos = delayPos = 0;
        chorusPhase = 0.0;
        reverb.clear();
        compEnv = 0.0f;
        delayLp[0] = delayLp[1] = 0.0f;
        smoothDelay = -1.0;
    }

    void process (juce::AudioBuffer<float>& buffer, const ParamPointers& p, double bpm)
    {
        const int n = buffer.getNumSamples();
        if (buffer.getNumChannels() < 2) return;
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);

        const float bright = p[P::macroBright], body = p[P::macroBody], move = p[P::macroMove], space = p[P::macroSpace];

        // ---- Brillo y Cuerpo (macros): estantes suaves ----
        if (std::abs (bright) > 0.01f || std::abs (body) > 0.01f)
        {
            *tiltHigh.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sr, 3500.0, 0.6, juce::Decibels::decibelsToGain (bright * 7.0f));
            *tiltLow.state  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sr, 160.0, 0.6, juce::Decibels::decibelsToGain (body * 6.0f));
            juce::dsp::AudioBlock<float> blk (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            tiltHigh.process (ctx); tiltLow.process (ctx);
        }

        // ---- EQ ----
        if (p[P::fxEqOn] > 0.5f)
        {
            *eqLow.state  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sr, 120.0, 0.7, juce::Decibels::decibelsToGain (p[P::fxEqLow]));
            *eqMid.state  = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sr, p[P::fxEqMidFreq], 0.9, juce::Decibels::decibelsToGain (p[P::fxEqMid]));
            *eqHigh.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sr, 6000.0, 0.7, juce::Decibels::decibelsToGain (p[P::fxEqHigh]));
            juce::dsp::AudioBlock<float> blk (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            eqLow.process (ctx); eqMid.process (ctx); eqHigh.process (ctx);
        }

        // ---- Compresor (una perilla: umbral, razón y ganancia de compensación juntos) ----
        if (p[P::fxCompOn] > 0.5f)
        {
            const float amt = p[P::fxCompAmt];
            const float thr = juce::Decibels::decibelsToGain (-6.0f - 30.0f * amt);
            const float ratio = 1.5f + 6.0f * amt;
            const float makeup = juce::Decibels::decibelsToGain (10.0f * amt);
            const float att = std::exp (-1.0f / (0.005f * (float) sr)), rel = std::exp (-1.0f / (0.12f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                const float lvl = juce::jmax (std::abs (L[i]), std::abs (R[i]));
                compEnv = lvl > compEnv ? att * compEnv + (1.0f - att) * lvl : rel * compEnv + (1.0f - rel) * lvl;
                float gain = 1.0f;
                if (compEnv > thr) gain = std::pow (compEnv / thr, 1.0f / ratio - 1.0f);
                L[i] *= gain * makeup; R[i] *= gain * makeup;
            }
        }

        // ---- Chorus (también lo enciende la macro Movimiento) ----
        const float chorusMix = juce::jlimit (0.0f, 1.0f, (p[P::fxChorusOn] > 0.5f ? p[P::fxChorusMix] : 0.0f) + move * 0.45f);
        if (chorusMix > 0.001f)
        {
            const float rate = p[P::fxChorusOn] > 0.5f ? p[P::fxChorusRate] : 0.5f;
            const float depth = (p[P::fxChorusOn] > 0.5f ? p[P::fxChorusDepth] : 0.5f) * 0.006f * (float) sr;
            const int len = (int) chorusBuf[0].size();
            const float base = 0.012f * (float) sr;
            for (int i = 0; i < n; ++i)
            {
                chorusPhase += rate / sr;
                if (chorusPhase >= 1.0) chorusPhase -= 1.0;
                const float mL = base + depth * (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * chorusPhase));
                const float mR = base + depth * (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * (chorusPhase + 0.25)));
                chorusBuf[0][(size_t) chorusPos] = L[i];
                chorusBuf[1][(size_t) chorusPos] = R[i];
                const float wl = readFrac (chorusBuf[0], (float) chorusPos - mL, len), wr = readFrac (chorusBuf[1], (float) chorusPos - mR, len);
                L[i] = L[i] * (1.0f - 0.5f * chorusMix) + wl * chorusMix * 0.8f;
                R[i] = R[i] * (1.0f - 0.5f * chorusMix) + wr * chorusMix * 0.8f;
                chorusPos = (chorusPos + 1) % len;
            }
        }

        // ---- Delay (la macro Espacio le suma) ----
        const float delayMix = juce::jlimit (0.0f, 1.0f, (p[P::fxDelayOn] > 0.5f ? p[P::fxDelayMix] : 0.0f) + space * 0.2f);
        if (delayMix > 0.001f)
        {
            const int sync = p.i (P::fxDelaySync);
            double secs = sync > 0 ? delaySyncBeats (sync) * 60.0 / juce::jmax (30.0, bpm) : p[P::fxDelayMs] / 1000.0;
            if (p[P::fxDelayOn] < 0.5f) secs = 60.0 / juce::jmax (30.0, bpm) * 0.75;   // sólo por Espacio: 1/8 con puntillo
            const double target = juce::jlimit (0.01, 2.0, secs) * sr;
            if (smoothDelay < 0.0) smoothDelay = target;
            const float fb = p[P::fxDelayOn] > 0.5f ? p[P::fxDelayFb] : 0.35f;
            const bool ping = p[P::fxDelayOn] < 0.5f || p[P::fxDelayPing] > 0.5f;
            const float toneHz = 800.0f * std::pow (20.0f, p[P::fxDelayOn] > 0.5f ? p[P::fxDelayTone] : 0.6f);
            const float lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * toneHz / (float) sr);
            const int len = (int) delayBuf[0].size();
            for (int i = 0; i < n; ++i)
            {
                smoothDelay += (target - smoothDelay) * 0.0005;
                const float dl = readFrac (delayBuf[0], (float) (delayPos - smoothDelay), len);
                const float dr = readFrac (delayBuf[1], (float) (delayPos - smoothDelay), len);
                delayLp[0] += lpA * (dl - delayLp[0]);
                delayLp[1] += lpA * (dr - delayLp[1]);
                if (ping)
                {
                    delayBuf[0][(size_t) delayPos] = 0.5f * (L[i] + R[i]) + delayLp[1] * fb;
                    delayBuf[1][(size_t) delayPos] = delayLp[0] * fb;
                }
                else
                {
                    delayBuf[0][(size_t) delayPos] = L[i] + delayLp[0] * fb;
                    delayBuf[1][(size_t) delayPos] = R[i] + delayLp[1] * fb;
                }
                L[i] += delayLp[0] * delayMix;
                R[i] += delayLp[1] * delayMix;
                delayPos = (delayPos + 1) % len;
            }
        }

        // ---- Reverb (la macro Espacio le suma) ----
        const float revMix = juce::jlimit (0.0f, 1.0f, (p[P::fxRevOn] > 0.5f ? p[P::fxRevMix] : 0.0f) + space * 0.45f);
        if (revMix > 0.001f)
        {
            const bool on = p[P::fxRevOn] > 0.5f;
            reverb.set (on ? p[P::fxRevSize] : 0.7f, on ? p[P::fxRevDecay] : 1.5f + 3.0f * space, on ? p[P::fxRevDamp] : 0.55f,
                        on ? p[P::fxRevPre] : 0.02f, sr);
            for (int i = 0; i < n; ++i)
            {
                float wl, wr;
                reverb.process (L[i], R[i], wl, wr);
                // mezcla con potencia constante
                const float dry = std::cos (revMix * juce::MathConstants<float>::halfPi * 0.8f);
                L[i] = L[i] * dry + wl * revMix;
                R[i] = R[i] * dry + wr * revMix;
            }
        }

        // ---- Ancho estéreo (centro / lados) ----
        const float width = juce::jlimit (0.0f, 2.0f, p[P::fxWidth] + space * 0.3f);
        if (std::abs (width - 1.0f) > 0.01f)
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * width;
                L[i] = m + s; R[i] = m - s;
            }
    }

private:
    static float readFrac (const std::vector<float>& b, float pos, int len)
    {
        while (pos < 0.0f) pos += (float) len;
        const int i0 = (int) pos % len, i1 = (i0 + 1) % len;
        const float f = pos - std::floor (pos);
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
    }

    // Reverb de red de retardos realimentada (8 líneas, matriz de Hadamard, modulada)
    class Fdn
    {
    public:
        void prepare (double s)
        {
            sr = s;
            for (int k = 0; k < 8; ++k) lines[(size_t) k].assign ((size_t) (sr * 0.25) + 8, 0.0f);
            pre.assign ((size_t) (sr * 0.25) + 4, 0.0f);
            clear();
        }
        void clear()
        {
            for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
            std::fill (pre.begin(), pre.end(), 0.0f);
            for (auto& d : damp) d = 0.0f;
            modPhase = 0.0;
        }
        void set (float size, float decay, float bright, float preDelay, double s)
        {
            static const float base[8] { 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0533f, 0.0617f, 0.0687f, 0.0793f };
            for (int k = 0; k < 8; ++k)
            {
                len[(size_t) k] = (float) ((0.35 + 1.3 * size) * base[k] * s);
                // ganancia para que la cola caiga 60 dB en "decay" segundos
                gain[(size_t) k] = std::pow (10.0f, -3.0f * len[(size_t) k] / (float) s / juce::jmax (0.1f, decay));
            }
            dampA = 0.15f + 0.8f * bright;
            preLen = juce::jlimit (1, (int) pre.size() - 2, (int) (preDelay * s));
        }
        void process (float inL, float inR, float& outL, float& outR)
        {
            const int pl = (int) pre.size();
            pre[(size_t) prePos] = 0.5f * (inL + inR);
            const float in = pre[(size_t) ((prePos - preLen + pl) % pl)];
            prePos = (prePos + 1) % pl;
            modPhase += 0.7 / sr;
            if (modPhase > 1.0) modPhase -= 1.0;

            std::array<float, 8> y {};
            for (int k = 0; k < 8; ++k)
            {
                const auto& l = lines[(size_t) k];
                const int ll = (int) l.size();
                const float mod = 4.0f * (float) std::sin (juce::MathConstants<double>::twoPi * (modPhase + k * 0.125));
                float pos = (float) pos_ - len[(size_t) k] - mod;
                while (pos < 0.0f) pos += (float) ll;
                const int i0 = (int) pos % ll, i1 = (i0 + 1) % ll;
                const float f = pos - std::floor (pos);
                float v = l[(size_t) i0] + (l[(size_t) i1] - l[(size_t) i0]) * f;
                damp[(size_t) k] += dampA * (v - damp[(size_t) k]);
                y[(size_t) k] = damp[(size_t) k] * gain[(size_t) k];
            }
            // Hadamard 8x8 (normalizada)
            std::array<float, 8> h = y;
            for (int sz = 1; sz < 8; sz *= 2)
                for (int i = 0; i < 8; i += 2 * sz)
                    for (int j = i; j < i + sz; ++j) { const float a = h[(size_t) j], b = h[(size_t) (j + sz)]; h[(size_t) j] = a + b; h[(size_t) (j + sz)] = a - b; }
            for (int k = 0; k < 8; ++k)
            {
                auto& l = lines[(size_t) k];
                l[(size_t) (pos_ % (int) l.size())] = h[(size_t) k] * 0.35355339f + in * (k % 2 ? 0.5f : -0.5f);
            }
            ++pos_;
            if (pos_ > 1 << 28) pos_ = 0;
            outL = (y[0] + y[2] + y[4] + y[6]) * 0.5f;
            outR = (y[1] + y[3] + y[5] + y[7]) * 0.5f;
        }
    private:
        double sr = 44100.0, modPhase = 0.0;
        std::array<std::vector<float>, 8> lines;
        std::array<float, 8> len {}, gain {}, damp {};
        std::vector<float> pre;
        int prePos = 0, preLen = 1, pos_ = 0;
        float dampA = 0.5f;
    };

    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Filter tiltLow, tiltHigh, eqLow, eqMid, eqHigh;
    double sr = 44100.0, chorusPhase = 0.0, smoothDelay = -1.0;
    std::vector<std::vector<float>> chorusBuf, delayBuf;
    int chorusPos = 0, delayPos = 0;
    float compEnv = 0.0f;
    float delayLp[2] {};
    Fdn reverb;
};
