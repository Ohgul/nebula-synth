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
    // Ondas: 0 Seno, 1 Triangulo, 2 Sierra, 3 Cuadrada
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
    };
    return presets;
}

inline void applyPreset (juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    // Primero todo a valores por defecto, luego los del preset
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ParamID::engine && ! rp->getParameterID().startsWith ("smp"))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& [id, value] : list[(size_t) index].values)
        if (auto* rp = state.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}
