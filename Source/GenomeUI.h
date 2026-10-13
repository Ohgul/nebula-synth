#pragma once
#include "Panels.h"
#include "TransformUI.h"
#include "Forge/Polarity.h"

// ===========================================================================
// Interfaz del ADN estructural (P4) y de TRANSFORMAR (P5/P6)
//   PatchGraphView   - el grafo de una receta: fuentes -> filtros -> drive -> amplificador, y las rutas de
//                      modulación (punteadas) desde envolventes, LFOs y la voz
//   ModuleLockStrip  - un chip por módulo: encendido/apagado en la receta y candado (clic = bloquear)
//   TransformView    - el espacio TRANSFORMAR: Cosmos/Abismo sobre la receta (reversible) y la
//                      transformación del motor (procesa el audio)
// ===========================================================================
// visible en toda la jerarquía (isShowing() además exige ventana en pantalla, y en las pruebas no la hay)
inline bool visibleInTree (const juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent()) if (! c->isVisible()) return false;
    return true;
}

class PatchGraphView : public juce::Component
{
public:
    void setPatch (const Forge::SynthPatch& p, const juce::String& caption = {})
    {
        graph = Forge::PatchGraph::from (p);
        title = caption;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (track.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 6.0f);
        if (title.isNotEmpty())
        {
            g.setColour (textDim); g.setFont (juce::FontOptions (10.5f));
            g.drawText (title, r.removeFromTop (16).reduced (8, 0), juce::Justification::centredLeft);
        }
        r = r.reduced (8, 4);
        const auto pos = layout (r);
        // aristas de audio
        for (auto& e : graph.edges)
        {
            auto a = pos.find (e.from), b = pos.find (e.to);
            if (a == pos.end() || b == pos.end()) continue;
            const auto pa = a->second.getCentre(), pb = b->second.getCentre();
            if (! e.modulation)
            {
                g.setColour (text.withAlpha (0.45f));
                const juce::Line<float> line (a->second.getRight(), pa.y, b->second.getX(), pb.y);
                g.drawArrow (line, 1.2f, 6.0f, 6.0f);
            }
            else
            {
                juce::Path p;
                p.startNewSubPath (pa);
                p.cubicTo (pa.x, (pa.y + pb.y) * 0.5f, pb.x, (pa.y + pb.y) * 0.5f, pb.x, b->second.getBottom());
                juce::Path dashed;
                const float dash[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dash, 2);
                g.setColour ((e.amount >= 0 ? cosmos : abyss).withAlpha (0.35f + 0.6f * juce::jmin (1.0f, std::abs (e.amount))));
                g.fillPath (dashed);
            }
        }
        // nodos activos
        for (auto& n : graph.nodes)
        {
            if (! n.active) continue;
            auto it = pos.find (n.key);
            if (it == pos.end()) continue;
            const bool mod = n.module == Forge::Module::Env2 || n.module == Forge::Module::Env3 || n.module == Forge::Module::Lfo1
                          || n.module == Forge::Module::Lfo2 || n.module == Forge::Module::Voice;
            g.setColour (panel);
            g.fillRoundedRectangle (it->second, 4.0f);
            g.setColour (mod ? cosmos.withAlpha (0.7f) : panelEdge.brighter (0.4f));
            g.drawRoundedRectangle (it->second, 4.0f, 1.0f);
            g.setColour (text);
            g.setFont (juce::FontOptions (10.0f));
            juce::String label = n.name;
            if (n.module == Forge::Module::Osc1)
            {
                if (isActive ("fm")) label << " +FM";
                if (isActive ("uni")) label << " +Uni";
            }
            g.drawFittedText (label, it->second.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
        }
        if (auto it = pos.find ("out"); it != pos.end())
        {
            g.setColour (textDim); g.setFont (juce::FontOptions (10.0f));
            g.drawText ("salida", it->second, juce::Justification::centred);
        }
    }

    const Forge::PatchGraph& currentGraph() const { return graph; }

private:
    std::map<juce::String, juce::Rectangle<float>> layout (juce::Rectangle<float> r) const
    {
        std::map<juce::String, juce::Rectangle<float>> pos;
        const float audioH = r.getHeight() * 0.62f;
        auto audio = r.removeFromTop (audioH);
        auto mods = r.withTrimmedTop (6.0f);
        const float colW = audio.getWidth() / 5.0f, boxW = juce::jmin (110.0f, colW - 16.0f), boxH = 18.0f;
        auto column = [&] (int col, const juce::StringArray& keys)
        {
            juce::StringArray act;
            for (auto& k : keys) if (k == "out" || isActive (k)) act.add (k);
            for (int i = 0; i < act.size(); ++i)
            {
                const float y = audio.getY() + (i + 0.5f) * audio.getHeight() / (float) juce::jmax (1, act.size()) - boxH * 0.5f;
                pos[act[i]] = { audio.getX() + col * colW + (colW - boxW) * 0.5f, y, boxW, boxH };
            }
        };
        // FM y unísono no son una etapa del camino: se marcan sobre el oscilador 1
        column (0, { "op3", "osc1", "osc2", "sub", "noise" });
        column (1, { "f1", "f2" });
        column (2, { "drive" });
        column (3, { "amp" });
        column (4, { "out" });
        juce::StringArray m; for (auto* k : { "env2", "env3", "lfo1", "lfo2", "voice" }) if (isActive (k)) m.add (k);
        // la voz siempre está: sólo se dibuja si modula algo
        if (m.contains ("voice") && ! hasModulationFrom ("voice")) m.removeString ("voice");
        const float mw = mods.getWidth() / (float) juce::jmax (1, m.size());
        for (int i = 0; i < m.size(); ++i)
            pos[m[i]] = { mods.getX() + i * mw + (mw - boxW) * 0.5f, mods.getCentreY() - boxH * 0.5f, boxW, boxH };
        return pos;
    }
    bool isActive (const juce::String& key) const { for (auto& n : graph.nodes) if (n.key == key) return n.active; return false; }
    bool hasModulationFrom (const juce::String& key) const { for (auto& e : graph.edges) if (e.from == key && e.modulation) return true; return false; }

    Forge::PatchGraph graph;
    juce::String title;
};

class ModuleLockStrip : public juce::Component, private juce::Timer
{
public:
    explicit ModuleLockStrip (NebulaAudioProcessor& p) : proc (p)
    {
        for (auto& mi : Forge::moduleTable())
        {
            auto b = std::make_unique<IconButton> (shortName (mi.m), juce::Path(), juce::String ("Candado: ") + mi.name + " no cambia al mutar, cruzar ni con Cosmos/Abismo de la receta");
            b->horizontal = true;
            b->setClickingTogglesState (false);
            const auto m = mi.m;
            b->onClick = [this, m] { proc.setForgeLock (m, ! proc.forgeLocks().isLocked (m)); refresh(); };
            addAndMakeVisible (*b);
            chips.push_back (std::move (b));
        }
        startTimerHz (4);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int rows = r.getWidth() < 900 ? 2 : 1;
        const int perRow = (int) std::ceil (chips.size() / (double) rows);
        const int h = r.getHeight() / rows;
        for (int row = 0; row < rows; ++row)
        {
            auto line = r.removeFromTop (h);
            const int w = line.getWidth() / perRow;
            for (int i = row * perRow; i < juce::jmin ((int) chips.size(), (row + 1) * perRow); ++i)
                chips[(size_t) i]->setBounds (line.removeFromLeft (w).reduced (2, 2));
        }
    }

