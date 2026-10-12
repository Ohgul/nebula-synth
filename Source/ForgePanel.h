#pragma once
#include "GenomeUI.h"

// ===========================================================================
// FORGE: busca recetas del motor Synth para el sample (síntesis inversa). Cada candidato se escucha, se
// compara con la referencia, se usa (pasa a ser el sonido; ya no depende del sample) o se muta en una
// nueva generación. La fidelidad que se muestra es de recreación (cuánto se parece), no de calidad musical.
// ===========================================================================
class ForgeRow : public juce::Component
{
public:
    std::function<void()> onPlay, onUse, onMutate, onPick;

    ForgeRow()
    {
        for (auto* b : { &play, &use, &mutate, &pick }) { addAndMakeVisible (b); b->horizontal = true; }
        use.colour = NebulaColours::accent; use.filled = true;
        pick.colour = NebulaColours::abyss;
        play.onClick = [this] { if (onPlay) onPlay(); };
        use.onClick = [this] { if (onUse) onUse(); };
        mutate.onClick = [this] { if (onMutate) onMutate(); };
        pick.onClick = [this] { if (onPick) onPick(); };
    }
    void setPicked (bool on) { pick.setToggleState (on, juce::dontSendNotification); }

    void set (const Forge::PatchCandidate& c, bool isBest) { cand = c; best = isBest; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (panel.brighter (best ? 0.06f : 0.0f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (best ? accent.withAlpha (0.6f) : panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        auto a = getLocalBounds().reduced (12, 6);
        a.removeFromRight (buttonsWidth + 8);
        // fidelidad: número y barras por dimensión
        auto fid = a.removeFromRight (230);
        g.setColour (text);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText (juce::String (juce::roundToInt (cand.fidelity.overall * 100.0f)) + " %", fid.removeFromLeft (64), juce::Justification::centredLeft);
        const std::pair<const char*, float> dims[] { { "color", cand.fidelity.color }, { "env", cand.fidelity.envelope }, { "tono", cand.fidelity.pitch }, { "golpe", cand.fidelity.attack } };
        int y = fid.getY() + 2;
        for (auto& [name, v] : dims)
        {
            if (v < 0.0f) continue;
            auto line = juce::Rectangle<int> (fid.getX(), y, fid.getWidth(), 10);
            g.setColour (textDim); g.setFont (juce::FontOptions (9.5f));
            g.drawText (name, line.removeFromLeft (40), juce::Justification::centredLeft);
            auto bar = line.removeFromLeft (110).withSizeKeepingCentre (110, 3);
            g.setColour (track); g.fillRect (bar);
            g.setColour (accent2); g.fillRect (bar.withWidth ((int) (bar.getWidth() * v)));
            g.setColour (textDim);
            g.drawText (juce::String (juce::roundToInt (v * 100.0f)), line, juce::Justification::centredRight);
            y += 11;
        }
        a.removeFromRight (10);
        g.setColour (text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        auto top = a.removeFromTop (18);
        g.drawText (cand.id + "   " + cand.patch.architecture, top, juce::Justification::centredLeft);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("semilla " + juce::String (cand.patch.seed) + "   complejidad " + juce::String (cand.complexity) + "   CPU " + juce::String (cand.cpuPercent, 1) + " %"
                    + (cand.parentId.isNotEmpty() ? "   hijo de " + cand.parentId : juce::String()), a.removeFromTop (14), juce::Justification::centredLeft);
        g.drawFittedText (cand.structure, a, juce::Justification::topLeft, 2, 0.9f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8).removeFromRight (buttonsWidth);
        const int h = juce::jmin (28, r.getHeight());
        r = r.withSizeKeepingCentre (r.getWidth(), h);
        play.setBounds (r.removeFromLeft (40).reduced (2, 0));
        use.setBounds (r.removeFromLeft (72).reduced (2, 0));
        mutate.setBounds (r.removeFromLeft (84).reduced (2, 0));
        pick.setBounds (r.reduced (2, 0));
    }

private:
    static constexpr int buttonsWidth = 196 + 64;
    Forge::PatchCandidate cand;
    bool best = false;
    IconButton play { "", Icons::play(), "Escuchar este candidato (no cambia tu sonido)" },
               use { "Usar", Icons::wand(), "Este patch pasa a ser tu sonido: osciladores, filtros y envolventes reales (ya no necesita el sample)" },
               mutate { "Mutar", Icons::dice(), "Nueva generacion: 8 descendientes de este candidato, medidos contra la referencia" },
               pick { "Par", Icons::dna(), "Elegir para cruzar: elige dos candidatos y pulsa Cruzar" };
};

class ForgePanel : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onClose;   // en el espacio DESCUBRIR: volver a CREAR

    explicit ForgePanel (NebulaAudioProcessor& p) : proc (p), locks (p)
    {
        for (auto* b : { &searchButton, &cancelButton, &refButton, &closeButton, &stopButton, &crossButton }) { addAndMakeVisible (b); b->horizontal = true; }
        addAndMakeVisible (graphView);
        addAndMakeVisible (locks);
        crossButton.colour = NebulaColours::abyss;
        crossButton.onClick = [this] { crossover(); };
        searchButton.colour = NebulaColours::accent; searchButton.filled = true;
        searchButton.onClick = [this] { search(); };
        cancelButton.onClick = [this] { proc.cancelForge(); status = "Cancelado"; };
        refButton.onClick = [this] { proc.auditionForgeReference(); };
        stopButton.onClick = [this] { proc.stopPreview(); };
        closeButton.onClick = [this] { if (onClose) onClose(); else setVisible (false); };
        seedBox.setInputRestrictions (9, "0123456789");
        seedBox.setText ("1", juce::dontSendNotification);
        seedBox.setJustification (juce::Justification::centred);
        seedBox.setTooltip ("Semilla de la busqueda: la misma semilla y el mismo sample dan los mismos candidatos");
        addAndMakeVisible (seedBox);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    void open()
    {
        setVisible (true);
        toFront (true);
        refresh();
        startTimerHz (10);
    }
    void visibilityChanged() override { if (! isVisible()) stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        drawOverlayFrame (g, getLocalBounds(), Icons::sparkle(), "DESCUBRIR  FORGE: SINTESIS INVERSA",
                          "Recetas del motor Synth para tu sample. Suenan sin el sample. El % es fidelidad de recreacion, no calidad musical.");
        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("CANDADOS: los modulos bloqueados no cambian al mutar ni al cruzar", locksLabel, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.5f));
        g.setColour (text.withAlpha (0.9f));
        g.drawFittedText (refText, refArea, juce::Justification::topLeft, 3);
        if (proc.isForging())
        {
            auto bar = progressArea.toFloat();
            g.setColour (track); g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (accent); g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * proc.forgeProgress())), 2.0f);
        }
        g.setColour (accent2);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (status, statusArea, juce::Justification::centredLeft);
        g.setColour (textDim);
        g.drawText ("Semilla", seedLabel, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (48);
        auto bar = r.removeFromTop (32);
        closeButton.setBounds (bar.removeFromRight (96).reduced (2));
        bar.removeFromRight (8);
        cancelButton.setBounds (bar.removeFromRight (100).reduced (2));
        searchButton.setBounds (bar.removeFromRight (150).reduced (2));
        seedBox.setBounds (bar.removeFromRight (64).reduced (2, 4));
        seedLabel = bar.removeFromRight (54);
        bar.removeFromRight (8);
        stopButton.setBounds (bar.removeFromRight (40).reduced (2));
        refButton.setBounds (bar.removeFromRight (140).reduced (2));
        statusArea = bar;
        r.removeFromTop (6);
        refArea = r.removeFromTop (44);
        progressArea = r.removeFromTop (4);
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (juce::jmin (190, r.getHeight() / 2));
        viewport.setBounds (r);
        bottom.removeFromTop (6);
        auto lockArea = bottom.removeFromBottom (32);
        locksLabel = bottom.removeFromBottom (16);
        crossButton.setBounds (bottom.removeFromRight (150).removeFromTop (30).reduced (2));
        bottom.removeFromRight (6);
        graphView.setBounds (bottom);
        locks.setBounds (lockArea);
        layoutList();
    }

private:
    struct ListComp : public juce::Component {};

    void crossover()
    {
        if (picked.size() != 2) { status = "Elige dos candidatos con \"Par\" para cruzarlos"; repaint(); return; }
        const auto seed = juce::jmax ((juce::int64) 1, seedBox.getText().getLargeIntValue());
        if (proc.forgeCrossover (picked[0], picked[1], seed)) status = "Cruzando por modulos...";
        else status = "Para medir a los hijos hace falta el sample de referencia";
        picked.clear();
        repaint();
    }

    void search()
    {
        if (proc.sampleSource.get() == nullptr) { status = "Carga un sample en el Cerebro primero"; repaint(); return; }
        const auto seed = juce::jmax ((juce::int64) 1, seedBox.getText().getLargeIntValue());
        proc.startForge (seed, 8);
        status = "Buscando (semilla " + juce::String (seed) + ")...";
        repaint();
    }

    void refresh()
    {
        const auto cands = proc.forgeCandidates();
        const auto ref = proc.forgeReference();
        refText = ref.lengthS > 0 ? "Referencia: " + ref.name + " - " + ref.summary() : juce::String ("Pulsa Buscar: Nebula analiza la seccion del sample y busca recetas.");
        for (auto& w : ref.warnings) refText << "\nAviso: " << w;
        if (proc.forgeDiagnosticsText().isNotEmpty()) refText << "\nBusqueda: " << proc.forgeDiagnosticsText();
        rows.clear();
        for (size_t i = 0; i < cands.size(); ++i)
        {
            auto row = std::make_unique<ForgeRow>();
            row->set (cands[i], i == 0);
            const int idx = (int) i;
            row->onPlay = [this, idx, c = cands[i]] { proc.auditionForgeCandidate (idx); graphView.setPatch (c.patch, c.id + "  " + c.patch.architecture); };
            row->onPick = [this, idx]
            {
                auto it = std::find (picked.begin(), picked.end(), idx);
                if (it != picked.end()) picked.erase (it);
                else { picked.push_back (idx); if (picked.size() > 2) picked.erase (picked.begin()); }
                for (size_t k = 0; k < rows.size(); ++k) rows[k]->setPicked (std::find (picked.begin(), picked.end(), (int) k) != picked.end());
                crossButton.setEnabled (picked.size() == 2);
            };
            row->onUse = [this, idx, c = cands[i]]
            {
                proc.applySynthPatch (c.patch);
                status = c.id + " es tu sonido. Abre Editar y Modulacion para ver sus osciladores, filtros y envolventes.";
                repaint();
            };
            row->onMutate = [this, idx]
            {
                const auto seed = juce::jmax ((juce::int64) 1, seedBox.getText().getLargeIntValue());
                if (proc.forgeMutate (idx, seed)) status = "Mutando...";
                else status = "Para medir a los descendientes hace falta el sample de referencia";
                repaint();
            };
            list.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        seenRuns = proc.forgeRuns.load();
        picked.clear();
        crossButton.setEnabled (false);
        if (! cands.empty()) graphView.setPatch (cands.front().patch, cands.front().id + "  " + cands.front().patch.architecture);
        else graphView.setPatch (proc.currentSynthPatch(), "Receta actual");
        if (! cands.empty() && ! proc.isForging())
            status = juce::String ((int) cands.size()) + " candidatos, generacion " + juce::String (cands.front().generation)
                   + (proc.forgeGenerations().empty() ? juce::String() : "  (" + juce::String ((int) proc.forgeGenerations().size()) + " anteriores guardadas)");
        layoutList();
        repaint();
    }

    void layoutList()
    {
        const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 2, h = 62;
        list.setSize (juce::jmax (100, w), juce::jmax (viewport.getHeight(), (int) rows.size() * (h + 6)));
        int y = 0;
        for (auto& r : rows) { r->setBounds (0, y, list.getWidth(), h); y += h + 6; }
    }

    void timerCallback() override
    {
        searchButton.setEnabled (! proc.isForging());
        crossButton.setEnabled (! proc.isForging() && picked.size() == 2);
        cancelButton.setEnabled (proc.isForging());
        if (proc.forgeRuns.load() != seenRuns) refresh();
        if (proc.isForging()) repaint();
    }

    NebulaAudioProcessor& proc;
    IconButton searchButton { "Buscar recetas", Icons::wand(), "Analiza la seccion del sample y busca 8 recetas distintas con el motor Synth" },
               cancelButton { "Cancelar", Icons::stop(), "Detener la busqueda" },
               refButton { "Referencia", Icons::speaker(), "Escuchar la referencia (afinada a su tecla)" },
               stopButton { "", Icons::stop(), "Detener la escucha" },
               closeButton { "Cerrar", Icons::back(), "Volver a CREAR" },
               crossButton { "Cruzar", Icons::dice(), "8 hijos que toman cada modulo entero de uno de los dos elegidos (respetando candados)" };
    PatchGraphView graphView;
    ModuleLockStrip locks;
    std::vector<int> picked;
    juce::Rectangle<int> locksLabel;
    juce::TextEditor seedBox;
    juce::Viewport viewport;
    ListComp list;
    std::vector<std::unique_ptr<ForgeRow>> rows;
    juce::Rectangle<int> refArea, progressArea, statusArea, seedLabel;
    juce::String refText, status;
    int seenRuns = -1;
};
