#pragma once
#include "GenomeUI.h"
#include "Forge/Explain.h"

// ===========================================================================
// DESCUBRIR · el cerebro vivo
//
// La referencia está en el centro. Cada neurona es un patch real que produjo el trabajo de fondo; aparece
// cuando existe, se conecta con sus padres y late sólo mientras de verdad se está optimizando. Tamaño =
// parecido a la referencia (medido); color = novedad (azul Cosmos = cerca de lo conocido, rojo Abismo = lejos
// de todo lo que hay en la base).
//   clic: escuchar y ver cómo está hecho      mayús/cmd + clic: elegir como padre
//   doble clic: usar y abrir el editor        Evolucionar: hijos de los padres elegidos
// ===========================================================================
class BrainCanvas : public juce::Component
{
public:
    std::function<void (const juce::String&)> onFocus, onOpen;
    std::function<void (const juce::String&)> onTogglePick;

    void setData (std::vector<Forge::Neuron> n, const juce::String& working, const juce::StringArray& picked, const juce::String& focused)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (auto& x : n) if (born.find (x.id) == born.end()) born[x.id] = now;
        neurons = std::move (n); workingId = working; pickedIds = picked; focusId = focused;
        layout();
        repaint();
    }
    void clearAnimation() { born.clear(); }
    bool animating() const
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (auto& [id, t] : born) if (now - t < 600.0) return true;
        return workingId.isNotEmpty();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (track.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 8.0f);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto c = centre();
        // conexiones
        for (auto& n : neurons)
        {
            auto it = pos.find (n.id); if (it == pos.end()) continue;
            const float grow = growth (n.id, now);
            if (n.parents.isEmpty())
            {
                g.setColour (cosmos.withAlpha (0.18f * grow));
                g.drawLine ({ c, it->second }, 1.0f);
            }
            for (auto& pid : n.parents)
                if (auto pit = pos.find (pid); pit != pos.end())
                {
                    g.setColour ((n.source == "cruce" ? abyss : cosmos).withAlpha (0.45f * grow));
                    g.drawLine ({ pit->second, it->second }, n.parents.size() > 1 ? 1.4f : 1.0f);
                }
        }
        // referencia
        g.setColour (text.withAlpha (0.85f));
        g.drawEllipse (c.x - 11, c.y - 11, 22, 22, 1.5f);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (textDim);
        g.drawText ("referencia", juce::Rectangle<float> (c.x - 40, c.y + 13, 80, 12), juce::Justification::centred);
        // neuronas
        for (auto& n : neurons)
        {
            auto it = pos.find (n.id); if (it == pos.end()) continue;
            const float grow = growth (n.id, now);
            const float rad = radiusOf (n) * grow;
            const auto p = it->second;
            const float nov = n.novelty < 0 ? 0.0f : juce::jlimit (0.0f, 1.0f, (n.novelty - 0.2f) / 0.6f);
            auto col = cosmos.interpolatedWith (abyss, nov);
            if (n.feedback < 0) col = col.withMultipliedSaturation (0.2f).withMultipliedAlpha (0.5f);
            if (n.provisional)
            {
                // en optimización ahora mismo: anillo que late (estado real del trabajo de fondo)
                const float pulse = n.id == workingId ? 0.5f + 0.5f * std::sin ((float) now * 0.008f) : 0.3f;
                g.setColour (col.withAlpha (0.35f + 0.5f * pulse));
                g.drawEllipse (p.x - rad - 3 * pulse, p.y - rad - 3 * pulse, 2 * (rad + 3 * pulse), 2 * (rad + 3 * pulse), 1.5f);
                continue;
            }
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (p.x - rad * 1.7f, p.y - rad * 1.7f, rad * 3.4f, rad * 3.4f);
            g.setColour (col);
            g.fillEllipse (p.x - rad, p.y - rad, 2 * rad, 2 * rad);
            if (pickedIds.contains (n.id))
            {
                g.setColour (text);
                g.drawEllipse (p.x - rad - 4, p.y - rad - 4, 2 * rad + 8, 2 * rad + 8, 1.6f);
            }
            if (n.id == focusId)
            {
                g.setColour (text.withAlpha (0.9f));
                g.drawEllipse (p.x - rad - 7, p.y - rad - 7, 2 * rad + 14, 2 * rad + 14, 1.0f);
            }
            if (n.feedback > 0)
            {
                g.setColour (text);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText ("+", juce::Rectangle<float> (p.x + rad - 2, p.y - rad - 12, 12, 12), juce::Justification::centred);
            }
        }
        if (neurons.empty())
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("Carga un sample en el Cerebro y pulsa Sembrar: cada neurona que aparezca es un patch real del motor.",
                              getLocalBounds().reduced (40), juce::Justification::centred, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto id = hit (e.position);
        if (id.isEmpty()) return;
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) { if (onTogglePick) onTogglePick (id); }
        else if (onFocus) onFocus (id);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto id = hit (e.position);
        if (id.isNotEmpty() && onOpen) onOpen (id);
    }

    juce::Point<float> positionOf (const juce::String& id) const { auto it = pos.find (id); return it != pos.end() ? it->second : juce::Point<float>(); }

