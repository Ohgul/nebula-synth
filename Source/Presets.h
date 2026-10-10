#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

// Presets de fábrica. Cualquier parámetro que no aparezca vuelve a su valor por defecto.
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
    const char* category = "Basico";
};

inline const juce::StringArray& presetCategories()
{
    static const juce::StringArray c { "Bajo", "Lead", "Pad", "Pluck", "Keys", "Campana", "Textura", "Basico" };
    return c;
}

inline const std::vector<Preset>& factoryPresets()
{
    using namespace ParamID;
    // Ondas: 0 Seno, 1 Triangulo, 2 Sierra, 3 Pulso, 4 Super Saw, 5 Sync, 6 Wavetable
    // Matriz "mS_D": S = 0 Env2, 1 Env3, 2 LFO1, 3 LFO2, 4 Vel, 5 Rueda, 6 AT, 7 Nota
    //                D = 0 Tono, 1 Forma1, 2 Forma2, 3 Mezcla, 4 FM, 5 Corte1, 6 Reso1, 7 Corte2, 8 Vol, 9 Paneo
    static const std::vector<Preset> presets
    {
        { "Inicial", {} , "Basico" },

        { "Bajo Profundo", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { osc2Wave, 3 }, { osc2Semi, -12 }, { osc2Fine, 4 },
            { oscMix, 0.4f }, { subLevel, 0.7f },
            { cutoff, 380 }, { resonance, 0.3f }, { filterEnv, 0.45f }, { drive, 0.35f },
            { ampA, 0.002f }, { ampD, 0.4f }, { ampS, 0.85f }, { ampR, 0.12f },
            { fltA, 0.001f }, { fltD, 0.25f }, { fltS, 0.1f }, { fltR, 0.15f } } , "Bajo" },

        { "Pad Nube", {
            { osc1Wave, 2 }, { osc2Wave, 2 }, { osc2Fine, 12 }, { oscMix, 0.5f }, { subLevel, 0.15f },
            { cutoff, 1400 }, { resonance, 0.15f }, { filterEnv, 0.25f }, { drive, 0.0f },
            { ampA, 1.2f }, { ampD, 1.5f }, { ampS, 0.8f }, { ampR, 2.5f },
            { fltA, 1.8f }, { fltD, 2.0f }, { fltS, 0.5f }, { fltR, 2.5f },
            { lfoRate, 0.3f }, { lfoFilter, 0.25f }, { lfoPitch, 6 } } , "Pad" },

        { "Lead Brillante", {
            { osc1Wave, 2 }, { osc2Wave, 3 }, { osc2Semi, 12 }, { osc2Fine, -6 }, { oscMix, 0.35f },
            { cutoff, 3500 }, { resonance, 0.4f }, { filterEnv, 0.3f }, { drive, 0.4f },
            { ampA, 0.01f }, { ampD, 0.3f }, { ampS, 0.9f }, { ampR, 0.25f },
            { fltA, 0.01f }, { fltD, 0.5f }, { fltS, 0.4f }, { fltR, 0.3f },
            { lfoRate, 5.5f }, { lfoPitch, 18 } } , "Lead" },

        { "Pluck", {
            { osc1Wave, 3 }, { osc2Wave, 2 }, { osc2Fine, 8 }, { oscMix, 0.5f },
            { cutoff, 250 }, { resonance, 0.35f }, { filterEnv, 0.7f }, { drive, 0.15f },
            { ampA, 0.001f }, { ampD, 0.45f }, { ampS, 0.0f }, { ampR, 0.35f },
            { fltA, 0.001f }, { fltD, 0.18f }, { fltS, 0.0f }, { fltR, 0.2f } } , "Pluck" },

        { "Brass Analogo", {
            { osc1Wave, 2 }, { osc2Wave, 2 }, { osc2Fine, -9 }, { oscMix, 0.5f }, { subLevel, 0.2f },
            { cutoff, 600 }, { resonance, 0.2f }, { filterEnv, 0.5f }, { drive, 0.25f },
            { ampA, 0.04f }, { ampD, 0.3f }, { ampS, 0.85f }, { ampR, 0.3f },
            { fltA, 0.09f }, { fltD, 0.6f }, { fltS, 0.45f }, { fltR, 0.3f } } , "Keys" },

        { "Acid", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { oscMix, 0.0f },
            { cutoff, 300 }, { resonance, 0.8f }, { filterEnv, 0.65f }, { drive, 0.6f },
            { ampA, 0.001f }, { ampD, 0.3f }, { ampS, 0.7f }, { ampR, 0.08f },
            { fltA, 0.001f }, { fltD, 0.22f }, { fltS, 0.0f }, { fltR, 0.1f } } , "Bajo" },

        { "Campana Suave", {
            { osc1Wave, 0 }, { osc2Wave, 1 }, { osc2Semi, 19 }, { oscMix, 0.3f },
            { cutoff, 6000 }, { resonance, 0.1f }, { filterEnv, 0.0f }, { drive, 0.0f },
            { ampA, 0.002f }, { ampD, 1.8f }, { ampS, 0.0f }, { ampR, 1.8f },
            { lfoRate, 4.5f }, { lfoPitch, 4 } } , "Campana" },

        // ---- Nuevos en la v2 ----
        { "Super Saw Lead", {
            { osc1Wave, 4 }, { osc1Shape, 0.55f }, { osc2Wave, 4 }, { osc2Shape, 0.35f }, { osc2Semi, 12 }, { osc2Fine, 0 },
            { oscMix, 0.3f }, { uniVoices, 3 }, { uniDetune, 0.2f }, { uniWidth, 0.9f },
            { cutoff, 5000 }, { resonance, 0.15f }, { filterEnv, 0.2f }, { drive, 0.15f },
            { ampA, 0.004f }, { ampD, 0.4f }, { ampS, 0.85f }, { ampR, 0.35f },
            { fltA, 0.002f }, { fltD, 0.5f }, { fltS, 0.5f }, { polyMode, 2 }, { glide, 0.06f },
            { "m5_5", 0.5f }, { lfoRate, 5.5f }, { lfoPitch, 12 } } , "Lead" },

        { "Pad Wavetable", {
            { osc1Wave, 6 }, { osc1Table, 2 }, { osc1Shape, 0.2f }, { osc2Wave, 6 }, { osc2Table, 1 }, { osc2Shape, 0.4f },
            { osc2Fine, 9 }, { oscMix, 0.45f }, { uniVoices, 2 }, { uniDetune, 0.15f }, { uniWidth, 1.0f },
            { cutoff, 2200 }, { resonance, 0.12f }, { filterEnv, 0.1f }, { drive, 0.0f },
            { ampA, 1.0f }, { ampD, 1.5f }, { ampS, 0.85f }, { ampR, 2.5f },
            { lfo2Rate, 0.15f }, { lfo2Shape, 5 }, { "m3_1", 0.45f }, { "m3_2", -0.35f },
            { env3A, 2.5f }, { env3D, 3.0f }, { env3S, 0.6f }, { "m1_5", 0.25f } } , "Pad" },

        { "Sync Lead", {
            { osc1Wave, 5 }, { osc1Shape, 0.25f }, { osc2Wave, 2 }, { osc2Fine, -5 }, { oscMix, 0.25f },
            { cutoff, 4000 }, { resonance, 0.3f }, { filterEnv, 0.25f }, { drive, 0.35f },
            { ampA, 0.003f }, { ampD, 0.3f }, { ampS, 0.9f }, { ampR, 0.25f },
            { fltA, 0.003f }, { fltD, 0.6f }, { fltS, 0.3f },
            { env3A, 0.001f }, { env3D, 0.7f }, { env3S, 0.15f }, { "m1_1", 0.6f }, { polyMode, 2 }, { glide, 0.04f } } , "Lead" },

        { "Cuerdas PWM", {
            { osc1Wave, 3 }, { osc1Shape, 0.5f }, { osc2Wave, 3 }, { osc2Shape, 0.5f }, { osc2Fine, 11 }, { oscMix, 0.5f },
            { cutoff, 2600 }, { resonance, 0.1f }, { filterEnv, 0.15f }, { drive, 0.05f },
            { ampA, 0.35f }, { ampD, 1.0f }, { ampS, 0.85f }, { ampR, 1.2f },
            { lfoRate, 0.6f }, { lfo2Rate, 0.43f }, { "m2_1", 0.4f }, { "m3_2", 0.4f },
            { f2On, 1 }, { f2Type, 2 }, { f2Cutoff, 180 } } , "Pad" },

        { "Bajo Mono Glide", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { osc2Wave, 3 }, { osc2Shape, 0.3f }, { osc2Semi, -12 }, { osc2Fine, 3 },
            { oscMix, 0.45f }, { subLevel, 0.6f }, { cutoff, 300 }, { resonance, 0.35f }, { filterEnv, 0.45f },
            { filterSlope, 1 }, { filterChar, 1 }, { drive, 0.3f },
            { ampA, 0.002f }, { ampD, 0.5f }, { ampS, 0.8f }, { ampR, 0.12f },
            { fltA, 0.001f }, { fltD, 0.28f }, { fltS, 0.1f }, { polyMode, 2 }, { glide, 0.09f }, { "m4_5", 0.35f } } , "Bajo" },

        // ---- Nuevos en la v3 (con efectos y macros) ----
        { "Bajo Moog Gordo", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { osc2Wave, 2 }, { osc2Semi, -12 }, { osc2Fine, 6 }, { oscMix, 0.5f }, { subLevel, 0.5f },
            { filterSlope, 1 }, { filterChar, 1 }, { cutoff, 420 }, { resonance, 0.35f }, { filterEnv, 0.4f }, { drive, 0.3f },
            { ampA, 0.002f }, { ampD, 0.5f }, { ampS, 0.8f }, { ampR, 0.1f }, { fltA, 0.001f }, { fltD, 0.3f }, { fltS, 0.15f },
            { polyMode, 1 }, { analog, 0.3f }, { fxCompOn, 1 }, { fxCompAmt, 0.35f } }, "Bajo" },
        { "Bajo Reese", {
            { osc1Wave, 2 }, { osc1Octave, -1 }, { osc2Wave, 2 }, { osc2Semi, -12 }, { osc2Fine, 18 }, { oscMix, 0.5f },
            { uniVoices, 3 }, { uniDetune, 0.18f }, { uniWidth, 0.5f }, { cutoff, 700 }, { resonance, 0.2f }, { filterEnv, 0.1f },
            { filterSlope, 1 }, { drive, 0.4f }, { ampA, 0.005f }, { ampS, 1.0f }, { ampR, 0.15f },
            { lfo2Rate, 0.25f }, { "m3_5", 0.2f }, { polyMode, 1 }, { fxChorusOn, 1 }, { fxChorusMix, 0.3f } }, "Bajo" },
        { "Sub 808", {
            { osc1Wave, 0 }, { osc1Octave, -1 }, { oscMix, 0.0f }, { cutoff, 20000 }, { resonance, 0.0f }, { filterEnv, 0.0f }, { drive, 0.25f },
            { ampA, 0.001f }, { ampD, 1.6f }, { ampS, 0.0f }, { ampR, 0.3f }, { env3A, 0.001f }, { env3D, 0.08f }, { env3S, 0.0f },
            { "m1_0", 0.12f }, { polyMode, 1 }, { glide, 0.05f } }, "Bajo" },
        { "Lead Moog", {
            { osc1Wave, 2 }, { osc2Wave, 3 }, { osc2Shape, 0.4f }, { osc2Fine, -7 }, { oscMix, 0.45f },
            { filterSlope, 1 }, { filterChar, 1 }, { cutoff, 1800 }, { resonance, 0.45f }, { filterEnv, 0.3f }, { drive, 0.35f },
            { ampA, 0.005f }, { ampD, 0.3f }, { ampS, 0.9f }, { ampR, 0.2f }, { fltA, 0.01f }, { fltD, 0.4f }, { fltS, 0.5f },
            { polyMode, 2 }, { glide, 0.07f }, { lfoRate, 5.2f }, { lfoPitch, 10 }, { lfoFade, 0.5f }, { analog, 0.25f },
            { fxDelayOn, 1 }, { fxDelaySync, 4 }, { fxDelayMix, 0.18f }, { fxRevOn, 1 }, { fxRevMix, 0.15f } }, "Lead" },
        { "Lead Chillon", {
            { osc1Wave, 5 }, { osc1Shape, 0.45f }, { osc2Wave, 4 }, { osc2Shape, 0.4f }, { oscMix, 0.3f },
            { cutoff, 6000 }, { resonance, 0.2f }, { filterEnv, 0.15f }, { drive, 0.5f }, { filterChar, 2 },
            { ampA, 0.003f }, { ampS, 0.95f }, { ampR, 0.2f }, { env3A, 0.001f }, { env3D, 0.4f }, { env3S, 0.2f }, { "m1_1", 0.5f },
            { polyMode, 2 }, { glide, 0.03f }, { fxDelayOn, 1 }, { fxDelaySync, 3 }, { fxDelayMix, 0.22f } }, "Lead" },
        { "Lead Cristal", {
            { osc1Wave, 6 }, { osc1Table, 3 }, { osc1Shape, 0.3f }, { osc2Wave, 0 }, { osc2Semi, 12 }, { oscMix, 0.25f },
            { cutoff, 9000 }, { resonance, 0.1f }, { filterEnv, 0.0f }, { ampA, 0.005f }, { ampS, 0.85f }, { ampR, 0.5f },
            { lfoRate, 0.8f }, { "m2_1", 0.25f }, { fxChorusOn, 1 }, { fxRevOn, 1 }, { fxRevMix, 0.3f }, { fxRevDecay, 3.5f } }, "Lead" },
        { "Pad Galaxia", {
            { osc1Wave, 4 }, { osc1Shape, 0.4f }, { osc2Wave, 6 }, { osc2Table, 2 }, { osc2Shape, 0.3f }, { oscMix, 0.4f }, { uniVoices, 3 },
            { uniDetune, 0.2f }, { uniWidth, 1.0f }, { cutoff, 1800 }, { resonance, 0.15f }, { filterEnv, 0.15f },
            { ampA, 1.5f }, { ampD, 2.0f }, { ampS, 0.85f }, { ampR, 3.5f }, { fltA, 2.0f }, { fltD, 3.0f }, { fltS, 0.6f },
            { lfo2Rate, 0.12f }, { lfo2Shape, 5 }, { "m3_2", 0.4f }, { "m3_5", 0.2f },
            { fxChorusOn, 1 }, { fxChorusMix, 0.35f }, { fxRevOn, 1 }, { fxRevMix, 0.4f }, { fxRevDecay, 6.0f }, { fxRevSize, 0.85f } }, "Pad" },
        { "Pad Calido", {
            { osc1Wave, 3 }, { osc1Shape, 0.45f }, { osc2Wave, 2 }, { osc2Fine, 9 }, { oscMix, 0.5f }, { subLevel, 0.2f },
            { filterChar, 1 }, { cutoff, 1100 }, { resonance, 0.1f }, { filterEnv, 0.2f },
            { ampA, 0.9f }, { ampD, 1.5f }, { ampS, 0.9f }, { ampR, 2.0f }, { fltA, 1.2f }, { fltD, 2.0f }, { fltS, 0.5f },
            { lfoRate, 0.35f }, { "m2_1", 0.3f }, { analog, 0.4f }, { fxChorusOn, 1 }, { fxRevOn, 1 }, { fxRevMix, 0.3f } }, "Pad" },
        { "Pad Respira", {
            { osc1Wave, 6 }, { osc1Table, 2 }, { osc1Shape, 0.1f }, { oscMix, 0.0f }, { noise, 0.2f }, { noiseColor, 0.6f },
            { cutoff, 2500 }, { resonance, 0.25f }, { filterEnv, 0.0f }, { ampA, 1.2f }, { ampS, 1.0f }, { ampR, 2.5f },
            { env3A, 3.0f }, { env3D, 3.0f }, { env3S, 0.0f }, { env3Mode, 1 }, { "m1_1", 0.7f }, { "m1_5", 0.4f },
            { fxRevOn, 1 }, { fxRevMix, 0.45f }, { fxRevDecay, 5.0f }, { fxDelayOn, 1 }, { fxDelayMix, 0.2f } }, "Pad" },
        { "Pluck Cristal", {
            { osc1Wave, 1 }, { osc2Wave, 0 }, { osc2Semi, 12 }, { oscMix, 0.35f }, { fmAmount, 0.25f },
            { cutoff, 1200 }, { resonance, 0.2f }, { filterEnv, 0.6f }, { ampA, 0.001f }, { ampD, 0.6f }, { ampS, 0.0f }, { ampR, 0.5f },
            { fltA, 0.001f }, { fltD, 0.25f }, { fltS, 0.0f }, { fxDelayOn, 1 }, { fxDelaySync, 4 }, { fxDelayMix, 0.25f },
            { fxRevOn, 1 }, { fxRevMix, 0.2f } }, "Pluck" },
        { "Pluck Madera", {
            { osc1Wave, 3 }, { osc1Shape, 0.2f }, { oscMix, 0.0f }, { noise, 0.15f }, { noiseColor, 0.5f },
            { cutoff, 500 }, { resonance, 0.15f }, { filterEnv, 0.55f }, { ampA, 0.001f }, { ampD, 0.25f }, { ampS, 0.0f }, { ampR, 0.2f },
            { fltA, 0.001f }, { fltD, 0.1f }, { fltS, 0.0f }, { fxEqOn, 1 }, { fxEqMid, 3.0f }, { fxEqMidFreq, 900 } }, "Pluck" },
        { "Pluck Super", {
            { osc1Wave, 4 }, { osc1Shape, 0.5f }, { oscMix, 0.0f }, { uniVoices, 3 }, { uniDetune, 0.15f },
            { cutoff, 600 }, { resonance, 0.2f }, { filterEnv, 0.65f }, { ampA, 0.001f }, { ampD, 0.35f }, { ampS, 0.0f }, { ampR, 0.3f },
            { fltA, 0.001f }, { fltD, 0.2f }, { fltS, 0.0f }, { fxDelayOn, 1 }, { fxDelaySync, 3 }, { fxDelayMix, 0.25f }, { fxRevOn, 1 } }, "Pluck" },
        { "Piano Electrico", {
            { osc1Wave, 0 }, { osc2Wave, 0 }, { osc2Semi, 12 }, { oscMix, 0.25f }, { fmAmount, 0.3f },
            { cutoff, 5000 }, { resonance, 0.0f }, { filterEnv, 0.0f }, { ampA, 0.002f }, { ampD, 1.5f }, { ampS, 0.25f }, { ampR, 0.5f },
            { env3A, 0.001f }, { env3D, 0.4f }, { env3S, 0.1f }, { "m1_4", 0.4f }, { "m4_4", 0.3f },
            { fxChorusOn, 1 }, { fxChorusRate, 0.8f }, { fxChorusMix, 0.35f } }, "Keys" },
        { "Organo", {
            { osc1Wave, 0 }, { osc2Wave, 0 }, { osc2Semi, 19 }, { oscMix, 0.35f }, { subLevel, 0.5f },
            { cutoff, 8000 }, { resonance, 0.0f }, { filterEnv, 0.0f }, { ampA, 0.004f }, { ampS, 1.0f }, { ampR, 0.08f },
            { fxChorusOn, 1 }, { fxChorusRate, 5.0f }, { fxChorusDepth, 0.25f }, { fxChorusMix, 0.5f } }, "Keys" },
        { "Keys Vintage", {
            { osc1Wave, 3 }, { osc1Shape, 0.35f }, { osc2Wave, 1 }, { osc2Fine, 6 }, { oscMix, 0.4f }, { filterChar, 1 },
            { cutoff, 1600 }, { resonance, 0.1f }, { filterEnv, 0.3f }, { ampA, 0.003f }, { ampD, 1.0f }, { ampS, 0.4f }, { ampR, 0.4f },
            { fltA, 0.002f }, { fltD, 0.6f }, { fltS, 0.3f }, { analog, 0.5f }, { fxRevOn, 1 }, { fxRevMix, 0.18f } }, "Keys" },
        { "Campana FM", {
            { osc1Wave, 0 }, { osc2Wave, 0 }, { osc2Semi, 17 }, { osc2Fine, 7 }, { oscMix, 0.0f }, { fmAmount, 0.45f },
            { cutoff, 20000 }, { resonance, 0.0f }, { filterEnv, 0.0f }, { ampA, 0.001f }, { ampD, 2.5f }, { ampS, 0.0f }, { ampR, 2.5f },
            { env3A, 0.001f }, { env3D, 1.2f }, { env3S, 0.0f }, { "m1_4", 0.5f }, { fxRevOn, 1 }, { fxRevMix, 0.3f }, { fxRevDecay, 4.0f } }, "Campana" },
        { "Campana Metal", {
            { osc1Wave, 1 }, { osc2Wave, 0 }, { osc2Semi, 23 }, { oscMix, 0.0f }, { fmAmount, 0.6f },
            { cutoff, 9000 }, { filterEnv, 0.0f }, { ampA, 0.001f }, { ampD, 1.5f }, { ampS, 0.0f }, { ampR, 1.5f },
            { env3A, 0.001f }, { env3D, 0.6f }, { env3S, 0.1f }, { "m1_4", 0.4f }, { fxDelayOn, 1 }, { fxDelayMix, 0.2f } }, "Campana" },
        { "Textura Polvo", {
            { osc1Wave, 6 }, { osc1Table, 3 }, { osc1Shape, 0.5f }, { oscMix, 0.0f }, { noise, 0.35f }, { noiseColor, 0.7f },
            { cutoff, 3000 }, { resonance, 0.4f }, { filterType, 1 }, { filterEnv, 0.0f }, { ampA, 0.8f }, { ampS, 1.0f }, { ampR, 3.0f },
            { lfoShape, 4 }, { lfoRate, 7.0f }, { lfoFilter, 0.3f }, { lfo2Rate, 0.1f }, { "m3_1", 0.5f },
            { fxDelayOn, 1 }, { fxDelayMix, 0.3f }, { fxDelayFb, 0.6f }, { fxRevOn, 1 }, { fxRevMix, 0.5f }, { fxRevDecay, 8.0f } }, "Textura" },
        { "Textura Tormenta", {
            { osc1Wave, 5 }, { osc1Shape, 0.7f }, { osc2Wave, 4 }, { oscMix, 0.5f }, { noise, 0.25f },
            { cutoff, 900 }, { resonance, 0.55f }, { filterEnv, 0.0f }, { filterChar, 2 }, { ampA, 1.0f }, { ampS, 1.0f }, { ampR, 3.0f },
            { lfoShape, 5 }, { lfoRate, 0.4f }, { lfoFilter, 0.6f }, { macroChaos, 0.35f },
            { fxRevOn, 1 }, { fxRevMix, 0.45f }, { fxRevDecay, 7.0f } }, "Textura" },
        { "Textura Glitch", {
            { osc1Wave, 3 }, { osc1Shape, 0.2f }, { osc2Wave, 5 }, { oscMix, 0.5f }, { fmAmount, 0.3f },
            { cutoff, 2000 }, { resonance, 0.3f }, { filterEnv, 0.0f }, { ampA, 0.01f }, { ampS, 1.0f }, { ampR, 0.5f },
            { lfoShape, 4 }, { lfoSync, 7 }, { "m2_0", 0.2f }, { "m2_5", 0.4f }, { macroChaos, 0.6f },
            { fxDelayOn, 1 }, { fxDelaySync, 1 }, { fxDelayFb, 0.5f }, { fxDelayMix, 0.3f } }, "Textura" },
        { "Brass Moog", {
            { osc1Wave, 2 }, { osc2Wave, 2 }, { osc2Fine, -8 }, { oscMix, 0.5f }, { filterSlope, 1 }, { filterChar, 1 },
            { cutoff, 500 }, { resonance, 0.25f }, { filterEnv, 0.55f }, { drive, 0.3f },
            { ampA, 0.05f }, { ampD, 0.3f }, { ampS, 0.85f }, { ampR, 0.3f }, { fltA, 0.12f }, { fltD, 0.6f }, { fltS, 0.45f },
            { analog, 0.3f }, { fxRevOn, 1 }, { fxRevMix, 0.18f } }, "Keys" },
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

    // Los presets de fábrica son patches de sinte: desde Resíntesis se vuelve al motor Sinte
    if (auto* e = state.getParameter (ParamID::engine))
        if (juce::roundToInt (e->convertFrom0to1 (e->getValue())) == Engine::Resynth)
            e->setValueNotifyingHost (e->convertTo0to1 ((float) Engine::Synth));

    // Primero todo a valores por defecto, luego los del preset (la calidad y el MPE son ajustes, no se tocan)
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ParamID::engine && ! rp->getParameterID().startsWith ("smp")
                && rp->getParameterID() != ParamID::quality && rp->getParameterID() != ParamID::mpeOn
                && ! (keepPerformance && GenomeOps::isPerformanceParam (rp->getParameterID())))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& [id, value] : list[(size_t) index].values)
        if (auto* rp = state.getParameter (id))
            if (! (keepPerformance && GenomeOps::isPerformanceParam (rp->getParameterID())))
                rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}
