#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include "TransformUI.h"

// ===========================================================================
// Fondo neuronal: una red viva que se vuelve más loca con el sonido
// ===========================================================================
class NeuralBackground
{
public:
    NeuralBackground()
    {
        juce::Random r (2026);
        nodes.resize (38);
        for (auto& n : nodes)
        {
            n.x = r.nextFloat(); n.y = r.nextFloat();
            n.vx = (r.nextFloat() - 0.5f) * 0.01f; n.vy = (r.nextFloat() - 0.5f) * 0.01f;
            n.phase = r.nextFloat() * 6.28f;
        }
    }

    // Avanza la animación (dt en segundos)
    void step (float dt, float craziness, float audioLevel)
    {
        time += dt;
        const float speed = 0.4f + 3.0f * craziness;
        for (auto& n : nodes)
        {
            n.x += n.vx * dt * speed; n.y += n.vy * dt * speed;
            if (n.x < 0.0f || n.x > 1.0f) n.vx = -n.vx;
            if (n.y < 0.0f || n.y > 1.0f) n.vy = -n.vy;
            n.x = juce::jlimit (0.0f, 1.0f, n.x); n.y = juce::jlimit (0.0f, 1.0f, n.y);
        }
        // Disparos: pulsos que viajan por las conexiones (más con sonido y con locura)
        const float rate = 0.25f + 4.0f * craziness + 5.0f * audioLevel;
        fireAcc += rate * dt;
        while (fireAcc > 1.0f && pulses.size() < 24)
        {
            fireAcc -= 1.0f;
            const int a = rng.nextInt ((int) nodes.size());
            int b = -1; float best = 1e9f;
            for (int k = 0; k < (int) nodes.size(); ++k)
            {
                if (k == a) continue;
                const float d = std::hypot (nodes[(size_t) k].x - nodes[(size_t) a].x, nodes[(size_t) k].y - nodes[(size_t) a].y) + rng.nextFloat() * 0.05f;
                if (d < best) { best = d; b = k; }
            }
            if (b >= 0) pulses.push_back ({ a, b, 0.0f });
        }
        for (auto& p : pulses) p.t += dt * (1.2f + 2.0f * craziness);
        pulses.erase (std::remove_if (pulses.begin(), pulses.end(), [] (auto& p) { return p.t >= 1.0f; }), pulses.end());
        level = audioLevel;
        crazy = craziness;
    }

    // intensity: 1 en el Cerebro (la red se ve), baja en las otras pestañas (sólo un rastro)
    float intensity = 1.0f;