private:
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    static float radiusOf (const Forge::Neuron& n) { return 4.0f + 9.0f * juce::jlimit (0.0f, 1.0f, n.similarity < 0 ? 0.3f : n.similarity); }
    float growth (const juce::String& id, double now) const
    {
        auto it = born.find (id);
        if (it == born.end()) return 1.0f;
        const float t = (float) juce::jlimit (0.0, 1.0, (now - it->second) / 450.0);
        return 1.0f - (1.0f - t) * (1.0f - t);
    }
    juce::String hit (juce::Point<float> p) const
    {
        juce::String best; float bd = 18.0f;
        for (auto& [id, q] : pos) { const float d = q.getDistanceFrom (p); if (d < bd) { bd = d; best = id; } }
        return best;
    }
    // anillos por generación; las semillas se reparten alrededor y cada hijo se abre cerca del ángulo de sus padres
    void layout()
    {
        pos.clear();
        const auto c = centre();
        const float maxR = juce::jmin (getWidth() * 0.5f, getHeight() * 0.5f) - 22.0f;
        int maxGen = 0; for (auto& n : neurons) maxGen = juce::jmax (maxGen, n.generation);
        const float ring0 = maxR * (maxGen == 0 ? 0.6f : 0.42f);
        const float step = maxGen > 0 ? (maxR - ring0) / (float) maxGen : 0.0f;
        std::map<juce::String, float> angle;
        std::vector<const Forge::Neuron*> g0;
        for (auto& n : neurons) if (n.generation == 0) g0.push_back (&n);
        for (size_t i = 0; i < g0.size(); ++i)
            angle[g0[i]->id] = juce::MathConstants<float>::twoPi * (float) i / (float) juce::jmax ((size_t) 1, g0.size()) - juce::MathConstants<float>::halfPi;
        for (int gen = 1; gen <= maxGen; ++gen)
        {
            std::map<juce::String, int> siblings;   // hijos por grupo de padres, para abrirlos en abanico
            for (auto& n : neurons)
            {
                if (n.generation != gen) continue;
                float sx = 0, sy = 0; int k = 0;
                for (auto& pid : n.parents) if (auto a = angle.find (pid); a != angle.end()) { sx += std::cos (a->second); sy += std::sin (a->second); ++k; }
                const float base = k > 0 ? std::atan2 (sy, sx) : 0.0f;
                const int idx = siblings[n.parents.joinIntoString (",")]++;
                angle[n.id] = base + ((idx % 2 == 0 ? 1.0f : -1.0f) * (0.5f + (float) (idx / 2))) * 0.16f;
            }
        }
        for (auto& n : neurons)
        {
            const float rad = n.generation == 0 ? ring0 : ring0 + step * (float) n.generation;
            const float a = angle.count (n.id) ? angle[n.id] : 0.0f;
            pos[n.id] = { c.x + rad * std::cos (a), c.y + rad * std::sin (a) };
        }
    }

    std::vector<Forge::Neuron> neurons;
    std::map<juce::String, juce::Point<float>> pos;
    std::map<juce::String, double> born;
    juce::String workingId, focusId;
    juce::StringArray pickedIds;
};

