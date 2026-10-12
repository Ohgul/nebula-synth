#pragma once
#include "SynthPatch.h"
#include <array>
#include <set>

// ===========================================================================
// FORGE · ADN estructural de un patch (P4)
//
// Un SynthPatch visto como grafo: módulos (osciladores, sub, ruido, FM, unísono, filtros, drive, amplificador,
// envolventes, LFOs, voz) con sus parámetros ("bindings"), conexiones de audio y rutas de modulación con su
// cantidad. Cada parámetro de síntesis pertenece a exactamente un módulo, así que sobre el grafo se puede:
//   - bloquear módulos (los candados se respetan al mutar y al cruzar),
//   - mutar (perillas de los módulos libres y, con baja probabilidad, encender/apagar un módulo opcional),
//   - cruzar dos patches por módulos completos (un módulo viene entero de un padre: no se mezclan perillas
//     sueltas que sólo tienen sentido juntas).
// Todo es determinista con la semilla.
// ===========================================================================
namespace Forge
{
    enum class Module { Osc1, Osc2, Fm, Sub, Noise, Unison, Filter1, Filter2, Drive, Amp, Env2, Env3, Lfo1, Lfo2, Voice, count };
    static constexpr int moduleCount = (int) Module::count;

    struct ModuleInfo { Module m; const char* key; const char* name; bool optional; std::vector<const char*> params; };

    inline const std::vector<ModuleInfo>& moduleTable()
    {
        using namespace ParamID;
        static const std::vector<ModuleInfo> t {
            { Module::Osc1,    "osc1",  "Oscilador 1",  false, { osc1On, osc1Wave, osc1Shape, osc1Table, osc1Octave } },
            { Module::Osc2,    "osc2",  "Oscilador 2",  true,  { osc2On, osc2Wave, osc2Shape, osc2Table, osc2Semi, osc2Fine, oscMix } },
            { Module::Fm,      "fm",    "FM",           true,  { fmAmount } },
            { Module::Sub,     "sub",   "Sub",          true,  { subOn, subLevel } },
            { Module::Noise,   "noise", "Ruido",        true,  { noiseOn, noise, noiseColor } },
            { Module::Unison,  "uni",   "Unisono",      true,  { uniOn, uniVoices, uniDetune, uniWidth, analog } },
            { Module::Filter1, "f1",    "Filtro 1",     false, { f1On, filterType, filterSlope, filterChar, cutoff, resonance, filterEnv, filterRouting } },
            { Module::Filter2, "f2",    "Filtro 2",     true,  { f2On, f2Type, f2Slope, f2Char, f2Cutoff, f2Reso, f2Env } },
            { Module::Drive,   "drive", "Drive",        true,  { driveOn, drive } },
            { Module::Amp,     "amp",   "Amplificador", false, { ampA, ampD, ampS, ampR, ampCurve } },
            { Module::Env2,    "env2",  "Envolvente 2", false, { fltA, fltD, fltS, fltR, fltCurve, fltMode } },
            { Module::Env3,    "env3",  "Envolvente 3", false, { env3A, env3D, env3S, env3R, env3Curve, env3Mode } },
            { Module::Lfo1,    "lfo1",  "LFO 1",        false, { lfoOn, lfoRate, lfoFilter, lfoPitch, lfoShape, lfoSync, lfoRetrig, lfoFade } },
            { Module::Lfo2,    "lfo2",  "LFO 2",        false, { lfo2On, lfo2Rate, lfo2Shape, lfo2Sync, lfo2Retrig, lfo2Fade } },
            { Module::Voice,   "voice", "Voz",          false, { polyMode, voiceCount, glide, bendRange, velSens, modTime, modAmount } },
        };
        return t;
    }

    inline const ModuleInfo& infoFor (Module m) { return moduleTable()[(size_t) m]; }

    // Fuente de la matriz -> módulo dueño de la ruta (las rutas viajan con su fuente)
    inline Module moduleOfSource (int src)
    {
        switch (src)
        {
            case ModSrc::Env2: return Module::Env2;
            case ModSrc::Env3: return Module::Env3;
            case ModSrc::Lfo1: return Module::Lfo1;
            case ModSrc::Lfo2: return Module::Lfo2;
            default:           return Module::Voice;   // velocidad, rueda, aftertouch, seguimiento de tecla
        }
    }