    void paint (juce::Graphics& g, juce::Rectangle<float> area)
    {
        using namespace NebulaColours;
        const float c = crazy;
        // Fondo grafito con un degradado mínimo (apenas se calienta con la locura)
        g.setGradientFill (juce::ColourGradient (background.brighter (0.06f).interpolatedWith (juce::Colour (0xff1a1018), c * 0.35f), area.getCentreX(), area.getCentreY() * 0.8f,
                                                 juce::Colour (0xff08090a), area.getX(), area.getY(), true));
        g.fillRect (area);
        if (intensity <= 0.01f) return;
        const float vis = 0.45f * intensity;   // la red queda en segundo plano

        auto pos = [&] (const Node& n)
        {
            // Con locura las neuronas tiemblan
            const float jit = c * c * 0.012f;
            return juce::Point<float> (area.getX() + area.getWidth() * (n.x + jit * std::sin (time * 13.0f + n.phase)),
                                       area.getY() + area.getHeight() * (n.y + jit * std::cos (time * 11.0f + n.phase * 1.7f)));
        };
        const float maxD = 0.11f + 0.08f * c;
        const auto lineCol = Neural::hot (accent.withAlpha (1.0f));
        for (size_t i = 0; i < nodes.size(); ++i)
            for (size_t j = i + 1; j < nodes.size(); ++j)
            {
                const float d = std::hypot (nodes[i].x - nodes[j].x, (nodes[i].y - nodes[j].y) * 0.66f);
                if (d > maxD) continue;
                g.setColour (lineCol.withAlpha ((1.0f - d / maxD) * (0.035f + 0.10f * c) * vis));
                g.drawLine ({ pos (nodes[i]), pos (nodes[j]) }, 0.8f + c);
            }
        for (auto& n : nodes)
        {
            const auto p = pos (n);
            const float r = 1.0f + 0.7f * (0.5f + 0.5f * std::sin (time * 2.0f + n.phase)) + 1.0f * level;
            g.setColour (Neural::hot (accent2).withAlpha ((0.18f + 0.22f * c) * vis));
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }
        for (auto& pl : pulses)
        {
            const auto a = pos (nodes[(size_t) pl.a]), b = pos (nodes[(size_t) pl.b]);
            const auto p = a + (b - a) * pl.t;
            g.setColour (Neural::hot (accent2, 0.2f).withAlpha (0.8f * (1.0f - pl.t) * vis));
            g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
        }
        // Mucha locura: glitches (franjas desplazadas)
        if (c > 0.75f && intensity > 0.5f)
        {
            const int n = (int) ((c - 0.75f) * 8.0f);
            juce::Random r ((juce::int64) (time * 20.0f));
            for (int k = 0; k < n; ++k)
            {
                const float y = area.getY() + r.nextFloat() * area.getHeight();
                g.setColour (Neural::hot (accent, 0.3f).withAlpha (0.05f + 0.1f * r.nextFloat()));
                g.fillRect (area.getX() + r.nextFloat() * 40.0f - 20.0f, y, area.getWidth(), 1.0f + r.nextFloat() * 3.0f);
            }
        }
    }

private:
    struct Node { float x, y, vx, vy, phase; };
    struct Pulse { int a, b; float t; };
    std::vector<Node> nodes;
    std::vector<Pulse> pulses;
    juce::Random rng { 7 };
    float time = 0.0f, fireAcc = 0.0f, level = 0.0f, crazy = 0.0f;
};

// ===========================================================================
// Botón que se arrastra a Ableton (MIDI, audio, onda). Con clic, abre la carpeta.
// ===========================================================================
class DragChip : public IconButton
{
public:
    DragChip (const juce::String& caption, juce::Path icon, const juce::String& tip) : IconButton (caption, std::move (icon), tip)
    {
        horizontal = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    std::function<juce::File()> makeFile;
    std::function<void (const juce::String&)> onMessage;

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || makeFile == nullptr) return;
        dragging = true;
        const auto f = makeFile();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
        else
        {
            dragging = false;
            if (onMessage) onMessage ("No hay nada que exportar todavia.");
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() < 6 && makeFile != nullptr)
        {
            const auto f = makeFile();
            if (f.existsAsFile()) { f.revealToUser(); if (onMessage) onMessage ("Guardado en Musica/Nebula/Exportados: " + f.getFileName()); }
            else if (onMessage) onMessage ("No hay nada que exportar todavia.");
        }
        dragging = false;
        IconButton::mouseUp (e);
    }

private:
    bool dragging = false;
};

