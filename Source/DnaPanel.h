#pragma once
#include "Panels.h"

// ===========================================================================
// Editor de ADN: los genes del sonido recreado, agrupados, con candados, mutación con semilla,
// copiar/pegar y comparación A/B. Cada perilla es un parámetro real del motor (automatizable).
// A la izquierda se ve el resultado: armónicos del cuerpo del sonido (actual sobre original) y cómo
// cambia el color a lo largo de la nota.
// ===========================================================================
class DnaSpectrum : public juce::Component
{
public:
    // Recalcula sólo cuando cambia el modelo (la variante cambia cuando se mueve un gen estructural)
    void update (std::shared_ptr<const ResynthModel> variant, std::shared_ptr<const ResynthModel> base)
    {
        if (variant.get() == lastVariant && base.get() == lastBase) return;
        lastVariant = variant.get(); lastBase = base.get();
        cur = analyse (variant); orig = analyse (base);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0d0f14));
        g.fillRoundedRectangle (r, 6.0f);
        if (cur.harm.empty())
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (12.5f));
            g.drawFittedText ("Recrea un sonido en Cerebro para ver y editar su ADN", getLocalBounds().reduced (16), juce::Justification::centred, 3);
            return;
        }
        auto top = r.removeFromTop (r.getHeight() * 0.58f).reduced (10.0f, 8.0f);
        r.removeFromTop (6.0f);
        auto bottom = r.reduced (10.0f, 8.0f);

        g.setFont (juce::FontOptions (10.5f));
        g.setColour (textDim);
        g.drawText ("ARMONICOS DEL CUERPO", top.removeFromTop (14.0f), juce::Justification::centredLeft);
        // barras en dB (0 a -60): original en gris, actual en color
        const int n = (int) cur.harm.size();
        const float bw = top.getWidth() / (float) n;
        auto barH = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * top.getHeight(); };
        for (int k = 0; k < n; ++k)
        {
            const float x = top.getX() + k * bw;
            if (k < (int) orig.harm.size())
            {
                g.setColour (panelEdge.brighter (0.2f));
                const float h = barH (orig.harm[(size_t) k]);
                g.fillRect (x + 1.0f, top.getBottom() - h, juce::jmax (1.0f, bw - 2.0f), h);
            }
            g.setColour (accent2.withAlpha (0.85f));
            const float h = barH (cur.harm[(size_t) k]);
            g.fillRect (x + bw * 0.3f, top.getBottom() - h, juce::jmax (1.0f, bw * 0.4f), h);
        }

        g.setColour (textDim);
        g.drawText ("BRILLO A LO LARGO DE LA NOTA", bottom.removeFromTop (14.0f), juce::Justification::centredLeft);
        auto curve = [&] (const std::vector<float>& c, juce::Colour col, float thick)
        {
            if (c.size() < 2) return;
            float mx = 1.0f;
            for (auto v : cur.bright) mx = juce::jmax (mx, v);
            for (auto v : orig.bright) mx = juce::jmax (mx, v);
            juce::Path p;
            for (size_t i = 0; i < c.size(); ++i)
            {
                const float x = bottom.getX() + bottom.getWidth() * (float) i / (float) (c.size() - 1);
                const float y = bottom.getBottom() - bottom.getHeight() * juce::jlimit (0.0f, 1.0f, c[i] / mx);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        curve (orig.bright, panelEdge.brighter (0.4f), 1.2f);
        curve (cur.bright, accent, 1.8f);
    }

private:
    struct View { std::vector<float> harm, bright; };

    static View analyse (const std::shared_ptr<const ResynthModel>& m)
    {
        View v;
        if (m == nullptr || m->harmonic == nullptr || m->harmonic->numFrames() == 0) return v;
        static constexpr int maxH = 48;
        const int fs = Wavetable::frameSize, frames = m->harmonic->numFrames();
        juce::dsp::FFT fft (11);
        std::vector<float> buf ((size_t) fs * 2);
        auto spectrum = [&] (int f, std::vector<float>& amp)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const float* raw = m->harmonic->rawFrame (f);
            std::copy (raw, raw + fs, buf.begin());
            fft.performFrequencyOnlyForwardTransform (buf.data());
            amp.assign (buf.begin(), buf.begin() + fs / 2);
        };
        std::vector<float> amp;
        // cuadro representativo: donde empieza el cuerpo sostenido, o el centro
        const int body = m->hasLoop() ? juce::jlimit (0, frames - 1, (int) m->frameAt (m->loopStart)) : frames / 2;
        spectrum (body, amp);
        float mx = 1.0e-9f;
        for (int k = 1; k <= maxH; ++k) mx = juce::jmax (mx, amp[(size_t) k]);
        for (int k = 1; k <= maxH; ++k) v.harm.push_back (juce::Decibels::gainToDecibels (amp[(size_t) k] / mx, -80.0f));
        // brillo (centroide en número de armónico) de hasta 64 cuadros repartidos
        const int steps = juce::jmin (64, frames);
        for (int i = 0; i < steps; ++i)
        {
            spectrum (steps > 1 ? i * (frames - 1) / (steps - 1) : 0, amp);
            double num = 0, den = 0;
            for (int k = 1; k < 256; ++k) { const double p = (double) amp[(size_t) k] * amp[(size_t) k]; num += p * k; den += p; }
            v.bright.push_back (den > 0 ? (float) (num / den) : 0.0f);
        }
        return v;
    }

    const ResynthModel* lastVariant = nullptr;
    const ResynthModel* lastBase = nullptr;
    View cur, orig;
};

