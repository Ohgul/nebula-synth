#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Identificadores de todos los parámetros (también son los nombres internos
// que Ableton usa para la automatización).
namespace ParamID
{
    inline constexpr const char* osc1Wave   = "osc1Wave";
    inline constexpr const char* osc1Octave = "osc1Octave";
    inline constexpr const char* osc2Wave   = "osc2Wave";
    inline constexpr const char* osc2Semi   = "osc2Semi";
    inline constexpr const char* osc2Fine   = "osc2Fine";
    inline constexpr const char* oscMix     = "oscMix";
    inline constexpr const char* subLevel   = "subLevel";
    inline constexpr const char* fmAmount   = "fmAmount";
    inline constexpr const char* noise      = "noise";
    inline constexpr const char* filterType = "filterType";

    inline constexpr const char* cutoff     = "cutoff";
    inline constexpr const char* resonance  = "resonance";
    inline constexpr const char* filterEnv  = "filterEnv";
    inline constexpr const char* drive      = "drive";

    inline constexpr const char* ampA = "ampA";
    inline constexpr const char* ampD = "ampD";
    inline constexpr const char* ampS = "ampS";
    inline constexpr const char* ampR = "ampR";

    inline constexpr const char* fltA = "fltA";
    inline constexpr const char* fltD = "fltD";
    inline constexpr const char* fltS = "fltS";
    inline constexpr const char* fltR = "fltR";

    inline constexpr const char* lfoRate   = "lfoRate";
    inline constexpr const char* lfoFilter = "lfoFilter";
    inline constexpr const char* lfoPitch  = "lfoPitch";

    inline constexpr const char* master = "master";

    // Motor y sample
    inline constexpr const char* engine     = "engine";
    inline constexpr const char* smpStart   = "smpStart";
    inline constexpr const char* smpTune    = "smpTune";
    inline constexpr const char* smpReverse = "smpReverse";
    inline constexpr const char* smpLoop    = "smpLoop";
    inline constexpr const char* smpTexture = "smpTexture";
    inline constexpr const char* smpGrain   = "smpGrain";
}

inline const juce::StringArray& engineNames()
{
    static const juce::StringArray names { "Sinte", "Sample" };
    return names;
}

inline const juce::StringArray& onOffNames()
{
    static const juce::StringArray names { "No", "Si" };
    return names;
}

inline const juce::StringArray& waveNames()
{
    static const juce::StringArray names { "Seno", "Triangulo", "Sierra", "Cuadrada" };
    return names;
}

inline const juce::StringArray& filterNames()
{
    static const juce::StringArray names { "Graves", "Banda", "Agudos" };
    return names;
}

