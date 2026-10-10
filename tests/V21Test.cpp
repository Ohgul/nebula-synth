// Pruebas de Nebula 2.1: filtros transparentes/Moog, LFO apagado, granos, Recrear (resíntesis),
// tono exacto (C# es C#), tonalidad de frases, onda desde archivo, Limpiar e historial, receta.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static constexpr double sr = 48000.0;
static constexpr int block = 512;
static int failures = 0;
static juce::File outDir;

static void check (const char* what, bool ok, const juce::String& detail = {})
{
    std::printf ("%-46s %s  %s\n", what, ok ? "OK   " : "FALLO", detail.toRawUTF8());
    if (! ok) ++failures;
}

static void writeWav (const juce::String& name, const juce::AudioBuffer<float>& b, double rate)
{
    auto f = outDir.getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), rate, (unsigned) b.getNumChannels(), 24, {}, 0)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

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
    for (int i = 0; i < 400; ++i) p.processBlock (buf, none);
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

// Tono preciso: YIN sobre una ventana
static double pitchOf (const juce::AudioBuffer<float>& b, double t0, double len)
{
    return SampleTools::detectPitch (b.getReadPointer (0) + (int) (t0 * sr), (int) (len * sr), sr);
}

static double centsOff (double hz, int note) { return 1200.0 * std::log2 (hz / (440.0 * std::exp2 ((note - 69) / 12.0))); }

// ---- Sonidos de prueba ----
static juce::AudioBuffer<float> makePluck (double hz, double seconds)   // sierra que decae + golpe de ruido
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    juce::Random r (5);
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        ph += hz / sr;
        double s = 0.0;
        const double bright = std::exp (-t * 3.0);
        for (int k = 1; k < 40 && k * hz < sr * 0.45; ++k)
            s += std::sin (2 * juce::MathConstants<double>::pi * k * ph) / k * std::pow (bright, (k - 1) * 0.15);
        const double env = juce::jmin (1.0, t / 0.003) * std::exp (-t * 2.2);
        const double noise = (r.nextFloat() * 2 - 1) * 0.5 * std::exp (-t * 40.0);
        b.setSample (0, i, (float) (0.4 * (s * env + noise)));
    }
    return b;
}

static juce::AudioBuffer<float> makeSquarePad (double hz, double seconds)   // cuadrada con vibrato y ataque lento
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const double f = hz * std::exp2 (20.0 / 1200.0 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * t));
        ph += f / sr;
        double s = 0.0;
        for (int k = 1; k < 30 && k * f < sr * 0.45; k += 2) s += std::sin (2 * juce::MathConstants<double>::pi * k * ph) / k;
        const double env = juce::jmin (1.0, t / 0.3) * (t > seconds - 0.4 ? (seconds - t) / 0.4 : 1.0);
        b.setSample (0, i, (float) (0.35 * s * env));
    }
    return b;
}

static juce::AudioBuffer<float> makeBell (double hz, double seconds)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    const double ratios[] { 1.0, 2.76, 5.4, 8.93 }, decays[] { 1.2, 0.6, 0.3, 0.15 };
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += std::exp (-t / decays[k]) * std::sin (2 * juce::MathConstants<double>::pi * hz * ratios[k] * t) / (k + 1);
        b.setSample (0, i, (float) (0.5 * s * juce::jmin (1.0, t / 0.002)));
    }
    return b;
}

