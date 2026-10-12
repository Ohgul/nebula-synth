#pragma once
#include "Fingerprint.h"
#include "PatchRenderer.h"
#if NEBULA_HAS_FORGE_DATA
 #include <BinaryData.h>
#endif

// ===========================================================================
// FORGE · DNA_DATABASE + PATCH_RETRIEVER
//
// Una base de patches nativos (recetas del motor Synth) con su huella sonora, generada de forma determinista:
// cada categoría tiene un muestreador de parámetros con rangos musicales; cada patch se renderiza en una nota
// canónica de su categoría y se mide. Se descartan casi-duplicados y se separa en entrenamiento / validación
// / prueba (índice % 10: 0 = prueba, 1 = validación).
//
// Índice: búsqueda exacta de vecinos (fuerza bruta con pesos por clase). Con 2 000 a 40 000 entradas de 69
// dimensiones cuesta < 5 ms, así que no hace falta una estructura aproximada (HNSW/FAISS); el contrato de
// búsqueda (query -> k vecinos ordenados, con diversidad) es el mismo y se puede cambiar de motor después.
// ===========================================================================
namespace Forge
{
    // Sube cuando cambia algo del motor que altera el sonido de un patch (la base queda desactualizada)
    constexpr int engineDspVersion = 1;
    constexpr int datasetFormatVersion = 1;

    struct CategorySpec { Category cat; int note; double holdS, totalS; bool pitched; };

    inline const std::vector<CategorySpec>& categorySpecs()
    {
        static const std::vector<CategorySpec> s {
            { Category::Bass, 36, 0.9, 1.2, true },  { Category::Eight08, 33, 1.6, 1.6, true }, { Category::Lead, 60, 1.2, 1.5, true },
            { Category::Pluck, 60, 1.0, 1.2, true }, { Category::Pad, 60, 2.4, 3.0, true },     { Category::FmBell, 72, 1.8, 2.0, true },
            { Category::Noise, 60, 1.2, 1.5, false } };
        return s;
    }
    inline const CategorySpec& specFor (Category c) { for (auto& s : categorySpecs()) if (s.cat == c) return s; return categorySpecs().front(); }

