#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

// ===========================================================================
// Forma de onda del sample con la sección elegida (Inicio / Fin).
// - Arrastra las banderas para elegir la sección
// - Rueda del mouse: zoom · Arrastrar el fondo: moverse
// ===========================================================================
class RegionView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit RegionView (NebulaAudioProcessor& p, bool editable = true) : proc (p), canEdit (editable)
    {
        startTimerHz (30);
        if (canEdit) setTooltip ("Arrastra las banderas para elegir la sección. Rueda: zoom. Arrastra el fondo para moverte.");
    }

    std::function<void()> onClick;   // vista pequeña: abre el editor

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (r, 6.0f);

        auto s = proc.sampleSource.get();
        if (s == nullptr)
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Sin sample", getLocalBounds(), juce::Justification::centred);
            return;
        }
        if (s != shown || (int) r.getWidth() != cachedW || viewStart != cachedVS || viewEnd != cachedVE) rebuild (s);

        // Fuera de la selección: oscuro
        const float a = xFor (proc.regionStart()), b = xFor (proc.regionEnd());
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (a, r.getY(), b - a, r.getHeight()));

        g.setColour (textDim.withAlpha (0.35f));
        g.fillPath (wavePath);
        g.saveState();
        g.reduceClipRegion (juce::Rectangle<int> ((int) a, 0, (int) (b - a) + 1, getHeight()));
        g.setGradientFill (juce::ColourGradient (accent, a, 0.0f, accent2, b, 0.0f, false));
        g.fillPath (wavePath);
        g.restoreState();

        // Banderas
        for (int k = 0; k < 2; ++k)
        {
            const float x = k == 0 ? a : b;
            const auto c = (dragging == k + 1 || hover == k + 1) ? juce::Colours::white : accent2;
            g.setColour (c);
            g.drawLine (x, r.getY(), x, r.getBottom(), 2.0f);
            if (canEdit)
            {
                juce::Path flag;
                if (k == 0) flag.addTriangle (x, r.getY(), x + 12.0f, r.getY() + 7.0f, x, r.getY() + 14.0f);
                else        flag.addTriangle (x, r.getY(), x - 12.0f, r.getY() + 7.0f, x, r.getY() + 14.0f);
                g.fillPath (flag);
            }
        }

        // Cabezal de "Escuchar selección"
        if (proc.isPreviewing())
        {
            const float px = a + (b - a) * juce::jlimit (0.0f, 1.0f, proc.previewProgress());
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawLine (px, r.getY(), px, r.getBottom(), 1.5f);
        }

        if (canEdit && (viewStart > 0.0f || viewEnd < 1.0f))
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText ("zoom x" + juce::String (1.0f / (viewEnd - viewStart), 1), getLocalBounds().reduced (6, 4), juce::Justification::topRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! canEdit) return;
        const int h = hit (e.position.x);
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! canEdit) { if (onClick) onClick(); return; }
        dragging = hit (e.position.x);
        panStart = viewStart;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! canEdit) return;
        const float pos = posFor (e.position.x);
        if (dragging == 1)      proc.setRegion (juce::jmin (pos, proc.regionEnd() - 0.005f), proc.regionEnd());
        else if (dragging == 2) proc.setRegion (proc.regionStart(), juce::jmax (pos, proc.regionStart() + 0.005f));
        else
        {
            // Mover la vista
            const float span = viewEnd - viewStart;
            const float delta = -(float) e.getDistanceFromDragStartX() / (float) getWidth() * span;
            viewStart = juce::jlimit (0.0f, 1.0f - span, panStart + delta);
            viewEnd = viewStart + span;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = 0; }

    // Doble clic: usar todo el sonido
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! canEdit) return;
        proc.setRegion (0.0f, 1.0f);
        zoomAll();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! canEdit) return;
        const float centre = posFor (e.position.x);
        float span = (viewEnd - viewStart) * (w.deltaY > 0 ? 0.8f : 1.25f);
        span = juce::jlimit (0.002f, 1.0f, span);
        const float rel = (centre - viewStart) / (viewEnd - viewStart);
        viewStart = juce::jlimit (0.0f, 1.0f - span, centre - rel * span);
        viewEnd = viewStart + span;
        repaint();
    }

    void zoomToSelection()
    {
        const float pad = (proc.regionEnd() - proc.regionStart()) * 0.1f;
        viewStart = juce::jmax (0.0f, proc.regionStart() - pad);
        viewEnd = juce::jmin (1.0f, proc.regionEnd() + pad);
        repaint();
    }
    void zoomAll() { viewStart = 0.0f; viewEnd = 1.0f; repaint(); }

