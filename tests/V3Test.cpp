// Pruebas de Nebula 3: resíntesis nueva (parecido con muchos tipos de sonido), afinado a C, A/B,
// efectos, macros, encendidos, oversampling, MPE y CPU.
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

static void writeWav (const juce::String& name, const juce::AudioBuffer<float>& b)
{
    auto f = outDir.getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), sr, (unsigned) b.getNumChannels(), 24, {}, 0)))
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
    for (int i = 0; i < 300; ++i) p.processBlock (buf, none);
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

static double avgPitch (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    double acc = 0.0; int cnt = 0;
    for (double t = t0; t < t1; t += 0.02)
    {
        const double h = SampleTools::detectPitch (b.getReadPointer (0) + (int) (t * sr), (int) (0.04 * sr), sr);
        if (h > 0) { acc += std::log2 (h); ++cnt; }
    }
    return cnt > 0 ? std::exp2 (acc / cnt) : 0.0;
}
static double centsOff (double hz, int note) { return 1200.0 * std::log2 (hz / (440.0 * std::exp2 ((note - 69) / 12.0))); }

// ---- Sonidos de prueba ----
using Gen = std::function<double (double t, juce::Random&)>;
static juce::AudioBuffer<float> make (double seconds, Gen g, bool stereo = false)
{
    juce::AudioBuffer<float> b (stereo ? 2 : 1, (int) (sr * seconds));
    juce::Random r (11);
    juce::Random r2 (12);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double v = g (i / sr, r);
        b.setSample (0, i, (float) v);
        if (stereo) b.setSample (1, i, (float) (0.7 * v + 0.3 * g (i / sr + 0.0007, r2)));
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

    constexpr double pi2 = juce::MathConstants<double>::twoPi;

    // ================= Recrear: muchos tipos de sonido =================
    struct Case { const char* name; juce::AudioBuffer<float> audio; float minSim; bool pitched; };
    std::vector<Case> cases;
    cases.push_back ({ "pluck_sierra", make (1.5, [&] (double t, juce::Random& r) {
        double s = 0; const double hz = 233.08 * std::exp2 (25.0 / 1200.0);
        for (int k = 1; k < 40 && k * hz < 20000; ++k) s += std::sin (pi2 * k * hz * t) / k * std::exp (-t * (1.5 + 0.4 * k));
        return 0.4 * (s * juce::jmin (1.0, t / 0.003) + (r.nextFloat() * 2 - 1) * 0.5 * std::exp (-t * 40.0)); }), 0.80f, true });
    cases.push_back ({ "pad_cuadrado_vibrato", make (2.5, [&] (double t, juce::Random&) {
        const double ph = 196.0 * t + 196.0 * (20.0 / 1200.0 * std::log (2.0)) * (-std::cos (pi2 * 5.5 * t) / (pi2 * 5.5)) * 1.0;
        double s = 0; for (int k = 1; k < 30; k += 2) s += std::sin (pi2 * k * ph) / k;
        return 0.35 * s * juce::jmin (1.0, t / 0.3) * (t > 2.1 ? (2.5 - t) / 0.4 : 1.0); }), 0.80f, true });
    cases.push_back ({ "campana", make (2.0, [&] (double t, juce::Random&) {
        const double ratios[] { 1.0, 2.76, 5.4, 8.93 }, decays[] { 1.2, 0.6, 0.3, 0.15 };
        double s = 0; for (int k = 0; k < 4; ++k) s += std::exp (-t / decays[k]) * std::sin (pi2 * 330.0 * ratios[k] * t) / (k + 1);
        return 0.5 * s * juce::jmin (1.0, t / 0.002); }), 0.70f, true });
    cases.push_back ({ "golpe_ruido", make (0.6, [&] (double t, juce::Random& r) {
        static double lp = 0; lp += 0.3 * ((r.nextFloat() * 2 - 1) - lp); return lp * std::exp (-t / 0.12); }), 0.70f, false });
    cases.push_back ({ "bombo_808", make (1.2, [&] (double t, juce::Random&) {
        static double ph = 0; const double f = 45.0 + 120.0 * std::exp (-t * 30.0); ph += f / sr;
        return 0.8 * std::sin (pi2 * ph) * std::exp (-t * 2.5) * juce::jmin (1.0, t / 0.001); }), 0.70f, false });
    cases.push_back ({ "voz_a", make (1.8, [&] (double t, juce::Random& r) {
        double s = 0; const double f = 220.0 * (1.0 + 0.004 * std::sin (pi2 * 5.0 * t));
        for (int h = 1; h < 30; ++h)
        {
            const double fh = h * f;
            s += (1.0 / h) * (1.0 + 4.0 * std::exp (-std::pow ((fh - 800.0) / 150.0, 2)) + 3.0 * std::exp (-std::pow ((fh - 1200.0) / 200.0, 2))) * std::sin (pi2 * h * 220.0 * t + 0.02 * h * std::sin (pi2 * 5.0 * t));
        }
        return 0.15 * s * juce::jmin (1.0, t / 0.05) * (t > 1.6 ? (1.8 - t) / 0.2 : 1.0) + (r.nextFloat() * 2 - 1) * 0.01; }), 0.80f, true });
    cases.push_back ({ "pad_estereo", make (2.0, [&] (double t, juce::Random&) {
        double s = 0; for (int k = 1; k < 20; ++k) s += std::sin (pi2 * k * 130.81 * t * (1.0 + 0.002 * (k % 3))) / k;
        return 0.25 * s * juce::jmin (1.0, t / 0.4) * (t > 1.6 ? (2.0 - t) / 0.4 : 1.0); }, true), 0.75f, true });

    juce::AudioBuffer<float> demo (2, 1);
    std::vector<juce::AudioBuffer<float>> demos;
    for (auto& c : cases)
    {
        proc->loadPreset (0);
        proc->setSample (SampleTools::fromBuffer (c.audio, sr, c.name));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto res = proc->recreateFromSample();
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        const float sim = proc->lastSimilarity.load();
        auto m = proc->resynthModel();
        const int home = proc->homeKey();
        // El original tocado en su raíz exacta vs. lo recreado en la tecla C más cercana (para el demo)
        const double len = c.audio.getNumSamples() / sr;
        auto rec = render (*proc, { { 0.0, home, true }, { len * 0.9, home, false } }, len + 0.3);
        juce::AudioBuffer<float> pair (2, c.audio.getNumSamples() + rec.getNumSamples() + (int) (0.3 * sr));
        pair.clear();
        for (int ch = 0; ch < 2; ++ch) pair.copyFrom (ch, 0, c.audio, juce::jmin (ch, c.audio.getNumChannels() - 1), 0, c.audio.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) pair.copyFrom (ch, c.audio.getNumSamples() + (int) (0.3 * sr), rec, ch, 0, rec.getNumSamples());
        writeWav (juce::String ("v3_recrear_") + c.name + ".wav", pair);
        demos.push_back (pair);

        juce::String det = juce::String::formatted ("parecido %.0f%%, %.2f s, %s armonica, %d parciales, ruido %s, raiz %s, C=%d",
            sim * 100.0f, secs, m && m->harmonic ? "con" : "sin", m ? (int) m->partials.size() : 0, m && ! m->noise.empty() ? "si" : "no",
            m ? SoundProfiler::noteWithCents (m->rootNote).toRawUTF8() : "?", home);
        check ((juce::String ("Recrear ") + c.name).toRawUTF8(), res == NebulaAudioProcessor::RecreateResult::Resynthesized && sim >= c.minSim
                                                                  && peak (rec) > 0.01f && peak (rec) < 1.0f, det);
    }
    {
        int total = 0; for (auto& d : demos) total += d.getNumSamples() + (int) (0.5 * sr);
        juce::AudioBuffer<float> all (2, total); all.clear();
        int pos = 0; for (auto& d : demos) { for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, d, ch, 0, d.getNumSamples()); pos += d.getNumSamples() + (int) (0.5 * sr); }
        writeWav ("v3_demo_original_y_recreado.wav", all);
    }

    // ================= Afinado a C =================
    {
        // pad a 240 Hz (B3 -49 ct): la tecla C (la más cercana) debe sonar C exacto
        auto s = cases[1].audio;   // pad cuadrado a 196 Hz (G3)
        proc->setSample (SampleTools::fromBuffer (make (1.5, [&] (double t, juce::Random&) {
            double v = 0; for (int k = 1; k < 20; k += 2) v += std::sin (pi2 * k * 240.0 * t) / k; return 0.3 * v * juce::jmin (1.0, t / 0.05); }), sr, "pad240"));
        proc->recreateFromSample();
        const int home = proc->homeKey();
        auto o = render (*proc, { { 0.0, home, true }, { 1.2, home, false } }, 1.3);
        const double c = centsOff (avgPitch (o, 0.2, 1.0), home);
        auto o2 = render (*proc, { { 0.0, home + 7, true }, { 1.2, home + 7, false } }, 1.3);
        const double c2 = centsOff (avgPitch (o2, 0.2, 1.0), home + 7);
        check ("Afinado a C: tecla C suena C, G suena G", home % 12 == 0 && std::abs (c) < 8.0 && std::abs (c2) < 8.0,
               juce::String::formatted ("tecla %d: %+.1f ct, +7: %+.1f ct", home, c, c2));
    }

    // ================= A/B al mismo volumen =================
    {
        const int home = proc->homeKey();
        proc->setAB (false);
        auto a = render (*proc, { { 0.0, home, true }, { 1.0, home, false } }, 1.2);
        proc->setAB (true);
        auto b = render (*proc, { { 0.0, home, true }, { 1.0, home, false } }, 1.2);
        proc->setAB (false);
        const float ra = a.getRMSLevel (0, (int) (0.1 * sr), (int) (0.8 * sr)), rb = b.getRMSLevel (0, (int) (0.1 * sr), (int) (0.8 * sr));
        const double db = 20.0 * std::log10 (ra / juce::jmax (1e-9f, rb));
        check ("A/B: mismo volumen (+/-2 dB)", std::abs (db) < 2.0, juce::String::formatted ("recreado vs original %+.1f dB", db));
    }

    // ================= Efectos =================
    {
        proc->loadPreset (3);
        juce::String det; bool ok = true;
        const char* fxOn[] { ParamID::fxEqOn, ParamID::fxCompOn, ParamID::fxChorusOn, ParamID::fxDelayOn, ParamID::fxRevOn };
        const char* fxName[] { "EQ", "Comp", "Chorus", "Delay", "Reverb" };
        auto dry = render (*proc, { { 0.0, 60, true }, { 0.3, 60, false } }, 1.5);
        for (int k = 0; k < 5; ++k)
        {
            for (auto* id : fxOn) set (id, 0.0f);
            set (fxOn[k], 1.0f);
            if (k == 0) { set (ParamID::fxEqHigh, 8.0f); }
            auto o = render (*proc, { { 0.0, 60, true }, { 0.3, 60, false } }, 1.5);
            const float pk = peak (o);
            const float tail = o.getRMSLevel (0, (int) (0.9 * sr), (int) (0.5 * sr)), dryTail = dry.getRMSLevel (0, (int) (0.9 * sr), (int) (0.5 * sr));
            double diff = 0; for (int i = 0; i < o.getNumSamples(); ++i) diff += std::abs (o.getSample (0, i) - dry.getSample (0, i));
            const bool good = pk > 0.01f && pk < 1.0f && diff > 1.0 && (k < 3 || tail > dryTail * 3.0f + 1e-4f);
            ok = ok && good;
            det << fxName[k] << (good ? " ok  " : " MAL  ");
        }
        for (auto* id : fxOn) set (id, 0.0f);
        set (ParamID::fxEqHigh, 0.0f);
        check ("Efectos: EQ, compresor, chorus, delay, reverb", ok, det);
    }

    // ================= Macros =================
    {
        proc->loadPreset (3);
        auto centroid = [&] (const juce::AudioBuffer<float>& b)   // % de energía arriba de 2 kHz
        {
            juce::dsp::IIR::Filter<float> hp (juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 2000.0));
            double hi = 0, tot = 1e-12;
            for (int i = 0; i < (int) (0.7 * sr); ++i) { const float x = b.getSample (0, i); const float y = hp.processSample (x); hi += y * y; tot += x * x; }
            return 100.0 * hi / tot;
        };
        auto base = render (*proc, { { 0.0, 48, true }, { 0.8, 48, false } }, 1.0);
        set (ParamID::macroBright, 1.0f);
        auto bright = render (*proc, { { 0.0, 48, true }, { 0.8, 48, false } }, 1.0);
        set (ParamID::macroBright, -1.0f);
        auto dark = render (*proc, { { 0.0, 48, true }, { 0.8, 48, false } }, 1.0);
        set (ParamID::macroBright, 0.0f);
        set (ParamID::macroSpace, 1.0f);
        auto space = render (*proc, { { 0.0, 48, true }, { 0.3, 48, false } }, 1.6);
        set (ParamID::macroSpace, 0.0f);
        set (ParamID::macroChaos, 1.0f);
        auto chaos = render (*proc, { { 0.0, 48, true }, { 0.8, 48, false } }, 1.0);
        set (ParamID::macroChaos, 0.0f);
        const float spaceTail = space.getRMSLevel (0, (int) (1.0 * sr), (int) (0.5 * sr));
        check ("Macros: Brillo, Espacio y Locura hacen algo", centroid (bright) > centroid (base) * 1.15 && centroid (dark) < centroid (base) * 0.9
                                                              && spaceTail > 1e-3f && peak (chaos) < 1.0f && peak (chaos) > 0.01f,
               juce::String::formatted ("agudos %.1f/%.1f/%.1f %%, cola espacio %.4f", centroid (dark), centroid (base), centroid (bright), spaceTail));
    }

    // ================= Encendidos =================
    {
        proc->loadPreset (3);
        set (ParamID::osc1On, 0.0f); set (ParamID::osc2On, 0.0f); set (ParamID::subOn, 0.0f); set (ParamID::noiseOn, 0.0f);
        auto silent = render (*proc, { { 0.0, 48, true }, { 0.5, 48, false } }, 0.6);
        set (ParamID::osc1On, 1.0f);
        auto one = render (*proc, { { 0.0, 48, true }, { 0.5, 48, false } }, 0.6);
        for (auto* id : { ParamID::osc2On, ParamID::subOn, ParamID::noiseOn }) set (id, 1.0f);
        set (ParamID::f1On, 0.0f);
        auto nof = render (*proc, { { 0.0, 48, true }, { 0.5, 48, false } }, 0.6);
        set (ParamID::f1On, 1.0f);
        check ("Botones de encendido", peak (silent) < 1e-4f && peak (one) > 0.01f && peak (nof) > 0.01f,
               juce::String::formatted ("todo apagado %.5f, osc1 %.3f, sin filtro %.3f", peak (silent), peak (one), peak (nof)));
    }

    // ================= Oversampling =================
    {
        proc->loadPreset (0);
        set (ParamID::osc1Wave, Wave::Saw); set (ParamID::oscMix, 0.0f); set (ParamID::cutoff, 20000.0f); set (ParamID::filterEnv, 0.0f);
        set (ParamID::drive, 1.0f); set (ParamID::subLevel, 0.0f); set (ParamID::noise, 0.0f);
        // Aliasing: energía por debajo de la fundamental en una nota muy aguda
        auto alias = [&]
        {
            auto o = render (*proc, { { 0.0, 100, true }, { 0.6, 100, false } }, 0.6);
            juce::dsp::FFT fft (14); std::vector<float> b (32768, 0.0f);
            for (int i = 0; i < 16384; ++i) b[(size_t) i] = o.getSample (0, (int) (0.1 * sr) + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / 16384));
            fft.performFrequencyOnlyForwardTransform (b.data());
            double lo = 0, tot = 1e-12;
            for (int k = 1; k < 8192; ++k) { const double hz = k * sr / 16384.0, e = (double) b[(size_t) k] * b[(size_t) k]; tot += e; if (hz > 100 && hz < 2000) lo += e; }
            return 10.0 * std::log10 (lo / tot + 1e-12);   // todo lo que hay debajo de la fundamental (2637 Hz) es aliasing
        };
        if (std::getenv ("DBG_OS"))
        {
            for (int q = 0; q < 3; ++q)
            {
                set (ParamID::quality, (float) q);
                auto o = render (*proc, { { 0.0, 100, true }, { 0.6, 100, false } }, 0.6);
                juce::dsp::FFT fft (13); std::vector<float> b (16384, 0.0f);
                for (int i = 0; i < 8192; ++i) b[(size_t) i] = o.getSample (0, (int) (0.1 * sr) + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / 8192));
                fft.performFrequencyOnlyForwardTransform (b.data());
                std::printf ("q%d:", q);
                for (int k = 0; k < 4096; k += 128) { float mx = 0; for (int j = k; j < k + 128; ++j) mx = juce::jmax (mx, b[(size_t) j]); std::printf (" %.0f", 20 * std::log10 (mx + 1e-9)); }
                int am = 0; for (int j = 0; j < 256; ++j) if (b[(size_t) j] > b[(size_t) am]) am = j;
                std::printf ("  [pico bajo %.1f Hz %.0f dB]", am * sr / 8192.0, 20 * std::log10 (b[(size_t) am] + 1e-9));
                std::printf ("\n");
            }
        }
        set (ParamID::quality, 0.0f); const double a1 = alias();
        set (ParamID::quality, 1.0f); const double a2 = alias();
        set (ParamID::quality, 2.0f); const double a4 = alias();
        set (ParamID::quality, 0.0f);
        check ("Oversampling baja el aliasing", a2 < a1 - 2.0 && a4 < a1 - 10.0, juce::String::formatted ("aliasing 1x %.1f dB, 2x %.1f dB, 4x %.1f dB", a1, a2, a4));
    }

    // ================= MPE =================
    {
        proc->loadPreset (0);
        set (ParamID::osc1Wave, Wave::Sine); set (ParamID::oscMix, 0.0f); set (ParamID::subLevel, 0.0f); set (ParamID::noise, 0.0f);
        set (ParamID::cutoff, 20000.0f); set (ParamID::mpeOn, 1.0f);
        juce::AudioBuffer<float> out (2, (int) (sr * 1.0)), buf (2, block);
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::pitchWheel (2, 8192 + 8192 * 12 / 48), 1);   // +12 semitonos en el canal 2
            }
            proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, juce::jmin (block, out.getNumSamples() - pos));
        }
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (2, 60), 0); proc->processBlock (buf, off);
        set (ParamID::mpeOn, 0.0f);
        const double hz = avgPitch (out, 0.3, 0.8);
        check ("MPE: bend por nota de 48 semitonos", std::abs (centsOff (hz, 72)) < 15.0, juce::String::formatted ("%.1f Hz (esperado 523)", hz));
    }

    // ================= Guardar y cargar el modelo =================
    {
        proc->setSample (SampleTools::fromBuffer (cases[2].audio, sr, "campana"));
        proc->recreateFromSample();
        auto a = render (*proc, { { 0.0, 60, true }, { 0.8, 60, false } }, 1.0);
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        std::unique_ptr<NebulaAudioProcessor> p2 (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
        p2->setPlayConfigDetails (0, 2, sr, block); p2->prepareToPlay (sr, block);
        p2->setStateInformation (mb.getData(), (int) mb.getSize());
        auto b = render (*p2, { { 0.0, 60, true }, { 0.8, 60, false } }, 1.0);
        const float ra = a.getRMSLevel (0, 0, a.getNumSamples()), rb = b.getRMSLevel (0, 0, b.getNumSamples());
        check ("Proyecto guarda el sonido recreado", p2->resynthModel() != nullptr && std::abs (20 * std::log10 (rb / juce::jmax (1e-9f, ra))) < 1.0,
               juce::String::formatted ("%.1f KB, nivel %.3f / %.3f", mb.getSize() / 1024.0, ra, rb));
    }

    // ================= Exportar, deshacer, cruzar y mezclar =================
    {
        // Frase: D F A D (4 notas) -> MIDI
        auto phrase = make (1.6, [&] (double t, juce::Random&) {
            const int notes[] { 62, 65, 69, 74 }; const int k = juce::jmin (3, (int) (t / 0.4)); const double tt = t - k * 0.4;
            const double hz = 440.0 * std::exp2 ((notes[k] - 69) / 12.0);
            return 0.4 * std::sin (pi2 * hz * t) * juce::jmin (1.0, tt / 0.01) * juce::jmin (1.0, (0.4 - tt) / 0.03); });
        proc->setSample (SampleTools::fromBuffer (phrase, sr, "frase4"));
        proc->setRegion (0.0f, 1.0f);
        const auto midiFile = proc->exportMidi();
        juce::MidiFile mf; int count = 0; juce::String pitches;
        if (auto in = midiFile.createInputStream(); in != nullptr && mf.readFrom (*in))
            for (auto* ev : *mf.getTrack (0)) if (ev->message.isNoteOn()) { ++count; pitches << ev->message.getNoteNumber() << " "; }
        check ("Exportar MIDI de una frase", count == 4 && pitches.trim() == "62 65 69 74", pitches);

        proc->recreateFromSample();
        const auto audioFile = proc->exportAudio();
        const auto waveFile = proc->exportWavetable();
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (audioFile));
        float mx = 0.0f;
        if (rd != nullptr) { juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples); rd->read (&b, 0, b.getNumSamples(), 0, true, true); mx = b.getMagnitude (0, b.getNumSamples()); }
        check ("Exportar audio y onda", mx > 0.05f && waveFile.existsAsFile() && waveFile.getSize() > 10000,
               audioFile.getFileName() + " | " + waveFile.getFileName());

        // Deshacer
        proc->undoManager.beginNewTransaction();
        const float before = st.getRawParameterValue (ParamID::cutoff)->load();
        set (ParamID::cutoff, 300.0f);
        // (la interfaz copia los parámetros al árbol con su temporizador; aquí lo forzamos)
        juce::MemoryBlock dummy; proc->getStateInformation (dummy);
        proc->undoManager.beginNewTransaction();
        proc->undoManager.undo();
        const float after = st.getRawParameterValue (ParamID::cutoff)->load();
        check ("Deshacer", std::abs (after - before) < 1.0f, juce::String::formatted ("corte %.0f -> 300 -> %.0f", before, after));

        // Cruzar y mezclar en el cerebro
        proc->loadPreset (3);
        proc->garden.germinate();
        const int gen = proc->garden.generation;
        proc->garden.cross (1, 5);
        const bool crossed = proc->garden.generation == gen + 1;
        auto& gd = proc->garden;
        gd.blend (2, 0.0f);  const float d0 = GenomeOps::distance (gd.seed, gd.capture());
        gd.blend (2, 0.5f);  const float dm = GenomeOps::distance (gd.seed, gd.capture());
        gd.blend (2, 1.0f);  const float d1 = GenomeOps::distance (gd.seed, gd.capture());
        check ("Cerebro: cruzar y mezclar", crossed && d0 < 0.01f && dm > d0 && dm < d1,
               juce::String::formatted ("distancia al nucleo: 0%% %.3f, 50%% %.3f, 100%% %.3f", d0, dm, d1));
    }

    // ================= CPU =================
    {
        proc->setSample (SampleTools::fromBuffer (cases[0].audio, sr, "cpu"));
        proc->recreateFromSample();
        set (ParamID::fxRevOn, 1.0f); set (ParamID::fxDelayOn, 1.0f); set (ParamID::fxChorusOn, 1.0f);
        std::vector<Ev> evs; for (int k = 0; k < 8; ++k) evs.push_back ({ 0.0, 48 + k * 3, true });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (*proc, evs, 6.0);
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 6000.0 * 100.0;
        std::vector<Ev> offs; for (auto e : evs) offs.push_back ({ 0.0, e.note, false }); render (*proc, offs, 0.1);
        set (ParamID::quality, 1.0f);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        render (*proc, evs, 6.0);
        const double cpu2 = (juce::Time::getMillisecondCounterHiRes() - t1) / 6000.0 * 100.0;
        render (*proc, offs, 0.1);
        set (ParamID::quality, 0.0f);
        // campana (parciales) también
        proc->setSample (SampleTools::fromBuffer (cases[2].audio, sr, "cpu2"));
        proc->recreateFromSample();
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        render (*proc, evs, 6.0);
        const double cpu3 = (juce::Time::getMillisecondCounterHiRes() - t2) / 6000.0 * 100.0;
        render (*proc, offs, 0.1);
        check ("CPU: 8 notas resintesis + efectos", cpu < 60.0 && cpu3 < 60.0,
               juce::String::formatted ("armonico %.1f %% (2x: %.1f %%), parciales %.1f %% de un nucleo", cpu, cpu2, cpu3));
    }

    std::printf ("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    proc.reset();
    return failures == 0 ? 0 : 1;
}
