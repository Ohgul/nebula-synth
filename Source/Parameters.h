#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// ===========================================================================
// Parámetros de Nebula v2.
// Los identificadores de la v1 se conservan para que los proyectos viejos abran igual.
// ===========================================================================

namespace ParamID
{
    // Osciladores
    inline constexpr const char* osc1Wave   = "osc1Wave";
    inline constexpr const char* osc1Shape  = "osc1Shape";
    inline constexpr const char* osc1Table  = "osc1Table";
    inline constexpr const char* osc1Octave = "osc1Octave";
    inline constexpr const char* osc2Wave   = "osc2Wave";
    inline constexpr const char* osc2Shape  = "osc2Shape";
    inline constexpr const char* osc2Table  = "osc2Table";
    inline constexpr const char* osc2Semi   = "osc2Semi";
    inline constexpr const char* osc2Fine   = "osc2Fine";
    inline constexpr const char* oscMix     = "oscMix";
    inline constexpr const char* subLevel   = "subLevel";
    inline constexpr const char* fmAmount   = "fmAmount";
    inline constexpr const char* noise      = "noise";

    // Unísono
    inline constexpr const char* uniVoices  = "uniVoices";
    inline constexpr const char* uniDetune  = "uniDetune";
    inline constexpr const char* uniWidth   = "uniWidth";

    // Filtro 1
    inline constexpr const char* filterType  = "filterType";
    inline constexpr const char* filterSlope = "filterSlope";
    inline constexpr const char* filterChar  = "filterChar";
    inline constexpr const char* cutoff      = "cutoff";
    inline constexpr const char* resonance   = "resonance";
    inline constexpr const char* filterEnv   = "filterEnv";
    inline constexpr const char* drive       = "drive";

    // Filtro 2 y ruteo
    inline constexpr const char* f2On      = "f2On";
    inline constexpr const char* f2Type    = "f2Type";
    inline constexpr const char* f2Slope   = "f2Slope";
    inline constexpr const char* f2Char    = "f2Char";
    inline constexpr const char* f2Cutoff  = "f2Cutoff";
    inline constexpr const char* f2Reso    = "f2Reso";
    inline constexpr const char* f2Env     = "f2Env";
    inline constexpr const char* filterRouting = "filterRouting";

    // Envolventes
    inline constexpr const char* ampA = "ampA";
    inline constexpr const char* ampD = "ampD";
    inline constexpr const char* ampS = "ampS";
    inline constexpr const char* ampR = "ampR";
    inline constexpr const char* ampCurve = "ampCurve";

    inline constexpr const char* fltA = "fltA";
    inline constexpr const char* fltD = "fltD";
    inline constexpr const char* fltS = "fltS";
    inline constexpr const char* fltR = "fltR";
    inline constexpr const char* fltCurve = "fltCurve";
    inline constexpr const char* fltMode  = "fltMode";

    inline constexpr const char* env3A = "env3A";
    inline constexpr const char* env3D = "env3D";
    inline constexpr const char* env3S = "env3S";
    inline constexpr const char* env3R = "env3R";
    inline constexpr const char* env3Curve = "env3Curve";
    inline constexpr const char* env3Mode  = "env3Mode";

    // LFOs
    inline constexpr const char* lfoRate   = "lfoRate";
    inline constexpr const char* lfoFilter = "lfoFilter";
    inline constexpr const char* lfoPitch  = "lfoPitch";
    inline constexpr const char* lfoShape  = "lfoShape";
    inline constexpr const char* lfoSync   = "lfoSync";
    inline constexpr const char* lfoRetrig = "lfoRetrig";
    inline constexpr const char* lfoFade   = "lfoFade";

    inline constexpr const char* lfo2Rate   = "lfo2Rate";
    inline constexpr const char* lfo2Shape  = "lfo2Shape";
    inline constexpr const char* lfo2Sync   = "lfo2Sync";
    inline constexpr const char* lfo2Retrig = "lfo2Retrig";
    inline constexpr const char* lfo2Fade   = "lfo2Fade";

