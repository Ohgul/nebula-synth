// Pruebas de lo nuevo en Nebula v2 (sin DAW).
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

static constexpr double sr = 48000.0;
static constexpr int block = 512;
static int failures = 0;

static void check (const char* what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-44s %s  %s\n", what, ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

struct FakeHost : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (false);
        return p;
    }
};

struct Ev { double t; int note; bool on; };

static juce::AudioBuffer<float> render (NebulaAudioProcessor& p, const std::vector<Ev>& evs, double seconds)
{
    const int total = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (auto& e : evs)
        {
            const int s = (int) (e.t * sr);
            if (s >= pos && s < pos + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.note), s - pos);
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    juce::MidiBuffer none;
    buf.setSize (2, block);
    for (int i = 0; i < 400; ++i) p.processBlock (buf, none);   // dejar que todo se apague
    return out;
}

static float peak (const juce::AudioBuffer<float>& b)
{
    float pk = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            if (! std::isfinite (v)) return -1.0f;
            pk = juce::jmax (pk, std::abs (v));
        }
    return pk;
}

// Frecuencia aproximada por cruces por cero en una ventana
static double pitchAt (const juce::AudioBuffer<float>& b, double t0, double len)
{
    const int a = (int) (t0 * sr), n = (int) (len * sr);
    int crossings = 0;
    for (int i = a + 1; i < a + n; ++i)
        if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f) ++crossings;
    return crossings / len;
}

// Tono por autocorrelación (sirve para ondas con muchos cruces por cero, como Super Saw)
static double pitchAC (const juce::AudioBuffer<float>& b, double t0, double len)
{
    const int a = (int) (t0 * sr), n = (int) (len * sr);
    const float* x = b.getReadPointer (0) + a;
    int bestLag = 0; double best = -1.0;
    for (int lag = (int) (sr / 1000.0); lag < (int) (sr / 50.0); ++lag)
    {
        double acc = 0, e1 = 1e-9, e2 = 1e-9;
        for (int i = 0; i + lag < n; i += 2) { acc += x[i] * x[i + lag]; e1 += x[i] * x[i]; e2 += x[i + lag] * x[i + lag]; }
        const double r = acc / std::sqrt (e1 * e2);
        if (r > best + 0.02) { best = r; bestLag = lag; }
    }
    return bestLag > 0 ? sr / bestLag : 0.0;
}