private:
    void timerCallback() override { if (proc.isPreviewing() || dragging != 0) repaint(); else if (proc.sampleSource.get() != shown) repaint(); }

    float xFor (float pos) const { return (pos - viewStart) / (viewEnd - viewStart) * (float) getWidth(); }
    float posFor (float x) const { return juce::jlimit (0.0f, 1.0f, viewStart + x / (float) juce::jmax (1, getWidth()) * (viewEnd - viewStart)); }
    int hit (float x) const
    {
        if (std::abs (x - xFor (proc.regionStart())) < 8.0f) return 1;
        if (std::abs (x - xFor (proc.regionEnd())) < 8.0f) return 2;
        return 0;
    }

    void rebuild (std::shared_ptr<const SampleData> s)
    {
        shown = s; cachedW = getWidth(); cachedVS = viewStart; cachedVE = viewEnd;
        wavePath.clear();
        const int n = s->length();
        const float* d = s->data();
        const float h = (float) getHeight() * 0.46f, cy = (float) getHeight() * 0.5f;
        for (int c = 0; c < cachedW; ++c)
        {
            const int i0 = (int) ((viewStart + (viewEnd - viewStart) * (float) c / (float) cachedW) * (float) n);
            const int i1 = juce::jmax (i0 + 1, (int) ((viewStart + (viewEnd - viewStart) * (float) (c + 1) / (float) cachedW) * (float) n));
            float mx = 0.0f;
            for (int i = juce::jmax (0, i0); i < juce::jmin (n, i1); ++i) mx = juce::jmax (mx, std::abs (d[i]));
            const float hh = juce::jmax (0.5f, juce::jmin (1.0f, mx) * h);
            wavePath.addRectangle ((float) c, cy - hh, 1.0f, hh * 2.0f);
        }
    }

    NebulaAudioProcessor& proc;
    bool canEdit;
    std::shared_ptr<const SampleData> shown;
    juce::Path wavePath;
    int cachedW = 0, dragging = 0, hover = 0;
    float viewStart = 0.0f, viewEnd = 1.0f, cachedVS = -1.0f, cachedVE = -1.0f, panStart = 0.0f;
};