    // Matriz (globales; las celdas se nombran con matrixID())
    inline constexpr const char* modTime   = "modTime";
    inline constexpr const char* modAmount = "modAmount";

    // Voces
    inline constexpr const char* polyMode   = "polyMode";
    inline constexpr const char* voiceCount = "voiceCount";
    inline constexpr const char* glide      = "glide";
    inline constexpr const char* bendRange  = "bendRange";
    inline constexpr const char* velSens    = "velSens";

    // Motor y sample
    inline constexpr const char* engine     = "engine";
    inline constexpr const char* smpStart   = "smpStart";
    inline constexpr const char* smpTune    = "smpTune";
    inline constexpr const char* smpReverse = "smpReverse";
    inline constexpr const char* smpLoop    = "smpLoop";
    inline constexpr const char* smpTexture = "smpTexture";
    inline constexpr const char* smpGrain   = "smpGrain";
    inline constexpr const char* smpPlayMode = "smpPlayMode";
    inline constexpr const char* smpBeats    = "smpBeats";

    inline constexpr const char* master = "master";
}

// ---------------------------------------------------------------------------
// Tipos (el orden importa: se guarda el índice)
namespace Wave { enum { Sine, Triangle, Saw, Pulse, SuperSaw, Sync, Table, count }; }
namespace FilterKind { enum { LowPass, BandPass, HighPass, Notch }; }
namespace LfoShape { enum { Sine, Triangle, Saw, Square, Random, SmoothRandom, count }; }
namespace PolyMode { enum { Poly, Mono, Legato }; }

inline const juce::StringArray& waveNames()
{
    static const juce::StringArray n { "Seno", "Triangulo", "Sierra", "Pulso", "Super Saw", "Sync", "Wavetable" };
    return n;
}
inline const juce::StringArray& tableNames()
{
    static const juce::StringArray n { "Basica", "Armonicos", "Vocal", "Digital", "Usuario" };
    return n;
}
inline const juce::StringArray& filterNames()
{
    static const juce::StringArray n { "Pasa bajos", "Pasa banda", "Pasa altos", "Notch" };
    return n;
}
inline const juce::StringArray& slopeNames()    { static const juce::StringArray n { "12 dB", "24 dB" }; return n; }
inline const juce::StringArray& charNames()     { static const juce::StringArray n { "Limpio", "Calido", "Saturado" }; return n; }
inline const juce::StringArray& routingNames()  { static const juce::StringArray n { "Serie", "Paralelo" }; return n; }
inline const juce::StringArray& envModeNames()  { static const juce::StringArray n { "Normal", "Loop" }; return n; }
inline const juce::StringArray& lfoShapeNames() { static const juce::StringArray n { "Seno", "Triangulo", "Sierra", "Cuadrada", "Aleatorio", "Aleatorio suave" }; return n; }
inline const juce::StringArray& polyNames()     { static const juce::StringArray n { "Poli", "Mono", "Legato" }; return n; }
inline const juce::StringArray& engineNames()   { static const juce::StringArray n { "Sinte", "Sample" }; return n; }
inline const juce::StringArray& onOffNames()    { static const juce::StringArray n { "No", "Si" }; return n; }
inline const juce::StringArray& playModeNames() { static const juce::StringArray n { "Clasico", "Tempo" }; return n; }
inline const juce::StringArray& beatNames()     { static const juce::StringArray n { "Auto", "1/4", "1/2", "1 tiempo", "2 tiempos", "1 compas", "2 compases", "4 compases", "8 compases" }; return n; }
inline double beatsFor (int index) { static const double b[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 }; return b[juce::jlimit (0, 8, index)]; }