// Último instante en que la señal pasa de un umbral
static double lastSoundTime (const juce::AudioBuffer<float>& b, float thr)
{
    for (int i = b.getNumSamples() - 1; i >= 0; --i)
        if (std::abs (b.getSample (0, i)) > thr) return i / sr;
    return 0.0;
}

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::unique_ptr<NebulaAudioProcessor> proc (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    FakeHost host;
    proc->setPlayHead (&host);
    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);

    auto& st = proc->state;
    auto set = [&] (const juce::String& id, float v) { auto* p = st.getParameter (id); jassert (p != nullptr); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto get = [&] (const juce::String& id) { return st.getRawParameterValue (id)->load(); };
    auto plain = [&]
    {
        proc->loadPreset (0);
        set (ParamID::engine, 0); set (ParamID::oscMix, 0); set (ParamID::subLevel, 0); set (ParamID::noise, 0);
        set (ParamID::fmAmount, 0); set (ParamID::uniVoices, 1); set (ParamID::f2On, 0);
        set (ParamID::filterType, FilterKind::LowPass); set (ParamID::cutoff, 18000); set (ParamID::resonance, 0.1f);
        set (ParamID::filterEnv, 0); set (ParamID::lfoPitch, 0); set (ParamID::lfoFilter, 0);
        set (ParamID::ampA, 0.005f); set (ParamID::ampD, 0.3f); set (ParamID::ampS, 1.0f); set (ParamID::ampR, 0.05f);
        set (ParamID::polyMode, PolyMode::Poly); set (ParamID::glide, 0); set (ParamID::osc1Octave, 0);
        for (int s = 0; s < ModSrc::count; ++s) for (int d = 0; d < ModDst::count; ++d) set (matrixID (s, d), 0);
    };

    // ---- 1. Todas las ondas suenan ----
    {
        juce::AudioBuffer<float> all (2, (int) (sr * 0.8) * Wave::count);
        for (int w = 0; w < Wave::count; ++w)
        {
            plain();
            set (ParamID::osc1Wave, (float) w);
            set (ParamID::osc1Shape, 0.4f);
            auto o = render (*proc, { { 0.0, 57, true }, { 0.6, 57, false } }, 0.8);
            const float pk = peak (o);
            const double hz = pitchAC (o, 0.2, 0.3);
            const bool ok = pk > 0.05f && pk < 1.0f && std::abs (hz - 220.0) < 12.0;
            check (("Onda " + waveNames()[w]).toRawUTF8(), ok, juce::String::formatted ("pico %.2f, ~%.0f Hz", pk, hz));
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, (int) (sr * 0.8) * w, o, ch, 0, o.getNumSamples());
        }
        juce::WavAudioFormat wav;
        auto f = outDir.getChildFile ("v2_ondas.wav"); f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), sr, 2, 24, {}, 0)))
            w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    }

    // ---- 2. Unísono abre el estéreo ----
    {
        plain();
        set (ParamID::osc1Wave, Wave::Saw);
        auto mono = render (*proc, { { 0.0, 48, true }, { 0.8, 48, false } }, 1.0);
        set (ParamID::uniVoices, 5); set (ParamID::uniDetune, 0.4f); set (ParamID::uniWidth, 1.0f);
        auto wide = render (*proc, { { 0.0, 48, true }, { 0.8, 48, false } }, 1.0);
        auto diff = [] (const juce::AudioBuffer<float>& b)
        {
            double d = 0, s = 0;
            for (int i = 0; i < b.getNumSamples(); ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); d += (l - r) * (l - r); s += (l + r) * (l + r); }
            return std::sqrt (d / juce::jmax (1.0e-9, s));
        };
        check ("Unisono estereo", diff (mono) < 0.01 && diff (wide) > 0.15 && peak (wide) < 1.0f,
               juce::String::formatted ("lados/centro: 1 voz %.3f, 5 voces %.3f, pico %.2f", diff (mono), diff (wide), peak (wide)));
    }

    // ---- 3. Mono / Legato / Glide ----
    {
        plain();
        set (ParamID::osc1Wave, Wave::Sine);
        set (ParamID::polyMode, PolyMode::Legato); set (ParamID::glide, 0.25f);
        auto o = render (*proc, { { 0.0, 57, true }, { 0.5, 69, true }, { 1.1, 69, false }, { 1.9, 57, false } }, 2.0);
        const double before = pitchAt (o, 0.2, 0.25), mid = pitchAt (o, 0.52, 0.06), after = pitchAt (o, 0.8, 0.25),
                     back = pitchAt (o, 1.5, 0.3);
        check ("Legato + glide", std::abs (before - 220) < 15 && mid > 230 && mid < 430 && std::abs (after - 440) < 20 && std::abs (back - 220) < 20,
               juce::String::formatted ("%.0f -> %.0f (deslizando) -> %.0f, suelta -> %.0f Hz", before, mid, after, back));

        set (ParamID::polyMode, PolyMode::Mono); set (ParamID::glide, 0);
        render (*proc, { { 0.0, 48, true }, { 0.1, 52, true }, { 0.2, 55, true } }, 0.4);
        // las notas siguen apretadas al terminar el render... soltamos
        const int voicesMono = proc->activeVoices();
        render (*proc, { { 0.0, 48, false }, { 0.0, 52, false }, { 0.0, 55, false } }, 0.2);
        set (ParamID::polyMode, PolyMode::Poly);
        render (*proc, { { 0.0, 48, true }, { 0.0, 52, true }, { 0.0, 55, true } }, 0.3);
        const int voicesPoly = proc->activeVoices();
        render (*proc, { { 0.0, 48, false }, { 0.0, 52, false }, { 0.0, 55, false } }, 0.2);
        check ("Mono usa 1 voz, Poli 3", voicesMono == 1 && voicesPoly == 3, juce::String::formatted ("mono %d, poli %d", voicesMono, voicesPoly));
    }

    // ---- 4. Filtro 2 y ruteo ----
    {
        plain();
        set (ParamID::osc1Wave, Wave::Saw);
        auto rmsOf = [&] { auto o = render (*proc, { { 0.0, 45, true }, { 0.6, 45, false } }, 0.7); return o.getRMSLevel (0, (int) (0.1 * sr), (int) (0.4 * sr)); };
        const float open = rmsOf();
        set (ParamID::f2On, 1); set (ParamID::f2Type, FilterKind::HighPass); set (ParamID::f2Cutoff, 4000); set (ParamID::f2Env, 0);
        set (ParamID::filterRouting, 0);
        const float serial = rmsOf();
        set (ParamID::cutoff, 300); set (ParamID::filterRouting, 0);
        const float serialBoth = rmsOf();
        set (ParamID::filterRouting, 1);
        const float parallel = rmsOf();
        check ("Filtro 2 en serie y paralelo", serial < open * 0.6f && serialBoth < serial * 0.5f && parallel > serialBoth * 2.0f,
               juce::String::formatted ("abierto %.3f, HP serie %.3f, LP+HP serie %.3f, paralelo %.3f", open, serial, serialBoth, parallel));
    }

    // ---- 5. Matriz: LFO1 -> volumen; Env 3 en loop -> corte ----
    {
        plain();
        set (ParamID::osc1Wave, Wave::Saw);
        set (ParamID::lfoRate, 4.0f); set (ParamID::lfoSync, 0); set (ParamID::lfoShape, LfoShape::Square);
        set (matrixID (ModSrc::Lfo1, ModDst::Volume), -1.0f);
        auto o = render (*proc, { { 0.0, 48, true }, { 1.4, 48, false } }, 1.5);
        float lo = 1e9f, hi = 0.0f;
        for (double t = 0.2; t < 1.2; t += 0.02) { const float r = o.getRMSLevel (0, (int) (t * sr), (int) (0.015 * sr)); lo = juce::jmin (lo, r); hi = juce::jmax (hi, r); }
        check ("Matriz LFO1 -> volumen", hi > 0.05f && lo < hi * 0.4f, juce::String::formatted ("rms min %.3f max %.3f", lo, hi));

        plain();
        set (ParamID::osc1Wave, Wave::Saw); set (ParamID::cutoff, 400);
        set (ParamID::env3A, 0.05f); set (ParamID::env3D, 0.15f); set (ParamID::env3S, 0.0f); set (ParamID::env3Mode, 1);
        set (matrixID (ModSrc::Env3, ModDst::Cutoff1), 1.0f);
        auto e = render (*proc, { { 0.0, 48, true }, { 1.9, 48, false } }, 2.0);
        // con loop, el brillo debe volver a subir varias veces
        int rises = 0; float prev = 0.0f; bool up = false;
        for (double t = 0.0; t < 1.8; t += 0.01)
        {
            const float r = e.getRMSLevel (0, (int) (t * sr), (int) (0.01 * sr));
            if (r > prev * 1.15f && ! up) { ++rises; up = true; } else if (r < prev * 0.9f) up = false;
            prev = r;
        }
        check ("Envolvente 3 en loop", rises >= 4, juce::String::formatted ("%d subidas en 1.8 s", rises));
    }

    // ---- 6. Modo Tempo del sample: dura lo que dice el compás ----
    {
        // Sample de 3 s (tono con golpes cada 0.5 s)
        juce::AudioBuffer<float> s (1, (int) (sr * 3.0));
        for (int i = 0; i < s.getNumSamples(); ++i)
        {
            const double t = i / sr, ph = std::fmod (t, 0.5);
            s.setSample (0, i, (float) (0.6 * std::sin (2 * juce::MathConstants<double>::pi * 220 * t) * std::exp (-ph * 6.0)));
        }
        plain();
        proc->setSample (SampleTools::fromBuffer (s, sr, "golpes"));
        proc->setRegion (0.0f, 1.0f);
        proc->useSampleAsInstrument();
        set (ParamID::smpLoop, 0); set (ParamID::smpReverse, 0); set (ParamID::smpTexture, 0);
        set (ParamID::ampR, 0.02f);
        const int root = juce::roundToInt (proc->sampleSource.get()->rootNote + proc->rootOffset());

        set (ParamID::smpPlayMode, 0);
        auto classic = render (*proc, { { 0.0, root, true }, { 5.5, root, false } }, 6.0);
        set (ParamID::smpPlayMode, 1); set (ParamID::smpBeats, 5);   // 1 compás
        host.bpm = 120.0;
        auto t120 = render (*proc, { { 0.0, root, true }, { 5.5, root, false } }, 6.0);
        host.bpm = 90.0;
        auto t90 = render (*proc, { { 0.0, root, true }, { 5.5, root, false } }, 6.0);
        host.bpm = 120.0;
        if (std::getenv ("DEBUG_TEMPO"))
            for (auto* b : { &classic, &t120, &t90 })
            {
                for (double t = 0; t < 3.2; t += 0.1) std::printf ("%.2f ", b->getMagnitude (0, (int) (t * sr), (int) (0.1 * sr)));
                std::printf ("\n");
            }
        const double dc = lastSoundTime (classic, 0.01f), d120 = lastSoundTime (t120, 0.01f), d90 = lastSoundTime (t90, 0.01f);
        const double p120 = pitchAt (t120, 0.0, 0.2);
        check ("Tempo: 1 compas a 120 BPM = 2 s", std::abs (d120 - 2.0) < 0.15, juce::String::formatted ("clasico %.2f s, tempo %.2f s, ~%.0f Hz", dc, d120, p120));
        check ("Tempo: 1 compas a 90 BPM = 2.67 s", std::abs (d90 - 2.667) < 0.15, juce::String::formatted ("%.2f s", d90));
        check ("Tempo conserva el tono", std::abs (p120 - 220.0) < 20.0, juce::String::formatted ("%.0f Hz", p120));
        set (ParamID::smpBeats, 0);   // Auto: 3 s a 120 BPM = 6 tiempos -> potencia de 2 más cercana (8 tiempos = 4 s)
        auto ta = render (*proc, { { 0.0, root, true }, { 5.5, root, false } }, 6.0);
        const double da = lastSoundTime (ta, 0.01f);
        check ("Tempo Auto ajusta a tiempos enteros", std::abs (da - 2.0) < 0.15 || std::abs (da - 4.0) < 0.15, juce::String::formatted ("%.2f s", da));

        // Sección elegida: sólo el segundo 1..2 -> en clásico dura ~1 s
        set (ParamID::smpPlayMode, 0);
        proc->setRegion (1.0f / 3.0f, 2.0f / 3.0f);
        auto reg = render (*proc, { { 0.0, root, true }, { 3.0, root, false } }, 3.5);
        const double dr = lastSoundTime (reg, 0.01f);
        check ("Seccion elegida (1 s del medio)", std::abs (dr - 1.0) < 0.15, juce::String::formatted ("%.2f s", dr));
    }

    // ---- 7. Imitar usando solo la sección ----
    {
        juce::AudioBuffer<float> s (1, (int) (sr * 4.0));
        s.clear();
        juce::Random r (3);
        for (int i = 0; i < (int) (sr * 2.0); ++i) s.setSample (0, i, 0.3f * (r.nextFloat() * 2 - 1));            // ruido
        for (int i = (int) (sr * 2.0); i < s.getNumSamples(); ++i)
            s.setSample (0, i, (float) (0.5 * std::sin (2 * juce::MathConstants<double>::pi * 220 * i / sr)));      // seno
        plain();
        proc->setSample (SampleTools::fromBuffer (s, sr, "mitad"));
        proc->setRegion (0.5f, 1.0f);
        proc->startImitation();
        juce::Thread::sleep (50);
        while (proc->isImitating()) juce::Thread::sleep (50);
        proc->applyImitationResult();
        auto o = render (*proc, { { 0.0, 57, true }, { 1.0, 57, false } }, 1.2);
        // el resultado debe ser tonal (seno), no ruido: pocos cruces "de más"
        const double hz = pitchAt (o, 0.3, 0.4);
        check ("Imitar usa solo la seccion", proc->lastSimilarity.load() > 0.0f && peak (o) > 0.02f && hz < 1500.0,
               juce::String::formatted ("parecido %.0f%%, ~%.0f cruces/s", proc->lastSimilarity.load() * 100.0f, hz));
    }

    // ---- 8. Hacer onda desde el sample y dibujar una onda ----
    {
        auto vow = juce::AudioBuffer<float> (1, (int) (sr * 1.5));
        for (int i = 0; i < vow.getNumSamples(); ++i)
        {
            const double t = i / sr; double v = 0;
            for (int h = 1; h < 12; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * 220 * h * t) / h * (1.0 + 0.8 * std::sin (t * 3 + h));
            vow.setSample (0, i, (float) (0.2 * v));
        }
        plain();
        proc->setSample (SampleTools::fromBuffer (vow, sr, "voz"));
        const bool made = proc->makeWavetableFromSample (16, 0.3f);
        set (ParamID::engine, 0);
        auto o = render (*proc, { { 0.0, 57, true }, { 0.6, 57, false } }, 0.8);
        check ("Hacer onda desde el sample", made && (int) get (ParamID::osc1Wave) == Wave::Table && (int) get (ParamID::osc1Table) == WavetableBank::userIndex
                                              && peak (o) > 0.05f && std::abs (pitchAt (o, 0.2, 0.3) - 220) < 30,
               juce::String::formatted ("pico %.2f, ~%.0f Hz", peak (o), pitchAt (o, 0.2, 0.3)));

        std::vector<float> pts (256);
        for (size_t i = 0; i < pts.size(); ++i) pts[i] = i < 128 ? 0.8f : -0.8f;   // cuadrada dibujada
        proc->setUserTableFromDrawing (pts);
        auto d = render (*proc, { { 0.0, 57, true }, { 0.6, 57, false } }, 0.8);
        check ("Dibujar onda", peak (d) > 0.05f && peak (d) < 1.0f, juce::String::formatted ("pico %.2f", peak (d)));

        // Se guarda en el proyecto (sample + onda + sección)
        proc->setRegion (0.2f, 0.7f);
        juce::MemoryBlock mb;
        proc->getStateInformation (mb);
        std::unique_ptr<NebulaAudioProcessor> p2 (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
        p2->setPlayConfigDetails (0, 2, sr, block);
        p2->prepareToPlay (sr, block);
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        auto s2 = p2->sampleSource.get();
        const bool sampleBack = s2 != nullptr && std::abs (s2->length() - (int) (1.5 * s2->sampleRate)) < 64;
        const bool regionBack = std::abs (p2->regionStart() - 0.2f) < 1e-3f && std::abs (p2->regionEnd() - 0.7f) < 1e-3f;
        auto d2 = render (*p2, { { 0.0, 57, true }, { 0.6, 57, false } }, 0.8);
        float diff = 0.0f;
        for (int i = 0; i < d2.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (d2.getSample (0, i) - d.getSample (0, i)));
        check ("Proyecto guarda sample, seccion y onda", sampleBack && regionBack && p2->tables.hasCustomUser && peak (d2) > 0.05f,
               juce::String::formatted ("sample %s (%.1f KB guardado), seccion %s, dif onda %.3f", sampleBack ? "si" : "no",
                                        mb.getSize() / 1024.0, regionBack ? "si" : "no", diff));
    }

    // ---- 9. Presets del usuario ----
    {
        plain();
        set (ParamID::cutoff, 1234.0f); set (ParamID::osc1Wave, Wave::SuperSaw);
        const bool saved = proc->saveUserPreset ("Prueba v2");
        set (ParamID::cutoff, 9000.0f); set (ParamID::osc1Wave, Wave::Sine);
        const bool listed = proc->userPresetNames().contains ("Prueba v2");
        const bool loaded = proc->loadUserPreset ("Prueba v2");
        check ("Guardar / cargar preset propio", saved && listed && loaded && std::abs (get (ParamID::cutoff) - 1234.0f) < 1.0f
                                                  && (int) get (ParamID::osc1Wave) == Wave::SuperSaw,
               NebulaAudioProcessor::userPresetFolder().getFullPathName());
        NebulaAudioProcessor::userPresetFolder().getChildFile ("Prueba v2.nebula").deleteFile();
    }

    // ---- 10. Todos los presets de fábrica suenan ----
    {
        int bad = 0;
        juce::String names;
        for (int p = 0; p < (int) factoryPresets().size(); ++p)
        {
            proc->loadPreset (p);
            set (ParamID::engine, 0);
            auto o = render (*proc, { { 0.0, 48, true }, { 0.0, 55, true }, { 1.5, 48, false }, { 1.5, 55, false } }, 2.0);
            const float pk = peak (o);
            if (! (pk > 0.01f && pk < 1.0f)) { ++bad; names << factoryPresets()[(size_t) p].name << " (" << pk << ") "; }
        }
        check ("Presets de fabrica", bad == 0, juce::String ((int) factoryPresets().size()) + " presets " + names);
    }

    // ---- 11. CPU: el peor caso razonable ----
    {
        plain();
        set (ParamID::osc1Wave, Wave::SuperSaw); set (ParamID::osc2Wave, Wave::Table); set (ParamID::oscMix, 0.5f);
        set (ParamID::uniVoices, 7); set (ParamID::f2On, 1); set (ParamID::filterSlope, 1); set (ParamID::f2Slope, 1);
        set (ParamID::filterChar, 2); set (ParamID::ampR, 2.0f);
        for (int s = 0; s < 4; ++s) set (matrixID (s, s + 1), 0.3f);
        std::vector<Ev> evs;
        for (int k = 0; k < 8; ++k) evs.push_back ({ 0.0, 40 + k * 3, true });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto o = render (*proc, evs, 10.0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        render (*proc, [&] { std::vector<Ev> off; for (auto e : evs) off.push_back ({ 0.0, e.note, false }); return off; }(), 0.1);
        const double cpu = ms / 10000.0 * 100.0;
        // La CPU absoluta depende de la máquina (en un servidor de 2 núcleos ronda el 60 %): aquí solo avisa.
        // La regresión de CPU se vigila de forma relativa en el banco (NebulaTests Bench, sección 4).
        check ("CPU: 8 notas x Super Saw x 7 unisono (sonido)", peak (o) < 1.0f && peak (o) > 0.0f,
               juce::String::formatted ("%.1f %% de un nucleo (pico %.2f)%s", cpu, peak (o), cpu >= 60.0 ? "  AVISO: mas de 60 %" : ""));
    }

    proc->setPlayHead (nullptr);
    std::printf ("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    proc.reset();
    return failures == 0 ? 0 : 1;
}