// Barra Cerca <-> Lejos (distancia a la referencia)
class DistanceSlider : public juce::Slider
{
public:
    DistanceSlider() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setRange (0.0, 1.0, 0.01);
        setTooltip ("Distancia a la referencia: cerca = parecido; lejos = exploracion radical (cambia el objetivo y la fuerza de las mutaciones)");
    }
    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat().reduced (2.0f, 8.0f);
        g.setColour (track); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float x = r.getX() + (float) getValue() * r.getWidth();
        g.setColour (cosmos.interpolatedWith (abyss, (float) getValue()));
        g.fillRoundedRectangle (r.withRight (x), r.getHeight() * 0.5f);
        g.setColour (text); g.fillEllipse (x - 6, r.getCentreY() - 6, 12, 12);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (text.withAlpha (0.8f));
        g.drawText ("CERCA", r.reduced (10, 0), juce::Justification::centredLeft);
        g.drawText ("LEJOS", r.reduced (10, 0), juce::Justification::centredRight);
    }
};

class BrainView : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onOpenEditor;   // ir a CREAR > Editar con el patch aplicado

    explicit BrainView (NebulaAudioProcessor& p) : proc (p)
    {
        for (auto* b : { &seedButton, &evolveButton, &likeButton, &dislikeButton, &useButton, &editButton, &undoButton, &redoButton, &resetButton, &cancelButton, &refButton })
        { addAndMakeVisible (b); b->horizontal = true; }
        seedButton.colour = NebulaColours::cosmos; seedButton.filled = true;
        evolveButton.colour = NebulaColours::abyss; evolveButton.filled = true;
        useButton.filled = true;
        seedBox.setInputRestrictions (9, "0123456789"); seedBox.setText ("1", juce::dontSendNotification);
        seedBox.setJustification (juce::Justification::centred);
        seedBox.setTooltip ("Semilla: la misma semilla y las mismas acciones dan la misma red");
        addAndMakeVisible (seedBox);
        addAndMakeVisible (distance);
        addAndMakeVisible (canvas);
        addAndMakeVisible (graph);
        info.setMultiLine (true); info.setReadOnly (true); info.setScrollbarsShown (true);
        info.setColour (juce::TextEditor::backgroundColourId, NebulaColours::track.withAlpha (0.4f));
        info.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        info.setColour (juce::TextEditor::textColourId, NebulaColours::text);
        info.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (info);

        seedButton.onClick = [this]
        {
            if (proc.sampleSource.get() == nullptr) { status = "Carga un sample en el Cerebro primero"; repaint(); return; }
            picked.clear(); focus = {}; canvas.clearAnimation();
            if (! proc.brainStart (juce::jmax ((juce::int64) 1, seedBox.getText().getLargeIntValue()), (float) distance.getValue())) status = "No se pudo leer la seccion del sample";
            repaint();
        };
        evolveButton.onClick = [this]
        {
            auto parents = picked; if (parents.isEmpty() && focus.isNotEmpty()) parents.add (focus);
            if (parents.isEmpty()) { status = "Elige un padre: clic en una neurona (o mayus + clic para varios)"; repaint(); return; }
            if (! proc.brainEvolve (parents, 8)) status = "Espera a que termine el trabajo en curso";
            picked.clear();
            repaint();
        };
        likeButton.onClick = [this] { if (focus.isNotEmpty()) proc.brain.setFeedback (focus, current().feedback > 0 ? 0 : 1); };
        dislikeButton.onClick = [this] { if (focus.isNotEmpty()) proc.brain.setFeedback (focus, current().feedback < 0 ? 0 : -1); };
        resetButton.onClick = [this] { proc.brain.resetPreferences(); };
        useButton.onClick = [this] { if (focus.isNotEmpty()) { proc.applySynthPatch (current().patch); status = focus + " es tu sonido: editalo en CREAR"; repaint(); } };
        editButton.onClick = [this] { open (focus); };
        undoButton.onClick = [this] { proc.brain.undo(); };
        redoButton.onClick = [this] { proc.brain.redoAction(); };
        cancelButton.onClick = [this] { proc.brain.cancel(); };
        refButton.onClick = [this] { proc.auditionForgeReference(); };
        distance.onValueChange = [this] { proc.brain.setDistance ((float) distance.getValue()); };
        canvas.onFocus = [this] (const juce::String& id) { focus = id; proc.auditionSynthPatch (current().patch, id); refreshInfo(); refresh(); };
        canvas.onTogglePick = [this] (const juce::String& id) { if (picked.contains (id)) picked.removeString (id); else picked.add (id); refresh(); };
        canvas.onOpen = [this] (const juce::String& id) { open (id); };
        startTimerHz (30);
    }

    void visibilityChanged() override { if (isVisible()) refresh(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        drawOverlayFrame (g, getLocalBounds(), Icons::seed(), "DESCUBRIR  CEREBRO VIVO",
                          "Cada neurona es un patch real que va creciendo. Tamano = parecido medido; color = novedad (azul conocido, rojo nuevo).");
        g.setColour (textDim); g.setFont (juce::FontOptions (10.5f));
        g.drawText ("Semilla", seedLabel, juce::Justification::centredRight);
        g.drawText ("Distancia a la referencia", distLabel, juce::Justification::centredLeft);
        g.setColour (accent2); g.setFont (juce::FontOptions (11.0f));
        g.drawText (status, statusArea, juce::Justification::centredLeft);
        if (proc.brain.isWorking())
        {
            auto bar = progressArea.toFloat();
            g.setColour (track); g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (cosmos); g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * proc.brain.progress())), 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (46);
        auto row1 = r.removeFromTop (30);
        seedButton.setBounds (row1.removeFromLeft (120).reduced (2));
        seedLabel = row1.removeFromLeft (52); seedBox.setBounds (row1.removeFromLeft (56).reduced (2, 3));
        row1.removeFromLeft (8);
        distLabel = row1.removeFromLeft (150);
        distance.setBounds (row1.removeFromLeft (220));
        row1.removeFromLeft (8);
        evolveButton.setBounds (row1.removeFromLeft (120).reduced (2));
        cancelButton.setBounds (row1.removeFromLeft (96).reduced (2));
        redoButton.setBounds (row1.removeFromRight (34).reduced (2));
        undoButton.setBounds (row1.removeFromRight (34).reduced (2));
        refButton.setBounds (row1.removeFromRight (120).reduced (2));
        r.removeFromTop (4);
        auto row2 = r.removeFromTop (22);
        progressArea = row2.removeFromBottom (3).withWidth (300);
        statusArea = row2;
        r.removeFromTop (6);
        auto side = r.removeFromRight (juce::roundToInt (r.getWidth() * 0.38f));
        canvas.setBounds (r.withTrimmedRight (8));
        auto actions = side.removeFromTop (30);
        const int w = actions.getWidth() / 5;
        likeButton.setBounds (actions.removeFromLeft (w).reduced (2));
        dislikeButton.setBounds (actions.removeFromLeft (w).reduced (2));
        useButton.setBounds (actions.removeFromLeft (w).reduced (2));
        editButton.setBounds (actions.removeFromLeft (w).reduced (2));
        resetButton.setBounds (actions.reduced (2));
        side.removeFromTop (6);
        graph.setBounds (side.removeFromTop (juce::jmin (150, side.getHeight() / 3)));
        side.removeFromTop (6);
        info.setBounds (side);
    }

    // para las pruebas
    juce::String focusedId() const { return focus; }
    void focusNeuron (const juce::String& id) { focus = id; refreshInfo(); refresh(); }
    void togglePick (const juce::String& id) { canvas.onTogglePick (id); }
    juce::String infoText() const { return info.getText(); }