namespace ParamFormat
{
    inline juce::String time (float s, int)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                        : juce::String (s, 2) + " s";
    }
    inline juce::String percent (float v, int)   { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    inline juce::String bipolar (float v, int)   { auto p = juce::roundToInt (v * 100.0f); return (p > 0 ? "+" : "") + juce::String (p) + " %"; }
    inline juce::String hertz (float hz, int)
    {
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                            : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    }
    inline juce::String lfoHz (float hz, int)    { return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz"; }
    inline juce::String cents (float c, int)     { auto r = juce::roundToInt (c); return (r > 0 ? "+" : "") + juce::String (r) + " ct"; }
    inline juce::String decibels (float db, int) { return db <= -59.9f ? juce::String ("-inf") : juce::String (db, 1) + " dB"; }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using Attr = AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto timeRange = [] (float max) { NormalisableRange<float> r (0.001f, max); r.setSkewForCentre (0.4f); return r; };
    auto add = [&p] (auto* param) { p.emplace_back (param); };

    // Osciladores
    add (new AudioParameterChoice ({ ParamID::osc1Wave, 1 }, "Osc 1 Onda", waveNames(), 2));
    add (new AudioParameterInt    ({ ParamID::osc1Octave, 1 }, "Osc 1 Octava", -2, 2, 0));
    add (new AudioParameterChoice ({ ParamID::osc2Wave, 1 }, "Osc 2 Onda", waveNames(), 3));
    add (new AudioParameterInt    ({ ParamID::osc2Semi, 1 }, "Osc 2 Semitonos", -24, 24, 0));
    add (new F ({ ParamID::osc2Fine, 1 }, "Osc 2 Fino", NormalisableRange<float> (-50.0f, 50.0f), 7.0f,
                Attr().withStringFromValueFunction (ParamFormat::cents)));
    add (new F ({ ParamID::oscMix, 1 }, "Mezcla Osc", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new F ({ ParamID::subLevel, 1 }, "Sub", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));

    add (new F ({ ParamID::fmAmount, 1 }, "FM", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new F ({ ParamID::noise, 1 }, "Ruido", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));

    // Filtro
    add (new AudioParameterChoice ({ ParamID::filterType, 1 }, "Tipo Filtro", filterNames(), 0));
    NormalisableRange<float> cutRange (20.0f, 20000.0f);
    cutRange.setSkewForCentre (1000.0f);
    add (new F ({ ParamID::cutoff, 1 }, "Corte", cutRange, 2500.0f,
                Attr().withStringFromValueFunction (ParamFormat::hertz)));
    add (new F ({ ParamID::resonance, 1 }, "Resonancia", NormalisableRange<float> (0.0f, 1.0f), 0.25f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new F ({ ParamID::filterEnv, 1 }, "Env Filtro Cant", NormalisableRange<float> (-1.0f, 1.0f), 0.35f,
                Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::drive, 1 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.1f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));

    // Envolvente de amplitud
    add (new F ({ ParamID::ampA, 1 }, "Amp Ataque",     timeRange (5.0f), 0.005f, Attr().withStringFromValueFunction (ParamFormat::time)));
    add (new F ({ ParamID::ampD, 1 }, "Amp Decaimiento", timeRange (5.0f), 0.3f,  Attr().withStringFromValueFunction (ParamFormat::time)));
    add (new F ({ ParamID::ampS, 1 }, "Amp Sostenido",  NormalisableRange<float> (0.0f, 1.0f), 0.8f, Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new F ({ ParamID::ampR, 1 }, "Amp Liberacion", timeRange (8.0f), 0.3f,   Attr().withStringFromValueFunction (ParamFormat::time)));

    // Envolvente del filtro
    add (new F ({ ParamID::fltA, 1 }, "Flt Ataque",     timeRange (5.0f), 0.005f, Attr().withStringFromValueFunction (ParamFormat::time)));
    add (new F ({ ParamID::fltD, 1 }, "Flt Decaimiento", timeRange (5.0f), 0.4f,  Attr().withStringFromValueFunction (ParamFormat::time)));
    add (new F ({ ParamID::fltS, 1 }, "Flt Sostenido",  NormalisableRange<float> (0.0f, 1.0f), 0.3f, Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new F ({ ParamID::fltR, 1 }, "Flt Liberacion", timeRange (8.0f), 0.4f,   Attr().withStringFromValueFunction (ParamFormat::time)));

    // LFO
    NormalisableRange<float> lfoRange (0.05f, 20.0f);
    lfoRange.setSkewForCentre (2.0f);
    add (new F ({ ParamID::lfoRate, 1 }, "LFO Velocidad", lfoRange, 4.0f, Attr().withStringFromValueFunction (ParamFormat::lfoHz)));
    add (new F ({ ParamID::lfoFilter, 1 }, "LFO a Filtro", NormalisableRange<float> (0.0f, 1.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new F ({ ParamID::lfoPitch, 1 }, "LFO a Tono", NormalisableRange<float> (0.0f, 100.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::cents)));

    // Motor y sample
    add (new AudioParameterChoice ({ ParamID::engine, 1 }, "Motor", engineNames(), 0));
    add (new F ({ ParamID::smpStart, 1 }, "Sample Inicio", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));
    add (new AudioParameterInt ({ ParamID::smpTune, 1 }, "Sample Afinacion", -24, 24, 0));
    add (new AudioParameterChoice ({ ParamID::smpReverse, 1 }, "Sample Reversa", onOffNames(), 0));
    add (new AudioParameterChoice ({ ParamID::smpLoop, 1 }, "Sample Loop", onOffNames(), 0));
    add (new F ({ ParamID::smpTexture, 1 }, "Sample Textura", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                Attr().withStringFromValueFunction (ParamFormat::percent)));
    NormalisableRange<float> grainRange (0.01f, 0.4f);
    grainRange.setSkewForCentre (0.08f);
    add (new F ({ ParamID::smpGrain, 1 }, "Sample Grano", grainRange, 0.08f,
                Attr().withStringFromValueFunction (ParamFormat::time)));

    // Master
    add (new F ({ ParamID::master, 1 }, "Volumen", NormalisableRange<float> (-60.0f, 6.0f), -6.0f,
                Attr().withStringFromValueFunction (ParamFormat::decibels)));

    return { p.begin(), p.end() };
}

// Punteros rápidos a los valores de parámetros, para leerlos desde el hilo de audio.
struct ParamPointers
{
    std::atomic<float> *osc1Wave{}, *osc1Octave{}, *osc2Wave{}, *osc2Semi{}, *osc2Fine{}, *oscMix{}, *subLevel{};
    std::atomic<float> *fmAmount{}, *noise{}, *filterType{};
    std::atomic<float> *cutoff{}, *resonance{}, *filterEnv{}, *drive{};
    std::atomic<float> *ampA{}, *ampD{}, *ampS{}, *ampR{};
    std::atomic<float> *fltA{}, *fltD{}, *fltS{}, *fltR{};
    std::atomic<float> *lfoRate{}, *lfoFilter{}, *lfoPitch{};
    std::atomic<float> *engine{}, *smpStart{}, *smpTune{}, *smpReverse{}, *smpLoop{}, *smpTexture{}, *smpGrain{};

    explicit ParamPointers (juce::AudioProcessorValueTreeState& s)
        : ParamPointers ([&s] (const char* id) { return s.getRawParameterValue (id); }) {}

    // Permite apuntar a otro juego de valores (lo usa el renderizador de la imitación)
    explicit ParamPointers (std::function<std::atomic<float>* (const char*)> lookup)
    {
        auto g = [&lookup] (const char* id) { auto* v = lookup (id); jassert (v != nullptr); return v; };
        osc1Wave = g (ParamID::osc1Wave);  osc1Octave = g (ParamID::osc1Octave);
        osc2Wave = g (ParamID::osc2Wave);  osc2Semi = g (ParamID::osc2Semi); osc2Fine = g (ParamID::osc2Fine);
        oscMix = g (ParamID::oscMix);      subLevel = g (ParamID::subLevel);
        fmAmount = g (ParamID::fmAmount);  noise = g (ParamID::noise); filterType = g (ParamID::filterType);
        cutoff = g (ParamID::cutoff);      resonance = g (ParamID::resonance);
        filterEnv = g (ParamID::filterEnv); drive = g (ParamID::drive);
        ampA = g (ParamID::ampA); ampD = g (ParamID::ampD); ampS = g (ParamID::ampS); ampR = g (ParamID::ampR);
        fltA = g (ParamID::fltA); fltD = g (ParamID::fltD); fltS = g (ParamID::fltS); fltR = g (ParamID::fltR);
        lfoRate = g (ParamID::lfoRate); lfoFilter = g (ParamID::lfoFilter); lfoPitch = g (ParamID::lfoPitch);
        engine = g (ParamID::engine); smpStart = g (ParamID::smpStart); smpTune = g (ParamID::smpTune);
        smpReverse = g (ParamID::smpReverse); smpLoop = g (ParamID::smpLoop);
        smpTexture = g (ParamID::smpTexture); smpGrain = g (ParamID::smpGrain);
    }
};
