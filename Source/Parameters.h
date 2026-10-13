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
    inline constexpr const char* noiseColor = "noiseColor";

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
    inline constexpr const char* lfoOn      = "lfoOn";
    inline constexpr const char* lfo2On     = "lfo2On";

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
    inline constexpr const char* smpGrainAmt  = "smpGrainAmt";
    inline constexpr const char* smpGrainType = "smpGrainType";

    inline constexpr const char* master = "master";

    // ---- v3: encendido / apagado por sección ----
    inline constexpr const char* osc1On  = "osc1On";
    inline constexpr const char* osc2On  = "osc2On";
    inline constexpr const char* subOn   = "subOn";
    inline constexpr const char* noiseOn = "noiseOn";
    inline constexpr const char* uniOn   = "uniOn";
    inline constexpr const char* f1On    = "f1On";
    inline constexpr const char* driveOn = "driveOn";
    inline constexpr const char* grainsOn = "grainsOn";
    inline constexpr const char* analog  = "analog";      // deriva analógica

    // ---- v3: motor de resíntesis ----
    inline constexpr const char* rsSpeed    = "rsSpeed";
    inline constexpr const char* rsHarm     = "rsHarm";
    inline constexpr const char* rsPartials = "rsPartials";
    inline constexpr const char* rsNoise    = "rsNoise";
    inline constexpr const char* rsTone     = "rsTone";
    inline constexpr const char* rsStretch  = "rsStretch";
    inline constexpr const char* rsPitch    = "rsPitch";   // sigue la curva de tono del original
    inline constexpr const char* rsWidth    = "rsWidth";

    // ---- v3: macros ----
    inline constexpr const char* macroBright = "macroBright";
    inline constexpr const char* macroBody   = "macroBody";
    inline constexpr const char* macroMove   = "macroMove";
    inline constexpr const char* macroSpace  = "macroSpace";
    inline constexpr const char* macroChaos  = "macroChaos";

    // ---- v3: efectos ----
    inline constexpr const char* fxEqOn      = "fxEqOn";
    inline constexpr const char* fxEqLow     = "fxEqLow";
    inline constexpr const char* fxEqMid     = "fxEqMid";
    inline constexpr const char* fxEqMidFreq = "fxEqMidFreq";
    inline constexpr const char* fxEqHigh    = "fxEqHigh";
    inline constexpr const char* fxCompOn    = "fxCompOn";
    inline constexpr const char* fxCompAmt   = "fxCompAmt";
    inline constexpr const char* fxChorusOn    = "fxChorusOn";
    inline constexpr const char* fxChorusRate  = "fxChorusRate";
    inline constexpr const char* fxChorusDepth = "fxChorusDepth";
    inline constexpr const char* fxChorusMix   = "fxChorusMix";
    inline constexpr const char* fxDelayOn   = "fxDelayOn";
    inline constexpr const char* fxDelaySync = "fxDelaySync";
    inline constexpr const char* fxDelayMs   = "fxDelayMs";
    inline constexpr const char* fxDelayFb   = "fxDelayFb";
    inline constexpr const char* fxDelayMix  = "fxDelayMix";
    inline constexpr const char* fxDelayPing = "fxDelayPing";
    inline constexpr const char* fxDelayTone = "fxDelayTone";
    inline constexpr const char* fxRevOn     = "fxRevOn";
    inline constexpr const char* fxRevSize   = "fxRevSize";
    inline constexpr const char* fxRevDecay  = "fxRevDecay";
    inline constexpr const char* fxRevDamp   = "fxRevDamp";
    inline constexpr const char* fxRevPre    = "fxRevPre";
    inline constexpr const char* fxRevMix    = "fxRevMix";
    inline constexpr const char* fxWidth     = "fxWidth";

    // ---- v3: interpretación (no se guardan en el genoma) ----
    inline constexpr const char* quality = "quality";
    inline constexpr const char* mpeOn   = "mpeOn";

    // ---- v3.1: ADN del sonido recreado (todos neutros por defecto: los proyectos anteriores suenan igual) ----
    // A · Identidad tonal
    inline constexpr const char* dnaPitchDepth = "dnaPitchDepth";   // profundidad de la curva de tono (vibrato, glides, caídas)
    inline constexpr const char* dnaFormant    = "dnaFormant";      // desplaza los formantes sin mover la nota
    // B · Estructura armónica
    inline constexpr const char* dnaTilt       = "dnaTilt";         // inclinación espectral (dB/octava)
    inline constexpr const char* dnaTiltEvolve = "dnaTiltEvolve";   // inclinación que cambia del ataque al final
    inline constexpr const char* dnaOddEven    = "dnaOddEven";      // pares / impares
    inline constexpr const char* dnaHarmonics  = "dnaHarmonics";    // cantidad de armónicos
    // C · Textura
    inline constexpr const char* dnaNoiseTilt  = "dnaNoiseTilt";    // color del ruido
    inline constexpr const char* dnaRough      = "dnaRough";        // aspereza
    inline constexpr const char* dnaDensity    = "dnaDensity";      // densidad (copias desafinadas)
    // D · Comportamiento temporal
    inline constexpr const char* dnaAttack     = "dnaAttack";       // estira o comprime el ataque
    inline constexpr const char* dnaTransient  = "dnaTransient";    // nivel del golpe/cabeza original
    inline constexpr const char* dnaRelease    = "dnaRelease";      // estira o comprime la cola
    // E · Movimiento
    inline constexpr const char* dnaEvolve     = "dnaEvolve";       // cuánto cambia el timbre en el tiempo
    inline constexpr const char* dnaHold       = "dnaHold";         // al sostener: recorrer o congelar
    // F · Transformación
    inline constexpr const char* transform     = "transform";       // ABISMO (-1) ← ORIGINAL (0) → COSMOS (+1)
    inline constexpr const char* identity      = "identity";        // cuánto del original se conserva al transformar
    inline constexpr const char* transformOut  = "transformOut";    // nivel de salida de Cosmos/Abismo (dB)
    // ---- 0.3.1: tercer operador FM (neutro por defecto: los proyectos anteriores suenan igual) ----
    inline constexpr const char* op3On     = "op3On";
    inline constexpr const char* op3Ratio  = "op3Ratio";      // relación de frecuencia con su destino
    inline constexpr const char* op3Amount = "op3Amount";     // índice de modulación
    inline constexpr const char* op3Env    = "op3Env";        // Envolvente 3 -> índice (bipolar)
    inline constexpr const char* op3Target = "op3Target";     // a qué oscilador modula
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
inline const juce::StringArray& charNames()     { static const juce::StringArray n { "Limpio", "Moog", "Moog +" }; return n; }
inline const juce::StringArray& routingNames()  { static const juce::StringArray n { "Serie", "Paralelo" }; return n; }
inline const juce::StringArray& envModeNames()  { static const juce::StringArray n { "Normal", "Loop" }; return n; }
inline const juce::StringArray& lfoShapeNames() { static const juce::StringArray n { "Seno", "Triangulo", "Sierra", "Cuadrada", "Aleatorio", "Aleatorio suave" }; return n; }
inline const juce::StringArray& polyNames()     { static const juce::StringArray n { "Poli", "Mono", "Legato" }; return n; }
namespace GrainType { enum { Smooth, Sharp, Scatter, Reverse, Octave, count }; }
inline const juce::StringArray& grainTypeNames() { static const juce::StringArray n { "Suave", "Nitido", "Disperso", "Reversa", "Octava" }; return n; }
inline const juce::StringArray& holdNames()     { static const juce::StringArray n { "Recorrer", "Congelar" }; return n; }
inline const juce::StringArray& engineNames()   { static const juce::StringArray n { "Sinte", "Sample", "Resintesis" }; return n; }
namespace Engine { enum { Synth, Sample, Resynth }; }
inline const juce::StringArray& qualityNames()  { static const juce::StringArray n { "Normal", "Alta (2x)", "Ultra (4x)" }; return n; }
inline const juce::StringArray& delaySyncNames() { static const juce::StringArray n { "Libre (ms)", "1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1 compas" }; return n; }
inline double delaySyncBeats (int i) { static const double b[] { 0.0, 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 }; return b[juce::jlimit (0, 9, i)]; }
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
namespace ModDst { enum { Pitch, Shape1, Shape2, Mix, Fm, Cutoff1, Reso1, Cutoff2, Volume, Pan, Noise, count }; }