private:
    Forge::Neuron current() const
    {
        for (auto& n : last) if (n.id == focus) return n;
        return {};
    }
    void open (const juce::String& id)
    {
        if (id.isEmpty()) return;
        focus = id;
        proc.applySynthPatch (current().patch);
        if (onOpenEditor) onOpenEditor();
    }
    void refresh()
    {
        last = proc.brain.snapshot();
        seen = proc.brain.changeCount();
        canvas.setData (last, proc.brain.working(), picked, focus);
        const bool busy = proc.brain.isWorking();
        evolveButton.setEnabled (! busy && ! last.empty());
        seedButton.setEnabled (! busy);
        cancelButton.setEnabled (busy);
        undoButton.setEnabled (! busy && proc.brain.canUndo());
        redoButton.setEnabled (! busy && proc.brain.canRedo());
        const bool has = focus.isNotEmpty() && ! current().provisional && current().id.isNotEmpty();
        for (auto* b : { &likeButton, &dislikeButton, &useButton, &editButton }) b->setEnabled (has);
        likeButton.setToggleState (has && current().feedback > 0, juce::dontSendNotification);
        dislikeButton.setToggleState (has && current().feedback < 0, juce::dontSendNotification);
        juce::String st = proc.brain.status();
        if (picked.size() > 0) st << "   |   padres elegidos: " << picked.joinIntoString (", ");
        if (st.isNotEmpty()) status = st;
        repaint();
    }
    void refreshInfo()
    {
        const auto n = current();
        if (n.id.isEmpty()) { info.setText ({}, false); return; }
        graph.setPatch (n.patch, n.id + "  " + n.source);
        juce::String t;
        t << n.id << "   generacion " << n.generation << "   origen: " << n.source << "\n";
        if (! n.parents.isEmpty()) t << "padres: " << n.parents.joinIntoString (" x ") << "\n";
        if (n.similarity >= 0) t << "parecido a la referencia: " << juce::roundToInt (n.similarity * 100) << " %   (color " << juce::roundToInt (n.fidelity.color * 100)
                                 << ", envolvente " << juce::roundToInt (n.fidelity.envelope * 100) << ", tono " << juce::roundToInt (juce::jmax (0.0f, n.fidelity.pitch) * 100) << ")\n";
        if (n.novelty >= 0) t << "novedad: " << juce::roundToInt (n.novelty * 100) << " %  (100 % = lejos de todos los patches de la base)\n";
        t << "\nCOMO ESTA HECHO\n" << Forge::explainText (n.patch);
        info.setText (t, false);
    }
    void timerCallback() override
    {
        if (! visibleInTree (this)) return;
        if (proc.brain.changeCount() != seen) { refresh(); refreshInfo(); }
        else if (canvas.animating() || proc.brain.isWorking()) { canvas.repaint(); repaint (progressArea); }
    }

    NebulaAudioProcessor& proc;
    BrainCanvas canvas;
    PatchGraphView graph;
    juce::TextEditor info, seedBox;
    DistanceSlider distance;
    IconButton seedButton { "Sembrar", Icons::seed(), "Analiza el sample y hace crecer la primera generacion (FORGE en vivo)" },
               evolveButton { "Evolucionar", Icons::dna(), "8 hijos de los padres elegidos: mutacion y cruce, respetando candados y tus preferencias" },
               likeButton { "Me gusta", Icons::sprout(), "La evolucion se acerca a este sonido (y lo usa como pareja de cruce)" },
               dislikeButton { "No", Icons::stop(), "La evolucion se aleja de este sonido" },
               useButton { "Usar", Icons::wand(), "Este patch pasa a ser tu sonido" },
               editButton { "Editar", Icons::sliders(), "Usar y abrir el editor de sintesis (CREAR > Editar)" },
               undoButton { "", Icons::undo(), "Deshacer la ultima generacion" },
               redoButton { "", Icons::redo(), "Rehacer" },
               resetButton { "Olvidar", Icons::broom(), "Olvidar las preferencias de esta sesion" },
               cancelButton { "Cancelar", Icons::stop(), "Detener el trabajo de fondo" },
               refButton { "Referencia", Icons::speaker(), "Escuchar la referencia" };
    juce::StringArray picked;
    juce::String focus, status;
    std::vector<Forge::Neuron> last;
    int seen = -1;
    juce::Rectangle<int> seedLabel, distLabel, statusArea, progressArea;
};