// Sincronía del LFO: "Libre" usa Hz; el resto son fracciones de compás (en negras)
inline const juce::StringArray& syncNames()
{
    static const juce::StringArray n { "Libre", "4 compases", "2 compases", "1 compas", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T" };
    return n;
}
inline double syncBeats (int index)   // duración de un ciclo en negras
{
    static const double b[] { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
    return b[juce::jlimit (0, 10, index)];
}

// ---------------------------------------------------------------------------
// Matriz de modulación: fuentes × destinos
namespace ModSrc { enum { Env2, Env3, Lfo1, Lfo2, Velocity, ModWheel, Aftertouch, KeyTrack, count }; }
namespace ModDst { enum { Pitch, Shape1, Shape2, Mix, Fm, Cutoff1, Reso1, Cutoff2, Volume, Pan, count }; }

inline const juce::StringArray& modSrcNames()
{
    static const juce::StringArray n { "Env 2", "Env 3", "LFO 1", "LFO 2", "Velocidad", "Rueda Mod", "Aftertouch", "Nota" };
    return n;
}
inline const juce::StringArray& modDstNames()
{
    static const juce::StringArray n { "Tono", "Forma 1", "Forma 2", "Mezcla", "FM", "Corte 1", "Reso 1", "Corte 2", "Volumen", "Paneo" };
    return n;
}
inline juce::String matrixID (int src, int dst) { return "m" + juce::String (src) + "_" + juce::String (dst); }

// ---------------------------------------------------------------------------
namespace ParamFormat
{
    inline juce::String time (float s, int)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s";
    }
    inline juce::String percent (float v, int)   { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    inline juce::String bipolar (float v, int)   { auto p = juce::roundToInt (v * 100.0f); return (p > 0 ? "+" : "") + juce::String (p) + " %"; }
    inline juce::String hertz (float hz, int)
    {
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    }
    inline juce::String lfoHz (float hz, int)    { return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz"; }
    inline juce::String cents (float c, int)     { auto r = juce::roundToInt (c); return (r > 0 ? "+" : "") + juce::String (r) + " ct"; }
    inline juce::String decibels (float db, int) { return db <= -59.9f ? juce::String ("-inf") : juce::String (db, 1) + " dB"; }
    inline juce::String times (float x, int)     { return "x" + juce::String (x, 2); }
    inline juce::String curve (float c, int)
    {
        if (std::abs (c) < 0.02f) return "Lineal";
        return (c > 0 ? "Rapida " : "Lenta ") + juce::String (juce::roundToInt (std::abs (c) * 100.0f)) + " %";
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using Attr = AudioParameterFloatAttributes;
    using C = AudioParameterChoice;
    using I = AudioParameterInt;
    using B = AudioParameterBool;

    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add = [&p] (auto* param) { p.emplace_back (param); };
    auto timeRange = [] (float max) { NormalisableRange<float> r (0.001f, max); r.setSkewForCentre (0.4f); return r; };
    auto unit = NormalisableRange<float> (0.0f, 1.0f);
    auto bip  = NormalisableRange<float> (-1.0f, 1.0f);
    NormalisableRange<float> cutRange (20.0f, 20000.0f);
    cutRange.setSkewForCentre (1000.0f);
    NormalisableRange<float> lfoRange (0.05f, 20.0f);
    lfoRange.setSkewForCentre (2.0f);
    auto pct = Attr().withStringFromValueFunction (ParamFormat::percent);

    // ---- Osciladores ----
    add (new C ({ ParamID::osc1Wave, 1 }, "Osc 1 Onda", waveNames(), Wave::Saw));
    add (new F ({ ParamID::osc1Shape, 1 }, "Osc 1 Forma", unit, 0.5f, pct));
    add (new C ({ ParamID::osc1Table, 1 }, "Osc 1 Tabla", tableNames(), 0));
    add (new I ({ ParamID::osc1Octave, 1 }, "Osc 1 Octava", -2, 2, 0));
    add (new C ({ ParamID::osc2Wave, 1 }, "Osc 2 Onda", waveNames(), Wave::Pulse));
    add (new F ({ ParamID::osc2Shape, 1 }, "Osc 2 Forma", unit, 0.5f, pct));
    add (new C ({ ParamID::osc2Table, 1 }, "Osc 2 Tabla", tableNames(), 1));
    add (new I ({ ParamID::osc2Semi, 1 }, "Osc 2 Semitonos", -24, 24, 0));
    add (new F ({ ParamID::osc2Fine, 1 }, "Osc 2 Fino", NormalisableRange<float> (-50.0f, 50.0f), 7.0f, Attr().withStringFromValueFunction (ParamFormat::cents)));
    add (new F ({ ParamID::oscMix, 1 }, "Mezcla Osc", unit, 0.5f, pct));
    add (new F ({ ParamID::subLevel, 1 }, "Sub", unit, 0.0f, pct));
    add (new F ({ ParamID::fmAmount, 1 }, "FM", unit, 0.0f, pct));
    add (new F ({ ParamID::noise, 1 }, "Ruido", unit, 0.0f, pct));

    add (new I ({ ParamID::uniVoices, 1 }, "Unisono Voces", 1, 7, 1));
    add (new F ({ ParamID::uniDetune, 1 }, "Unisono Desafinacion", unit, 0.25f, pct));
    add (new F ({ ParamID::uniWidth, 1 }, "Unisono Ancho", unit, 0.7f, pct));

    // ---- Filtro 1 ----
    add (new C ({ ParamID::filterType, 1 }, "Filtro 1 Tipo", filterNames(), FilterKind::LowPass));
    add (new C ({ ParamID::filterSlope, 1 }, "Filtro 1 Pendiente", slopeNames(), 0));
    add (new C ({ ParamID::filterChar, 1 }, "Filtro 1 Caracter", charNames(), 0));
    add (new F ({ ParamID::cutoff, 1 }, "Filtro 1 Corte", cutRange, 2500.0f, Attr().withStringFromValueFunction (ParamFormat::hertz)));
    add (new F ({ ParamID::resonance, 1 }, "Filtro 1 Resonancia", unit, 0.25f, pct));
    add (new F ({ ParamID::filterEnv, 1 }, "Filtro 1 Env", bip, 0.35f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::drive, 1 }, "Drive", unit, 0.1f, pct));

    // ---- Filtro 2 ----
    add (new B ({ ParamID::f2On, 1 }, "Filtro 2 Encendido", false));
    add (new C ({ ParamID::f2Type, 1 }, "Filtro 2 Tipo", filterNames(), FilterKind::HighPass));
    add (new C ({ ParamID::f2Slope, 1 }, "Filtro 2 Pendiente", slopeNames(), 0));
    add (new C ({ ParamID::f2Char, 1 }, "Filtro 2 Caracter", charNames(), 0));
    add (new F ({ ParamID::f2Cutoff, 1 }, "Filtro 2 Corte", cutRange, 120.0f, Attr().withStringFromValueFunction (ParamFormat::hertz)));
    add (new F ({ ParamID::f2Reso, 1 }, "Filtro 2 Resonancia", unit, 0.1f, pct));
    add (new F ({ ParamID::f2Env, 1 }, "Filtro 2 Env", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new C ({ ParamID::filterRouting, 1 }, "Ruteo Filtros", routingNames(), 0));

    // ---- Envolventes ----
    auto curveAttr = Attr().withStringFromValueFunction (ParamFormat::curve);
    auto tAttr = Attr().withStringFromValueFunction (ParamFormat::time);
    add (new F ({ ParamID::ampA, 1 }, "Env 1 Ataque",      timeRange (5.0f), 0.005f, tAttr));
    add (new F ({ ParamID::ampD, 1 }, "Env 1 Decaimiento", timeRange (5.0f), 0.3f,   tAttr));
    add (new F ({ ParamID::ampS, 1 }, "Env 1 Sostenido",   unit, 0.8f, pct));
    add (new F ({ ParamID::ampR, 1 }, "Env 1 Liberacion",  timeRange (8.0f), 0.3f,   tAttr));
    add (new F ({ ParamID::ampCurve, 1 }, "Env 1 Curva",   bip, 0.0f, curveAttr));

    add (new F ({ ParamID::fltA, 1 }, "Env 2 Ataque",      timeRange (5.0f), 0.005f, tAttr));
    add (new F ({ ParamID::fltD, 1 }, "Env 2 Decaimiento", timeRange (5.0f), 0.4f,   tAttr));
    add (new F ({ ParamID::fltS, 1 }, "Env 2 Sostenido",   unit, 0.3f, pct));
    add (new F ({ ParamID::fltR, 1 }, "Env 2 Liberacion",  timeRange (8.0f), 0.4f,   tAttr));
    add (new F ({ ParamID::fltCurve, 1 }, "Env 2 Curva",   bip, 0.0f, curveAttr));
    add (new C ({ ParamID::fltMode, 1 }, "Env 2 Modo", envModeNames(), 0));

    add (new F ({ ParamID::env3A, 1 }, "Env 3 Ataque",      timeRange (5.0f), 0.2f, tAttr));
    add (new F ({ ParamID::env3D, 1 }, "Env 3 Decaimiento", timeRange (5.0f), 0.6f, tAttr));
    add (new F ({ ParamID::env3S, 1 }, "Env 3 Sostenido",   unit, 0.0f, pct));
    add (new F ({ ParamID::env3R, 1 }, "Env 3 Liberacion",  timeRange (8.0f), 0.5f, tAttr));
    add (new F ({ ParamID::env3Curve, 1 }, "Env 3 Curva",   bip, 0.0f, curveAttr));
    add (new C ({ ParamID::env3Mode, 1 }, "Env 3 Modo", envModeNames(), 0));

    // ---- LFOs ----
    auto fadeRange = NormalisableRange<float> (0.0f, 4.0f);
    fadeRange.setSkewForCentre (0.5f);
    add (new F ({ ParamID::lfoRate, 1 }, "LFO 1 Velocidad", lfoRange, 4.0f, Attr().withStringFromValueFunction (ParamFormat::lfoHz)));
    add (new F ({ ParamID::lfoFilter, 1 }, "LFO 1 a Filtro", unit, 0.0f, pct));
    add (new F ({ ParamID::lfoPitch, 1 }, "LFO 1 a Tono", NormalisableRange<float> (0.0f, 100.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::cents)));
    add (new C ({ ParamID::lfoShape, 1 }, "LFO 1 Forma", lfoShapeNames(), LfoShape::Sine));
    add (new C ({ ParamID::lfoSync, 1 }, "LFO 1 Sync", syncNames(), 0));
    add (new B ({ ParamID::lfoRetrig, 1 }, "LFO 1 Reinicio", true));
    add (new F ({ ParamID::lfoFade, 1 }, "LFO 1 Entrada", fadeRange, 0.0f, tAttr));

    add (new F ({ ParamID::lfo2Rate, 1 }, "LFO 2 Velocidad", lfoRange, 0.5f, Attr().withStringFromValueFunction (ParamFormat::lfoHz)));
    add (new C ({ ParamID::lfo2Shape, 1 }, "LFO 2 Forma", lfoShapeNames(), LfoShape::Triangle));
    add (new C ({ ParamID::lfo2Sync, 1 }, "LFO 2 Sync", syncNames(), 0));
    add (new B ({ ParamID::lfo2Retrig, 1 }, "LFO 2 Reinicio", false));
    add (new F ({ ParamID::lfo2Fade, 1 }, "LFO 2 Entrada", fadeRange, 0.0f, tAttr));

    // ---- Matriz ----
    for (int s = 0; s < ModSrc::count; ++s)
        for (int d = 0; d < ModDst::count; ++d)
            add (new F ({ matrixID (s, d), 1 }, "Mod " + modSrcNames()[s] + " > " + modDstNames()[d], bip, 0.0f,
                        Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    NormalisableRange<float> timeX (0.25f, 4.0f);
    timeX.setSkewForCentre (1.0f);
    add (new F ({ ParamID::modTime, 1 }, "Mod Tiempo", timeX, 1.0f, Attr().withStringFromValueFunction (ParamFormat::times)));
    add (new F ({ ParamID::modAmount, 1 }, "Mod Cantidad", unit, 1.0f, pct));

    // ---- Voces ----
    NormalisableRange<float> glideRange (0.0f, 2.0f);
    glideRange.setSkewForCentre (0.2f);
    add (new C ({ ParamID::polyMode, 1 }, "Modo Voces", polyNames(), PolyMode::Poly));
    add (new I ({ ParamID::voiceCount, 1 }, "Voces", 1, 16, 12));
    add (new F ({ ParamID::glide, 1 }, "Glide", glideRange, 0.0f, tAttr));
    add (new I ({ ParamID::bendRange, 1 }, "Rango Pitch Bend", 1, 24, 2));
    add (new F ({ ParamID::velSens, 1 }, "Sensibilidad Velocidad", unit, 0.65f, pct));

    // ---- Motor y sample ----
    add (new C ({ ParamID::engine, 1 }, "Motor", engineNames(), 0));
    add (new F ({ ParamID::smpStart, 1 }, "Sample Inicio", unit, 0.0f, pct));
    add (new I ({ ParamID::smpTune, 1 }, "Sample Afinacion", -24, 24, 0));
    add (new C ({ ParamID::smpReverse, 1 }, "Sample Reversa", onOffNames(), 0));
    add (new C ({ ParamID::smpLoop, 1 }, "Sample Loop", onOffNames(), 0));
    add (new F ({ ParamID::smpTexture, 1 }, "Sample Textura", unit, 0.0f, pct));
    NormalisableRange<float> grainRange (0.01f, 0.4f);
    grainRange.setSkewForCentre (0.08f);
    add (new F ({ ParamID::smpGrain, 1 }, "Sample Grano", grainRange, 0.08f, tAttr));
    add (new C ({ ParamID::smpPlayMode, 1 }, "Sample Reproduccion", playModeNames(), 0));
    add (new C ({ ParamID::smpBeats, 1 }, "Sample Duracion", beatNames(), 0));

    add (new F ({ ParamID::master, 1 }, "Volumen", NormalisableRange<float> (-60.0f, 6.0f), -6.0f, Attr().withStringFromValueFunction (ParamFormat::decibels)));

    return { p.begin(), p.end() };
}

// ---------------------------------------------------------------------------
// Acceso rápido a los valores desde el hilo de audio
namespace P
{
    enum Id
    {
        osc1Wave, osc1Shape, osc1Table, osc1Octave, osc2Wave, osc2Shape, osc2Table, osc2Semi, osc2Fine,
        oscMix, subLevel, fmAmount, noise, uniVoices, uniDetune, uniWidth,
        filterType, filterSlope, filterChar, cutoff, resonance, filterEnv, drive,
        f2On, f2Type, f2Slope, f2Char, f2Cutoff, f2Reso, f2Env, filterRouting,
        ampA, ampD, ampS, ampR, ampCurve,
        fltA, fltD, fltS, fltR, fltCurve, fltMode,
        env3A, env3D, env3S, env3R, env3Curve, env3Mode,
        lfoRate, lfoFilter, lfoPitch, lfoShape, lfoSync, lfoRetrig, lfoFade,
        lfo2Rate, lfo2Shape, lfo2Sync, lfo2Retrig, lfo2Fade,
        modTime, modAmount, polyMode, voiceCount, glide, bendRange, velSens,
        engine, smpStart, smpTune, smpReverse, smpLoop, smpTexture, smpGrain, smpPlayMode, smpBeats,
        count
    };

    inline const char* const* ids()
    {
        static const char* const list[] {
            ParamID::osc1Wave, ParamID::osc1Shape, ParamID::osc1Table, ParamID::osc1Octave, ParamID::osc2Wave, ParamID::osc2Shape,
            ParamID::osc2Table, ParamID::osc2Semi, ParamID::osc2Fine,
            ParamID::oscMix, ParamID::subLevel, ParamID::fmAmount, ParamID::noise, ParamID::uniVoices, ParamID::uniDetune, ParamID::uniWidth,
            ParamID::filterType, ParamID::filterSlope, ParamID::filterChar, ParamID::cutoff, ParamID::resonance, ParamID::filterEnv, ParamID::drive,
            ParamID::f2On, ParamID::f2Type, ParamID::f2Slope, ParamID::f2Char, ParamID::f2Cutoff, ParamID::f2Reso, ParamID::f2Env, ParamID::filterRouting,
            ParamID::ampA, ParamID::ampD, ParamID::ampS, ParamID::ampR, ParamID::ampCurve,
            ParamID::fltA, ParamID::fltD, ParamID::fltS, ParamID::fltR, ParamID::fltCurve, ParamID::fltMode,
            ParamID::env3A, ParamID::env3D, ParamID::env3S, ParamID::env3R, ParamID::env3Curve, ParamID::env3Mode,
            ParamID::lfoRate, ParamID::lfoFilter, ParamID::lfoPitch, ParamID::lfoShape, ParamID::lfoSync, ParamID::lfoRetrig, ParamID::lfoFade,
            ParamID::lfo2Rate, ParamID::lfo2Shape, ParamID::lfo2Sync, ParamID::lfo2Retrig, ParamID::lfo2Fade,
            ParamID::modTime, ParamID::modAmount, ParamID::polyMode, ParamID::voiceCount, ParamID::glide, ParamID::bendRange, ParamID::velSens,
            ParamID::engine, ParamID::smpStart, ParamID::smpTune, ParamID::smpReverse, ParamID::smpLoop, ParamID::smpTexture, ParamID::smpGrain,
            ParamID::smpPlayMode, ParamID::smpBeats
        };
        static_assert (sizeof (list) / sizeof (list[0]) == count, "lista de parámetros incompleta");
        return list;
    }
}

struct ParamPointers
{
    std::array<std::atomic<float>*, P::count> v {};
    std::array<std::array<std::atomic<float>*, ModDst::count>, ModSrc::count> matrix {};

    float operator[] (P::Id id) const { return v[(size_t) id]->load (std::memory_order_relaxed); }
    int   i (P::Id id) const          { return (int) std::lround (v[(size_t) id]->load (std::memory_order_relaxed)); }
    float mod (int s, int d) const    { return matrix[(size_t) s][(size_t) d]->load (std::memory_order_relaxed); }

    explicit ParamPointers (juce::AudioProcessorValueTreeState& s)
        : ParamPointers ([&s] (const juce::String& id) { return s.getRawParameterValue (id); }) {}

    // Permite apuntar a otro juego de valores (lo usa el renderizador de la imitación)
    explicit ParamPointers (std::function<std::atomic<float>* (const juce::String&)> lookup)
    {
        for (int k = 0; k < P::count; ++k)
        {
            v[(size_t) k] = lookup (P::ids()[k]);
            jassert (v[(size_t) k] != nullptr);
        }
        for (int s = 0; s < ModSrc::count; ++s)
            for (int d = 0; d < ModDst::count; ++d)
            {
                matrix[(size_t) s][(size_t) d] = lookup (matrixID (s, d));
                jassert (matrix[(size_t) s][(size_t) d] != nullptr);
            }
    }
};
