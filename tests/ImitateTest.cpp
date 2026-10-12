// Prueba del modo Sample y del modo Imitar, sin DAW.
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

static constexpr double sr = 48000.0;
static constexpr int block = 512;

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), rate, (unsigned) b.getNumChannels(), 24, {}, 0)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Toca una nota en el procesador en vivo y devuelve el audio (mono)
static juce::AudioBuffer<float> play (NebulaAudioProcessor& p, int note, double holdS, double totalS)
{
    const int total = (int) (sr * totalS), hold = (int) (sr * holdS);
    juce::AudioBuffer<float> out (1, total), buf (2, block);
    out.clear();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        if (hold >= pos && hold < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, note), hold - pos);
        p.processBlock (buf, midi);
        out.copyFrom (0, pos, buf, 0, 0, n);
    }
    juce::MidiBuffer none;
    buf.setSize (2, block);
    for (int i = 0; i < 300; ++i) p.processBlock (buf, none);
    return out;
}

static float peakOf (const juce::AudioBuffer<float>& b)
{
    float pk = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = b.getSample (0, i);
        if (! std::isfinite (v)) return -1.0f;
        pk = juce::jmax (pk, std::abs (v));
    }
    return pk;
}

// Sonidos de prueba que no vienen del sinte
static juce::AudioBuffer<float> makeVowel (double hz, double seconds)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const double f = hz * (1.0 + 0.004 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t));
        ph += f / sr;
        double s = 0.0;
        for (int h = 1; h < 30; ++h)
        {
            const double fh = h * f;
            // formantes tipo "a": picos cerca de 800 y 1200 Hz
            const double amp = (1.0 / h) * (1.0 + 4.0 * std::exp (-std::pow ((fh - 800.0) / 150.0, 2)) + 3.0 * std::exp (-std::pow ((fh - 1200.0) / 200.0, 2)));
            s += amp * std::sin (2.0 * juce::MathConstants<double>::pi * h * ph);
        }
        const double env = juce::jmin (1.0, t / 0.05) * (t > seconds - 0.2 ? (seconds - t) / 0.2 : 1.0);
        b.setSample (0, i, (float) (0.2 * s * env));
    }
    return b;
}

static juce::AudioBuffer<float> makeBell (double hz, double seconds)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    const double ratios[] = { 1.0, 2.76, 5.4, 8.93 };
    const double decays[] = { 1.2, 0.6, 0.3, 0.15 };
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        double s = 0.0;
        for (int k = 0; k < 4; ++k)
            s += std::exp (-t / decays[k]) * std::sin (2.0 * juce::MathConstants<double>::pi * hz * ratios[k] * t) / (k + 1);
        b.setSample (0, i, (float) (0.5 * s * juce::jmin (1.0, t / 0.002)));
    }
    return b;
}

