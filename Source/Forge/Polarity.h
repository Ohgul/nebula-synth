#pragma once
#include "PatchGraph.h"

// ===========================================================================
// FORGE · Cosmos / Abismo como operaciones sobre el genoma (P6)
//
// A diferencia de la transformación por voz del motor (que procesa el audio), esto mueve la receta: el
// resultado es otro SynthPatch nativo, editable perilla por perilla, que suena sin procesos extra.
// Mismo carácter que la transformación del motor (Transform.h), pero escrito en la receta:
//   Cosmos (+):  expansión: ataque y cola más largos, filtro más abierto, capa fantasma una octava arriba,
//                unísono más ancho, vibrato lento que entra de a poco, deriva analógica; menos drive y ruido.
//   Abismo (-):  caos: drive, resonancia, FM inarmónica (tritono), ruido, caos de afinación por pasos
//                (LFO 2 aleatorio al tono) y decaimiento más corto.
// Reversible por construcción: el resultado es una función pura de (base, cantidad, candados); con cantidad 0
// devuelve la base exacta y volver a la base no depende de cuántas veces se movió el control (PolarityEdit
// guarda la base). Los módulos con candado no se tocan.
// ===========================================================================
namespace Forge
{
    inline SynthPatch polarize (const SynthPatch& base, float amount, const ModuleLocks& locks = {})
    {
        using namespace ParamID;
        amount = juce::jlimit (-1.0f, 1.0f, amount);
        if (std::abs (amount) < 1.0e-6f) return base;
        auto p = base;
        auto free = [&] (const char* id) { return ! locks.paramLocked (id); };
        auto scale = [&] (const char* id, float factor) { if (free (id)) p.set (id, base.get (id) * factor); };
        auto add = [&] (const char* id, float delta) { if (free (id)) p.set (id, base.get (id) + delta); };
        auto towards = [&] (const char* id, float target, float t) { if (free (id)) p.set (id, base.get (id) + (target - base.get (id)) * t); };

        const bool fmFree = base.get (fmAmount) < 0.01f && ! modulatedBySome (base, ModDst::Fm) && free (fmAmount);
        const bool osc2Silent = ! moduleActive (base, Module::Osc2) && free (osc2On) && free (osc2Semi);
        if (amount > 0.0f)
        {
            const float a = amount;
            scale (ampA, 1.0f + 6.0f * a * a);           // ataque que se abre
            add (ampA, 0.08f * a * a);
            scale (ampR, 1.0f + 2.5f * a);               // cola más larga
            scale (cutoff, std::exp2 (0.8f * a));
            towards (resonance, 0.0f, 0.4f * a);
            towards (drive, 0.0f, a);
            towards (noise, 0.0f, 0.5f * a);
            add (analog, 0.35f * a);                    // deriva lenta de afinación
            if (free (uniOn) && base.geti (uniOn) != 0) { add (uniDetune, 0.25f * a); add (uniWidth, 0.5f * a); }
            // capa fantasma una octava arriba (sólo si el osc 2 no suena y no hace FM: no cambia la FM existente)
            if (osc2Silent && fmFree && free (oscMix))
            {
                p.set (osc2On, 1.0f); p.set (osc2Wave, base.get (osc1Wave)); p.set (osc2Semi, 12.0f); p.set (osc2Fine, 7.0f);
                p.set (oscMix, 0.25f * a);
            }
            else if (free (osc2Fine) && moduleActive (base, Module::Osc2))
                add (osc2Fine, (base.get (osc2Fine) >= 0 ? 1.0f : -1.0f) * 10.0f * a);
            if (free (lfoPitch) && free (lfoRate) && free (lfoOn))
            {
                if (base.get (lfoPitch) < 0.5f) { p.set (lfoOn, 1.0f); p.set (lfoRate, 4.8f); p.set (lfoFade, 0.4f); }
                add (lfoPitch, 12.0f * a);               // vibrato lento que entra de a poco
            }
        }
        else
        {
            const float a = -amount;
            if (free (driveOn) && free (drive)) { if (base.geti (driveOn) == 0) p.set (drive, 0.0f); p.set (driveOn, 1.0f); add (drive, 0.6f * a); }
            add (resonance, 0.3f * a);
            // FM inarmónica: el modulador a un tritono y unos cents (sólo si no había FM: no se pisa una existente)
            if (fmFree)
            {
                p.set (fmAmount, 0.35f * a);
                if (osc2Silent) { p.set (osc2Semi, 6.0f); p.set (osc2Fine, 17.0f); }
            }
            if (free (noiseOn) && free (noise)) { if (base.geti (noiseOn) == 0) p.set (noise, 0.0f); p.set (noiseOn, 1.0f); add (noise, 0.12f * a); }
            // caos de afinación por pasos (LFO 2 aleatorio al tono), a partir de un tercio
            if (a > 0.3f && free (lfo2On) && free (lfo2Rate) && free (lfo2Shape) && base.geti (lfo2On) == 0 && ! modulates (base, ModSrc::Lfo2))
            {
                p.set (lfo2On, 1.0f); p.set (lfo2Shape, (float) LfoShape::Random); p.set (lfo2Rate, 7.0f);
                p.setMod (ModSrc::Lfo2, ModDst::Pitch, 0.04f * (a - 0.3f) / 0.7f);
            }
            scale (ampD, 1.0f - 0.3f * a);
            towards (lfoPitch, 0.0f, a);
        }
        p.name = base.name + (amount > 0 ? " Cosmos " : " Abismo ") + juce::String (juce::roundToInt (std::abs (amount) * 100)) + "%";
        return p;
    }

    // Edición reversible: la base queda guardada; aplicar cualquier cantidad y volver a 0 devuelve la base exacta
    struct PolarityEdit
    {
        SynthPatch base;
        float amount = 0.0f;
        ModuleLocks locks;
        bool active = false;

        void begin (const SynthPatch& b) { base = b; amount = 0.0f; active = true; }
        SynthPatch result() const { return polarize (base, amount, locks); }
        SynthPatch revert() { amount = 0.0f; return base; }

        juce::ValueTree toValueTree() const
        {
            juce::ValueTree t ("PolarityEdit");
            t.setProperty ("amount", amount, nullptr);
            t.setProperty ("active", active, nullptr);
            t.setProperty ("locks", locks.toString(), nullptr);
            t.appendChild (base.toValueTree(), nullptr);
            return t;
        }
        static PolarityEdit fromValueTree (const juce::ValueTree& t)
        {
            PolarityEdit e;
            if (! t.hasType ("PolarityEdit")) return e;
            e.amount = (float) t.getProperty ("amount", 0.0f);
            e.active = (bool) t.getProperty ("active", false);
            e.locks = ModuleLocks::fromString (t.getProperty ("locks").toString());
            e.base = SynthPatch::fromValueTree (t.getChildWithName ("SynthPatch"));
            return e;
        }
    };
}