class DnaPanel : public juce::Component, private juce::Timer
{
public:
    explicit DnaPanel (NebulaAudioProcessor& p) : proc (p)
    {
        addAndMakeVisible (spectrum);
        // Perillas por grupo (A-E; la transformación Cosmos/Abismo está en su propio panel)
        for (auto& gene : Dna::genes())
        {
            const juce::String group (gene.group);
            if (group == "F") continue;
            Cell c;
            c.id = gene.id; c.group = group;
            if (c.id == ParamID::dnaHold)
            {
                c.toggle = std::make_unique<IconToggle> (proc.state, c.id, "Congelar", Icons::cycle(),
                                                         "Al sostener la nota: recorrer el cuerpo del sonido o congelar su timbre");
                c.toggle->horizontal = true;
                addAndMakeVisible (*c.toggle);
            }
            else
            {
                c.knob = std::make_unique<NebKnob> (proc.state, c.id, gene.name, tipFor (c.id));
                c.knob->maxKnob = 38;
                addAndMakeVisible (*c.knob);
                c.lock = std::make_unique<IconButton> ("", Icons::lock(), "Candado: la mutacion no cambia este gen");
                c.lock->setClickingTogglesState (true);
                c.lock->colour = NebulaColours::warm;
                const auto id = c.id;
                auto* lockPtr = c.lock.get();
                c.lock->onClick = [this, id, lockPtr] { proc.setGeneLocked (id, lockPtr->getToggleState()); };
                addAndMakeVisible (*c.lock);
            }
            cells.push_back (std::move (c));
        }

        for (auto* b : { &mutateButton, &undoMutation, &copyButton, &pasteButton, &storeA, &recallA, &storeB, &recallB, &resetButton, &closeButton })
        { addAndMakeVisible (b); b->horizontal = true; }
        mutateButton.colour = NebulaColours::accent; mutateButton.filled = true;
        mutateButton.onClick = [this] { mutate(); };
        undoMutation.onClick = [this] { if (preMutation.isNotEmpty()) { proc.dnaFromJson (preMutation); preMutation = {}; status = "Mutacion deshecha"; } };
        copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (proc.dnaToJson()); status = "ADN copiado"; };
        pasteButton.onClick = [this] { status = proc.dnaFromJson (juce::SystemClipboard::getTextFromClipboard()) ? "ADN pegado" : "El portapapeles no tiene un ADN de Nebula"; };
        storeA.onClick = [this] { proc.storeDnaSlot (0); status = "ADN guardado en A"; };
        storeB.onClick = [this] { proc.storeDnaSlot (1); status = "ADN guardado en B"; };
        recallA.onClick = [this] { status = proc.recallDnaSlot (0) ? "Escuchando A" : "A esta vacio: usa Guardar A"; };
        recallB.onClick = [this] { status = proc.recallDnaSlot (1) ? "Escuchando B" : "B esta vacio: usa Guardar B"; };
        resetButton.onClick = [this]
        {
            for (auto& c : cells) if (! proc.isGeneLocked (c.id)) proc.resetGene (c.id);
            status = "ADN original (salvo los genes con candado)";
        };
        closeButton.onClick = [this] { setVisible (false); };

        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setRange (0.05, 1.0, 0.01);
        amount.setValue (0.35, juce::dontSendNotification);
        amount.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        amount.setTooltip ("Cuanto cambia cada mutacion");
        amount.setColour (juce::Slider::trackColourId, NebulaColours::accent);
        amount.setColour (juce::Slider::backgroundColourId, NebulaColours::track);
        amount.setColour (juce::Slider::thumbColourId, NebulaColours::text);
        addAndMakeVisible (amount);