static juce::AudioBuffer<float> makeNoiseHit (double seconds)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    juce::Random r (7);
    float lp = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        lp += 0.3f * ((r.nextFloat() * 2.0f - 1.0f) - lp);
        b.setSample (0, i, (float) (lp * std::exp (-t / 0.12)));
    }
    return b;
}

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::unique_ptr<NebulaAudioProcessor> proc (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);
    int failures = 0;

    // ---------- Detección de tono ----------
    {
        auto v = SampleTools::fromBuffer (makeVowel (220.0, 1.5), sr, "vocal");
        std::printf ("Tono vocal 220 Hz -> nota %.2f (esperado 57.00) %s\n", v->rootNote, v->hasPitch && std::abs (v->rootNote - 57.0f) < 0.3f ? "OK" : "FALLO");
        if (! (v->hasPitch && std::abs (v->rootNote - 57.0f) < 0.3f)) ++failures;
        auto b = SampleTools::fromBuffer (makeBell (330.0, 2.0), sr, "campana");
        std::printf ("Tono campana 330 Hz -> nota %.2f (esperado 64.00) tiene=%d\n", b->rootNote, (int) b->hasPitch);
        auto n = SampleTools::fromBuffer (makeNoiseHit (0.6), sr, "ruido");
        std::printf ("Ruido -> tiene tono=%d (esperado 0)\n", (int) n->hasPitch);
    }

    // ---------- Modo Sample ----------
    {
        auto vowel = makeVowel (220.0, 1.5);
        writeWav (outDir.getChildFile ("obj_vocal.wav"), vowel, sr);
        proc->setSample (SampleTools::fromBuffer (vowel, sr, "vocal"));
        proc->useSampleAsInstrument();

        auto a = play (*proc, 57, 1.2, 1.6);   // misma nota = sonido original
        auto c = play (*proc, 64, 1.2, 1.6);   // una quinta arriba
        std::printf ("Sample tocado: pico nota original %.3f, quinta %.3f\n", peakOf (a), peakOf (c));
        if (peakOf (a) < 0.05f || peakOf (c) < 0.05f) ++failures;

        // Variantes: textura granular, reversa, loop
        auto& st = proc->state;
        auto set = [&] (const char* id, float v) { auto* p = st.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set (ParamID::smpTexture, 0.8f); set (ParamID::smpGrain, 0.12f); set (ParamID::smpLoop, 1.0f);
        auto g = play (*proc, 57, 3.0, 3.4);
        set (ParamID::smpTexture, 0.0f); set (ParamID::smpReverse, 1.0f);
        auto r = play (*proc, 57, 1.2, 1.6);
        std::printf ("Granular pico %.3f, reversa pico %.3f\n", peakOf (g), peakOf (r));
        if (peakOf (g) < 0.02f || peakOf (r) < 0.05f) ++failures;
        writeWav (outDir.getChildFile ("sample_granular.wav"), g, sr);

        // Jardín en modo sample
        proc->useSampleAsInstrument();
        int tested = 0, silent = 0, broken = 0;
        juce::AudioBuffer<float> demo (1, (int) (sr * 1.0) * 24);
        demo.clear();
        int dpos = 0;
        for (int gen = 0; gen < 12; ++gen)
        {
            proc->garden.mutation = 0.6f;
            for (int k = 0; k < Garden::numSprouts; ++k)
            {
                proc->garden.selectSprout (k);
                auto o = play (*proc, 52 + (k % 3) * 5, 0.8, 1.0);
                const float pk = peakOf (o);
                ++tested;
                if (pk < 0.0f) ++broken; else if (pk < 0.01f) ++silent;
                if (gen < 3 && dpos + o.getNumSamples() <= demo.getNumSamples())
                {
                    demo.copyFrom (0, dpos, o, 0, 0, o.getNumSamples());
                    dpos += o.getNumSamples();
                }
            }
            proc->garden.plant (gen % Garden::numSprouts);
        }
        writeWav (outDir.getChildFile ("sample_jardin.wav"), demo, sr);
        const bool engineKept = proc->state.getRawParameterValue (ParamID::engine)->load() > 0.5f;
        std::printf ("Jardin con sample: %d brotes, %d mudos, %d rotos, motor sigue en Sample: %s\n",
                     tested, silent, broken, engineKept ? "si" : "NO");
        if (broken > 0 || silent > tested / 10 || ! engineKept) ++failures;
    }

    // ---------- Calibración: distancia entre dos tomas del mismo preset ----------
    if (std::getenv ("CALIBRAR"))
    {
        SoundAnalyser an;
        for (int p = 0; p < (int) factoryPresets().size(); ++p)
        {
            proc->state.getParameter (ParamID::engine)->setValueNotifyingHost (0.0f);
            proc->loadPreset (p);
            auto a = play (*proc, 57, 1.0, 1.6), b = play (*proc, 57, 1.0, 1.6);
            auto fa = an.analyse (a.getReadPointer (0), a.getNumSamples(), sr, 1.5);
            auto fb = an.analyse (b.getReadPointer (0), b.getNumSamples(), sr, 1.5);
            float dmin = 1e9f; int best = -1;
            for (int q = 0; q < (int) factoryPresets().size(); ++q)
            {
                if (q == p) continue;
                proc->loadPreset (q);
                auto c = play (*proc, 57, 1.0, 1.6);
                const float d = SoundAnalyser::distance (fa, an.analyse (c.getReadPointer (0), c.getNumSamples(), sr, 1.5));
                if (d < dmin) { dmin = d; best = q; }
            }
            std::printf ("  %-16s misma=%.2f  otro mas cercano=%.2f (%s)\n", factoryPresets()[(size_t) p].name,
                         SoundAnalyser::distance (fa, fb), dmin, factoryPresets()[(size_t) best].name);
        }
    }

    // ---------- Modo Imitar ----------
    struct Target { const char* name; juce::AudioBuffer<float> audio; };
    std::vector<Target> targets;

    // Objetivos hechos con el propio sinte (deberían quedar muy parecidos)
    for (int presetIdx : { 4, 1 })
    {
        proc->state.getParameter (ParamID::engine)->setValueNotifyingHost (0.0f);
        proc->loadPreset (presetIdx);
        targets.push_back ({ factoryPresets()[(size_t) presetIdx].name, play (*proc, 57, 1.0, 1.6) });
    }
    targets.push_back ({ "Vocal (externo)", makeVowel (220.0, 1.5) });
    targets.push_back ({ "Campana (externo)", makeBell (330.0, 2.0) });
    targets.push_back ({ "Golpe de ruido (externo)", makeNoiseHit (0.6) });

    for (auto& t : targets)
    {
        proc->loadPreset (0);
        proc->state.getParameter (ParamID::engine)->setValueNotifyingHost (0.0f);
        proc->setSample (SampleTools::fromBuffer (t.audio, sr, t.name));

        const auto start = juce::Time::getMillisecondCounterHiRes();
        proc->startImitation();
        juce::Thread::sleep (50);
        while (proc->isImitating()) juce::Thread::sleep (50);
        proc->applyImitationResult();
        const double secs = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

        const float sim = proc->lastSimilarity.load();
        const int note = proc->sampleSource.get()->hasPitch ? juce::roundToInt (proc->sampleSource.get()->rootNote) : 48;
        auto result = play (*proc, note, juce::jmin (1.3, t.audio.getNumSamples() / sr * 0.85), t.audio.getNumSamples() / sr + 0.3);
        std::printf ("Imitar %-26s parecido %3.0f%%  (%.1f s)  motor=%s  pico %.3f\n", t.name, sim * 100.0f, secs,
                     proc->state.getRawParameterValue (ParamID::engine)->load() > 0.5f ? "sample" : "sinte", peakOf (result));
        if (sim <= 0.0f || peakOf (result) < 0.01f) ++failures;

        const auto base = juce::String (t.name).upToFirstOccurrenceOf (" ", false, false);
        writeWav (outDir.getChildFile ("imitar_" + base + "_objetivo.wav"), t.audio, sr);
        writeWav (outDir.getChildFile ("imitar_" + base + "_resultado.wav"), result, sr);
    }

    // ---------- Cancelar a medias ----------
    proc->startImitation();
    juce::Thread::sleep (200);
    proc->cancelImitation();
    juce::Thread::sleep (300);
    std::printf ("Cancelar imitacion: %s\n", proc->isImitating() ? "FALLO" : "OK");
    if (proc->isImitating()) ++failures;

    std::printf ("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    proc.reset();
    return failures == 0 ? 0 : 1;
}