// ===========================================================================
// Barra de macros (abajo): Brillo, Cuerpo, Movimiento, Espacio, LOCURA + A/B + exportar
// ===========================================================================
class MacroStrip : public juce::Component, private juce::Timer
{
public:
    explicit MacroStrip (NebulaAudioProcessor& p) : proc (p)
    {
        const char* ids[] { ParamID::macroBright, ParamID::macroBody, ParamID::macroMove, ParamID::macroSpace, ParamID::macroChaos };
        const char* names[] { "BRILLO", "CUERPO", "MOVIMIENTO", "ESPACIO", "LOCURA" };
        const char* tips[] { "Mas oscuro o mas brillante", "Menos o mas cuerpo (graves y sub)", "Le da vida: vibrato, filtro que se mece y chorus",
                             "Reverb y delay de un jalon", "Lo vuelve loco: FM, saturacion y modulaciones al azar. La interfaz enloquece contigo." };
        for (int i = 0; i < 5; ++i)
        {
            knobs[(size_t) i] = std::make_unique<NebKnob> (p.state, ids[i], names[i], tips[i]);
            knobs[(size_t) i]->maxKnob = 40;
            addAndMakeVisible (*knobs[(size_t) i]);
        }
        knobs[4]->setColour (juce::Colour (0xffff3f7a));
        abButton.setClickingTogglesState (true);
        abButton.colour = NebulaColours::warm;
        abButton.horizontal = true;
        abButton.onClick = [this] { proc.setAB (abButton.getToggleState()); };
        addAndMakeVisible (abButton);
        for (auto* c : { &midiChip, &audioChip, &waveChip }) { addAndMakeVisible (c); c->onMessage = [this] (const juce::String& m) { message = m; msgTime = juce::Time::getMillisecondCounterHiRes(); repaint(); }; }
        midiChip.makeFile  = [this] { return proc.exportMidi(); };
        audioChip.makeFile = [this] { return proc.exportAudio(); };
        waveChip.makeFile  = [this] { return proc.exportWavetable(); };
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (panel.withAlpha (0.96f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        // Medidor de locura bajo la perilla LOCURA
        const auto kb = knobs[4]->getBounds().toFloat();
        const auto meter = juce::Rectangle<float> (kb.getRight() + 4.0f, kb.getY() + 12.0f, 5.0f, kb.getHeight() - 24.0f);
        g.setColour (track);
        g.fillRoundedRectangle (meter, 2.5f);
        const float c = Neural::level();
        g.setGradientFill (juce::ColourGradient (accent2, 0.0f, meter.getBottom(), warm, 0.0f, meter.getY(), false));
        g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * c), 2.5f);

        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("ARRASTRA A ABLETON", exportArea.withHeight (14).translated (0, -1), juce::Justification::centredLeft);
        if (message.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() - msgTime < 4000.0)
        {
            g.setColour (text);
            g.setFont (juce::FontOptions (10.5f));
            g.drawFittedText (message, exportArea.withTrimmedTop (44), juce::Justification::topLeft, 1);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 2);
        exportArea = r.removeFromRight (300);
        auto chips = exportArea.withTrimmedTop (14).withHeight (28);
        midiChip.setBounds (chips.removeFromLeft (90).reduced (2, 0));
        audioChip.setBounds (chips.removeFromLeft (100).reduced (2, 0));
        waveChip.setBounds (chips.removeFromLeft (100).reduced (2, 0));
        r.removeFromRight (10);
        abButton.setBounds (r.removeFromRight (150).withSizeKeepingCentre (150, 34));
        r.removeFromRight (16);
        const int w = r.getWidth() / 5;
        for (auto& k : knobs) k->setBounds (r.removeFromLeft (w).reduced (12, 0));
    }

private:
    void timerCallback() override
    {
        abButton.setToggleState (proc.isAB(), juce::dontSendNotification);
        abButton.setEnabled (proc.sampleSource.get() != nullptr);
        repaint();
    }

    NebulaAudioProcessor& proc;
    std::array<std::unique_ptr<NebKnob>, 5> knobs;
    IconButton abButton { "A/B: original", Icons::speaker(), "Mantenlo encendido para escuchar el sample original (al mismo volumen) y comparar con lo recreado" };
    DragChip midiChip  { "MIDI", Icons::piano(), "Arrastra las notas del sample (frases) como MIDI. Clic: guardar" },
             audioChip { "Audio", Icons::waveform(), "Arrastra el sonido actual como audio (en C). Clic: guardar" },
             waveChip  { "Onda", Icons::cycle(), "Arrastra la tabla de ondas (para Wavetable, Serum o Vital). Clic: guardar" };
    juce::Rectangle<int> exportArea;
    juce::String message;
    double msgTime = 0.0;
};