// ===========================================================================
// Editor de sample (se abre encima del Jardín)
// ===========================================================================
class SampleEditor : public juce::Component, private juce::Timer
{
public:
    explicit SampleEditor (NebulaAudioProcessor& p) : proc (p), region (p, true)
    {
        addAndMakeVisible (region);
        for (auto* b : std::initializer_list<juce::Button*> { &playButton, &autoButton, &allButton, &zoomButton, &doneButton })
            addAndMakeVisible (b);
        for (auto* b : { &playButton, &autoButton, &allButton, &zoomButton, &doneButton }) b->horizontal = true;
        doneButton.filled = true;

        playButton.onClick = [this] { if (proc.isPreviewing()) proc.stopPreview(); else proc.startPreview(); };
        autoButton.onClick = [this] { proc.autoRegion(); region.zoomAll(); };
        allButton.onClick  = [this] { proc.setRegion (0.0f, 1.0f); region.zoomAll(); };
        zoomButton.onClick = [this] { region.zoomToSelection(); };
        doneButton.onClick = [this] { proc.stopPreview(); setVisible (false); if (onClose) onClose(); };

        rootSlider.setSliderStyle (juce::Slider::IncDecButtons);
        rootSlider.setRange (-12.0, 12.0, 0.1);
        rootSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 24);
        rootSlider.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; };
        rootSlider.onValueChange = [this] { proc.setRootOffset ((float) rootSlider.getValue()); repaint(); };
        rootSlider.setTooltip ("Si el sample suena desafinado, corrige aquí su nota (en semitonos)");
        addAndMakeVisible (rootSlider);

        framesBox.addItemList ({ "8 cuadros", "16 cuadros", "32 cuadros", "64 cuadros" }, 1);
        framesBox.setSelectedId (3, juce::dontSendNotification);
        framesBox.setTooltip ("Cuántas ondas sacar del sonido para la tabla (Hacer onda)");
        addAndMakeVisible (framesBox);

        startTimerHz (10);
    }

    std::function<void()> onClose;
    int framesChoice() const { return 8 << (framesBox.getSelectedId() - 1); }

    void open()
    {
        rootSlider.setValue (proc.rootOffset(), juce::dontSendNotification);
        region.zoomAll();
        setVisible (true);
        toFront (true);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        g.setColour (juce::Colour (0xf0151824));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.2f);

        auto icon = Icons::scissors();
        Icons::draw (g, icon, { 16.0f, 12.0f, 18.0f, 18.0f }, accent2, 1.5f);
        g.setColour (text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("ELEGIR SECCION DEL SAMPLE", 42, 10, 400, 22, juce::Justification::centredLeft);

        if (auto s = proc.sampleSource.get())
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (12.0f));
            const double len = s->length() / s->sampleRate;
            const double sel = (proc.regionEnd() - proc.regionStart()) * len;
            g.drawText (s->name + "   |   seleccion: " + juce::String (sel, 2) + " s de " + juce::String (len, 1) + " s",
                        42, 30, getWidth() - 60, 18, juce::Justification::centredLeft);

            const float root = s->rootNote + proc.rootOffset();
            g.drawText (juce::String ("Nota: ") + (s->hasPitch ? juce::MidiMessage::getMidiNoteName (juce::roundToInt (root), true, true, 3) : "sin tono claro"),
                        rootLabelArea, juce::Justification::centredRight);
        }
        g.setColour (textDim.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Esta seccion se usa para Imitar, Usar el sample y Hacer onda", getLocalBounds().removeFromBottom (22).reduced (16, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (40);
        r.removeFromBottom (20);
        auto bar = r.removeFromBottom (36);
        r.removeFromBottom (10);
        region.setBounds (r);

        playButton.setBounds (bar.removeFromLeft (120).reduced (2));
        autoButton.setBounds (bar.removeFromLeft (110).reduced (2));
        allButton.setBounds (bar.removeFromLeft (90).reduced (2));
        zoomButton.setBounds (bar.removeFromLeft (120).reduced (2));
        doneButton.setBounds (bar.removeFromRight (100).reduced (2));
        bar.removeFromRight (10);
        framesBox.setBounds (bar.removeFromRight (110).reduced (2, 6));
        bar.removeFromRight (10);
        rootSlider.setBounds (bar.removeFromRight (130).reduced (2, 4));
        rootLabelArea = bar.removeFromRight (100);
    }

private:
    void timerCallback() override
    {
        const bool playing = proc.isPreviewing();
        playButton.setIcon (playing ? Icons::stop() : Icons::play());
        playButton.setButtonText (playing ? "Detener" : "Escuchar");
        repaint (0, 26, getWidth(), 24);
    }

    NebulaAudioProcessor& proc;
    RegionView region;
    IconButton playButton { "Escuchar", Icons::play(), "Escucha solo la seccion elegida" },
               autoButton { "Auto", Icons::magnet(), "Elige la seccion automaticamente (quita silencios)" },
               allButton  { "Todo", Icons::expand(), "Usa el sonido completo" },
               zoomButton { "Acercar", Icons::scissors(), "Acerca la vista a la seleccion" },
               doneButton { "Listo", Icons::play(), "Cerrar el editor" };
    juce::Slider rootSlider;
    juce::ComboBox framesBox;
    juce::Rectangle<int> rootLabelArea;
};

