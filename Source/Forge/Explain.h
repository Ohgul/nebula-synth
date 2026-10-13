#pragma once
#include "PatchGraph.h"

// ===========================================================================
// Cómo está hecho este sonido (capa educativa, NEBULA-CORE-050)
//
// Cada línea sale de los valores reales del patch: qué módulo está activo, con qué valor y qué hace eso al
// sonido. No hay textos fijos por preset ni explicaciones inventadas: si el módulo no actúa, no aparece.
// ===========================================================================
namespace Forge
{
    struct ExplainLine { Module module; juce::String what, why; };

    inline juce::String hz (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
    inline juce::String ms (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s"; }
    inline juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    inline std::vector<ExplainLine> explain (const SynthPatch& p)
    {
        using namespace ParamID;
        std::vector<ExplainLine> out;
        const auto& waves = waveNames();
        auto waveWhy = [] (int w)
        {
            switch (w)
            {
                case Wave::Sine:     return juce::String ("seno: un solo armonico, sonido limpio y redondo");
                case Wave::Triangle: return juce::String ("triangulo: armonicos impares que caen rapido, suave");
                case Wave::Saw:      return juce::String ("sierra: todos los armonicos, brillante y rico, la base clasica para filtrar");
                case Wave::Pulse:    return juce::String ("pulso: armonicos impares, hueco; la forma cambia el ancho del pulso");
                case Wave::SuperSaw: return juce::String ("super sierra: varias sierras desafinadas, ancho y grueso");
                case Wave::Sync:     return juce::String ("sync: un oscilador reinicia a otro; agudos metalicos que cambian con la forma");
                default:             return juce::String ("tabla de onda: el ciclo sale de una tabla y la forma la recorre");
            }
        };
        if (moduleActive (p, Module::Osc1))
            out.push_back ({ Module::Osc1, "Oscilador 1: " + waves[p.geti (osc1Wave)] + (p.geti (osc1Octave) != 0 ? juce::String::formatted (", %+d octava", p.geti (osc1Octave)) : juce::String()),
                             waveWhy (p.geti (osc1Wave)) });
        if (moduleActive (p, Module::Osc2))
            out.push_back ({ Module::Osc2, juce::String::formatted ("Oscilador 2: %s %+d st %+.0f ct, mezcla ", waves[p.geti (osc2Wave)].toRawUTF8(), p.geti (osc2Semi), p.get (osc2Fine)) + pct (p.get (oscMix)),
                             std::abs (p.get (osc2Fine)) > 3.0f && p.geti (osc2Semi) == 0 ? "desafinado contra el osc 1: los dos laten y el sonido se ensancha"
                                 : p.geti (osc2Semi) != 0 ? "otra altura sumada: agrega un intervalo (quinta, octava...) al cuerpo" : "segunda capa de timbre sobre el osc 1" });
        if (moduleActive (p, Module::Fm))
        {
            const float ratio = std::exp2 ((p.get (osc2Semi) + p.get (osc2Fine) / 100.0f) / 12.0f);
            const bool harmonic = std::abs (ratio - std::round (ratio)) < 0.02f || std::abs (1.0f / ratio - std::round (1.0f / ratio)) < 0.02f;
            out.push_back ({ Module::Fm, "FM: " + pct (p.get (fmAmount)) + juce::String::formatted (", relacion %.2f", ratio),
                             harmonic ? "el osc 2 modula la fase del osc 1 en una relacion entera: armonicos nuevos, sigue afinado"
                                      : "relacion no entera: parciales inarmonicos, como campanas o metal" });
        }
        if (moduleActive (p, Module::Sub))
            out.push_back ({ Module::Sub, "Sub: " + pct (p.get (subLevel)), "una octava abajo del osc 1: peso en graves sin cambiar el brillo" });
        if (moduleActive (p, Module::Noise))
            out.push_back ({ Module::Noise, "Ruido: " + pct (p.get (noise)) + (std::abs (p.getMod (ModSrc::Env2, ModDst::Noise)) > 0.01f ? " + golpe con la envolvente 2" : juce::String()),
                             std::abs (p.getMod (ModSrc::Env2, ModDst::Noise)) > 0.01f ? "un soplo de ruido sólo en el ataque: el clic o la respiracion del inicio" : "aire o textura sostenida por debajo del tono" });
        if (moduleActive (p, Module::Unison))
            out.push_back ({ Module::Unison, juce::String (p.geti (uniVoices)) + " voces de unisono, desafinacion " + pct (p.get (uniDetune)), "copias desafinadas de cada oscilador: coro, ancho estereo" });
        if (moduleActive (p, Module::Filter1))
        {
            const auto& fn = filterNames();
            const int type = p.geti (filterType);
            const float cut = p.get (cutoff);
            juce::String why;
            if (cut >= 19000.0f) why = "abierto: deja pasar todo el brillo";
            else if (type == FilterKind::LowPass) why = "quita los agudos por encima de " + hz (cut) + ": cuanto mas bajo, mas oscuro";
            else if (type == FilterKind::HighPass) why = "quita los graves por debajo de " + hz (cut) + ": mas fino";
            else if (type == FilterKind::BandPass) why = "deja una banda alrededor de " + hz (cut) + ": sonido nasal o de radio";
            else why = "recorta una banda angosta en " + hz (cut);
            if (p.get (resonance) > 0.3f) why << "; resonancia " << pct (p.get (resonance)) << " realza justo el corte";
            out.push_back ({ Module::Filter1, "Filtro 1: " + fn[juce::jlimit (0, fn.size() - 1, type)] + " a " + hz (cut), why });
        }
        if (moduleActive (p, Module::Filter2))
            out.push_back ({ Module::Filter2, "Filtro 2 a " + hz (p.get (f2Cutoff)) + (p.geti (filterRouting) == 1 ? " (en paralelo)" : " (en serie)"),
                             p.geti (filterRouting) == 1 ? "dos filtros a la vez y se suman: dos colores mezclados" : "un segundo filtro despues del primero: recorte mas marcado" });
        if (moduleActive (p, Module::Drive))
            out.push_back ({ Module::Drive, "Drive: " + pct (p.get (drive)), "satura despues del filtro: mas armonicos, mas agresivo y comprimido" });
        {
            const float a = p.get (ampA), d = p.get (ampD), s = p.get (ampS), r = p.get (ampR);
            juce::String why = a < 0.01f ? "empieza de golpe" : a < 0.08f ? "entra rapido pero sin clic" : "entra lento, como un pad o un arco";
            why << (s < 0.05f ? "; no se sostiene: cae en " + ms (d) + " (percusivo)" : s > 0.95f ? "; se sostiene al maximo mientras tocas" : "; baja a " + pct (s) + " en " + ms (d));
            why << "; al soltar se apaga en " << ms (r);
            out.push_back ({ Module::Amp, "Volumen: A " + ms (a) + "  D " + ms (d) + "  S " + pct (s) + "  R " + ms (r), why });
        }
        if (std::abs (p.get (filterEnv)) > 0.01f)
        {
            const float amt = p.get (filterEnv);
            out.push_back ({ Module::Env2, juce::String::formatted ("Envolvente 2 -> filtro %+.0f %% (%.1f octavas)", amt * 100.0f, amt * 6.0f),
                             (amt > 0 ? juce::String ("el filtro se abre al atacar y se cierra en ") : juce::String ("el filtro se cierra al atacar y vuelve en ")) + ms (p.get (fltD))
                                 + ": el brillo cambia con el tiempo (lo tipico de un pluck o un bajo)" });
        }
        if (std::abs (p.getMod (ModSrc::Env2, ModDst::Pitch)) > 0.01f)
            out.push_back ({ Module::Env2, juce::String::formatted ("Envolvente 2 -> tono %+.1f st", p.getMod (ModSrc::Env2, ModDst::Pitch) * 12.0f),
                             "el tono arranca desplazado y llega a la nota en " + ms (p.get (fltD)) + ": el golpe de un 808 o un bombo" });
        if (p.geti (lfoOn) != 0 && p.get (lfoPitch) > 0.5f)
            out.push_back ({ Module::Lfo1, juce::String::formatted ("Vibrato: %.1f Hz, %.0f cents", p.get (lfoRate), p.get (lfoPitch)),
                             p.get (lfoFade) > 0.05f ? "el tono oscila y el vibrato entra de a poco (" + ms (p.get (lfoFade)) + ")" : "el tono oscila desde el principio" });
        if (p.geti (lfoOn) != 0 && std::abs (p.get (lfoFilter)) > 0.01f)
            out.push_back ({ Module::Lfo1, juce::String::formatted ("LFO 1 -> filtro, %.1f Hz", p.get (lfoRate)), "el brillo sube y baja ciclicamente: wobble o movimiento" });
        const char* dstName[] { "tono", "forma 1", "forma 2", "mezcla", "FM", "corte", "resonancia", "corte 2", "volumen", "paneo", "ruido" };
        const char* srcName[] { "Envolvente 2", "Envolvente 3", "LFO 1", "LFO 2", "Velocidad", "Rueda", "Aftertouch", "Tecla" };
        for (int s = 0; s < ModSrc::count; ++s)
            for (int d = 0; d < ModDst::count; ++d)
            {
                if (s == ModSrc::Env2 && (d == ModDst::Pitch || d == ModDst::Noise)) continue;   // ya explicadas arriba
                const float v = p.getMod (s, d);
                if (std::abs (v) < 0.01f) continue;
                out.push_back ({ moduleOfSource (s), juce::String (srcName[s]) + " -> " + dstName[d] + juce::String::formatted (" %+.0f %%", v * 100.0f),
                                 juce::String ("ruta de la matriz: ") + srcName[s] + " mueve " + dstName[d] + (v < 0 ? " al reves" : "") });
            }
        return out;
    }

    inline juce::String explainText (const SynthPatch& p)
    {
        juce::StringArray lines;
        for (auto& l : explain (p)) lines.add (l.what + "\n   " + l.why);
        return lines.joinIntoString ("\n");
    }
}
