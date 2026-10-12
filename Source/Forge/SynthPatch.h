#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "../Parameters.h"

// ===========================================================================
// FORGE · SynthPatch
//
// Un patch del motor de síntesis de Nebula (osciladores, sub, ruido, filtros, envolventes, LFOs, matriz y
// voces) como datos: valores reales (Hz, s, %, semitonos...) por parámetro. Es exactamente lo que el motor
// Synth lee, así que un SynthPatch suena igual en el plugin y en el renderizador offline, y no depende de
// ningún sample: no guarda audio, ni tablas derivadas, ni modelos de resíntesis.
//
// Queda fuera del patch lo que no es "receta": efectos, macros, Cosmos/Abismo, ADN de resíntesis, sample,
// volumen general y calidad. Al aplicarlo, eso se deja neutro (ver Forge::applyPatch).
// ===========================================================================
namespace Forge
{
    // ¿Este parámetro es parte de una receta de síntesis?
    inline bool isSynthParam (const juce::String& id)
    {
        static const juce::StringArray excludedPrefixes { "fx", "macro", "smp", "rs", "dna" };
        for (auto& pre : excludedPrefixes) if (id.startsWith (pre)) return false;
        static const juce::StringArray excluded { ParamID::engine, ParamID::transform, ParamID::identity, ParamID::transformOut,
                                                  ParamID::master, ParamID::quality, ParamID::mpeOn, ParamID::grainsOn };
        return ! excluded.contains (id);
    }

    // Rango, valor por defecto y nombre de cada parámetro de síntesis (una sola vez por proceso)
    struct ParamInfo
    {
        juce::String id, name;
        juce::NormalisableRange<float> range;
        float defaultValue = 0.0f;
        bool discrete = false;
        juce::StringArray choices;   // para parámetros de opciones
    };

