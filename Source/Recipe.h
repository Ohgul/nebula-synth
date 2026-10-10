#pragma once
#include "SoundProfile.h"

// ===========================================================================
// La "receta": explica en palabras qué es un sonido y cómo está construido.
// Formato del texto: "# " título, "## " sección, "- " viñeta, "1. " paso.
// ===========================================================================
namespace Recipe
{
    using Value = std::function<float (const juce::String&)>;

    inline juce::String hz (float f)  { return ParamFormat::hertz (f, 0); }
    inline juce::String sec (float s) { return ParamFormat::time (s, 0); }
    inline juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    inline juce::String brightnessWord (float hzv)
    {
        return hzv < 600.0f ? "oscuro" : hzv < 1500.0f ? "calido" : hzv < 3500.0f ? "medio" : hzv < 7000.0f ? "brillante" : "muy brillante";
    }

    inline juce::String kindOf (const SoundProfile& p)
    {
        if (! p.pitched) return "Percusivo o ruido (no tiene una nota definida)";
        if (p.melodic)   return "Frase melodica (varias notas)";
        if (! p.sustained && p.attackS < 0.025f) return p.f0 < 130.0f ? "Bajo corto / pluck grave" : "Pluck o golpe corto: ataca rapido y se apaga solo";
        if (p.sustained && p.attackS > 0.15f)    return "Pad: entra lento y se sostiene";
        if (p.sustained) return p.f0 < 130.0f ? "Bajo sostenido" : "Sonido sostenido (lead, organo, cuerda, voz)";
        return "Nota que se apaga sola";
    }

    // Qué forma de onda clásica se parece más al reparto de armónicos
    inline int classicWave (const SoundProfile& p)
    {
        if (p.strongHarmonics <= 2) return p.harmonicsDb[2] > -26.0f && p.evenOddRatio < 0.3f ? Wave::Triangle : Wave::Sine;
        if (p.evenOddRatio < 0.25f) return Wave::Pulse;
        return Wave::Saw;
    }

    inline juce::String describeProfile (const SoundProfile& p, const juce::String& name)
    {
        juce::String t;
        t << "## Que es este sonido\n";
        if (name.isNotEmpty()) t << "- Audio: " << name << "\n";

        if (p.melodic)
            t << "- Tono: frase melodica en " << SoundProfiler::noteNames (p.keyTonic) << (p.keyMinor ? " menor" : " mayor")
              << ". Raiz: " << SoundProfiler::noteName (p.note) << ". Si tocas esa tecla suena como el original; las demas lo transportan.\n";
        else if (p.pitched)
            t << "- Tono: " << SoundProfiler::noteWithCents (p.note) << " (" << juce::String (p.f0, 1) << " Hz). "
              << "Nebula lo afina exacto: si tocas C# suena C#.\n";
        else
            t << "- Tono: no tiene una nota definida.\n";
        if (p.pitched && std::abs (p.tuningCents) >= 5.0f)
            t << "- El original estaba desafinado " << (p.tuningCents > 0 ? "+" : "") << juce::roundToInt (p.tuningCents) << " cents respecto a A440 (ya corregido).\n";

        t << "- Tipo: " << kindOf (p) << "\n";

        if (p.pitched)
        {
            juce::String timbre;
            if (p.strongHarmonics <= 2)         timbre = "casi puro, con muy pocos armonicos: suena como un seno";
            else if (p.evenOddRatio < 0.25f)    timbre = "dominan los armonicos impares: hueco, como una onda cuadrada o pulso (o un clarinete)";
            else if (p.slopeDb > -4.5f)         timbre = "muchos armonicos fuertes: muy brillante, mas que una sierra";
            else if (p.slopeDb > -8.0f)         timbre = "todos los armonicos, bajando parejo: como una sierra";
            else                                timbre = "armonicos que caen rapido: una sierra o cuadrada con el filtro pasa bajos bastante cerrado";
            t << "- Timbre: " << timbre << ".\n";
            t << "- Brillo: " << hz (p.brightnessHz) << " (" << brightnessWord (p.brightnessHz) << ")";
            if (p.brightnessEndHz < p.brightnessHz * 0.7f) t << "; se oscurece con el tiempo (un filtro que se cierra)";
            t << ".\n";
            t << "- Armonicos 1 a 8 (dB): ";
            for (int k = 0; k < 8; ++k) t << (k ? " / " : "") << juce::roundToInt (p.harmonicsDb[(size_t) k]);
            t << "\n";
        }
        if (p.inharmonic)
            t << "- Tiene parciales que no son armonicos (campana, metal, FM).\n";
        t << "- Ruido: " << pct (p.noiseRatio);
        if (p.noiseRatio > 0.03f)
        {
            if (p.noiseAttackRatio > p.noiseRatio * 1.8f) t << ", sobre todo en el ataque (soplido, golpe o pua)";
            t << "; color " << brightnessWord (p.noiseBrightnessHz);
        }
        t << ".\n";

        t << "- Envolvente: ataque " << sec (p.attackS);
        if (p.sustained) t << ", baja en " << sec (p.decayS) << " y se sostiene al " << pct (p.sustainLevel) << ", suelta " << sec (p.releaseS);
        else             t << ", se apaga solo en unos " << sec (p.decayS);
        t << ".\n";
        if (p.vibratoHz > 0.0f)
            t << "- Vibrato: " << juce::String (p.vibratoHz, 1) << " Hz, +/-" << juce::roundToInt (p.vibratoCents) << " cents.\n";
        t << "- Duracion analizada: " << sec (p.durationS) << ".\n";
        return t;
    }

