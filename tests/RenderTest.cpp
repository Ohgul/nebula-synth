// Prueba sin DAW: toca cada preset, guarda un WAV y una captura de la interfaz.
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    constexpr double sr = 48000.0;
    constexpr int block = 512;

    std::unique_ptr<NebulaAudioProcessor> proc (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);

    int failures = 0;
    const auto& presets = factoryPresets();

    for (int p = 0; p < (int) presets.size(); ++p)
    {
        proc->loadPreset (p);

        // Acorde (2 s), luego una melodía corta, luego 3 s de cola
        struct Ev { double t; int note; bool on; };
        std::vector<Ev> evs { { 0.0, 48, true }, { 0.0, 55, true }, { 0.0, 60, true }, { 0.0, 64, true },
                              { 2.0, 48, false }, { 2.0, 55, false }, { 2.0, 60, false }, { 2.0, 64, false } };
        const int mel[] = { 60, 63, 67, 70, 72, 70, 67, 63 };
        for (int i = 0; i < 8; ++i)
        {
            evs.push_back ({ 2.6 + i * 0.25, mel[i] - 12, true });
            evs.push_back ({ 2.6 + i * 0.25 + 0.2, mel[i] - 12, false });
        }

        const int total = (int) (sr * 7.6);
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> buf (2, block);
        float peak = 0.0f;
        bool bad = false;

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            buf.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            for (auto& e : evs)
            {
                const int s = (int) (e.t * sr);
                if (s >= pos && s < pos + n)
                    midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100)
                                        : juce::MidiMessage::noteOff (1, e.note), s - pos);
            }
            proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
            {
                out.copyFrom (ch, pos, buf, ch, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) bad = true;
                    peak = juce::jmax (peak, std::abs (v));
                }
            }
        }

        const float rms = out.getRMSLevel (0, 0, (int) (sr * 2.0));
        const float tail = out.getMagnitude (0, total - (int) (sr * 0.2), (int) (sr * 0.2));
        const bool ok = ! bad && peak > 0.01f && peak < 1.0f;
        if (! ok) ++failures;

        std::printf ("%-16s pico %.3f  rms %.3f  cola %.5f  %s\n", presets[(size_t) p].name, peak, rms, tail, ok ? "OK" : "FALLO");

        auto file = outDir.getChildFile (juce::String (p) + "_" + juce::String (presets[(size_t) p].name).replaceCharacter (' ', '_') + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), sr, 2, 24, {}, 0)))
            writer->writeFromAudioSampleBuffer (out, 0, total);
    }

    // Guardar y restaurar estado
    proc->loadPreset (3);
    juce::MemoryBlock mb;
    proc->getStateInformation (mb);
    proc->loadPreset (0);
    proc->setStateInformation (mb.getData(), (int) mb.getSize());
    const float cut = proc->state.getRawParameterValue (ParamID::cutoff)->load();
    std::printf ("Restaurar estado: corte = %.0f Hz (esperado 3500) %s\n", cut, std::abs (cut - 3500.0f) < 1.0f ? "OK" : "FALLO");
    if (std::abs (cut - 3500.0f) >= 1.0f) ++failures;

    // ---- Jardín: muchas generaciones, ningún brote debe quedar mudo o romperse ----
    auto renderNote = [&] (double seconds, int note, juce::AudioBuffer<float>* keep)
    {
        const int total = (int) (sr * seconds);
        juce::AudioBuffer<float> buf (2, block);
        float pk = 0.0f; bool finite = true;
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            if (pos <= (int) (sr * seconds * 0.6) && (int) (sr * seconds * 0.6) < pos + block)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) (sr * seconds * 0.6) - pos);
            proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    pk = juce::jmax (pk, std::abs (v));
                }
            if (keep != nullptr && pos + block <= keep->getNumSamples())
                for (int ch = 0; ch < 2; ++ch) keep->copyFrom (ch, pos, buf, ch, 0, block);
        }
        // silencio entre notas
        juce::MidiBuffer none; for (int i = 0; i < 400; ++i) proc->processBlock (buf, none);
        return finite ? pk : -1.0f;
    };

    auto& garden = proc->garden;
    juce::Random r (1234);
    int tested = 0, silent = 0, broken = 0, loud = 0;
    juce::AudioBuffer<float> demo (2, (int) (sr * 1.2) * 40);
    demo.clear();
    int demoPos = 0;

    for (int gen = 0; gen < 25; ++gen)
    {
        garden.mutation = 0.3f + 0.7f * r.nextFloat();
        if (gen % 6 == 0) garden.newRandomSeed();
        for (int k = 0; k < Garden::numSprouts; ++k)
        {
            garden.selectSprout (k);
            juce::AudioBuffer<float> one (2, (int) (sr * 1.2));
            one.clear();
            const float pk = renderNote (1.2, 48 + r.nextInt (12), &one);
            ++tested;
            if (pk < 0.0f) ++broken;
            else if (pk < 0.01f)
            {
                ++silent;
                auto& st = proc->state;
                auto v = [&] (const char* id) { return st.getRawParameterValue (id)->load(); };
                std::printf ("  mudo: pk %.4f tipo %.0f corte %.0f env %.2f amp A %.2f D %.2f S %.2f oct %.0f mix %.2f\n",
                             pk, v (ParamID::filterType), v (ParamID::cutoff), v (ParamID::filterEnv),
                             v (ParamID::ampA), v (ParamID::ampD), v (ParamID::ampS), v (ParamID::osc1Octave), v (ParamID::oscMix));
            }
            else if (pk >= 1.0f) ++loud;
            if (gen < 5 && demoPos + one.getNumSamples() <= demo.getNumSamples())
            {
                for (int ch = 0; ch < 2; ++ch) demo.copyFrom (ch, demoPos, one, ch, 0, one.getNumSamples());
                demoPos += one.getNumSamples();
            }
        }
        garden.plant (r.nextInt (Garden::numSprouts));
    }
    while (garden.back()) {}
    std::printf ("Jardin: %d brotes probados, %d mudos, %d saturados, %d rotos (gen %d)\n", tested, silent, loud, broken, garden.generation);
    if (broken > 0 || silent > tested / 10) ++failures;
    {
        auto file = outDir.getChildFile ("jardin_demo.wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.release(), sr, 2, 24, {}, 0)))
            w->writeFromAudioSampleBuffer (demo, 0, demoPos);
    }

    // Estado del jardín se guarda y recupera
    {
        garden.germinate();
        garden.selectSprout (5);
        const auto before = garden.sprouts[3].genes;
        juce::MemoryBlock m2;
        proc->getStateInformation (m2);
        garden.germinate();
        proc->setStateInformation (m2.getData(), (int) m2.getSize());
        bool same = garden.sprouts[3].genes.size() == before.size();
        for (size_t i = 0; same && i < before.size(); ++i) same = std::abs (before[i] - garden.sprouts[3].genes[i]) < 1.0e-3f;
        std::printf ("Jardin guardado/restaurado: %s\n", same ? "OK" : "FALLO");
        if (! same) ++failures;
    }

    proc->releaseResources();
    std::printf ("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