        seedBox.setInputRestrictions (9, "0123456789");
        seedBox.setJustification (juce::Justification::centred);
        seedBox.setTooltip ("Semilla de la proxima mutacion. La misma semilla sobre el mismo ADN da siempre el mismo resultado.");
        addAndMakeVisible (seedBox);
    }

    void open()
    {
        refreshSeed();
        status = {};
        setVisible (true);
        toFront (true);
        startTimerHz (10);
        timerCallback();
    }

    void visibilityChanged() override { if (! isVisible()) stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        drawOverlayFrame (g, getLocalBounds(), Icons::dna(), "ADN DEL SONIDO",
                          "Cada gen cambia el motor de resintesis. El candado protege un gen de las mutaciones.");
        for (auto& [name, area] : groupAreas)
            drawSection (g, area, name);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Cuanto", amountLabel, juce::Justification::centredRight);
        g.drawText ("Semilla", seedLabel, juce::Justification::centredRight);
        g.setColour (accent2);
        g.drawText (status, statusArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (48);
        auto bar = r.removeFromBottom (34);
        closeButton.setBounds (bar.removeFromRight (96).reduced (2));
        bar.removeFromRight (6);
        for (auto* b : { &recallB, &storeB, &recallA, &storeA }) b->setBounds (bar.removeFromRight (b == &storeA || b == &storeB ? 104 : 40).reduced (2));
        bar.removeFromRight (6);
        pasteButton.setBounds (bar.removeFromRight (76).reduced (2));
        copyButton.setBounds (bar.removeFromRight (76).reduced (2));
        resetButton.setBounds (bar.removeFromRight (96).reduced (2));
        statusArea = bar.reduced (4, 0);
        r.removeFromBottom (8);

        // barra de mutación arriba a la derecha (sobre el título)
        auto mut = getLocalBounds().reduced (14).removeFromTop (34).removeFromRight (520);
        mutateButton.setBounds (mut.removeFromRight (104).reduced (2));
        undoMutation.setBounds (mut.removeFromRight (124).reduced (2));
        mut.removeFromRight (8);
        seedBox.setBounds (mut.removeFromRight (74).reduced (2, 5));
        seedLabel = mut.removeFromRight (52);
        amount.setBounds (mut.removeFromRight (120).reduced (2, 6));
        amountLabel = mut.removeFromRight (52);

        spectrum.setBounds (r.removeFromLeft (juce::jmin (300, r.getWidth() / 3)));
        r.removeFromLeft (10);

        // 3 filas: A+B, C, D+E
        groupAreas.clear();
        const int rowGap = 8, rowH = (r.getHeight() - 2 * rowGap) / 3;
        const std::vector<std::vector<juce::String>> rows { { "A", "B" }, { "C" }, { "D", "E" } };
        for (size_t ri = 0; ri < rows.size(); ++ri)
        {
            auto row = r.removeFromTop (rowH);
            r.removeFromTop (rowGap);
            int total = 0;
            for (auto& gname : rows[ri]) total += count (gname);
            const int cellW = (row.getWidth() - 8 * ((int) rows[ri].size() - 1)) / juce::jmax (1, total);
            for (auto& gname : rows[ri])
            {
                auto area = row.removeFromLeft (cellW * count (gname));
                row.removeFromLeft (8);
                groupAreas.push_back ({ groupTitle (gname), area });
                auto inner = area.reduced (4).withTrimmedTop (24);
                for (auto& c : cells)
                {
                    if (c.group != gname) continue;
                    auto cell = inner.removeFromLeft (cellW);
                    if (c.knob != nullptr)
                    {
                        c.lock->setBounds (cell.getRight() - 22, cell.getY() - 2, 18, 18);
                        c.knob->setBounds (cell.reduced (2, 0));
                    }
                    else if (c.toggle != nullptr)
                        c.toggle->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 8, 30));
                }
            }
        }
    }

