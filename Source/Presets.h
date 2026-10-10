#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

// Presets de fábrica. Cualquier parámetro que no aparezca vuelve a su valor por defecto.
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Preset>& factoryPresets()
{
    using namespace ParamID;
    // Ondas: 0 Seno, 1 Triangulo, 2 Sierra, 3 Pulso, 4 Super Saw, 5 Sync, 6 Wavetable
    // Matriz "mS_D": S = 0 Env2, 1 Env3, 2 LFO1, 3 LFO2, 4 Vel, 5 Rueda, 6 AT, 7 Nota
    //                D = 0 Tono, 1 Forma1, 2 Forma2, 3 Mezcla, 4 FM, 5 Corte1, 6 Reso1, 7 Corte2, 8 Vol, 9 Paneo
    static const std::vector<Preset> presets
    {
        { "Inicial", {} },

        { "Bajo Profundo", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { osc2Wave, 3 }, { osc2Semi, -12 }, { osc2Fine, 4 },
            { oscMix, 0.4f }, { subLevel, 0.7f },
            { cutoff, 380 }, { resonance, 0.3f }, { filterEnv, 0.45f }, { drive, 0.35f },
            { ampA, 0.002f }, { ampD, 0.4f }, { ampS, 0.85f }, { ampR, 0.12f },
            { fltA, 0.001f }, { fltD, 0.25f }, { fltS, 0.1f }, { fltR, 0.15f } } },

        { "Pad Nube", {
            { osc1Wave, 2 }, { osc2Wave, 2 }, { osc2Fine, 12 }, { oscMix, 0.5f }, { subLevel, 0.15f },
            { cutoff, 1400 }, { resonance, 0.15f }, { filterEnv, 0.25f }, { drive, 0.0f },
            { ampA, 1.2f }, { ampD, 1.5f }, { ampS, 0.8f }, { ampR, 2.5f },
            { fltA, 1.8f }, { fltD, 2.0f }, { fltS, 0.5f }, { fltR, 2.5f },
            { lfoRate, 0.3f }, { lfoFilter, 0.25f }, { lfoPitch, 6 } } },

        { "Lead Brillante", {
            { osc1Wave, 2 }, { osc2Wave, 3 }, { osc2Semi, 12 }, { osc2Fine, -6 }, { oscMix, 0.35f },
            { cutoff, 3500 }, { resonance, 0.4f }, { filterEnv, 0.3f }, { drive, 0.4f },
            { ampA, 0.01f }, { ampD, 0.3f }, { ampS, 0.9f }, { ampR, 0.25f },
            { fltA, 0.01f }, { fltD, 0.5f }, { fltS, 0.4f }, { fltR, 0.3f },
            { lfoRate, 5.5f }, { lfoPitch, 18 } } },

        { "Pluck", {
            { osc1Wave, 3 }, { osc2Wave, 2 }, { osc2Fine, 8 }, { oscMix, 0.5f },
            { cutoff, 250 }, { resonance, 0.35f }, { filterEnv, 0.7f }, { drive, 0.15f },
            { ampA, 0.001f }, { ampD, 0.45f }, { ampS, 0.0f }, { ampR, 0.35f },
            { fltA, 0.001f }, { fltD, 0.18f }, { fltS, 0.0f }, { fltR, 0.2f } } },

        { "Brass Analogo", {
            { osc1Wave, 2 }, { osc2Wave, 2 }, { osc2Fine, -9 }, { oscMix, 0.5f }, { subLevel, 0.2f },
            { cutoff, 600 }, { resonance, 0.2f }, { filterEnv, 0.5f }, { drive, 0.25f },
            { ampA, 0.04f }, { ampD, 0.3f }, { ampS, 0.85f }, { ampR, 0.3f },
            { fltA, 0.09f }, { fltD, 0.6f }, { fltS, 0.45f }, { fltR, 0.3f } } },

        { "Acid", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { oscMix, 0.0f },
            { cutoff, 300 }, { resonance, 0.8f }, { filterEnv, 0.65f }, { drive, 0.6f },
            { ampA, 0.001f }, { ampD, 0.3f }, { ampS, 0.7f }, { ampR, 0.08f },
            { fltA, 0.001f }, { fltD, 0.22f }, { fltS, 0.0f }, { fltR, 0.1f } } },

        { "Campana Suave", {
            { osc1Wave, 0 }, { osc2Wave, 1 }, { osc2Semi, 19 }, { oscMix, 0.3f },
            { cutoff, 6000 }, { resonance, 0.1f }, { filterEnv, 0.0f }, { drive, 0.0f },
            { ampA, 0.002f }, { ampD, 1.8f }, { ampS, 0.0f }, { ampR, 1.8f },
            { lfoRate, 4.5f }, { lfoPitch, 4 } } },

        // ---- Nuevos en la v2 ----
        { "Super Saw Lead", {
            { osc1Wave, 4 }, { osc1Shape, 0.55f }, { osc2Wave, 4 }, { osc2Shape, 0.35f }, { osc2Semi, 12 }, { osc2Fine, 0 },
            { oscMix, 0.3f }, { uniVoices, 3 }, { uniDetune, 0.2f }, { uniWidth, 0.9f },
            { cutoff, 5000 }, { resonance, 0.15f }, { filterEnv, 0.2f }, { drive, 0.15f },
            { ampA, 0.004f }, { ampD, 0.4f }, { ampS, 0.85f }, { ampR, 0.35f },
            { fltA, 0.002f }, { fltD, 0.5f }, { fltS, 0.5f }, { polyMode, 2 }, { glide, 0.06f },
            { "m5_5", 0.5f }, { lfoRate, 5.5f }, { lfoPitch, 12 } } },

        { "Pad Wavetable", {
            { osc1Wave, 6 }, { osc1Table, 2 }, { osc1Shape, 0.2f }, { osc2Wave, 6 }, { osc2Table, 1 }, { osc2Shape, 0.4f },
            { osc2Fine, 9 }, { oscMix, 0.45f }, { uniVoices, 2 }, { uniDetune, 0.15f }, { uniWidth, 1.0f },
            { cutoff, 2200 }, { resonance, 0.12f }, { filterEnv, 0.1f }, { drive, 0.0f },
            { ampA, 1.0f }, { ampD, 1.5f }, { ampS, 0.85f }, { ampR, 2.5f },
            { lfo2Rate, 0.15f }, { lfo2Shape, 5 }, { "m3_1", 0.45f }, { "m3_2", -0.35f },
            { env3A, 2.5f }, { env3D, 3.0f }, { env3S, 0.6f }, { "m1_5", 0.25f } } },

        { "Sync Lead", {
            { osc1Wave, 5 }, { osc1Shape, 0.25f }, { osc2Wave, 2 }, { osc2Fine, -5 }, { oscMix, 0.25f },
            { cutoff, 4000 }, { resonance, 0.3f }, { filterEnv, 0.25f }, { drive, 0.35f },
            { ampA, 0.003f }, { ampD, 0.3f }, { ampS, 0.9f }, { ampR, 0.25f },
            { fltA, 0.003f }, { fltD, 0.6f }, { fltS, 0.3f },
            { env3A, 0.001f }, { env3D, 0.7f }, { env3S, 0.15f }, { "m1_1", 0.6f }, { polyMode, 2 }, { glide, 0.04f } } },

        { "Cuerdas PWM", {
            { osc1Wave, 3 }, { osc1Shape, 0.5f }, { osc2Wave, 3 }, { osc2Shape, 0.5f }, { osc2Fine, 11 }, { oscMix, 0.5f },
            { cutoff, 2600 }, { resonance, 0.1f }, { filterEnv, 0.15f }, { drive, 0.05f },
            { ampA, 0.35f }, { ampD, 1.0f }, { ampS, 0.85f }, { ampR, 1.2f },
            { lfoRate, 0.6f }, { lfo2Rate, 0.43f }, { "m2_1", 0.4f }, { "m3_2", 0.4f },
            { f2On, 1 }, { f2Type, 2 }, { f2Cutoff, 180 } } },

        { "Bajo Mono Glide", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { osc2Wave, 3 }, { osc2Shape, 0.3f }, { osc2Semi, -12 }, { osc2Fine, 3 },
            { oscMix, 0.45f }, { subLevel, 0.6f }, { cutoff, 300 }, { resonance, 0.35f }, { filterEnv, 0.45f },
            { filterSlope, 1 }, { filterChar, 1 }, { drive, 0.3f },
            { ampA, 0.002f }, { ampD, 0.5f }, { ampS, 0.8f }, { ampR, 0.12f },
            { fltA, 0.001f }, { fltD, 0.28f }, { fltS, 0.1f }, { polyMode, 2 }, { glide, 0.09f }, { "m4_5", 0.35f } } },
    };
    return presets;
}

#include "GenomeOps.h"

// keepPerformance: no tocar modo de voces, glide, pitch bend, etc. (lo usa el Jardín)
inline void applyPreset (juce::AudioProcessorValueTreeState& state, int index, bool keepPerformance = false)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    // Primero todo a valores por defecto, luego los del preset
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ParamID::engine && ! rp->getParameterID().startsWith ("smp")
                && ! (keepPerformance && GenomeOps::isPerformanceParam (rp->getParameterID())))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& [id, value] : list[(size_t) index].values)
        if (auto* rp = state.getParameter (id))
            if (! (keepPerformance && GenomeOps::isPerformanceParam (rp->getParameterID())))
                rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}
