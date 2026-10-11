// Capturas de la interfaz v2 (todas las pestañas, ventanas y la animación del Jardín).
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestEntry.h"
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {   // todo lo de esta prueba es local (permite juntar todas en NebulaTests)

static constexpr double sr = 48000.0;
static constexpr int block = 512;

} // namespace

int NEBULA_TEST_ENTRY (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::unique_ptr<NebulaAudioProcessor> proc (dynamic_cast<NebulaAudioProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);
    proc->loadPreset (1);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setVisible (true);   // necesario para que los temporizadores y las capturas funcionen igual que en el DAW

    std::function<void (juce::Component*, std::function<void (juce::Component*)>)> walk =
        [&walk] (juce::Component* c, std::function<void (juce::Component*)> fn) { fn (c); for (auto* ch : c->getChildren()) walk (ch, fn); };
    auto visible = [&] (juce::Component* c) { for (; c != nullptr && c != ed.get(); c = c->getParentComponent()) if (! c->isVisible()) return false; return true; };
    auto find = [&] (const juce::String& caption) -> juce::Button*
    {
        juce::Button* found = nullptr;
        walk (ed.get(), [&] (juce::Component* c) { if (auto* b = dynamic_cast<juce::Button*> (c); b && b->getButtonText() == caption && visible (b)) found = b; });
        if (found == nullptr) std::printf ("  (no se encontro el boton \"%s\")\n", caption.toRawUTF8());
        return found;
    };
    auto click = [&] (const juce::String& caption) { if (auto* b = find (caption)) { if (b->onClick) b->onClick(); else b->triggerClick(); } };
    auto first = [&] (auto* dummy) { using T = std::remove_pointer_t<decltype (dummy)>; T* f = nullptr; walk (ed.get(), [&] (juce::Component* c) { if (auto* x = dynamic_cast<T*> (c)) if (f == nullptr) f = x; }); return f; };

    // Hace correr la interfaz y el audio a la vez (como en el DAW) durante `ms`
    juce::MidiBuffer pending;
    juce::AudioBuffer<float> buf (2, block);
    auto pump = [&] (int ms)
    {
        const double end = juce::Time::getMillisecondCounterHiRes() + ms;
        while (juce::Time::getMillisecondCounterHiRes() < end)
        {
            for (int i = 0; i < 2; ++i)   // ~21 ms de audio por vuelta
            {
                proc->keyboardState.processNextMidiBuffer (pending, 0, block, true);
                proc->processBlock (buf, pending);
                pending.clear();
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
    };
    auto noteOn  = [&] (int n) { proc->keyboardState.noteOn (1, n, 0.8f); };
    auto noteOff = [&] (int n) { proc->keyboardState.noteOff (1, n, 0.0f); };

    auto shot = [&] (float scale = 1.0f) { return ed->createComponentSnapshot (ed->getLocalBounds(), true, scale); };
    auto save = [&] (const juce::Image& img, const char* name)
    {
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat().writeImageToStream (img, fos);
        std::printf ("captura: %s (%dx%d)\n", name, img.getWidth(), img.getHeight());
    };

    // --- Jardín: animación al germinar ---
    noteOn (48); noteOn (55);
    pump (400);
    proc->garden.selected = -1; pump (60); click ("Evolucionar");
    {
        const int times[] { 0, 90, 180, 300, 600, 1100 };
        std::vector<juce::Image> frames;
        int last = 0;
        for (int t : times) { pump (t - last); last = t; frames.push_back (shot (0.5f)); }
        const int w = frames[0].getWidth(), h = frames[0].getHeight();
        juce::Image strip (juce::Image::RGB, w * 3, h * 2, true);
        juce::Graphics g (strip);
        for (int i = 0; i < 6; ++i)
        {
            g.drawImageAt (frames[(size_t) i], (i % 3) * w, (i / 3) * h);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (juce::String (times[i]) + " ms", (i % 3) * w + 10, (i / 3) * h + h - 28, 120, 20, juce::Justification::left);
        }
        save (strip, "ui_animacion_germinar.png");
    }
    save (shot (1.0f), "ui_jardin.png");

    // Plantar un brote: la semilla viaja al centro
    if (auto* canvas = first ((GardenCanvas*) nullptr))
    {
        auto c = canvas->getLocalBounds().getCentre().toFloat();
        proc->garden.selectSprout (2);
        pump (300);
        juce::ignoreUnused (c);
    }
    click ("Usar esta");
    pump (150);
    save (shot (1.0f), "ui_jardin_plantando.png");
    pump (1200);
    noteOff (48); noteOff (55);
    pump (200);

    // Jardín con un sample cargado
    {
        juce::AudioBuffer<float> s (1, (int) (sr * 6.0));
        for (int i = 0; i < s.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double env = t < 1.0 ? 0.0 : std::exp (-std::fmod (t - 1.0, 0.75) * 3.0);   // 1 s de silencio y luego golpes
            double v = 0; for (int h = 1; h < 8; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * 196 * h * t) / h;
            s.setSample (0, i, (float) (0.4 * v * env));
        }
        proc->setSample (SampleTools::fromBuffer (s, sr, "Guitarra larga"));
        pump (300);
        save (shot (1.0f), "ui_jardin_sample.png");

        // Ventana para elegir la sección
        click ("Seccion");
        pump (300);
        save (shot (1.0f), "ui_elegir_seccion.png");
        click ("Listo");
        pump (100);

        // Recrear (resíntesis) y la receta
        click ("RECREAR");
        pump (1300);
        save (shot (1.0f), "ui_recreado.png");
        click ("Como se hizo?");
        pump (300);
        save (shot (1.0f), "ui_receta.png");
        click ("Cerrar");
        pump (100);

        // Limpiar: opciones + historial
        click ("Limpiar");
        for (int k = 0; k < 300 && proc->cleaner.isRunning(); ++k) pump (100);
        pump (300);
        save (shot (1.0f), "ui_limpiar.png");
        if (auto* card = first ((CleanCard*) nullptr)) juce::ignoreUnused (card);
        proc->rescueCleaned (Cleaner::Tonal);
        for (int k = 0; k < 300 && proc->cleaner.isRunning(); ++k) pump (100);
        pump (300);
        save (shot (1.0f), "ui_limpiar_historial.png");
        if (auto* cp = first ((CleanerPanel*) nullptr)) cp->setVisible (false);
        pump (100);

        // Editar en modo Resíntesis (lo que dejó RECREAR)
        click ("Editar");
        pump (300);
        save (shot (1.0f), "ui_editar_resintesis.png");
        click ("Cerebro");
        pump (100);
        { auto* p = proc->state.getParameter (ParamID::engine); p->setValueNotifyingHost (p->convertTo0to1 (1.0f)); }
        { auto* p = proc->state.getParameter (ParamID::smpGrainAmt); p->setValueNotifyingHost (0.4f); }
        pump (200);
    }

    // --- Editar en modo sample ---
    click ("Editar");
    pump (300);
    save (shot (1.0f), "ui_editar_sample.png");

    // --- Editar en modo sinte ---
    { auto* p = proc->state.getParameter (ParamID::engine); p->setValueNotifyingHost (0.0f); }
    proc->loadPreset (5);
    pump (300);
    save (shot (1.0f), "ui_editar.png");

    // Arrastrando un audio sobre el oscilador 1
    if (auto* ev = first ((EditView*) nullptr))
    {
        const auto c = ev->getLocalBounds().getTopLeft() + juce::Point<int> (150, 150);
        ev->fileDragMove ({ "x.wav" }, c.x, c.y);
        pump (100);
        save (shot (1.0f), "ui_arrastrar_onda.png");
        ev->fileDragExit ({});
    }

    // Dibujar onda
    if (auto* d = first ((DrawPanel*) nullptr)) { d->setVisible (true); d->toFront (false); }
    pump (200);
    save (shot (1.0f), "ui_dibujar_onda.png");
    if (auto* d = first ((DrawPanel*) nullptr)) d->setVisible (false);

    // --- Modulación ---
    click ("Modulacion");
    { auto* p = proc->state.getParameter (ParamID::lfo2On); p->setValueNotifyingHost (0.0f); }
    noteOn (60);
    pump (500);
    save (shot (1.0f), "ui_modulacion.png");
    noteOff (60);

    // --- Efectos ---
    click ("Efectos");
    { auto* p = proc->state.getParameter (ParamID::fxRevOn); p->setValueNotifyingHost (1.0f); }
    { auto* p = proc->state.getParameter (ParamID::fxDelayOn); p->setValueNotifyingHost (1.0f); }
    pump (300);
    save (shot (1.0f), "ui_efectos.png");

    // --- Locura: la interfaz enloquece ---
    click ("Cerebro");
    { auto* p = proc->state.getParameter (ParamID::macroChaos); p->setValueNotifyingHost (0.9f); }
    noteOn (48); noteOn (55);
    pump (1500);
    save (shot (1.0f), "ui_cerebro_locura.png");
    noteOff (48); noteOff (55);
    { auto* p = proc->state.getParameter (ParamID::macroChaos); p->setValueNotifyingHost (0.0f); }
    pump (1500);

    // --- Navegador de presets ---
    if (auto* b = first ((PresetBrowser*) nullptr)) b->open();
    pump (200);
    save (shot (1.0f), "ui_presets.png");
    if (auto* b = first ((PresetBrowser*) nullptr)) b->setVisible (false);

    // --- Zoom ---
    if (auto* content = first ((NebulaContent*) nullptr)) if (content->onZoom) content->onZoom (0.8f);
    click ("Cerebro");
    pump (300);
    save (shot (1.0f), "ui_zoom_80.png");
    std::printf ("tamano de ventana con zoom 80%%: %dx%d\n", ed->getWidth(), ed->getHeight());
    if (auto* content = first ((NebulaContent*) nullptr)) if (content->onZoom) content->onZoom (1.0f);

    ed.reset();
    proc.reset();
    std::printf ("UI OK\n");
    return 0;
}