    // Un patch de sinte "clásico" que se acerca (sirve de punto de partida para la imitación)
    inline std::vector<std::pair<juce::String, float>> classicGuess (const SoundProfile& p)
    {
        std::vector<std::pair<juce::String, float>> v;
        const int w = classicWave (p);
        v.push_back ({ ParamID::engine, 0.0f });
        v.push_back ({ ParamID::osc1Wave, (float) w });
        v.push_back ({ ParamID::osc1Shape, w == Wave::Pulse ? 0.5f : 0.3f });
        v.push_back ({ ParamID::osc1Octave, 0.0f });
        v.push_back ({ ParamID::oscMix, 0.0f });
        v.push_back ({ ParamID::fmAmount, p.inharmonic ? 0.35f : 0.0f });
        v.push_back ({ ParamID::osc2Semi, p.inharmonic ? 19.0f : 0.0f });
        v.push_back ({ ParamID::subLevel, 0.0f });
        v.push_back ({ ParamID::uniVoices, 1.0f });
        v.push_back ({ ParamID::noise, juce::jlimit (0.0f, 0.5f, p.noiseRatio * 0.6f) });
        v.push_back ({ ParamID::filterType, (float) FilterKind::LowPass });
        v.push_back ({ ParamID::filterSlope, 0.0f });
        v.push_back ({ ParamID::filterChar, 0.0f });
        v.push_back ({ ParamID::resonance, 0.05f });
        v.push_back ({ ParamID::f2On, 0.0f });
        const bool closes = p.brightnessEndHz < p.brightnessHz * 0.7f;
        const float baseCut = juce::jlimit (150.0f, 20000.0f, (closes ? p.brightnessEndHz : p.brightnessHz) * 2.2f);
        v.push_back ({ ParamID::cutoff, w == Wave::Sine ? 20000.0f : baseCut });
        const float envOct = closes ? juce::jlimit (0.0f, 1.0f, std::log2 (p.brightnessHz / juce::jmax (50.0f, p.brightnessEndHz)) / 6.0f) : 0.0f;
        v.push_back ({ ParamID::filterEnv, envOct });
        v.push_back ({ ParamID::fltA, juce::jmax (0.002f, p.attackS) });
        v.push_back ({ ParamID::fltD, p.decayS });
        v.push_back ({ ParamID::fltS, 0.0f });
        v.push_back ({ ParamID::ampA, juce::jmax (0.002f, p.attackS) });
        v.push_back ({ ParamID::ampD, p.decayS });
        v.push_back ({ ParamID::ampS, p.sustained ? p.sustainLevel : 0.0f });
        v.push_back ({ ParamID::ampR, p.releaseS });
        v.push_back ({ ParamID::lfoPitch, p.vibratoCents });
        v.push_back ({ ParamID::lfoRate, p.vibratoHz > 0.0f ? p.vibratoHz : 5.0f });
        v.push_back ({ ParamID::lfoShape, (float) LfoShape::Sine });
        v.push_back ({ ParamID::lfoFilter, 0.0f });
        return v;
    }