    void refresh()
    {
        const auto locks = proc.forgeLocks();
        const auto patch = proc.currentSynthPatch();
        for (auto& mi : Forge::moduleTable())
        {
            auto& b = *chips[(size_t) mi.m];
            const bool locked = locks.isLocked (mi.m);
            b.setToggleState (locked, juce::dontSendNotification);
            b.colour = locked ? NebulaColours::abyss : NebulaColours::cosmos;
            b.setAlpha (Forge::moduleActive (patch, mi.m) ? 1.0f : 0.55f);
            b.setButtonText (juce::String (locked ? "# " : "") + shortName (mi.m));
        }
    }

    static juce::String shortName (Forge::Module m)
    {
        static const char* n[] { "Osc 1", "Osc 2", "FM", "Sub", "Ruido", "Unis.", "Filtro 1", "Filtro 2", "Drive", "Amp", "Env 2", "Env 3", "LFO 1", "LFO 2", "Voz", "Op 3" };
        return n[(int) m];
    }

private:
    void timerCallback() override { if (visibleInTree (this)) refresh(); }
    NebulaAudioProcessor& proc;
    std::vector<std::unique_ptr<IconButton>> chips;
};

// Barra bipolar Abismo <-> Cosmos para la receta (no es un parámetro: mueve la receta y se puede revertir)
class RecipePolaritySlider : public juce::Slider
{
public:
    RecipePolaritySlider() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setRange (-1.0, 1.0, 0.01);
        setDoubleClickReturnValue (true, 0.0);
        setTooltip ("Cosmos/Abismo sobre la receta: cambia osciladores, filtro, envolventes y modulaciones (doble clic = base)");
    }
    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat().reduced (2.0f, 6.0f);
        const float v = (float) getValue();
        juce::ColourGradient grad (abyss, r.getX(), 0, cosmos, r.getRight(), 0, false);
        grad.addColour (0.5, track);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float x = r.getX() + (v + 1.0f) * 0.5f * r.getWidth();
        g.setColour (text);
        g.fillEllipse (x - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
        g.setColour (text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("ABISMO", r.reduced (12, 0), juce::Justification::centredLeft);
        g.drawText ("COSMOS", r.reduced (12, 0), juce::Justification::centredRight);
    }
};

class TransformView : public juce::Component, private juce::Timer
{
public:
    explicit TransformView (NebulaAudioProcessor& p) : proc (p), locks (p), engineStrip (p)
    {
        addAndMakeVisible (polarity);
        addAndMakeVisible (revertButton);
        addAndMakeVisible (graphBefore);
        addAndMakeVisible (graphAfter);
        addAndMakeVisible (locks);
        addAndMakeVisible (engineStrip);
        revertButton.horizontal = true;
        polarity.onValueChange = [this]
        {
            const float v = (float) polarity.getValue();
            if (std::abs (v - lastApplied) < 0.015f && v != 0.0f) return;
            lastApplied = v;
            proc.setRecipePolarity (v);
            refreshGraphs();
        };
        revertButton.onClick = [this]
        {
            proc.revertRecipePolarity();
            polarity.setValue (0.0, juce::dontSendNotification);
            lastApplied = 0.0f;
            refreshGraphs();
        };
        startTimerHz (4);
    }