    // Módulo de un parámetro de síntesis; -1 si no es de la receta
    inline int moduleOf (const juce::String& id)
    {
        static const std::map<juce::String, int> table = []
        {
            std::map<juce::String, int> t;
            for (auto& mi : moduleTable()) for (auto* p : mi.params) t[p] = (int) mi.m;
            for (int s = 0; s < ModSrc::count; ++s)
                for (int d = 0; d < ModDst::count; ++d) t[matrixID (s, d)] = (int) moduleOfSource (s);
            return t;
        }();
        auto it = table.find (id);
        return it != table.end() ? it->second : -1;
    }

    inline bool modulates (const SynthPatch& p, int src, float thr = 0.01f)
    {
        for (int d = 0; d < ModDst::count; ++d) if (std::abs (p.getMod (src, d)) > thr) return true;
        return false;
    }
    inline bool modulatedBySome (const SynthPatch& p, int dst, float thr = 0.01f)
    {
        for (int s = 0; s < ModSrc::count; ++s) if (std::abs (p.getMod (s, dst)) > thr) return true;
        return false;
    }

    // ¿El módulo suena o actúa en este patch?
    inline bool moduleActive (const SynthPatch& p, Module m)
    {
        using namespace ParamID;
        switch (m)
        {
            case Module::Osc1:    return p.geti (osc1On) != 0;
            case Module::Osc2:    return p.geti (osc2On) != 0 && p.get (oscMix) > 0.01f;
            case Module::Fm:      return p.get (fmAmount) > 0.01f || modulatedBySome (p, ModDst::Fm);
            case Module::Sub:     return p.geti (subOn) != 0 && p.get (subLevel) > 0.01f;
            case Module::Noise:   return p.geti (noiseOn) != 0 && (p.get (noise) > 0.01f || modulatedBySome (p, ModDst::Noise));
            case Module::Unison:  return p.geti (uniOn) != 0 && p.geti (uniVoices) > 1;
            case Module::Filter1: return p.geti (f1On) != 0;
            case Module::Filter2: return p.geti (f2On) != 0;
            case Module::Drive:   return p.geti (driveOn) != 0 && p.get (drive) > 0.01f;
            case Module::Amp:     return true;
            case Module::Env2:    return std::abs (p.get (filterEnv)) > 0.01f || (p.geti (f2On) != 0 && std::abs (p.get (f2Env)) > 0.01f) || modulates (p, ModSrc::Env2);
            case Module::Env3:    return modulates (p, ModSrc::Env3);
            case Module::Lfo1:    return p.geti (lfoOn) != 0 && (p.get (lfoPitch) > 0.5f || std::abs (p.get (lfoFilter)) > 0.01f || modulates (p, ModSrc::Lfo1));
            case Module::Lfo2:    return p.geti (lfo2On) != 0 && modulates (p, ModSrc::Lfo2);
            case Module::Voice:   return true;
            case Module::count:   break;
        }
        return false;
    }

    // ---------- el grafo ----------
    struct GraphNode { Module module; juce::String key, name; bool active = false; juce::StringArray params; };
    struct GraphEdge
    {
        juce::String from, to;      // claves de nodo ("out" = salida)
        bool modulation = false;    // false = audio
        float amount = 1.0f;        // cantidad de la ruta (modulación) o 1 (audio)
        juce::String param;         // parámetro que la controla ("binding"), vacío si es fija
    };

    struct PatchGraph
    {
        std::vector<GraphNode> nodes;
        std::vector<GraphEdge> edges;