// ===========================================================================
// Panel para dibujar una onda a mano (tabla "Usuario")
// ===========================================================================
class DrawPanel : public juce::Component
{
public:
    explicit DrawPanel (NebulaAudioProcessor& p) : proc (p)
    {
        points.assign (256, 0.0f);
        for (size_t i = 0; i < points.size(); ++i) points[i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / 256.0f);
        for (auto* b : { &smoothButton, &clearButton, &useButton, &cancelButton }) { addAndMakeVisible (b); b->horizontal = true; }
        useButton.filled = true;
        smoothButton.onClick = [this] { smooth(); };
        clearButton.onClick  = [this] { for (size_t i = 0; i < points.size(); ++i) points[i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / 256.0f); repaint(); };
        useButton.onClick    = [this]
        {
            proc.setUserTableFromDrawing (points);
            setParamValue (proc.state, ParamID::osc1Wave, (float) Wave::Table);
            setParamValue (proc.state, ParamID::osc1Table, (float) WavetableBank::userIndex);
            setVisible (false);
        };
        cancelButton.onClick = [this] { setVisible (false); };
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        g.setColour (juce::Colour (0xf0151824));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.2f);
        Icons::draw (g, Icons::pencil(), { 16.0f, 12.0f, 18.0f, 18.0f }, accent2, 1.5f);
        g.setColour (text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("DIBUJA TU ONDA", 42, 10, 300, 22, juce::Justification::centredLeft);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Pinta con el mouse un ciclo de la onda. Se usara en el oscilador 1 (tabla Usuario).", 42, 30, getWidth() - 60, 18, juce::Justification::centredLeft);

        auto r = canvas().toFloat();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (panelEdge);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        juce::Path p;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (points.size() - 1);
            const float y = r.getCentreY() - points[i] * r.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setGradientFill (juce::ColourGradient (accent, r.getX(), 0.0f, accent2, r.getRight(), 0.0f, false));
        g.strokePath (p, juce::PathStrokeType (2.5f));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        auto bar = r.removeFromBottom (36);
        smoothButton.setBounds (bar.removeFromLeft (120).reduced (2));
        clearButton.setBounds (bar.removeFromLeft (120).reduced (2));
        useButton.setBounds (bar.removeFromRight (140).reduced (2));
        cancelButton.setBounds (bar.removeFromRight (110).reduced (2));
    }

    void mouseDown (const juce::MouseEvent& e) override { last = e.position; paintAt (e.position, e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (last, e.position); last = e.position; }

private:
    juce::Rectangle<int> canvas() const { return getLocalBounds().reduced (14).withTrimmedTop (44).withTrimmedBottom (46); }

    void paintAt (juce::Point<float> from, juce::Point<float> to)
    {
        auto r = canvas().toFloat();
        auto idx = [&] (float x) { return juce::jlimit (0, (int) points.size() - 1, (int) ((x - r.getX()) / r.getWidth() * (float) (points.size() - 1) + 0.5f)); };
        auto val = [&] (float y) { return juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - y) / (r.getHeight() * 0.45f)); };
        int i0 = idx (from.x), i1 = idx (to.x);
        float v0 = val (from.y), v1 = val (to.y);
        if (i0 > i1) { std::swap (i0, i1); std::swap (v0, v1); }
        for (int i = i0; i <= i1; ++i)
            points[(size_t) i] = i1 == i0 ? v1 : v0 + (v1 - v0) * (float) (i - i0) / (float) (i1 - i0);
        repaint();
    }

    void smooth()
    {
        auto c = points;
        const int n = (int) points.size();
        for (int i = 0; i < n; ++i)
            points[(size_t) i] = (c[(size_t) ((i - 2 + n) % n)] + 2.0f * c[(size_t) ((i - 1 + n) % n)] + 3.0f * c[(size_t) i]
                                + 2.0f * c[(size_t) ((i + 1) % n)] + c[(size_t) ((i + 2) % n)]) / 9.0f;
        repaint();
    }

    NebulaAudioProcessor& proc;
    std::vector<float> points;
    juce::Point<float> last;
    IconButton smoothButton { "Suavizar", Icons::sine(), "Redondea la onda" },
               clearButton  { "Reiniciar", Icons::back(), "Vuelve a una onda seno" },
               useButton    { "Usar esta onda", Icons::cycle(), "La pone en el oscilador 1" },
               cancelButton { "Cancelar", Icons::stop(), "Cerrar sin cambios" };
};