static juce::AudioBuffer<float> makePhrase (const std::vector<int>& notes, double noteLen, double detuneCents)
{
    juce::AudioBuffer<float> b (1, (int) (sr * noteLen * notes.size()));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const int k = juce::jmin ((int) notes.size() - 1, (int) (i / (sr * noteLen)));
        const double t = i / sr - k * noteLen;
        const double hz = 440.0 * std::exp2 ((notes[(size_t) k] - 69) / 12.0 + detuneCents / 1200.0);
        ph += hz / sr;
        double s = 0.0;
        for (int h = 1; h < 8; ++h) s += std::sin (2 * juce::MathConstants<double>::pi * h * ph) / (h * h);
        const double env = juce::jmin (1.0, t / 0.01) * juce::jmin (1.0, (noteLen - t) / 0.03);
        b.setSample (0, i, (float) (0.4 * s * env));
    }
    return b;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    outDir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::unique_ptr<NebulaAudioProcessor> proc (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);
    auto& st = proc->state;
    auto set = [&] (const juce::String& id, float v) { auto* p = st.getParameter (id); jassert (p != nullptr); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto get = [&] (const juce::String& id) { return st.getRawParameterValue (id)->load(); };

    // ================= Filtros =================
    {
        juce::Random r (1);
        std::vector<float> noise (48000);
        for (auto& v : noise) v = r.nextFloat() * 2 - 1;

        NebFilter f;
        f.set (FilterKind::LowPass, 0, NebFilter::Clean, 20000.0f, 0.0f, sr);
        f.reset();
        float maxDiff = 0.0f;
        for (int i = 0; i < 48000; ++i) { const float y = f.process (noise[(size_t) i]); if (i > 4800) maxDiff = juce::jmax (maxDiff, std::abs (y - noise[(size_t) i])); }
        check ("Filtro abierto = transparente", maxDiff < 1.0e-3f, juce::String::formatted ("dif max %.5f", maxDiff));

        auto gainAt = [&] (int ch, int slope, float cut, float res, double hz)
        {
            NebFilter fl; fl.set (FilterKind::LowPass, slope, ch, cut, res, sr); fl.reset();
            double e = 0.0, ein = 0.0;
            for (int i = 0; i < 48000; ++i)
            {
                const float x = 0.25f * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * i / sr);
                const float y = fl.process (x);
                if (i > 9600) { e += y * y; ein += x * x; }
            }
            return 10.0 * std::log10 (e / ein);
        };
        const double clean24 = gainAt (0, 1, 2000.0f, 0.0f, 100.0);
        const double moog0 = gainAt (1, 1, 2000.0f, 0.0f, 100.0), moogRes = gainAt (1, 1, 2000.0f, 0.85f, 100.0);
        const double moogAbove = gainAt (1, 1, 1000.0f, 0.0f, 4000.0);
        const double hotRes = gainAt (2, 1, 2000.0f, 0.85f, 100.0);
        check ("Limpio 24 dB: graves intactos", std::abs (clean24) < 0.3, juce::String::formatted ("%.2f dB a 100 Hz", clean24));
        check ("Moog: graves intactos (con y sin reso)", std::abs (moog0) < 1.0 && std::abs (moogRes) < 3.0,
               juce::String::formatted ("reso 0: %.2f dB, reso 85%%: %.2f dB", moog0, moogRes));
        check ("Moog: 24 dB por octava", moogAbove < -40.0, juce::String::formatted ("%.1f dB 2 octavas arriba", moogAbove));
        check ("Moog +: colorea sin perder graves", std::abs (hotRes) < 4.0, juce::String::formatted ("%.2f dB", hotRes));
    }

    // ================= LFO apagado =================
    {
        proc->loadPreset (0);
        set (ParamID::engine, 0); set (ParamID::osc1Wave, Wave::Sine); set (ParamID::oscMix, 0); set (ParamID::subLevel, 0);
        set (ParamID::noise, 0); set (ParamID::uniVoices, 1); set (ParamID::cutoff, 20000); set (ParamID::filterEnv, 0);
        set (ParamID::ampS, 1); set (ParamID::lfoRate, 3.0f); set (ParamID::lfoPitch, 100.0f); set (ParamID::lfoSync, 0);
        auto spread = [&]
        {
            auto o = render (*proc, { { 0.0, 69, true }, { 1.4, 69, false } }, 1.5);
            double lo = 1e9, hi = 0;
            for (double t = 0.2; t < 1.2; t += 0.05) { const double hzv = pitchOf (o, t, 0.04); if (hzv > 0) { lo = juce::jmin (lo, hzv); hi = juce::jmax (hi, hzv); } }
            return 1200.0 * std::log2 (hi / lo);
        };
        set (ParamID::lfoOn, 1); const double on = spread();
        set (ParamID::lfoOn, 0); const double off = spread();
        set (ParamID::lfoOn, 1); set (ParamID::lfoPitch, 0.0f);
        check ("LFO 1 apagado no mueve el tono", on > 100.0 && off < 5.0, juce::String::formatted ("encendido %.0f ct, apagado %.1f ct", on, off));
    }

    // ================= Tono exacto del sample (C# es C#) =================
    {
        // Sample a 240 Hz (A#3 +39 ct): al tocar C#4 debe sonar C#4 exacto
        auto s = makeSquarePad (240.0, 2.0);
        proc->setSample (SampleTools::fromBuffer (s, sr, "pad 240 Hz"));
        proc->useSampleAsInstrument();
        auto o = render (*proc, { { 0.0, 61, true }, { 1.5, 61, false } }, 1.6);
        const double hz = pitchOf (o, 0.6, 0.3);
        const double c = centsOff (hz, 61);
        check ("Sample: tocar C#4 suena C#4", std::abs (c) < 12.0,
               juce::String::formatted ("raiz %s, suena %.1f Hz (%+.0f ct)", SoundProfiler::noteWithCents (proc->sampleSource.get()->rootNote).toRawUTF8(), hz, c));
    }

    // ================= Frase melódica: tonalidad =================
    {
        // Re menor: D F A D C A F E D (desafinada +20 ct)
        auto ph = makePhrase ({ 62, 65, 69, 74, 72, 69, 65, 64, 62, 57, 62 }, 0.35, 20.0);
        writeWav ("v21_frase_dmenor.wav", ph, sr);
        proc->setSample (SampleTools::fromBuffer (ph, sr, "frase"));
        auto sd = proc->sampleSource.get();
        check ("Frase: detecta Re menor", sd->melodic && sd->keyTonic == 2 && sd->keyMinor,
               juce::String::formatted ("melodica %d, tonica %s %s, raiz %s", (int) sd->melodic, SoundProfiler::noteNames (sd->keyTonic),
                                        sd->keyMinor ? "menor" : "mayor", SoundProfiler::noteWithCents (sd->rootNote).toRawUTF8()));
        // Tocar D (la raíz) debe dar la primera nota original pero afinada (sin los +20 ct)
        proc->useSampleAsInstrument();
        const int rootKey = juce::roundToInt (sd->rootNote);
        auto o = render (*proc, { { 0.0, rootKey, true }, { 0.3, rootKey, false } }, 0.4);
        const double c = centsOff (pitchOf (o, 0.08, 0.15), rootKey);
        check ("Frase: la raiz suena afinada", std::abs (c) < 12.0, juce::String::formatted ("%+.0f ct", c));
    }

    // ================= Recrear (resíntesis) =================
    auto recreateCase = [&] (const char* name, juce::AudioBuffer<float> audio, int playNote)
    {
        proc->loadPreset (0);
        proc->setSample (SampleTools::fromBuffer (audio, sr, name));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto res = proc->recreateFromSample();
        if (res == NebulaAudioProcessor::RecreateResult::Imitating)
        {
            while (proc->isImitating()) juce::Thread::sleep (50);
            proc->applyImitationResult();
        }
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        auto& pr = proc->lastProfile;
        const int root = juce::roundToInt (pr.note);
        auto orig = render (*proc, { { 0.0, root, true }, { audio.getNumSamples() / sr * 0.9, root, false } }, audio.getNumSamples() / sr + 0.3);
        auto other = render (*proc, { { 0.0, playNote, true }, { 1.0, playNote, false } }, 1.2);
        double hz = 0.0; int cnt = 0;   // promedio (por si tiene vibrato)
        for (double tt = 0.3; tt < 0.95; tt += 0.02) { const double h = pitchOf (other, tt, 0.04); if (h > 0) { hz += std::log2 (h); ++cnt; } }
        hz = cnt > 0 ? std::exp2 (hz / cnt) : 0.0;
        const double c = centsOff (hz, playNote);
        juce::AudioBuffer<float> both (1, audio.getNumSamples() + orig.getNumSamples());
        both.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
        both.copyFrom (0, audio.getNumSamples(), orig, 0, 0, orig.getNumSamples());
        writeWav (juce::String ("v21_recrear_") + name + ".wav", both, sr);
        std::printf ("   %s: %s, nota %s, parecido %.0f%%, %.2f s, ruido %.0f%%, vibrato %.1f Hz/%.0f ct, tipo: %s\n", name,
                     res == NebulaAudioProcessor::RecreateResult::Resynthesized ? "resintesis" : "imitacion",
                     SoundProfiler::noteWithCents (pr.note).toRawUTF8(), proc->lastSimilarity.load() * 100.0f, secs,
                     pr.noiseRatio * 100.0f, pr.vibratoHz, pr.vibratoCents, Recipe::kindOf (pr).toRawUTF8());
        return std::tuple<NebulaAudioProcessor::RecreateResult, double, float> { res, c, proc->lastSimilarity.load() };
    };
    {
        auto [res, c, sim] = recreateCase ("pluck", makePluck (233.08 * std::exp2 (25.0 / 1200.0), 1.5), 61);
        check ("Recrear pluck: resintesis", res == NebulaAudioProcessor::RecreateResult::Resynthesized && sim > 0.6f, juce::String::formatted ("parecido %.0f%%", sim * 100));
        check ("Recrear pluck: C#4 suena C#4", std::abs (c) < 10.0, juce::String::formatted ("%+.1f ct", c));
        {
            // Los brotes del Jardín a partir del sonido recreado deben sonar
            int silent = 0;
            for (int gen = 0; gen < 3; ++gen)
            {
                proc->garden.mutation = 0.7f;
                proc->garden.germinate();
                for (int k = 0; k < Garden::numSprouts; ++k)
                {
                    proc->garden.selectSprout (k);
                    auto o = render (*proc, { { 0.0, 57, true }, { 0.6, 57, false } }, 0.8);
                    const float pk = peak (o);
                    if (pk < 0.01f || pk >= 1.0f) ++silent;
                }
            }
            check ("Recrear: los brotes del Jardin suenan", silent <= 1, juce::String (silent) + " de 24 mudos o saturados");
            proc->garden.back(); proc->garden.back(); proc->garden.back();
        }
        check ("Recrear pluck: detecta el ruido del ataque", proc->lastProfile.noiseAttackRatio > 0.05f, juce::String::formatted ("%.0f%%", proc->lastProfile.noiseAttackRatio * 100));
    }
    {
        auto [res, c, sim] = recreateCase ("pad_cuadrado", makeSquarePad (196.0, 2.5), 61);
        const auto& pr = proc->lastProfile;
        if (std::getenv ("EXPERIMENT"))
        {
            std::printf ("   sim actual %.3f\n", proc->similarityOfCurrentPatch());
            set (ParamID::lfoPitch, 0.0f);
            std::printf ("   sin vibrato en el sinte %.3f\n", proc->similarityOfCurrentPatch());
            set (ParamID::lfoPitch, pr.vibratoCents); set (ParamID::lfoRetrig, 1.0f);
            std::printf ("   vibrato con reinicio %.3f\n", proc->similarityOfCurrentPatch());
            set (ParamID::lfoRetrig, 0.0f);
        }
        check ("Recrear pad: resintesis + vibrato", res == NebulaAudioProcessor::RecreateResult::Resynthesized && pr.vibratoHz > 4.5f && pr.vibratoHz < 6.5f,
               juce::String::formatted ("vibrato %.1f Hz %.0f ct, parecido %.0f%%", pr.vibratoHz, pr.vibratoCents, sim * 100));
        check ("Recrear pad: reconoce onda cuadrada", Recipe::classicWave (pr) == Wave::Pulse && pr.sustained,
               juce::String::formatted ("pares/impares %.3f, ataque %.2f s", pr.evenOddRatio, pr.attackS));
        check ("Recrear pad: C#4 suena C#4", std::abs (c) < 15.0, juce::String::formatted ("%+.1f ct", c));
        const auto recipe = proc->recipeText();
        check ("Receta completa", recipe.contains ("Que es este sonido") && recipe.contains ("Como lo recreo") && recipe.contains ("cualquier sinte")
                                  && recipe.contains ("Como esta hecho en Nebula") && recipe.contains ("Vibrato"), juce::String (recipe.length()) + " caracteres");
        auto f = outDir.getChildFile ("v21_receta_pad.txt"); f.replaceWithText (recipe);
    }
    {
        auto [res, c, sim] = recreateCase ("campana", makeBell (330.0, 2.0), 64);
        check ("Recrear campana (parciales inarmonicos)", res == NebulaAudioProcessor::RecreateResult::Resynthesized && sim > 0.6f,
               juce::String::formatted ("parecido %.0f%%", sim * 100));
    }

    // ================= Onda desde archivo (arrastrar a Usuario) =================
    {
        auto f = outDir.getChildFile ("v21_onda_origen.wav");
        writeWav ("v21_onda_origen.wav", makeSquarePad (150.0, 1.0), sr);
        proc->loadPreset (0);
        juce::String err;
        const bool ok = proc->makeWavetableFromFile (f, err);
        set (ParamID::oscMix, 0); set (ParamID::subLevel, 0);
        auto o = render (*proc, { { 0.0, 61, true }, { 0.8, 61, false } }, 0.9);
        const double c = centsOff (pitchOf (o, 0.3, 0.2), 61);
        check ("Audio a tabla Usuario", ok && (int) get (ParamID::osc1Wave) == Wave::Table && (int) get (ParamID::osc1Table) == 4 && std::abs (c) < 10.0,
               juce::String::formatted ("%s, C#4 %+.1f ct", err.toRawUTF8(), c));
    }

    // ================= Granos =================
    {
        proc->setSample (SampleTools::fromBuffer (makePluck (220.0, 1.5), sr, "granos"));
        proc->useSampleAsInstrument();
        set (ParamID::smpLoop, 1);
        juce::String det; bool ok = true;
        juce::AudioBuffer<float> all (2, (int) (sr * 1.5) * GrainType::count);
        for (int t = 0; t < GrainType::count; ++t)
        {
            set (ParamID::smpGrainAmt, 0.8f); set (ParamID::smpGrainType, (float) t);
            auto o = render (*proc, { { 0.0, 57, true }, { 1.3, 57, false } }, 1.5);
            const float pk = peak (o);
            ok = ok && pk > 0.02f && pk < 1.0f;
            det << grainTypeNames()[t] << " " << juce::String (pk, 2) << "  ";
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, (int) (sr * 1.5) * t, o, ch, 0, o.getNumSamples());
        }
        writeWav ("v21_granos.wav", all, sr);
        set (ParamID::smpGrainAmt, 0.0f); set (ParamID::smpLoop, 0);
        check ("Granos: los 5 tipos suenan", ok, det);
    }

    // ================= Limpiar =================
    {
        // Conjunto: nota de 440 Hz + golpes de ruido cada 0.5 s + soplido constante
        juce::AudioBuffer<float> mix (1, (int) (sr * 3.0));
        juce::Random r (9);
        for (int i = 0; i < mix.getNumSamples(); ++i)
        {
            const double t = i / sr, ph = std::fmod (t, 0.5);
            double note = 0.0;
            for (int h = 1; h < 6; ++h) note += std::sin (2 * juce::MathConstants<double>::pi * 440.0 * h * t) / h;
            const double hit = (r.nextFloat() * 2 - 1) * std::exp (-ph * 30.0);
            const double hiss = (r.nextFloat() * 2 - 1) * 0.05;
            mix.setSample (0, i, (float) (0.25 * note + 0.5 * hit + hiss));
        }
        writeWav ("v21_limpiar_mezcla.wav", mix, sr);
        auto src = SampleTools::fromBuffer (mix, sr, "conjunto");

        // Medidas: energía de la nota (Goertzel a 440) y energía de los golpes (inicio de cada golpe, quitando la nota)
        auto hfRatio = [&] (const SampleData& s)
        {
            // energía arriba de 4 kHz (la nota no llega ahí: ahí sólo hay golpes y soplido)
            juce::dsp::IIR::Filter<float> hpf (juce::dsp::IIR::Coefficients<float>::makeHighPass (s.sampleRate, 4000.0));
            double hi = 0, tot = 0;
            for (int i = 0; i < s.length(); ++i) { const float x = s.data()[i]; const float y = hpf.processSample (x); hi += y * y; tot += x * x; }
            return hi / (tot + 1e-12);
        };
        auto measure = [&] (const SampleData& s, double& noteE, double& hitE)
        {
            const float* x = s.data(); const int n = s.length();
            double re = 0, im = 0, tot = 0;
            for (int i = 0; i < n; ++i) { const double w = 2 * juce::MathConstants<double>::pi * 440.0 * i / s.sampleRate; re += x[i] * std::cos (w); im += x[i] * std::sin (w); tot += x[i] * x[i]; }
            noteE = (re * re + im * im) / n / (tot + 1e-12);
            double inHits = 0, between = 0;
            for (int i = 0; i < n; ++i) { const double ph = std::fmod (i / s.sampleRate, 0.5); (ph < 0.05 ? inHits : between) += x[i] * x[i]; }
            hitE = inHits / (between + 1e-12);
        };
        double n0, h0; measure (*src, n0, h0);
        juce::String det;
        auto opt = [&] (int k, double& ne, double& he)
        {
            auto o = Cleaner::Job::processOne (*src, k);
            if (o == nullptr) { ne = he = -1; return; }
            measure (*o, ne, he);
            juce::AudioBuffer<float> b (1, o->length()); b.copyFrom (0, 0, o->audio, 0, 0, o->length());
            writeWav ("v21_limpiar_" + Cleaner::names()[k].replaceCharacter (' ', '_') + ".wav", b, sr);
        };
        double nt, ht, nh, hh, nm, hm, nd, hd;
        opt (Cleaner::Tonal, nt, ht); opt (Cleaner::Hits, nh, hh); opt (Cleaner::MainNote, nm, hm); opt (Cleaner::Denoise, nd, hd);
        const double hf0 = hfRatio (*src), hfm = hfRatio (*Cleaner::Job::processOne (*src, Cleaner::MainNote));
        // Un audio sin golpes da ~0.11 (las ventanas de golpe son el 10 % del tiempo); la nota sola llega a ~0.34
        check ("Limpiar: Parte tonal quita golpes", ht < 0.13 && nt > n0, juce::String::formatted ("golpes %.2f -> %.2f, nota %.3f -> %.3f", h0, ht, n0, nt));
        check ("Limpiar: Solo golpes quita la nota", nh < n0 * 0.3 && hh > h0, juce::String::formatted ("nota %.3f -> %.3f, golpes %.2f -> %.2f", n0, nh, h0, hh));
        check ("Limpiar: Nota principal", nm > n0 * 0.95 && hfm < hf0 * 0.12 && hm > 0.08 && hm < 0.13,
               juce::String::formatted ("nota %.3f -> %.3f, agudos (ruido) %.3f -> %.4f, golpes %.2f -> %.2f", n0, nm, hf0, hfm, h0, hm));

        // Historial
        proc->setSample (src);
        proc->startCleaning();
        while (proc->cleaner.isRunning()) juce::Thread::sleep (50);
        int ready = 0; for (int k = 0; k < Cleaner::count; ++k) if (proc->cleaner.result (k) != nullptr) ++ready;
        const bool rescued = proc->rescueCleaned (Cleaner::Tonal);
        const auto afterRescue = proc->sampleSource.get();
        while (proc->cleaner.isRunning()) juce::Thread::sleep (50);
        proc->rescueCleaned (Cleaner::Denoise);
        const size_t histLen = proc->history.size();
        proc->restoreVersion (0);
        const bool backToOriginal = proc->sampleSource.get()->length() == src->length() && proc->historyIndex == 0
                                    && proc->sampleSource.get()->name == "conjunto";
        check ("Limpiar: opciones, rescatar e historial", ready == Cleaner::count && rescued && afterRescue->name.contains ("tonal") && histLen == 3 && backToOriginal,
               juce::String::formatted ("%d opciones, historial %d, volver al original %s", ready, (int) histLen, backToOriginal ? "si" : "no"));

        // El rescatado se guarda dentro del proyecto (no apunta al archivo original)
        proc->restoreVersion (1);
        juce::MemoryBlock mb;
        proc->getStateInformation (mb);
        std::unique_ptr<NebulaAudioProcessor> p2 (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        check ("Limpiar: lo rescatado se guarda en el proyecto", p2->sampleSource.get() != nullptr && p2->sampleSource.get()->name.contains ("tonal"),
               p2->sampleSource.get() != nullptr ? p2->sampleSource.get()->name : juce::String ("(nada)"));
    }

    std::printf ("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    proc.reset();
    return failures == 0 ? 0 : 1;
}