        static PatchGraph from (const SynthPatch& p)
        {
            using namespace ParamID;
            PatchGraph g;
            for (auto& mi : moduleTable())
            {
                GraphNode n { mi.m, mi.key, mi.name, moduleActive (p, mi.m), {} };
                for (auto* id : mi.params) n.params.add (id);
                if (mi.m == Module::Env2 || mi.m == Module::Env3 || mi.m == Module::Lfo1 || mi.m == Module::Lfo2 || mi.m == Module::Voice)
                    for (int s = 0; s < ModSrc::count; ++s)
                        if (moduleOfSource (s) == mi.m)
                            for (int d = 0; d < ModDst::count; ++d) n.params.add (matrixID (s, d));
                g.nodes.push_back (std::move (n));
            }
            auto on = [&] (Module m) { return g.nodes[(size_t) m].active; };
            // audio: fuentes -> filtros -> drive -> amplificador -> salida
            const bool parallel = p.geti (filterRouting) == 1 && on (Module::Filter2);
            const juce::String into = on (Module::Filter1) ? "f1" : (on (Module::Filter2) ? "f2" : (on (Module::Drive) ? "drive" : "amp"));
            struct Src { Module m; const char* key; const char* binding; };
            const Src sources[] { { Module::Osc1, "osc1", "" }, { Module::Osc2, "osc2", oscMix }, { Module::Sub, "sub", subLevel }, { Module::Noise, "noise", noise } };
            for (auto& s : sources)
            {
                if (! on (s.m)) continue;
                const juce::String binding (s.binding);
                g.edges.push_back ({ s.key, into, false, binding.isNotEmpty() ? p.get (binding) : 1.0f, binding });
                if (parallel && on (Module::Filter1)) g.edges.push_back ({ s.key, "f2", false, 1.0f, filterRouting });
            }
            if (on (Module::Fm)) g.edges.push_back ({ "osc2", "osc1", true, p.get (fmAmount), fmAmount });   // FM: el osc 2 modula la fase del osc 1
            if (on (Module::Unison)) g.edges.push_back ({ "uni", "osc1", true, p.get (uniDetune), uniDetune });
            const juce::String afterFilters = on (Module::Drive) ? "drive" : "amp";
            if (on (Module::Filter1)) g.edges.push_back ({ "f1", on (Module::Filter2) && ! parallel ? "f2" : afterFilters, false, 1.0f, {} });
            if (on (Module::Filter2)) g.edges.push_back ({ "f2", afterFilters, false, 1.0f, {} });
            if (on (Module::Drive)) g.edges.push_back ({ "drive", "amp", false, p.get (drive), drive });
            g.edges.push_back ({ "amp", "out", false, 1.0f, {} });
            // modulación fija del panel
            if (std::abs (p.get (filterEnv)) > 0.01f) g.edges.push_back ({ "env2", "f1", true, p.get (filterEnv), filterEnv });
            if (on (Module::Filter2) && std::abs (p.get (f2Env)) > 0.01f) g.edges.push_back ({ "env2", "f2", true, p.get (f2Env), f2Env });
            if (p.geti (lfoOn) != 0 && p.get (lfoPitch) > 0.5f) g.edges.push_back ({ "lfo1", "osc1", true, p.get (lfoPitch), lfoPitch });
            if (p.geti (lfoOn) != 0 && std::abs (p.get (lfoFilter)) > 0.01f) g.edges.push_back ({ "lfo1", "f1", true, p.get (lfoFilter), lfoFilter });
            // matriz
            static const char* dstKey[] { "osc1", "osc1", "osc2", "osc2", "osc1", "f1", "f1", "f2", "amp", "amp", "noise" };   // FM -> el portador (osc 1)
            static const char* srcKey[] { "env2", "env3", "lfo1", "lfo2", "voice", "voice", "voice", "voice" };
            for (int s = 0; s < ModSrc::count; ++s)
                for (int d = 0; d < ModDst::count; ++d)
                    if (std::abs (p.getMod (s, d)) > 0.01f)
                        g.edges.push_back ({ srcKey[s], dstKey[d], true, p.getMod (s, d), matrixID (s, d) });
            return g;
        }

        int activeNodes() const { int n = 0; for (auto& x : nodes) n += x.active ? 1 : 0; return n; }

        juce::String toText() const
        {
            juce::StringArray lines;
            for (auto& n : nodes) if (n.active) lines.add ("[" + n.name + "]");
            for (auto& e : edges)
                lines.add (e.from + (e.modulation ? " ~> " : " -> ") + e.to + (e.param.isNotEmpty() ? " (" + e.param + " " + juce::String (e.amount, 2) + ")" : juce::String()));
            return lines.joinIntoString ("\n");
        }
    };

    // ---------- candados ----------
    struct ModuleLocks
    {
        std::array<bool, (size_t) moduleCount> locked {};
        bool isLocked (Module m) const { return locked[(size_t) m]; }
        void set (Module m, bool l) { locked[(size_t) m] = l; }
        bool paramLocked (const juce::String& id) const { const int m = moduleOf (id); return m >= 0 && locked[(size_t) m]; }
        int count() const { int n = 0; for (bool b : locked) n += b ? 1 : 0; return n; }
        juce::String toString() const { juce::StringArray s; for (auto& mi : moduleTable()) if (isLocked (mi.m)) s.add (mi.key); return s.joinIntoString (","); }
        static ModuleLocks fromString (const juce::String& t)
        {
            ModuleLocks l; juce::StringArray s; s.addTokens (t, ",", {});
            for (auto& mi : moduleTable()) if (s.contains (mi.key)) l.set (mi.m, true);
            return l;
        }
    };