private:
    struct Cell
    {
        juce::String id, group;
        std::unique_ptr<NebKnob> knob;
        std::unique_ptr<IconButton> lock;
        std::unique_ptr<IconToggle> toggle;
    };

    int count (const juce::String& g) const { int n = 0; for (auto& c : cells) if (c.group == g) ++n; return n; }
    static juce::String groupTitle (const juce::String& g)
    {
        const auto& names = Dna::groupNames();
        const int i = juce::jlimit (0, names.size() - 1, g[0] - 'A');
        return names[i].toUpperCase();
    }

    static juce::String tipFor (const juce::String& id)
    {
        using namespace ParamID;
        static const std::map<juce::String, juce::String> tips {
            { dnaPitchDepth, "Cuanto se mueve el tono del original (vibrato, glides, caidas): 0 = plano, 2 = exagerado" },
            { dnaFormant, "Mueve el color (formantes) sin cambiar la nota" },
            { dnaTilt, "Brillo: inclina los armonicos hacia los agudos o los graves" },
            { dnaTiltEvolve, "El brillo cambia a lo largo de la nota (negativo = se oscurece)" },
            { dnaOddEven, "Pares/impares: a la izquierda quita los armonicos pares (hueco), a la derecha los refuerza" },
            { dnaHarmonics, "Cuantos armonicos quedan (de casi un seno a todos)" },
            { rsHarm, "Nivel de la capa armonica (tabla de ondas)" },
            { rsPartials, "Nivel de los parciales inarmonicos (campanas, metales)" },
            { rsNoise, "Nivel del ruido (aire, roce, golpe)" },
            { dnaNoiseTilt, "Color del ruido: mas oscuro o mas brillante" },
            { dnaRough, "Aspereza: modulacion rapida e irregular de la amplitud" },
            { dnaDensity, "Densidad: copias desafinadas de la capa armonica (coro interno)" },
            { dnaAttack, "Ataque: estira o acorta el comienzo del sonido original" },
            { dnaTransient, "Golpe: volumen del transitorio original del ataque" },
            { rsSpeed, "Velocidad con la que se recorre el sonido" },
            { dnaRelease, "Cola: estira o acorta el final real del sonido al soltar la nota" },
            { dnaEvolve, "Evolucion: 0 = timbre fijo, 1 = como el original, 2 = cambios exagerados" },
            { rsStretch, "Inarmonia: estira las frecuencias de los parciales (solo si el sonido tiene parciales)" },
            { rsWidth, "Ancho estereo de la resintesis" },
        };
        auto it = tips.find (id);
        return it != tips.end() ? it->second : juce::String();
    }

    void refreshSeed()
    {
        const auto s = (juce::int64) proc.state.state.getProperty ("dnaSeed", 1);
        seedBox.setText (juce::String (s), juce::dontSendNotification);
    }

    void mutate()
    {
        const auto typed = seedBox.getText().getLargeIntValue();
        // la semilla que se escribe tiene prioridad; si no, la siguiente de la secuencia
        juce::int64 seed = typed > 0 ? typed : proc.nextDnaSeed();
        if (typed > 0) proc.state.state.setProperty ("dnaSeed", typed + 1, nullptr);
        preMutation = proc.dnaToJson();
        proc.mutateDna ((float) amount.getValue(), seed);
        status = "Mutacion con semilla " + juce::String (seed);
        refreshSeed();
    }

    void timerCallback() override
    {
        spectrum.update (proc.resynthModel(), proc.baseResynthModel());
        const auto m = proc.resynthModel();
        const bool hasModel = m != nullptr;
        for (auto& c : cells)
        {
            if (c.lock != nullptr) c.lock->setToggleState (proc.isGeneLocked (c.id), juce::dontSendNotification);
            const bool enabled = hasModel && (c.id != ParamID::rsStretch || ! m->partials.empty());
            if (c.knob != nullptr) c.knob->setEnabled (enabled);
            if (c.toggle != nullptr) c.toggle->setEnabled (hasModel && m->hasLoop());
        }
        undoMutation.setEnabled (preMutation.isNotEmpty());
        recallA.setToggleState (false, juce::dontSendNotification);
        repaint (statusArea);
    }

    NebulaAudioProcessor& proc;
    DnaSpectrum spectrum;
    std::vector<Cell> cells;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> groupAreas;
    IconButton mutateButton { "Mutar", Icons::dice(), "Cambia los genes sin candado al azar (reproducible con la semilla)" },
               undoMutation { "Deshacer", Icons::undo(), "Vuelve al ADN anterior a la ultima mutacion" },
               copyButton { "Copiar", Icons::list(), "Copia el ADN como texto (para pegarlo en otra instancia de Nebula)" },
               pasteButton { "Pegar", Icons::save(), "Pega un ADN copiado" },
               storeA { "Guardar A", Icons::save(), "Guarda el ADN actual en A" },
               recallA { "A", {}, "Escuchar el ADN guardado en A" },
               storeB { "Guardar B", Icons::save(), "Guarda el ADN actual en B" },
               recallB { "B", {}, "Escuchar el ADN guardado en B" },
               resetButton { "Original", Icons::history(), "Vuelve todos los genes sin candado a su valor original" },
               closeButton { "Cerrar", Icons::back(), "Cerrar el editor de ADN" };
    juce::Slider amount;
    juce::TextEditor seedBox;
    juce::Rectangle<int> amountLabel, seedLabel, statusArea;
    juce::String status, preMutation;
};