    inline juce::String describeClassic (const SoundProfile& p)
    {
        juce::String t;
        t << "## Como hacerlo en cualquier sinte\n";
        const int w = classicWave (p);
        int step = 1;
        t << step++ << ". Oscilador: " << waveNames()[w];
        if (w == Wave::Pulse) t << " al 50 % (cuadrada)";
        t << ", afinado a la nota.\n";
        if (p.inharmonic)
            t << step++ << ". Para los parciales de campana: FM con un modulador en proporcion no entera (por ejemplo 3.5 : 1) y una envolvente que decae rapido.\n";
        const bool closes = p.brightnessEndHz < p.brightnessHz * 0.7f;
        if (w != Wave::Sine)
        {
            t << step++ << ". Filtro pasa bajos 12 dB en " << hz (juce::jlimit (150.0f, 20000.0f, (closes ? p.brightnessEndHz : p.brightnessHz) * 2.2f))
              << ", resonancia baja";
            if (closes) t << "; la envolvente del filtro lo abre unas " << juce::String (std::log2 (p.brightnessHz / juce::jmax (50.0f, p.brightnessEndHz)), 1)
                          << " octavas y lo cierra en " << sec (p.decayS);
            t << ".\n";
        }
        t << step++ << ". Envolvente de volumen: ataque " << sec (p.attackS) << ", decaimiento " << sec (p.decayS)
          << ", sostenido " << pct (p.sustained ? p.sustainLevel : 0.0f) << ", suelta " << sec (p.releaseS) << ".\n";
        if (p.noiseRatio > 0.03f)
            t << step++ << ". Ruido " << brightnessWord (p.noiseBrightnessHz) << " al " << pct (juce::jmin (0.5f, p.noiseRatio * 0.6f))
              << (p.noiseAttackRatio > p.noiseRatio * 1.8f ? ", con su propia envolvente corta (solo en el ataque).\n" : ".\n");
        if (p.vibratoHz > 0.0f)
            t << step++ << ". LFO seno a " << juce::String (p.vibratoHz, 1) << " Hz hacia el tono, +/-" << juce::roundToInt (p.vibratoCents) << " cents (vibrato).\n";
        return t;
    }