// ===========================================================================
// Navegador de presets: categorías, búsqueda y favoritos
// ===========================================================================
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (NebulaAudioProcessor& p) : proc (p)
    {
        loadFavourites();
        search.setTextToShowWhenEmpty ("Buscar...", NebulaColours::textDim);
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (search);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
        juce::StringArray cats { "Todos", "Favoritos", "Tuyos" };
        cats.addArray (presetCategories());
        for (auto& c : cats)
        {
            auto b = std::make_unique<IconButton> (c, juce::Path(), "Mostrar " + c);
            b->setClickingTogglesState (false);
            b->onClick = [this, c] { category = c; rebuild(); };
            addAndMakeVisible (*b);
            catButtons.push_back (std::move (b));
        }
        closeButton.horizontal = true;
        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);
    }

    std::function<void()> onLoaded;

    void open() { rebuild(); setVisible (true); toFront (true); search.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        g.setColour (juce::Colour (0xf4151824));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.2f);
        Icons::draw (g, Icons::folder(), { 16.0f, 12.0f, 18.0f, 18.0f }, accent2, 1.5f);
        g.setColour (text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("SONIDOS", 42, 10, 300, 22, juce::Justification::centredLeft);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Clic para cargar  |  la estrella marca favoritos", 42, 30, 400, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (44);
        auto bottom = r.removeFromBottom (34);
        closeButton.setBounds (bottom.removeFromRight (110).reduced (2));
        auto left = r.removeFromLeft (150);
        search.setBounds (left.removeFromTop (28));
        left.removeFromTop (8);
        for (auto& b : catButtons) { b->setBounds (left.removeFromTop (28)); left.removeFromTop (3); }
        r.removeFromLeft (12);
        list.setBounds (r);
    }

private:
    struct Item { juce::String name, category; int factory = -1; };

    void rebuild()
    {
        items.clear();
        const auto q = search.getText().trim().toLowerCase();
        const auto& f = factoryPresets();
        for (int i = 0; i < (int) f.size(); ++i)
            items.push_back ({ f[(size_t) i].name, f[(size_t) i].category, i });
        for (auto& u : proc.userPresetNames()) items.push_back ({ u, "Tuyos", -1 });
        items.erase (std::remove_if (items.begin(), items.end(), [&] (const Item& it)
        {
            if (q.isNotEmpty() && ! it.name.toLowerCase().contains (q) && ! it.category.toLowerCase().contains (q)) return true;
            if (category == "Todos") return false;
            if (category == "Favoritos") return ! favourites.contains (it.name);
            return it.category != category;
        }), items.end());
        for (auto& b : catButtons) b->setToggleState (b->getButtonText() == category, juce::dontSendNotification);
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return (int) items.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        using namespace NebulaColours;
        if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
        const auto& it = items[(size_t) row];
        if (selected) { g.setColour (accent.withAlpha (0.35f)); g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 5.0f); }
        const bool fav = favourites.contains (it.name);
        auto star = starPath();
        Icons::draw (g, star, { 8.0f, 7.0f, 16.0f, 16.0f }, fav ? warm : textDim.withAlpha (0.5f), 1.4f);
        g.setColour (text);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (it.name, 34, 0, w - 160, h, juce::Justification::centredLeft);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (it.category, w - 120, 0, 110, h, juce::Justification::centredRight);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
        const auto& it = items[(size_t) row];
        if (e.x < 30)
        {
            if (favourites.contains (it.name)) favourites.removeString (it.name); else favourites.add (it.name);
            saveFavourites();
            list.repaintRow (row);
            return;
        }
        if (it.factory >= 0) proc.loadPreset (it.factory);
        else proc.loadUserPreset (it.name);
        if (onLoaded) onLoaded();
    }

    static juce::Path starPath()
    {
        juce::Path p;
        p.addStar ({ 0.5f, 0.52f }, 5, 0.2f, 0.48f, 0.0f);
        return p;
    }

    juce::File favFile() const { return NebulaAudioProcessor::userPresetFolder().getParentDirectory().getChildFile ("favoritos.txt"); }
    void loadFavourites() { favourites.clear(); favourites.addLines (favFile().loadFileAsString()); favourites.removeEmptyStrings(); }
    void saveFavourites() { favFile().getParentDirectory().createDirectory(); favFile().replaceWithText (favourites.joinIntoString ("\n")); }

    NebulaAudioProcessor& proc;
    juce::TextEditor search;
    juce::ListBox list;
    std::vector<std::unique_ptr<IconButton>> catButtons;
    IconButton closeButton { "Cerrar", Icons::stop(), "Cerrar" };
    std::vector<Item> items;
    juce::StringArray favourites;
    juce::String category = "Todos";
};