inline const juce::StringArray& modSrcNames()
{
    static const juce::StringArray n { "Env 2", "Env 3", "LFO 1", "LFO 2", "Velocidad", "Rueda Mod", "Aftertouch", "Nota" };
    return n;
}
inline const juce::StringArray& modDstNames()
{
    static const juce::StringArray n { "Tono", "Forma 1", "Forma 2", "Mezcla", "FM", "Corte 1", "Reso 1", "Corte 2", "Volumen", "Paneo", "Ruido" };
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
    inline juce::String semis (float st, int)    { return (st > 0.05f ? "+" : "") + juce::String (st, 1) + " st"; }
    inline juce::String dbOct (float d, int)     { return (d > 0.05f ? "+" : "") + juce::String (d, 1) + " dB/oct"; }
    inline juce::String curve (float c, int)
    {
        if (std::abs (c) < 0.02f) return "Lineal";
        return (c > 0 ? "Rapida " : "Lenta ") + juce::String (juce::roundToInt (std::abs (c) * 100.0f)) + " %";
    }
}

// Lista de parámetros (objetos sueltos): la usa el plugin y también el renderizador offline de FORGE,
// que necesita los mismos rangos y valores por defecto sin depender de un procesador.
inline std::vector<std::unique_ptr<juce::RangedAudioParameter>> createParameterList()
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
    // Corte en escala logarítmica de verdad: cada tramo de la perilla es la misma cantidad de octavas
    NormalisableRange<float> cutRange (20.0f, 20000.0f,
        [] (float a, float b, float x) { return a * std::pow (b / a, x); },
        [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
        [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
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
    add (new F ({ ParamID::noiseColor, 1 }, "Ruido Color", unit, 1.0f, Attr().withStringFromValueFunction ([] (float v, int)
         { return v > 0.995f ? juce::String ("Blanco") : ParamFormat::hertz (200.0f * std::pow (100.0f, v), 0); })));

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
    add (new B ({ ParamID::lfoOn, 1 }, "LFO 1 Encendido", true));
    add (new B ({ ParamID::lfo2On, 1 }, "LFO 2 Encendido", true));

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
    add (new F ({ ParamID::smpGrainAmt, 1 }, "Sample Granos Cantidad", unit, 0.0f, pct));
    add (new C ({ ParamID::smpGrainType, 1 }, "Sample Granos Tipo", grainTypeNames(), 0));

    // ================== v3 ==================
    add (new B ({ ParamID::osc1On, 1 }, "Osc 1 Encendido", true));
    add (new B ({ ParamID::osc2On, 1 }, "Osc 2 Encendido", true));
    add (new B ({ ParamID::subOn, 1 }, "Sub Encendido", true));
    add (new B ({ ParamID::noiseOn, 1 }, "Ruido Encendido", true));
    add (new B ({ ParamID::uniOn, 1 }, "Unisono Encendido", true));
    add (new B ({ ParamID::f1On, 1 }, "Filtro 1 Encendido", true));
    add (new B ({ ParamID::driveOn, 1 }, "Drive Encendido", true));
    add (new B ({ ParamID::grainsOn, 1 }, "Granos Encendido", true));
    add (new F ({ ParamID::analog, 1 }, "Deriva Analogica", unit, 0.0f, pct));

    NormalisableRange<float> speedRange (0.25f, 4.0f); speedRange.setSkewForCentre (1.0f);
    add (new F ({ ParamID::rsSpeed, 1 }, "Resintesis Velocidad", speedRange, 1.0f, Attr().withStringFromValueFunction (ParamFormat::times)));
    add (new F ({ ParamID::rsHarm, 1 }, "Resintesis Armonicos", NormalisableRange<float> (0.0f, 1.5f), 1.0f, pct));
    add (new F ({ ParamID::rsPartials, 1 }, "Resintesis Parciales", NormalisableRange<float> (0.0f, 1.5f), 1.0f, pct));
    add (new F ({ ParamID::rsNoise, 1 }, "Resintesis Ruido", NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct));
    add (new F ({ ParamID::rsTone, 1 }, "Resintesis Tono", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::rsStretch, 1 }, "Resintesis Estirar", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new B ({ ParamID::rsPitch, 1 }, "Resintesis Curva de Tono", true));
    add (new F ({ ParamID::rsWidth, 1 }, "Resintesis Ancho", unit, 1.0f, pct));

    add (new F ({ ParamID::macroBright, 1 }, "Macro Brillo", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::macroBody, 1 }, "Macro Cuerpo", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::macroMove, 1 }, "Macro Movimiento", unit, 0.0f, pct));
    add (new F ({ ParamID::macroSpace, 1 }, "Macro Espacio", unit, 0.0f, pct));
    add (new F ({ ParamID::macroChaos, 1 }, "Macro Locura", unit, 0.0f, pct));

    auto db = Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
    NormalisableRange<float> midF (200.0f, 8000.0f); midF.setSkewForCentre (1200.0f);
    add (new B ({ ParamID::fxEqOn, 1 }, "EQ Encendido", false));
    add (new F ({ ParamID::fxEqLow, 1 }, "EQ Graves", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, db));
    add (new F ({ ParamID::fxEqMid, 1 }, "EQ Medios", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, db));
    add (new F ({ ParamID::fxEqMidFreq, 1 }, "EQ Frecuencia Medios", midF, 1200.0f, Attr().withStringFromValueFunction (ParamFormat::hertz)));
    add (new F ({ ParamID::fxEqHigh, 1 }, "EQ Agudos", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, db));
    add (new B ({ ParamID::fxCompOn, 1 }, "Compresor Encendido", false));
    add (new F ({ ParamID::fxCompAmt, 1 }, "Compresor Cantidad", unit, 0.4f, pct));
    NormalisableRange<float> chRate (0.05f, 5.0f); chRate.setSkewForCentre (0.6f);
    add (new B ({ ParamID::fxChorusOn, 1 }, "Chorus Encendido", false));
    add (new F ({ ParamID::fxChorusRate, 1 }, "Chorus Velocidad", chRate, 0.6f, Attr().withStringFromValueFunction (ParamFormat::lfoHz)));
    add (new F ({ ParamID::fxChorusDepth, 1 }, "Chorus Profundidad", unit, 0.4f, pct));
    add (new F ({ ParamID::fxChorusMix, 1 }, "Chorus Mezcla", unit, 0.5f, pct));
    NormalisableRange<float> dMs (10.0f, 1500.0f); dMs.setSkewForCentre (300.0f);
    add (new B ({ ParamID::fxDelayOn, 1 }, "Delay Encendido", false));
    add (new C ({ ParamID::fxDelaySync, 1 }, "Delay Tiempo", delaySyncNames(), 4));
    add (new F ({ ParamID::fxDelayMs, 1 }, "Delay ms", dMs, 300.0f, Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    add (new F ({ ParamID::fxDelayFb, 1 }, "Delay Repeticiones", NormalisableRange<float> (0.0f, 0.95f), 0.35f, pct));
    add (new F ({ ParamID::fxDelayMix, 1 }, "Delay Mezcla", unit, 0.25f, pct));
    add (new B ({ ParamID::fxDelayPing, 1 }, "Delay Ping Pong", true));
    add (new F ({ ParamID::fxDelayTone, 1 }, "Delay Tono", unit, 0.6f, pct));
    NormalisableRange<float> decay (0.2f, 20.0f); decay.setSkewForCentre (2.5f);
    add (new B ({ ParamID::fxRevOn, 1 }, "Reverb Encendido", false));
    add (new F ({ ParamID::fxRevSize, 1 }, "Reverb Tamano", unit, 0.6f, pct));
    add (new F ({ ParamID::fxRevDecay, 1 }, "Reverb Cola", decay, 2.5f, tAttr));
    add (new F ({ ParamID::fxRevDamp, 1 }, "Reverb Brillo", unit, 0.55f, pct));
    add (new F ({ ParamID::fxRevPre, 1 }, "Reverb Pre-delay", NormalisableRange<float> (0.0f, 0.2f), 0.02f, tAttr));
    add (new F ({ ParamID::fxRevMix, 1 }, "Reverb Mezcla", unit, 0.25f, pct));
    add (new F ({ ParamID::fxWidth, 1 }, "Ancho Estereo", NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct));

    add (new C ({ ParamID::quality, 1 }, "Calidad", qualityNames(), 0));
    add (new B ({ ParamID::mpeOn, 1 }, "MPE", false));

    add (new F ({ ParamID::master, 1 }, "Volumen", NormalisableRange<float> (-60.0f, 6.0f), -6.0f, Attr().withStringFromValueFunction (ParamFormat::decibels)));

    // ---- ADN (v3.1) ----
    auto timesAttr = Attr().withStringFromValueFunction (ParamFormat::times);
    NormalisableRange<float> scaleRange (0.25f, 4.0f); scaleRange.setSkewForCentre (1.0f);
    add (new F ({ ParamID::dnaPitchDepth, 1 }, "ADN Profundidad de tono", NormalisableRange<float> (0.0f, 2.0f), 1.0f, timesAttr));
    add (new F ({ ParamID::dnaFormant, 1 }, "ADN Formantes", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::semis)));
    add (new F ({ ParamID::dnaTilt, 1 }, "ADN Brillo", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::dbOct)));
    add (new F ({ ParamID::dnaTiltEvolve, 1 }, "ADN Brillo en el tiempo", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::dbOct)));
    add (new F ({ ParamID::dnaOddEven, 1 }, "ADN Pares/Impares", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::dnaHarmonics, 1 }, "ADN Armonicos", unit, 1.0f, pct));
    add (new F ({ ParamID::dnaNoiseTilt, 1 }, "ADN Color del ruido", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::dnaRough, 1 }, "ADN Aspereza", unit, 0.0f, pct));
    add (new F ({ ParamID::dnaDensity, 1 }, "ADN Densidad", unit, 0.0f, pct));
    add (new F ({ ParamID::dnaAttack, 1 }, "ADN Ataque", scaleRange, 1.0f, timesAttr));
    add (new F ({ ParamID::dnaTransient, 1 }, "ADN Golpe", NormalisableRange<float> (0.0f, 2.0f), 1.0f, timesAttr));
    add (new F ({ ParamID::dnaRelease, 1 }, "ADN Cola", scaleRange, 1.0f, timesAttr));
    add (new F ({ ParamID::dnaEvolve, 1 }, "ADN Evolucion", NormalisableRange<float> (0.0f, 2.0f), 1.0f, timesAttr));
    add (new C ({ ParamID::dnaHold, 1 }, "ADN Al sostener", holdNames(), 0));
    add (new F ({ ParamID::transform, 1 }, "Abismo - Original - Cosmos", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
    add (new F ({ ParamID::identity, 1 }, "Identidad", unit, 0.0f, pct));
    add (new F ({ ParamID::transformOut, 1 }, "Cosmos/Abismo Salida", NormalisableRange<float> (-24.0f, 6.0f), 0.0f, Attr().withStringFromValueFunction (ParamFormat::decibels)));

    // ---- 0.3.1: operador FM 3 (seno) que modula la fase del osc 1 o del osc 2 (cadena 3 -> 2 -> 1) ----
    {
        NormalisableRange<float> ratio (0.25f, 16.0f);
        ratio.setSkewForCentre (2.0f);
        add (new B ({ ParamID::op3On, 2 }, "FM Op 3 Encendido", false));
        add (new F ({ ParamID::op3Ratio, 2 }, "FM Op 3 Relacion", ratio, 2.0f, Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 3) + " x"; })));
        add (new F ({ ParamID::op3Amount, 2 }, "FM Op 3 Indice", unit, 0.0f, pct));
        add (new F ({ ParamID::op3Env, 2 }, "FM Op 3 Env 3", bip, 0.0f, Attr().withStringFromValueFunction (ParamFormat::bipolar)));
        add (new C ({ ParamID::op3Target, 2 }, "FM Op 3 Destino", StringArray { "Osc 1", "Osc 2" }, 1));
    }

    return p;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    auto p = createParameterList();
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
        lfoOn, lfo2On, smpGrainAmt, smpGrainType, noiseColor,
        osc1On, osc2On, subOn, noiseOn, uniOn, f1On, driveOn, grainsOn, analog,
        rsSpeed, rsHarm, rsPartials, rsNoise, rsTone, rsStretch, rsPitch, rsWidth,
        macroBright, macroBody, macroMove, macroSpace, macroChaos,
        fxEqOn, fxEqLow, fxEqMid, fxEqMidFreq, fxEqHigh, fxCompOn, fxCompAmt,
        fxChorusOn, fxChorusRate, fxChorusDepth, fxChorusMix,
        fxDelayOn, fxDelaySync, fxDelayMs, fxDelayFb, fxDelayMix, fxDelayPing, fxDelayTone,
        fxRevOn, fxRevSize, fxRevDecay, fxRevDamp, fxRevPre, fxRevMix, fxWidth, quality, mpeOn, master,
        dnaPitchDepth, dnaFormant, dnaTilt, dnaTiltEvolve, dnaOddEven, dnaHarmonics, dnaNoiseTilt, dnaRough, dnaDensity,
        dnaAttack, dnaTransient, dnaRelease, dnaEvolve, dnaHold, transform, identity, transformOut,
        op3On, op3Ratio, op3Amount, op3Env, op3Target,
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
            ParamID::smpPlayMode, ParamID::smpBeats,
            ParamID::lfoOn, ParamID::lfo2On, ParamID::smpGrainAmt, ParamID::smpGrainType, ParamID::noiseColor,
            ParamID::osc1On, ParamID::osc2On, ParamID::subOn, ParamID::noiseOn, ParamID::uniOn, ParamID::f1On, ParamID::driveOn, ParamID::grainsOn, ParamID::analog,
            ParamID::rsSpeed, ParamID::rsHarm, ParamID::rsPartials, ParamID::rsNoise, ParamID::rsTone, ParamID::rsStretch, ParamID::rsPitch, ParamID::rsWidth,
            ParamID::macroBright, ParamID::macroBody, ParamID::macroMove, ParamID::macroSpace, ParamID::macroChaos,
            ParamID::fxEqOn, ParamID::fxEqLow, ParamID::fxEqMid, ParamID::fxEqMidFreq, ParamID::fxEqHigh, ParamID::fxCompOn, ParamID::fxCompAmt,
            ParamID::fxChorusOn, ParamID::fxChorusRate, ParamID::fxChorusDepth, ParamID::fxChorusMix,
            ParamID::fxDelayOn, ParamID::fxDelaySync, ParamID::fxDelayMs, ParamID::fxDelayFb, ParamID::fxDelayMix, ParamID::fxDelayPing, ParamID::fxDelayTone,
            ParamID::fxRevOn, ParamID::fxRevSize, ParamID::fxRevDecay, ParamID::fxRevDamp, ParamID::fxRevPre, ParamID::fxRevMix, ParamID::fxWidth,
            ParamID::quality, ParamID::mpeOn, ParamID::master,
            ParamID::dnaPitchDepth, ParamID::dnaFormant, ParamID::dnaTilt, ParamID::dnaTiltEvolve, ParamID::dnaOddEven, ParamID::dnaHarmonics,
            ParamID::dnaNoiseTilt, ParamID::dnaRough, ParamID::dnaDensity, ParamID::dnaAttack, ParamID::dnaTransient, ParamID::dnaRelease,
            ParamID::dnaEvolve, ParamID::dnaHold, ParamID::transform, ParamID::identity, ParamID::transformOut,
            ParamID::op3On, ParamID::op3Ratio, ParamID::op3Amount, ParamID::op3Env, ParamID::op3Target
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