    // ---- El patch actual de Nebula, en palabras ----
    inline juce::String describePatch (const Value& v, bool userTableFromAudio, const juce::String& tableName)
    {
        auto i = [&] (const char* id) { return juce::roundToInt (v (id)); };
        juce::String t;
        t << "## Como esta hecho en Nebula\n";
        const bool sampler = i (ParamID::engine) == 1;
        int step = 1;
        if (sampler)
        {
            t << step++ << ". Motor Sample: toca tu audio";
            if (i (ParamID::smpPlayMode) == 1) t << " en modo Tempo (" << beatNames()[i (ParamID::smpBeats)] << ")";
            if (i (ParamID::smpReverse)) t << ", al reves";
            if (i (ParamID::smpLoop)) t << ", en loop";
            t << ".\n";
            if (v (ParamID::smpTexture) > 0.01f) t << step++ << ". Textura granular " << pct (v (ParamID::smpTexture)) << " (granos de " << sec (v (ParamID::smpGrain)) << ").\n";
            if (v (ParamID::smpGrainAmt) > 0.01f) t << step++ << ". Capa de granos " << grainTypeNames()[i (ParamID::smpGrainType)] << " al " << pct (v (ParamID::smpGrainAmt)) << ".\n";
        }
        else
        {
            auto osc = [&] (int n, const char* waveId, const char* tableId, const char* shapeId)
            {
                const int w = i (waveId);
                juce::String s = "Oscilador " + juce::String (n) + ": " + waveNames()[w];
                if (w == Wave::Table)
                {
                    const int tb = i (tableId);
                    s << " (tabla " << (tb == 4 && userTableFromAudio ? "Usuario: " + tableName : tableNames()[tb]) << ")";
                }
                if (w == Wave::Pulse)    s << ", ancho " << pct (v (shapeId));
                if (w == Wave::SuperSaw) s << ", desafinacion " << pct (v (shapeId));
                if (w == Wave::Sync)     s << ", sync " << pct (v (shapeId));
                if (w == Wave::Table)    s << ", posicion " << pct (v (shapeId));
                return s;
            };
            t << step++ << ". " << osc (1, ParamID::osc1Wave, ParamID::osc1Table, ParamID::osc1Shape);
            if (i (ParamID::osc1Octave) != 0) t << ", octava " << i (ParamID::osc1Octave);
            t << ".\n";
            if (v (ParamID::oscMix) > 0.02f)
                t << step++ << ". " << osc (2, ParamID::osc2Wave, ParamID::osc2Table, ParamID::osc2Shape)
                  << ", " << (i (ParamID::osc2Semi) >= 0 ? "+" : "") << i (ParamID::osc2Semi) << " semitonos"
                  << ", mezcla " << pct (v (ParamID::oscMix)) << ".\n";
            if (v (ParamID::fmAmount) > 0.02f) t << step++ << ". FM del osc 2 al osc 1: " << pct (v (ParamID::fmAmount)) << ".\n";
            if (i (ParamID::uniVoices) > 1)
                t << step++ << ". Unisono de " << i (ParamID::uniVoices) << " voces, desafinadas " << pct (v (ParamID::uniDetune))
                  << ", ancho " << pct (v (ParamID::uniWidth)) << ".\n";
        }
        if (v (ParamID::subLevel) > 0.02f) t << step++ << ". Sub una octava abajo: " << pct (v (ParamID::subLevel)) << ".\n";
        if (v (ParamID::noise) > 0.01f)
            t << step++ << ". Ruido " << pct (v (ParamID::noise)) << " (color " << (v (ParamID::noiseColor) > 0.995f ? juce::String ("blanco") : hz (200.0f * std::pow (100.0f, v (ParamID::noiseColor)))) << ").\n";

        // Filtros
        auto filt = [&] (const char* type, const char* slope, const char* ch, const char* cut, const char* res)
        {
            juce::String s = filterNames()[i (type)] + " " + slopeNames()[i (slope)] + " " + charNames()[i (ch)]
                           + ", corte " + hz (v (cut)) + ", resonancia " + pct (v (res));
            return s;
        };
        const bool f1Open = i (ParamID::filterType) == 0 && v (ParamID::cutoff) > 17500.0f && v (ParamID::resonance) < 0.05f
                            && std::abs (v (ParamID::filterEnv)) < 0.01f;
        if (f1Open) t << step++ << ". Filtro abierto: no colorea el sonido.\n";
        else
        {
            t << step++ << ". Filtro 1: " << filt (ParamID::filterType, ParamID::filterSlope, ParamID::filterChar, ParamID::cutoff, ParamID::resonance);
            if (std::abs (v (ParamID::filterEnv)) > 0.01f) t << "; la Env 2 lo mueve " << (v (ParamID::filterEnv) > 0 ? "+" : "") << pct (v (ParamID::filterEnv));
            t << ".\n";
        }
        if (v (ParamID::drive) > 0.02f) t << step++ << ". Saturacion (drive) " << pct (v (ParamID::drive)) << ".\n";
        if (i (ParamID::f2On))
            t << step++ << ". Filtro 2 en " << routingNames()[i (ParamID::filterRouting)] << ": "
              << filt (ParamID::f2Type, ParamID::f2Slope, ParamID::f2Char, ParamID::f2Cutoff, ParamID::f2Reso) << ".\n";

        t << step++ << ". Volumen (Env 1): ataque " << sec (v (ParamID::ampA)) << ", decae " << sec (v (ParamID::ampD))
          << ", sostiene " << pct (v (ParamID::ampS)) << ", suelta " << sec (v (ParamID::ampR)) << ".\n";

        // Modulaciones
        bool env2Used = std::abs (v (ParamID::filterEnv)) > 0.01f || (i (ParamID::f2On) && std::abs (v (ParamID::f2Env)) > 0.01f);
        bool env3Used = false, lfo1Used = v (ParamID::lfoPitch) > 0.5f || v (ParamID::lfoFilter) > 0.01f, lfo2Used = false;
        juce::StringArray cells;
        for (int s = 0; s < ModSrc::count; ++s)
            for (int d = 0; d < ModDst::count; ++d)
            {
                const float a = v (matrixID (s, d));
                if (std::abs (a) < 0.01f) continue;
                cells.add (modSrcNames()[s] + " -> " + modDstNames()[d] + " " + (a > 0 ? "+" : "") + pct (a));
                if (s == ModSrc::Env2) env2Used = true;
                if (s == ModSrc::Env3) env3Used = true;
                if (s == ModSrc::Lfo1) lfo1Used = true;
                if (s == ModSrc::Lfo2) lfo2Used = true;
            }
        if (env2Used)
            t << step++ << ". Env 2: ataque " << sec (v (ParamID::fltA)) << ", decae " << sec (v (ParamID::fltD)) << ", sostiene " << pct (v (ParamID::fltS))
              << (i (ParamID::fltMode) ? " (en loop)" : "") << ".\n";
        if (env3Used)
            t << step++ << ". Env 3: ataque " << sec (v (ParamID::env3A)) << ", decae " << sec (v (ParamID::env3D)) << ", sostiene " << pct (v (ParamID::env3S))
              << (i (ParamID::env3Mode) ? " (en loop)" : "") << ".\n";
        if (lfo1Used)
        {
            if (i (ParamID::lfoOn) == 0) t << step++ << ". LFO 1 apagado.\n";
            else
            {
                t << step++ << ". LFO 1 " << lfoShapeNames()[i (ParamID::lfoShape)] << " a "
                  << (i (ParamID::lfoSync) > 0 ? syncNames()[i (ParamID::lfoSync)] : juce::String (v (ParamID::lfoRate), 2) + " Hz");
                if (v (ParamID::lfoPitch) > 0.5f) t << ", vibrato " << juce::roundToInt (v (ParamID::lfoPitch)) << " cents";
                if (v (ParamID::lfoFilter) > 0.01f) t << ", al filtro " << pct (v (ParamID::lfoFilter));
                t << ".\n";
            }
        }
        if (lfo2Used)
            t << step++ << ". LFO 2 " << (i (ParamID::lfo2On) ? lfoShapeNames()[i (ParamID::lfo2Shape)] + " a "
                                         + (i (ParamID::lfo2Sync) > 0 ? syncNames()[i (ParamID::lfo2Sync)] : juce::String (v (ParamID::lfo2Rate), 2) + " Hz")
                                         : juce::String ("apagado")) << ".\n";
        if (! cells.isEmpty())
            t << step++ << ". Matriz: " << cells.joinIntoString (", ") << ".\n";
        return t;
    }
}