// ===========================================================================
// Pestaña EFECTOS
// ===========================================================================
class FxView : public juce::Component
{
public:
    explicit FxView (NebulaAudioProcessor& p) : proc (p),
        eqOn (p.state, ParamID::fxEqOn, "", Icons::power(), "Encender / apagar el EQ"),
        compOn (p.state, ParamID::fxCompOn, "", Icons::power(), "Encender / apagar el compresor"),
        chorusOn (p.state, ParamID::fxChorusOn, "", Icons::power(), "Encender / apagar el chorus"),
        delayOn (p.state, ParamID::fxDelayOn, "", Icons::power(), "Encender / apagar el delay"),
        revOn (p.state, ParamID::fxRevOn, "", Icons::power(), "Encender / apagar la reverb"),
        delayPing (p.state, ParamID::fxDelayPing, "Ping pong", Icons::serial(), "Las repeticiones rebotan de izquierda a derecha"),
        delaySync (p.state, ParamID::fxDelaySync, "Tiempo", "Tiempo de las repeticiones: al tempo del proyecto o libre en ms"),
        transform (p)
    {
        addAndMakeVisible (transform);
        auto k = [this] (const char* id, const char* name, const char* tip) { knobs.push_back (std::make_unique<NebKnob> (proc.state, id, name, tip)); addAndMakeVisible (*knobs.back()); return knobs.back().get(); };
        eq   = { k (ParamID::fxEqLow, "Graves", "Estante de graves (120 Hz)"), k (ParamID::fxEqMid, "Medios", "Campana de medios"),
                 k (ParamID::fxEqMidFreq, "Frecuencia", "Donde actua la banda de medios"), k (ParamID::fxEqHigh, "Agudos", "Estante de agudos (6 kHz)") };
        comp = { k (ParamID::fxCompAmt, "Cantidad", "Cuanto aprieta: umbral, razon y volumen de compensacion juntos") };
        chorus = { k (ParamID::fxChorusRate, "Velocidad", ""), k (ParamID::fxChorusDepth, "Profundidad", ""), k (ParamID::fxChorusMix, "Mezcla", "") };
        delay = { k (ParamID::fxDelayMs, "ms", "Tiempo en ms (si Tiempo esta en Libre)"), k (ParamID::fxDelayFb, "Repeticiones", ""),
                  k (ParamID::fxDelayTone, "Tono", "Las repeticiones se oscurecen"), k (ParamID::fxDelayMix, "Mezcla", "") };
        reverb = { k (ParamID::fxRevSize, "Tamano", ""), k (ParamID::fxRevDecay, "Cola", "Cuanto dura"), k (ParamID::fxRevDamp, "Brillo", ""),
                   k (ParamID::fxRevPre, "Pre-delay", ""), k (ParamID::fxRevMix, "Mezcla", "") };
        width = k (ParamID::fxWidth, "Ancho", "Ancho estereo de todo el sonido (0 = mono)");
        for (auto* c : std::initializer_list<juce::Component*> { &eqOn, &compOn, &chorusOn, &delayOn, &revOn, &delayPing, &delaySync }) addAndMakeVisible (c);
        for (auto* t : { &eqOn, &compOn, &chorusOn, &delayOn, &revOn }) t->colour = NebulaColours::accent2;
    }