    // ---------- cruce por módulos ----------
    // Cada módulo (con sus rutas de modulación si es una fuente) se toma entero de A o de B. Los módulos con
    // candado vienen siempre de A. pB = probabilidad de tomar un módulo libre de B.
    inline SynthPatch crossover (const SynthPatch& a, const SynthPatch& b, juce::Random& rng, const ModuleLocks& locks = {}, float pB = 0.5f)
    {
        SynthPatch child = a;
        std::array<bool, (size_t) moduleCount> fromB {};
        for (auto& mi : moduleTable()) fromB[(size_t) mi.m] = ! locks.isLocked (mi.m) && rng.nextFloat() < pB;
        for (auto& info : synthParams())
        {
            const int m = moduleOf (info.id);
            if (m >= 0 && fromB[(size_t) m]) child.set (info.id, b.get (info.id));
        }
        child.origin = "cruce";
        child.architecture = (a.architecture.isNotEmpty() ? a.architecture : a.name) + " x " + (b.architecture.isNotEmpty() ? b.architecture : b.name);
        return child;
    }

    // ---------- mutación sobre el grafo ----------
    // amount: tamaño del paso en el espacio normalizado (0.1 = suave, 0.4 = fuerte). structuralRate: probabilidad
    // de encender o apagar un módulo opcional libre (cambia la estructura, no sólo las perillas).
    inline SynthPatch mutateGenome (const SynthPatch& base, juce::Random& rng, float amount, const ModuleLocks& locks = {}, float structuralRate = 0.0f)
    {
        auto p = base;
        for (auto& mi : moduleTable())
        {
            if (locks.isLocked (mi.m) || ! moduleActive (base, mi.m)) continue;
            for (auto* id : mi.params)
            {
                auto* info = findParam (id);
                if (info == nullptr || info->discrete) continue;
                p.setNorm (id, p.getNorm (id) + (rng.nextFloat() * 2.0f - 1.0f) * amount * 0.5f);
            }
            if (mi.m == Module::Env2 || mi.m == Module::Env3 || mi.m == Module::Lfo1 || mi.m == Module::Lfo2)
                for (int s = 0; s < ModSrc::count; ++s)
                    if (moduleOfSource (s) == mi.m)
                        for (int d = 0; d < ModDst::count; ++d)
                            if (std::abs (p.getMod (s, d)) > 0.01f)
                                p.setMod (s, d, p.getMod (s, d) * (1.0f + (rng.nextFloat() * 2.0f - 1.0f) * amount));
        }
        if (structuralRate > 0.0f && rng.nextFloat() < structuralRate)
        {
            using namespace ParamID;
            std::vector<Module> options;
            for (auto& mi : moduleTable()) if (mi.optional && ! locks.isLocked (mi.m)) options.push_back (mi.m);
            if (! options.empty())
            {
                const auto m = options[(size_t) rng.nextInt ((int) options.size())];
                const bool enable = ! moduleActive (p, m);
                switch (m)
                {
                    case Module::Osc2:    p.set (osc2On, enable ? 1.0f : 0.0f); if (enable) { p.set (oscMix, 0.3f + 0.3f * rng.nextFloat()); p.set (osc2Fine, (rng.nextFloat() * 2 - 1) * 12.0f); } break;
                    case Module::Fm:      p.set (fmAmount, enable ? 0.05f + 0.25f * rng.nextFloat() : 0.0f); break;   // el osc 2 modula aunque no suene
                    case Module::Sub:     p.set (subOn, enable ? 1.0f : 0.0f); if (enable) p.set (subLevel, 0.2f + 0.4f * rng.nextFloat()); break;
                    case Module::Noise:   p.set (noiseOn, enable ? 1.0f : 0.0f); if (enable) p.set (noise, 0.03f + 0.12f * rng.nextFloat()); break;
                    case Module::Unison:  p.set (uniOn, enable ? 1.0f : 0.0f); if (enable) { p.set (uniVoices, 3.0f); p.set (uniDetune, 0.1f + 0.2f * rng.nextFloat()); } break;
                    case Module::Filter2: p.set (f2On, enable ? 1.0f : 0.0f); if (enable) p.set (f2Cutoff, 2000.0f + 6000.0f * rng.nextFloat()); break;
                    case Module::Drive:   p.set (driveOn, enable ? 1.0f : 0.0f); if (enable) p.set (drive, 0.1f + 0.3f * rng.nextFloat()); break;
                    default: break;
                }
            }
        }
        return p;
    }
}