    void visibilityChanged() override { if (isVisible()) sync(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        drawOverlayFrame (g, getLocalBounds(), Icons::sparkle(), "TRANSFORMAR",
                          "Cosmos/Abismo de la receta: cambia el patch nativo (editable, reversible). Abajo, la transformacion del motor procesa el audio.");
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("RECETA", recipeLabel, juce::Justification::centredLeft);
        g.drawText (statusText, statusArea, juce::Justification::centredLeft);
        g.drawText ("CANDADOS (los modulos bloqueados no cambian)", locksLabel, juce::Justification::centredLeft);
        g.drawText ("MOTOR (procesa el audio de cada voz; no cambia la receta)", engineLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        r.removeFromTop (46);
        recipeLabel = r.removeFromTop (18);
        auto bar = r.removeFromTop (40);
        revertButton.setBounds (bar.removeFromRight (170).reduced (4, 4));
        polarity.setBounds (bar);
        statusArea = r.removeFromTop (18);
        r.removeFromTop (4);
        auto graphs = r.removeFromTop (juce::jmax (120, r.getHeight() - 150));
        graphBefore.setBounds (graphs.removeFromLeft (graphs.getWidth() / 2).reduced (0, 0).withTrimmedRight (6));
        graphAfter.setBounds (graphs.withTrimmedLeft (6));
        r.removeFromTop (6);
        locksLabel = r.removeFromTop (16);
        locks.setBounds (r.removeFromTop (32));
        r.removeFromTop (4);
        engineLabel = r.removeFromTop (16);
        engineStrip.setBounds (r);
    }

private:
    void sync()
    {
        polarity.setValue (proc.recipePolarity(), juce::dontSendNotification);
        lastApplied = (float) polarity.getValue();
        refreshGraphs();
    }
    void refreshGraphs()
    {
        const auto now = proc.currentSynthPatch();
        graphAfter.setPatch (now, "Receta actual");
        if (proc.recipePolarityActive())
        {
            const auto e = proc.recipePolarity();
            graphBefore.setPatch (basePatch(), "Base (se vuelve exacta con \"Volver a la base\")");
            statusText = juce::String (e >= 0 ? "Cosmos " : "Abismo ") + juce::String (juce::roundToInt (std::abs (e) * 100)) + " % sobre la receta base";
        }
        else
        {
            graphBefore.setPatch (now, "Base");
            statusText = "Sin cambios: mueve la barra hacia Abismo o Cosmos";
        }
        repaint();
    }
    Forge::SynthPatch basePatch() const
    {
        const auto xml = proc.state.state["forgePolarity"].toString();
        if (auto x = juce::parseXML (xml)) return Forge::PolarityEdit::fromValueTree (juce::ValueTree::fromXml (*x)).base;
        return proc.currentSynthPatch();
    }
    void timerCallback() override
    {
        if (! visibleInTree (this) || polarity.isMouseButtonDown()) return;
        if (std::abs ((float) polarity.getValue() - proc.recipePolarity()) > 0.005f) sync();
    }

    NebulaAudioProcessor& proc;
    RecipePolaritySlider polarity;
    IconButton revertButton { "Volver a la base", Icons::undo(), "Deshace Cosmos/Abismo de la receta: vuelve exactamente al patch base" };
    PatchGraphView graphBefore, graphAfter;
    ModuleLockStrip locks;
    TransformStrip engineStrip;
    juce::Rectangle<int> recipeLabel, statusArea, locksLabel, engineLabel;
    juce::String statusText;
    float lastApplied = 0.0f;
};