    void paint (juce::Graphics& g) override
    {
        auto iEq = Icons::sliders(), iComp = Icons::lock(), iCh = Icons::superSaw(), iDl = Icons::loopArrows(), iRv = Icons::sparkle(), iW = Icons::expand();
        drawSection (g, rEq, "EQ", &iEq);
        drawSection (g, rComp, "COMPRESOR", &iComp);
        drawSection (g, rChorus, "CHORUS", &iCh);
        drawSection (g, rDelay, "DELAY", &iDl);
        drawSection (g, rRev, "REVERB", &iRv);
        drawSection (g, rWidth, "ESTEREO", &iW);
        drawSection (g, rTransform, "COSMOS / ABISMO", nullptr, NebulaColours::accent);
        g.setColour (NebulaColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Transforma la estructura del sonido de cada voz (antes de los filtros y de estos efectos).",
                          rTransform.reduced (14, 0).withTrimmedTop (rTransform.getHeight() - 22), juce::Justification::centredLeft, 1);
        g.setColour (NebulaColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Orden: Cosmos/Abismo (por voz) > EQ > Compresor > Chorus > Delay > Reverb > Ancho.",
                          rWidth.reduced (12).withTrimmedTop (width != nullptr ? width->getBottom() - rWidth.getY() + 4 : 120), juce::Justification::topLeft, 6);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        constexpr int gap = 10;
        rTransform = r.removeFromBottom (juce::jmin (130, r.getHeight() / 4));
        r.removeFromBottom (gap);
        transform.setBounds (rTransform.reduced (16, 0).withTrimmedTop (30).withTrimmedBottom (24));
        auto top = r.removeFromTop ((r.getHeight() - gap) / 2);
        r.removeFromTop (gap);
        rEq = top.removeFromLeft (top.getWidth() * 4 / 10); top.removeFromLeft (gap);
        rComp = top.removeFromLeft (top.getWidth() * 3 / 10); top.removeFromLeft (gap);
        rChorus = top;
        rDelay = r.removeFromLeft (r.getWidth() * 4 / 10); r.removeFromLeft (gap);
        rRev = r.removeFromLeft (r.getWidth() * 7 / 10); r.removeFromLeft (gap);
        rWidth = r;
        auto place = [] (juce::Rectangle<int> area, IconToggle* power, std::vector<NebKnob*> ks)
        {
            if (power != nullptr) power->setBounds (area.getRight() - 40, area.getY() + 5, 30, 24);
            auto a = area.reduced (10).withTrimmedTop (26);
            const int w = a.getWidth() / juce::jmax (1, (int) ks.size());
            for (auto* kk : ks) { kk->maxKnob = 50; kk->setBounds (a.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (a.getHeight(), 100))); }
        };
        place (rEq, &eqOn, eq);
        place (rComp, &compOn, comp);
        place (rChorus, &chorusOn, chorus);
        {
            auto a = rDelay;
            auto side = a.reduced (10).withTrimmedTop (26).removeFromLeft (110);
            delaySync.setBounds (side.removeFromTop (44));
            side.removeFromTop (6);
            delayPing.setBounds (side.removeFromTop (40));
            place (a.withTrimmedLeft (120), &delayOn, delay);
        }
        place (rRev, &revOn, reverb);
        place (rWidth.withHeight (130), nullptr, { width });
    }

private:
    NebulaAudioProcessor& proc;
    std::vector<std::unique_ptr<NebKnob>> knobs;
    std::vector<NebKnob*> eq, comp, chorus, delay, reverb;
    NebKnob* width = nullptr;
    IconToggle eqOn, compOn, chorusOn, delayOn, revOn, delayPing;
    ParamCombo delaySync;
    TransformStrip transform;
    juce::Rectangle<int> rEq, rComp, rChorus, rDelay, rRev, rWidth, rTransform;
};