    inline const std::vector<ParamInfo>& synthParams()
    {
        static const std::vector<ParamInfo> list = []
        {
            std::vector<ParamInfo> out;
            for (auto& p : createParameterList())
            {
                if (! isSynthParam (p->getParameterID())) continue;
                ParamInfo info;
                info.id = p->getParameterID();
                info.name = p->getName (64);
                info.range = p->getNormalisableRange();
                info.defaultValue = p->convertFrom0to1 (p->getDefaultValue());
                info.discrete = p->isDiscrete();
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.get())) info.choices = c->choices;
                out.push_back (std::move (info));
            }
            return out;
        }();
        return list;
    }

    inline const ParamInfo* findParam (const juce::String& id)
    {
        for (auto& p : synthParams()) if (p.id == id) return &p;
        return nullptr;
    }

    struct SynthPatch
    {
        static constexpr int formatVersion = 1;

        juce::String name { "Init" };
        juce::String origin { "manual" };        // "forge" = encontrado por síntesis inversa
        juce::String reference;                  // nombre del audio de referencia (sólo informativo)
        juce::String architecture;               // plantilla de FORGE que lo produjo
        juce::int64 seed = 0;                    // semilla de la búsqueda (reproducible)
        std::map<juce::String, float> values;    // valor real por parámetro de síntesis

        // ---- acceso ----
        float get (const juce::String& id) const
        {
            auto it = values.find (id);
            if (it != values.end()) return it->second;
            if (auto* p = findParam (id)) return p->defaultValue;
            return 0.0f;
        }
        int geti (const juce::String& id) const { return juce::roundToInt (get (id)); }
        void set (const juce::String& id, float v)
        {
            if (auto* p = findParam (id)) values[id] = p->range.snapToLegalValue (juce::jlimit (p->range.start, p->range.end, v));
        }
        float getNorm (const juce::String& id) const
        {
            if (auto* p = findParam (id)) return p->range.convertTo0to1 (get (id));
            return 0.0f;
        }
        void setNorm (const juce::String& id, float n)
        {
            if (auto* p = findParam (id)) set (id, p->range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
        }
        void setMod (int src, int dst, float amount) { set (matrixID (src, dst), amount); }
        float getMod (int src, int dst) const { return get (matrixID (src, dst)); }

        // Patch limpio: un oscilador de sierra, filtro abierto, sin modulaciones ni deriva
        static SynthPatch init()
        {
            SynthPatch p;
            for (auto& info : synthParams()) p.values[info.id] = info.defaultValue;
            p.set (ParamID::osc1On, 1); p.set (ParamID::osc2On, 0); p.set (ParamID::subOn, 0); p.set (ParamID::noiseOn, 0);
            p.set (ParamID::uniOn, 0); p.set (ParamID::uniVoices, 1);
            p.set (ParamID::osc1Wave, (float) Wave::Saw); p.set (ParamID::osc1Octave, 0);
            p.set (ParamID::subLevel, 0); p.set (ParamID::fmAmount, 0); p.set (ParamID::noise, 0);
            p.set (ParamID::f1On, 1); p.set (ParamID::filterType, (float) FilterKind::LowPass); p.set (ParamID::filterChar, 0);
            p.set (ParamID::cutoff, 20000.0f); p.set (ParamID::resonance, 0.0f); p.set (ParamID::filterEnv, 0.0f);
            p.set (ParamID::driveOn, 0); p.set (ParamID::drive, 0.0f); p.set (ParamID::f2On, 0);
            p.set (ParamID::ampA, 0.002f); p.set (ParamID::ampD, 0.3f); p.set (ParamID::ampS, 1.0f); p.set (ParamID::ampR, 0.2f); p.set (ParamID::ampCurve, 0.0f);
            p.set (ParamID::lfoFilter, 0.0f); p.set (ParamID::lfoPitch, 0.0f);
            for (int s = 0; s < ModSrc::count; ++s) for (int d = 0; d < ModDst::count; ++d) p.setMod (s, d, 0.0f);
            p.set (ParamID::modAmount, 1.0f); p.set (ParamID::modTime, 1.0f);
            p.set (ParamID::analog, 0.0f); p.set (ParamID::glide, 0.0f); p.set (ParamID::velSens, 0.0f);
            p.set (ParamID::polyMode, (float) PolyMode::Poly);
            return p;
        }

        // ---- guardar / cargar (con migración: lo que falta toma su valor por defecto, lo desconocido se ignora) ----
        juce::ValueTree toValueTree() const
        {
            juce::ValueTree t ("SynthPatch");
            t.setProperty ("version", formatVersion, nullptr);
            t.setProperty ("name", name, nullptr);
            t.setProperty ("origin", origin, nullptr);
            t.setProperty ("reference", reference, nullptr);
            t.setProperty ("architecture", architecture, nullptr);
            t.setProperty ("seed", seed, nullptr);
            for (auto& [id, v] : values)
            {
                juce::ValueTree p ("P");
                p.setProperty ("id", id, nullptr);
                p.setProperty ("v", v, nullptr);
                t.appendChild (p, nullptr);
            }
            return t;
        }
        static SynthPatch fromValueTree (const juce::ValueTree& t)
        {
            SynthPatch p = init();
            if (! t.hasType ("SynthPatch")) return p;
            // versión 1 = formato actual; versiones futuras agregan aquí su migración
            p.name = t.getProperty ("name", "Init").toString();
            p.origin = t.getProperty ("origin", "manual").toString();
            p.reference = t.getProperty ("reference").toString();
            p.architecture = t.getProperty ("architecture").toString();
            p.seed = (juce::int64) t.getProperty ("seed", 0);
            for (int i = 0; i < t.getNumChildren(); ++i)
            {
                auto c = t.getChild (i);
                const auto id = c.getProperty ("id").toString();
                if (findParam (id) != nullptr) p.set (id, (float) (double) c.getProperty ("v"));
            }
            return p;
        }
        juce::String toXmlString() const { return toValueTree().toXmlString(); }
        static SynthPatch fromXmlString (const juce::String& s)
        {
            if (auto xml = juce::parseXML (s)) return fromValueTree (juce::ValueTree::fromXml (*xml));
            return init();
        }

        bool sameSound (const SynthPatch& o, float tol = 1.0e-6f) const
        {
            for (auto& info : synthParams())
                if (std::abs (getNorm (info.id) - o.getNorm (info.id)) > tol) return false;
            return true;
        }
    };
}
