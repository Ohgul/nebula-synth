#pragma once
#include "Panels.h"

// ===========================================================================
// FORGE: busca recetas del motor Synth para el sample (síntesis inversa). Cada candidato se escucha, se
// compara con la referencia, se usa (pasa a ser el sonido; ya no depende del sample) o se muta en una
// nueva generación. La fidelidad que se muestra es de recreación (cuánto se parece), no de calidad musical.
// ===========================================================================
class ForgeRow : public juce::Component
{
public:
    std::function<void()> onPlay, onUse, onMutate;

    ForgeRow()
    {
        for (auto* b : { &play, &use, &mutate }) { addAndMakeVisible (b); b->horizontal = true; }
        use.colour = NebulaColours::accent; use.filled = true;
        play.onClick = [this] { if (onPlay) onPlay(); };
        use.onClick = [this] { if (onUse) onUse(); };
        mutate.onClick = [this] { if (onMutate) onMutate(); };
    }

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
        mutate.setBounds (r.reduced (2, 0));
    }

private:
    static constexpr int buttonsWidth = 196;
    Forge::PatchCandidate cand;
    bool best = false;
    IconButton play { "", Icons::play(), "Escuchar este candidato (no cambia tu sonido)" },
               use { "Usar", Icons::wand(), "Este patch pasa a ser tu sonido: osciladores, filtros y envolventes reales (ya no necesita el sample)" },
               mutate { "Mutar", Icons::dice(), "Nueva generacion: 8 descendientes de este candidato, medidos contra la referencia" };
};

class ForgePanel : public juce::Component, private juce::Timer
{
public:
    explicit ForgePanel (NebulaAudioProcessor& p) : proc (p)
    {
        for (auto* b : { &searchButton, &cancelButton, &refButton, &closeButton, &stopButton }) { addAndMakeVisible (b); b->horizontal = true; }
        searchButton.colour = NebulaColours::accent; searchButton.filled = true;
        searchButton.onClick = [this] { search(); };
        cancelButton.onClick = [this] { proc.cancelForge(); status = "Cancelado"; };
        refButton.onClick = [this] { proc.auditionForgeReference(); };
        stopButton.onClick = [this] { proc.stopPreview(); };
        closeButton.onClick = [this] { setVisible (false); };
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
        drawOverlayFrame (g, getLocalBounds(), Icons::sparkle(), "FORGE  SINTESIS INVERSA",
                          "Recetas del motor Synth para tu sample. Suenan sin el sample. El % es fidelidad de recreacion, no calidad musical.");
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
        viewport.setBounds (r);
        layoutList();
    }

private:
    struct ListComp : public juce::Component {};

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
        rows.clear();
        for (size_t i = 0; i < cands.size(); ++i)
        {
            auto row = std::make_unique<ForgeRow>();
            row->set (cands[i], i == 0);
            const int idx = (int) i;
            row->onPlay = [this, idx] { proc.auditionForgeCandidate (idx); };
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
        cancelButton.setEnabled (proc.isForging());
        if (proc.forgeRuns.load() != seenRuns) refresh();
        if (proc.isForging()) repaint();
    }

    NebulaAudioProcessor& proc;
    IconButton searchButton { "Buscar recetas", Icons::wand(), "Analiza la seccion del sample y busca 8 recetas distintas con el motor Synth" },
               cancelButton { "Cancelar", Icons::stop(), "Detener la busqueda" },
               refButton { "Referencia", Icons::speaker(), "Escuchar la referencia (afinada a su tecla)" },
               stopButton { "", Icons::stop(), "Detener la escucha" },
               closeButton { "Cerrar", Icons::back(), "Cerrar FORGE" };
    juce::TextEditor seedBox;
    juce::Viewport viewport;
    ListComp list;
    std::vector<std::unique_ptr<ForgeRow>> rows;
    juce::Rectangle<int> refArea, progressArea, statusArea, seedLabel;
    juce::String refText, status;
    int seenRuns = -1;
};