    // ---------- muestreadores por categoría ----------
    inline SynthPatch samplePatch (Category c, juce::Random& r)
    {
        auto p = SynthPatch::init();
        auto u = [&] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
        auto lg = [&] (float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); };   // logarítmico
        auto pick = [&] (std::initializer_list<int> v) { const int i = r.nextInt ((int) v.size()); return *(v.begin() + i); };
        auto chance = [&] (float pr) { return r.nextFloat() < pr; };
        auto ampADSR = [&] (float a0, float a1, float d0, float d1, float s0, float s1, float r0, float r1)
        { p.set (ParamID::ampA, lg (a0, a1)); p.set (ParamID::ampD, lg (d0, d1)); p.set (ParamID::ampS, u (s0, s1)); p.set (ParamID::ampR, lg (r0, r1)); };
        auto filt = [&] (float c0, float c1, float res1, float e0, float e1)
        { p.set (ParamID::cutoff, lg (c0, c1)); p.set (ParamID::resonance, u (0.0f, res1)); p.set (ParamID::filterEnv, u (e0, e1));
          p.set (ParamID::fltA, lg (0.001f, 0.2f)); p.set (ParamID::fltD, lg (0.05f, 1.5f)); p.set (ParamID::fltS, u (0.0f, 0.8f)); };
        auto osc2 = [&] (std::initializer_list<int> waves, float pr)
        {
            if (! chance (pr)) return;
            p.set (ParamID::osc2On, 1); p.set (ParamID::osc2Wave, (float) pick (waves)); p.set (ParamID::osc2Semi, (float) pick ({ 0, 0, 0, -12, 12, 7 }));
            p.set (ParamID::osc2Fine, u (-25.0f, 25.0f)); p.set (ParamID::oscMix, u (0.2f, 0.6f)); p.set (ParamID::osc2Shape, u (0.2f, 0.8f));
        };
        switch (c)
        {
            case Category::Bass:
                p.set (ParamID::osc1Wave, (float) pick ({ Wave::Saw, Wave::Pulse, Wave::Sine, Wave::Triangle, Wave::SuperSaw }));
                p.set (ParamID::osc1Shape, u (0.1f, 0.9f)); p.set (ParamID::osc1Octave, (float) pick ({ 0, 0, -1 }));
                osc2 ({ Wave::Saw, Wave::Pulse }, 0.5f);
                if (chance (0.5f)) { p.set (ParamID::subOn, 1); p.set (ParamID::subLevel, u (0.2f, 0.8f)); }
                filt (80.0f, 2500.0f, 0.6f, 0.0f, 0.6f);
                ampADSR (0.001f, 0.02f, 0.1f, 1.5f, 0.3f, 1.0f, 0.05f, 0.5f);
                if (chance (0.3f)) { p.set (ParamID::driveOn, 1); p.set (ParamID::drive, u (0.1f, 0.7f)); }
                break;
            case Category::Eight08:
                p.set (ParamID::osc1Wave, (float) pick ({ Wave::Sine, Wave::Sine, Wave::Triangle }));
                p.set (ParamID::filterEnv, 0.0f);
                p.setMod (ModSrc::Env2, ModDst::Pitch, u (0.0f, 1.0f));
                p.set (ParamID::fltA, 0.001f); p.set (ParamID::fltD, lg (0.01f, 0.3f)); p.set (ParamID::fltS, 0.0f); p.set (ParamID::fltCurve, u (0.0f, 0.9f));
                ampADSR (0.001f, 0.006f, 0.3f, 3.0f, 0.0f, 0.3f, 0.05f, 0.6f);
                p.set (ParamID::ampCurve, u (0.0f, 0.8f));
                if (chance (0.4f)) { p.set (ParamID::driveOn, 1); p.set (ParamID::drive, u (0.1f, 0.8f)); }
                if (chance (0.5f)) { p.set (ParamID::noiseOn, 1); p.setMod (ModSrc::Env2, ModDst::Noise, u (0.1f, 0.7f)); p.set (ParamID::noiseColor, u (0.4f, 1.0f)); }
                if (chance (0.2f)) { p.setMod (ModSrc::Env3, ModDst::Pitch, -u (0.2f, 1.0f)); p.set (ParamID::env3A, u (0.3f, 1.2f)); p.set (ParamID::env3S, 1.0f); p.set (ParamID::env3Curve, -u (0.3f, 0.9f)); }
                break;
            case Category::Lead:
                p.set (ParamID::osc1Wave, (float) pick ({ Wave::Saw, Wave::Pulse, Wave::SuperSaw, Wave::Sync }));
                p.set (ParamID::osc1Shape, u (0.1f, 0.9f));
                osc2 ({ Wave::Saw, Wave::Pulse, Wave::Sine }, 0.6f);
                filt (400.0f, 9000.0f, 0.5f, -0.3f, 0.7f);
                ampADSR (0.001f, 0.08f, 0.1f, 1.0f, 0.6f, 1.0f, 0.05f, 0.4f);
                if (chance (0.5f)) { p.set (ParamID::lfoRate, u (4.0f, 7.0f)); p.set (ParamID::lfoPitch, u (5.0f, 40.0f)); p.set (ParamID::lfoFade, u (0.0f, 0.8f)); p.set (ParamID::lfoRetrig, 1.0f); }
                if (chance (0.2f)) { p.set (ParamID::uniOn, 1); p.set (ParamID::uniVoices, (float) pick ({ 3, 5 })); p.set (ParamID::uniDetune, u (0.05f, 0.4f)); }
                if (chance (0.25f)) { p.set (ParamID::fmAmount, u (0.02f, 0.3f)); p.set (ParamID::osc2Wave, (float) Wave::Sine); }
                break;
            case Category::Pluck:
                p.set (ParamID::osc1Wave, (float) pick ({ Wave::Saw, Wave::Pulse, Wave::Triangle, Wave::SuperSaw }));
                p.set (ParamID::osc1Shape, u (0.1f, 0.9f));
                osc2 ({ Wave::Saw, Wave::Pulse }, 0.4f);
                p.set (ParamID::cutoff, lg (200.0f, 4000.0f)); p.set (ParamID::resonance, u (0.0f, 0.6f)); p.set (ParamID::filterEnv, u (0.3f, 1.0f));
                p.set (ParamID::fltA, 0.001f); p.set (ParamID::fltD, lg (0.05f, 0.6f)); p.set (ParamID::fltS, 0.0f); p.set (ParamID::fltCurve, u (0.0f, 0.7f));
                ampADSR (0.001f, 0.005f, 0.1f, 1.2f, 0.0f, 0.0f, 0.05f, 0.4f);
                p.set (ParamID::ampCurve, u (0.0f, 0.8f));
                break;
            case Category::Pad:
                p.set (ParamID::osc1Wave, (float) pick ({ Wave::Saw, Wave::SuperSaw, Wave::Table, Wave::Pulse }));
                p.set (ParamID::osc1Table, (float) r.nextInt (4)); p.set (ParamID::osc1Shape, u (0.1f, 0.9f));
                osc2 ({ Wave::Saw, Wave::Table, Wave::Pulse }, 0.7f);
                filt (300.0f, 6000.0f, 0.4f, -0.2f, 0.5f);
                p.set (ParamID::fltA, lg (0.2f, 2.0f));
                ampADSR (0.2f, 2.0f, 0.5f, 3.0f, 0.6f, 1.0f, 0.5f, 3.0f);
                if (chance (0.6f)) { p.set (ParamID::lfo2Rate, lg (0.1f, 1.0f)); p.setMod (ModSrc::Lfo2, ModDst::Cutoff1, u (0.05f, 0.4f)); }
                if (chance (0.4f)) { p.set (ParamID::uniOn, 1); p.set (ParamID::uniVoices, (float) pick ({ 3, 5, 7 })); p.set (ParamID::uniDetune, u (0.1f, 0.5f)); p.set (ParamID::uniWidth, u (0.5f, 1.0f)); }
                break;
            case Category::FmBell:
                p.set (ParamID::osc1Wave, (float) pick ({ Wave::Sine, Wave::Triangle }));
                p.set (ParamID::osc2Wave, (float) Wave::Sine); p.set (ParamID::osc2Semi, (float) pick ({ 7, 12, 19, 24, 5, 17 }));
                p.set (ParamID::osc2Fine, chance (0.5f) ? u (-50.0f, 50.0f) : 0.0f);
                p.set (ParamID::fmAmount, u (0.1f, 0.6f));
                p.setMod (ModSrc::Env2, ModDst::Fm, u (0.0f, 0.5f));
                p.set (ParamID::fltA, 0.001f); p.set (ParamID::fltD, lg (0.1f, 1.5f)); p.set (ParamID::fltS, 0.0f); p.set (ParamID::filterEnv, 0.0f);
                ampADSR (0.001f, 0.005f, 0.5f, 3.0f, 0.0f, 0.1f, 0.2f, 2.0f);
                p.set (ParamID::ampCurve, u (0.2f, 0.8f));
                break;
            case Category::Noise:
            default:
                p.set (ParamID::osc1On, 0);
                p.set (ParamID::noiseOn, 1); p.set (ParamID::noise, 1.0f); p.set (ParamID::noiseColor, u (0.1f, 1.0f));
                p.set (ParamID::filterType, (float) pick ({ FilterKind::LowPass, FilterKind::BandPass, FilterKind::HighPass }));
                filt (200.0f, 10000.0f, 0.8f, -0.4f, 0.6f);
                ampADSR (0.001f, 1.0f, 0.05f, 1.5f, 0.0f, 1.0f, 0.05f, 1.5f);
                if (chance (0.5f)) { p.set (ParamID::lfoRate, lg (0.2f, 8.0f)); p.set (ParamID::lfoFilter, u (0.1f, 0.6f)); }
                break;
        }
        return p;
    }

    // ---------- la base ----------
    struct DnaEntry
    {
        Category category = Category::Bass;
        int split = 2;                    // 0 prueba, 1 validación, 2 entrenamiento
        juce::uint32 seed = 0;
        FingerprintVec fp {};
        std::vector<float> norm;          // valores normalizados de synthParams(), en ese orden
        SynthPatch patch() const
        {
            auto p = SynthPatch::init();
            const auto& ps = synthParams();
            for (size_t i = 0; i < ps.size() && i < norm.size(); ++i) p.setNorm (ps[i].id, norm[i]);
            p.origin = "dataset"; p.architecture = categoryName (category); p.seed = seed;
            return p;
        }
    };

    inline juce::String engineTag()
    {
        juce::String ids;
        for (auto& p : synthParams()) ids << p.id << ";";
        return "dsp" + juce::String (engineDspVersion) + "-fp" + juce::String (fingerprintVersion) + "-" + juce::String::toHexString ((juce::int64) ids.hashCode64());
    }

    inline juce::uint32 entrySeed (Category c, int i) { return (juce::uint32) (0x9E3779B1u * (juce::uint32) ((int) c + 1) + 2654435761u * (juce::uint32) (i + 1)); }

    // Renderiza y mide un patch como lo hace la base (nota y duración canónicas de su categoría, 32 kHz)
    inline FingerprintVec measurePatch (PatchRenderer& r, const SynthPatch& p, Category c, double sr = 32000.0)
    {
        const auto& s = specFor (c);
        const auto audio = r.render (p, s.note, s.holdS, s.totalS, sr);
        return fingerprint (audio, sr, s.pitched ? 440.0 * std::exp2 ((s.note - 69) / 12.0) : 0.0, s.holdS);
    }

    struct Dataset
    {
        juce::String tag;
        std::vector<DnaEntry> entries;
        mutable juce::CriticalSection cacheLock;
        mutable std::array<std::vector<int>, (size_t) Category::count> variedCache;
        Dataset() = default;
        Dataset (const Dataset& o) : tag (o.tag), entries (o.entries) {}
        Dataset& operator= (const Dataset& o) { tag = o.tag; entries = o.entries; for (auto& v : variedCache) v.clear(); return *this; }
        Dataset (Dataset&& o) noexcept : tag (std::move (o.tag)), entries (std::move (o.entries)) {}
        Dataset& operator= (Dataset&& o) noexcept { tag = std::move (o.tag); entries = std::move (o.entries); for (auto& v : variedCache) v.clear(); return *this; }

        // Genera "perCategory" patches por categoría (determinista). progress: 0..1
        static Dataset generate (int perCategory, std::function<void (float)> progress = {}, std::atomic<bool>* cancel = nullptr)
        {
            Dataset d; d.tag = engineTag();
            PatchRenderer r;
            const auto& ps = synthParams();
            const int cats = (int) Category::count;
            for (int ci = 0; ci < cats; ++ci)
            {
                const auto cat = (Category) ci;
                const auto w = weightsFor (cat);
                int made = 0;
                for (int i = 0; made < perCategory && i < perCategory * 3; ++i)
                {
                    if (cancel != nullptr && cancel->load()) return d;
                    juce::Random rng ((juce::int64) entrySeed (cat, i));
                    const auto p = samplePatch (cat, rng);
                    DnaEntry e;
                    e.category = cat; e.seed = entrySeed (cat, i);
                    e.fp = measurePatch (r, p, cat);
                    // casi-duplicados (misma categoría): se descartan
                    bool dup = false;
                    for (auto it = d.entries.rbegin(); it != d.entries.rend() && it->category == cat; ++it)
                        if (fpDistance (it->fp, e.fp, w) < 0.02f) { dup = true; break; }
                    if (dup) continue;
                    e.split = made % 10 == 0 ? 0 : made % 10 == 1 ? 1 : 2;
                    e.norm.reserve (ps.size());
                    for (auto& info : ps) e.norm.push_back (p.getNorm (info.id));
                    d.entries.push_back (std::move (e));
                    ++made;
                    if (progress) progress ((float) (ci * perCategory + made) / (float) (cats * perCategory));
                }
            }
            return d;
        }

        // ---------- guardar / cargar (binario versionado) ----------
        juce::MemoryBlock toBinary() const
        {
            juce::MemoryOutputStream o;
            o.write ("NDNA", 4);
            o.writeInt (datasetFormatVersion);
            o.writeString (tag);
            const auto& ps = synthParams();
            o.writeInt ((int) ps.size());
            for (auto& p : ps) o.writeString (p.id);
            o.writeInt (fingerprintDims);
            o.writeInt ((int) entries.size());
            for (auto& e : entries)
            {
                o.writeByte ((char) e.category); o.writeByte ((char) e.split); o.writeInt ((int) e.seed);
                for (auto v : e.fp) o.writeFloat (v);
                for (auto v : e.norm) o.writeShort ((short) juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 32767.0f));
            }
            juce::MemoryBlock mb;
            juce::MemoryOutputStream zipped (mb, false);
            { juce::GZIPCompressorOutputStream gz (zipped, 9); gz.write (o.getData(), o.getDataSize()); }
            return mb;
        }

        // Devuelve false si el formato no es válido. Si los parámetros cambiaron, los valores se reubican por ID.
        bool fromBinary (const void* data, size_t size)
        {
            juce::MemoryInputStream raw (data, size, false);
            juce::GZIPDecompressorInputStream gz (raw);
            juce::MemoryBlock mb; gz.readIntoMemoryBlock (mb);
            juce::MemoryInputStream in (mb, false);
            char magic[4] {}; in.read (magic, 4);
            if (std::memcmp (magic, "NDNA", 4) != 0 || in.readInt() != datasetFormatVersion) return false;
            tag = in.readString();
            const int nIds = in.readInt();
            juce::StringArray fileIds; for (int i = 0; i < nIds; ++i) fileIds.add (in.readString());
            if (in.readInt() != fingerprintDims) return false;
            const int count = in.readInt();
            const auto& ps = synthParams();
            std::vector<int> map (ps.size());
            for (size_t i = 0; i < ps.size(); ++i) map[i] = fileIds.indexOf (ps[i].id);
            entries.clear(); entries.reserve ((size_t) count);
            std::vector<float> raw2 ((size_t) nIds);
            for (int k = 0; k < count && ! in.isExhausted(); ++k)
            {
                DnaEntry e;
                e.category = (Category) in.readByte(); e.split = in.readByte(); e.seed = (juce::uint32) in.readInt();
                for (auto& v : e.fp) v = in.readFloat();
                for (auto& v : raw2) v = (float) in.readShort() / 32767.0f;
                e.norm.resize (ps.size());
                for (size_t i = 0; i < ps.size(); ++i) e.norm[i] = map[i] >= 0 ? raw2[(size_t) map[i]] : ps[i].range.convertTo0to1 (ps[i].defaultValue);
                entries.push_back (std::move (e));
            }
            return true;
        }
        bool isCurrent() const { return tag == engineTag(); }

        // Parámetros que varían dentro de una categoría (índices en synthParams()), ordenados por su rango
        const std::vector<int>& variedParams (Category c) const
        {
            const juce::ScopedLock sl (cacheLock);
            auto& v = variedCache[(size_t) c];
            if (! v.empty() || entries.empty()) return v;
            std::vector<std::pair<float, int>> spread;
            for (size_t i = 0; i < synthParams().size(); ++i)
            {
                float mn = 1, mx = 0;
                for (auto& e : entries) if (e.category == c && i < e.norm.size()) { mn = juce::jmin (mn, e.norm[i]); mx = juce::jmax (mx, e.norm[i]); }
                if (mx - mn > 0.02f) spread.push_back ({ mx - mn, (int) i });
            }
            std::sort (spread.begin(), spread.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            for (auto& [r, i] : spread) v.push_back (i);
            return v;
        }

        // Consulta en dos pasadas: la categoría no se conoce, así que primero se buscan vecinos con pesos generales,
        // se vota la categoría, y se vuelve a buscar con los pesos de esa categoría

        // ---------- búsqueda de vecinos ----------
        struct Hit { int index; float distance; };

        std::vector<Hit> query (const FingerprintVec& q, int k, int splitMask, float diversity, int* categoryOut = nullptr) const
        {
            const auto first = nearest (q, weightsFor (Category::Lead), 7, splitMask);
            int votes[(int) Category::count] {};
            for (auto& h : first) ++votes[(int) entries[(size_t) h.index].category];
            const int guess = (int) (std::max_element (std::begin (votes), std::end (votes)) - std::begin (votes));
            if (categoryOut != nullptr) *categoryOut = guess;
            return nearest (q, weightsFor ((Category) guess), k, splitMask, diversity);
        }

        // k vecinos por la distancia ponderada; splitMask: qué particiones se buscan (bit 0 prueba, 1 val, 2 entr.)
        // diversity: no devuelve dos entradas a menos de esa distancia entre sí (CANDIDATE_DIVERSITY)
        std::vector<Hit> nearest (const FingerprintVec& q, const FpWeights& w, int k, int splitMask = 0b110, float diversity = 0.0f,
                                  int categoryFilter = -1) const
        {
            std::vector<Hit> all;
            all.reserve (entries.size());
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto& e = entries[i];
                if (((splitMask >> e.split) & 1) == 0) continue;
                if (categoryFilter >= 0 && (int) e.category != categoryFilter) continue;
                all.push_back ({ (int) i, fpDistance (q, e.fp, w) });
            }
            std::sort (all.begin(), all.end(), [] (const Hit& a, const Hit& b) { return a.distance < b.distance; });
            std::vector<Hit> out;
            for (auto& h : all)
            {
                bool tooClose = false;
                if (diversity > 0)
                    for (auto& o : out) if (fpDistance (entries[(size_t) h.index].fp, entries[(size_t) o.index].fp, w) < diversity) { tooClose = true; break; }
                if (! tooClose) out.push_back (h);
                if ((int) out.size() >= k) break;
            }
            return out;
        }
    };

    // La base incluida en el plugin (se descomprime la primera vez que se usa)
    inline const Dataset* builtInDataset()
    {
       #if NEBULA_HAS_FORGE_DATA
        static const Dataset d = []
        {
            Dataset x;
            x.fromBinary (BinaryData::nebula_dna_ndb, (size_t) BinaryData::nebula_dna_ndbSize);
            return x;
        }();
        return d.entries.empty() ? nullptr : &d;
       #else
        return nullptr;
       #endif
    }
}
