#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"
#include "DspParts.h"

// ===========================================================================
// Gráficas interactivas: forma de onda, filtros, envolventes, LFO, matriz
// y osciloscopio.
// ===========================================================================

// ---------------------------------------------------------------------------
// Vista previa de un ciclo del oscilador (se actualiza sola)
class WavePreview : public juce::Component, private juce::Timer
{
public:
    WavePreview (NebulaAudioProcessor& p, const char* waveId, const char* shapeId, const char* tableId)
        : proc (p), wId (waveId), sId (shapeId), tId (tableId) { startTimerHz (15); setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (panelEdge);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4.0f, r.getRight() - 4.0f);

        juce::Path p;
        const int n = (int) points.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + 4.0f + (r.getWidth() - 8.0f) * (float) i / (float) (n - 1);
            const float y = r.getCentreY() - points[(size_t) i] * (r.getHeight() * 0.4f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setGradientFill (juce::ColourGradient (accent, r.getX(), 0.0f, accent2, r.getRight(), 0.0f, false));
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

private:
    void timerCallback() override
    {
        const int w = juce::roundToInt (getParamValue (proc.state, wId));
        const float s = getParamValue (proc.state, sId);
        const int t = juce::roundToInt (getParamValue (proc.state, tId));
        const int uv = proc.tables.userVersion.load();
        if (w == lastW && std::abs (s - lastS) < 0.002f && t == lastT && uv == lastUser && ! points.empty()) return;
        lastW = w; lastS = s; lastT = t; lastUser = uv;

        NebOsc osc;
        juce::Random r (3);
        osc.reset (r, false);
        osc.setFrequency (2.0, 512.0);    // 2 ciclos en 512 muestras: dibujamos el segundo
        auto table = proc.tables.get (t);
        points.assign (256, 0.0f);
        for (int i = 0; i < 256; ++i) osc.next (w, s, table.get());
        float peak = 0.01f;
        for (auto& v : points) { v = osc.next (w, s, table.get()); peak = juce::jmax (peak, std::abs (v)); }
        for (auto& v : points) v /= peak;
        repaint();
    }

    NebulaAudioProcessor& proc;
    const char *wId, *sId, *tId;
    std::vector<float> points;
    int lastW = -1, lastT = -1, lastUser = -1;
    float lastS = -1.0f;
};

// ---------------------------------------------------------------------------
// Curva de los dos filtros, con puntos que se arrastran (frecuencia y resonancia)
class FilterGraph : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit FilterGraph (NebulaAudioProcessor& p) : proc (p)
    {
        startTimerHz (20);
        setTooltip ("Arrastra los puntos: izquierda/derecha cambia la frecuencia, arriba/abajo la resonancia. Doble clic para volver al inicio.");
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (r, 6.0f);

        g.setFont (juce::FontOptions (9.5f));
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (hz);
            g.setColour (panelEdge.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 12.0f);
            g.setColour (textDim.withAlpha (0.7f));
            g.drawText (hz < 1000.0f ? juce::String ((int) hz) : juce::String ((int) (hz / 1000.0f)) + "k",
                        juce::Rectangle<float> (x - 15.0f, r.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::centred);
        }
        g.setColour (panelEdge);
        g.drawHorizontalLine ((int) yFor (0.0f), r.getX(), r.getRight());

        const bool f2 = getParamValue (proc.state, ParamID::f2On) > 0.5f;
        const bool parallel = juce::roundToInt (getParamValue (proc.state, ParamID::filterRouting)) == 1;

        // Respuesta combinada
        juce::Path curve;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / 200.0f;
            const float hz = hzFor (x);
            auto h = response (1, hz);
            if (f2) h = parallel ? 0.5 * (h + response (2, hz)) : h * response (2, hz);
            const float db = juce::Decibels::gainToDecibels ((float) std::abs (h), -60.0f);
            const float y = yFor (db);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        auto fillPath = curve;
        fillPath.lineTo (r.getRight(), r.getBottom());
        fillPath.lineTo (r.getX(), r.getBottom());
        fillPath.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), 0.0f, r.getY(), accent.withAlpha (0.02f), 0.0f, r.getBottom(), false));
        g.fillPath (fillPath);
        g.setColour (accent2);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        // Puntos arrastrables
        for (int f = 1; f <= (f2 ? 2 : 1); ++f)
        {
            const auto pt = handle (f);
            const auto c = f == 1 ? accent2 : warm;
            g.setColour (c.withAlpha (dragging == f || hover == f ? 0.35f : 0.18f));
            g.fillEllipse (pt.x - 12.0f, pt.y - 12.0f, 24.0f, 24.0f);
            g.setColour (c);
            g.fillEllipse (pt.x - 7.0f, pt.y - 7.0f, 14.0f, 14.0f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (f), juce::Rectangle<float> (pt.x - 7.0f, pt.y - 7.0f, 14.0f, 14.0f), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override  { hover = 0; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = hit (e.position);
        if (dragging == 0) return;
        for (auto* id : ids (dragging)) if (auto* p = proc.state.getParameter (id)) p->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == 0) return;
        auto r = getLocalBounds().toFloat();
        const float hz = hzFor (juce::jlimit (r.getX(), r.getRight(), e.position.x));
        const float reso = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / (r.getHeight() * 0.85f));
        auto idsNow = ids (dragging);
        if (auto* p = proc.state.getParameter (idsNow[0])) p->setValueNotifyingHost (p->convertTo0to1 (hz));
        if (auto* p = proc.state.getParameter (idsNow[1])) p->setValueNotifyingHost (p->convertTo0to1 (reso));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging == 0) return;
        for (auto* id : ids (dragging)) if (auto* p = proc.state.getParameter (id)) p->endChangeGesture();
        dragging = 0;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h == 0) return;
        for (auto* id : ids (h)) if (auto* p = proc.state.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
    }

private:
    void timerCallback() override { repaint(); }

    std::array<const char*, 2> ids (int f) const
    {
        return f == 1 ? std::array<const char*, 2> { ParamID::cutoff, ParamID::resonance }
                      : std::array<const char*, 2> { ParamID::f2Cutoff, ParamID::f2Reso };
    }

    std::complex<double> response (int f, float hz) const
    {
        auto v = [this] (const char* id) { return getParamValue (proc.state, id); };
        if (f == 1) return NebFilter::response (juce::roundToInt (v (ParamID::filterType)), juce::roundToInt (v (ParamID::filterSlope)), v (ParamID::cutoff), v (ParamID::resonance), hz);
        return NebFilter::response (juce::roundToInt (v (ParamID::f2Type)), juce::roundToInt (v (ParamID::f2Slope)), v (ParamID::f2Cutoff), v (ParamID::f2Reso), hz);
    }

    juce::Point<float> handle (int f) const
    {
        auto r = getLocalBounds().toFloat();
        const float hz = getParamValue (proc.state, f == 1 ? ParamID::cutoff : ParamID::f2Cutoff);
        const float reso = getParamValue (proc.state, f == 1 ? ParamID::resonance : ParamID::f2Reso);
        const float y = r.getY() + (1.0f - reso) * r.getHeight() * 0.85f;
        return { juce::jlimit (r.getX() + 9.0f, r.getRight() - 9.0f, xFor (hz)), juce::jlimit (r.getY() + 9.0f, r.getBottom() - 9.0f, y) };
    }

    int hit (juce::Point<float> p) const
    {
        const bool f2 = getParamValue (proc.state, ParamID::f2On) > 0.5f;
        if (f2 && p.getDistanceFrom (handle (2)) < 14.0f) return 2;
        if (p.getDistanceFrom (handle (1)) < 14.0f) return 1;
        return 0;
    }

    float xFor (float hz) const
    {
        const float lo = std::log (20.0f), hi = std::log (20000.0f);
        return (float) getWidth() * (std::log (juce::jlimit (20.0f, 20000.0f, hz)) - lo) / (hi - lo);
    }
    float hzFor (float x) const
    {
        const float lo = std::log (20.0f), hi = std::log (20000.0f);
        return std::exp (lo + (hi - lo) * x / (float) juce::jmax (1, getWidth()));
    }
    float yFor (float db) const
    {
        // +18 dB arriba, -42 dB abajo
        const float h = (float) getHeight() - 12.0f;
        return juce::jlimit (0.0f, h, h * (18.0f - db) / 60.0f);
    }

    NebulaAudioProcessor& proc;
    int dragging = 0, hover = 0;
};

// ---------------------------------------------------------------------------
// Envolvente que se edita arrastrando sus puntos
class EnvGraph : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    EnvGraph (NebulaAudioProcessor& p, std::array<const char*, 5> paramIds, juce::Colour c)
        : proc (p), ids (paramIds), colour (c)
    {
        startTimerHz (20);
        setTooltip ("Arrastra los puntos para cambiar la envolvente. Rueda del mouse: curva.");
    }

    void setIds (std::array<const char*, 5> paramIds, juce::Colour c) { ids = paramIds; colour = c; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = plotArea();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        const auto prm = current();
        juce::Path p;
        const float total = totalWidthSeconds (prm);
        const float hold = prm.a + prm.d + total * 0.18f;
        for (int i = 0; i <= 160; ++i)
        {
            const float t = total * (float) i / 160.0f;
            const float x = xForTime (t, prm);
            const float y = r.getBottom() - NebEnv::shapeAt (prm, t, hold) * r.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        auto fill = p;
        fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.35f), 0.0f, r.getY(), colour.withAlpha (0.03f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.0f));

        for (int k = 0; k < 3; ++k)
        {
            const auto pt = point (k, prm);
            g.setColour (colour.withAlpha (dragging == k || hover == k ? 0.35f : 0.15f));
            g.fillEllipse (pt.x - 10.0f, pt.y - 10.0f, 20.0f, 20.0f);
            g.setColour (colour);
            g.fillEllipse (pt.x - 5.0f, pt.y - 5.0f, 10.0f, 10.0f);
        }

        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.0f));
        const char* labels[] { "A", "D / S", "R" };
        for (int k = 0; k < 3; ++k)
        {
            const auto pt = point (k, prm);
            g.drawText (labels[k], juce::Rectangle<float> (pt.x - 20.0f, pt.y - 22.0f, 40.0f, 12.0f), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override  { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = hit (e.position);
        if (dragging < 0) return;
        startPrm = current();
        for (auto* id : ids) if (auto* p = proc.state.getParameter (id)) p->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        auto r = plotArea();
        const float seg = r.getWidth() / 4.0f;
        auto timeFor = [seg] (float dx, float maxT) { const float u = juce::jlimit (0.0f, 1.0f, dx / seg); return juce::jmax (0.001f, u * u * maxT); };
        if (dragging == 0)
            set (ids[0], timeFor (e.position.x - r.getX(), 5.0f));
        else if (dragging == 1)
        {
            const float ax = xForTime (startPrm.a, startPrm);
            set (ids[1], timeFor (e.position.x - ax, 5.0f));
            set (ids[2], juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight()));
        }
        else
        {
            const float sx = r.getX() + seg * 3.0f;
            set (ids[3], timeFor (e.position.x - sx, 8.0f));
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0) return;
        for (auto* id : ids) if (auto* p = proc.state.getParameter (id)) p->endChangeGesture();
        dragging = -1;
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float c = getParamValue (proc.state, ids[4]);
        setParamValue (proc.state, ids[4], juce::jlimit (-1.0f, 1.0f, c + w.deltaY * 0.5f));
    }

private:
    void timerCallback() override { repaint(); }

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (12.0f, 14.0f).withTrimmedTop (6.0f); }

    NebEnv::Params current() const
    {
        auto v = [this] (int k) { return getParamValue (proc.state, ids[(size_t) k]); };
        return { v (0), v (1), v (2), v (3), v (4), false };
    }

    // El eje de tiempo usa 4 segmentos fijos (A, D, sostenido, R) con escala cuadrática
    static float totalWidthSeconds (const NebEnv::Params& p) { return p.a + p.d + 1.0f + p.r; }
    float xForTime (float t, const NebEnv::Params& p) const
    {
        auto r = plotArea();
        const float seg = r.getWidth() / 4.0f;
        auto w = [seg] (float sec, float maxT) { return std::sqrt (juce::jlimit (0.0f, 1.0f, sec / maxT)) * seg; };
        const float wa = w (p.a, 5.0f), wd = w (p.d, 5.0f), wr = w (p.r, 8.0f);
        const float hold = (p.a + p.d) + (totalWidthSeconds (p) * 0.18f);
        juce::ignoreUnused (hold);
        if (t <= p.a) return r.getX() + (p.a > 0 ? t / p.a : 1.0f) * wa;
        t -= p.a;
        if (t <= p.d) return r.getX() + wa + (p.d > 0 ? t / p.d : 1.0f) * wd;
        t -= p.d;
        const float sustainT = totalWidthSeconds (p) * 0.18f;
        if (t <= sustainT) return r.getX() + wa + wd + (t / sustainT) * (seg * 3.0f - wa - wd);
        t -= sustainT;
        return r.getX() + seg * 3.0f + juce::jmin (1.0f, p.r > 0 ? t / p.r : 1.0f) * wr;
    }

    juce::Point<float> point (int k, const NebEnv::Params& p) const
    {
        auto r = plotArea();
        if (k == 0) return { xForTime (p.a, p), r.getY() };
        if (k == 1) return { xForTime (p.a + p.d, p), r.getBottom() - p.s * r.getHeight() };
        const float sustainT = totalWidthSeconds (p) * 0.18f;
        return { xForTime (p.a + p.d + sustainT + p.r, p), r.getBottom() };
    }

    int hit (juce::Point<float> pos) const
    {
        const auto prm = current();
        for (int k = 2; k >= 0; --k) if (pos.getDistanceFrom (point (k, prm)) < 12.0f) return k;
        return -1;
    }

    void set (const char* id, float v) { if (auto* p = proc.state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    NebulaAudioProcessor& proc;
    std::array<const char*, 5> ids;
    juce::Colour colour;
    NebEnv::Params startPrm;
    int dragging = -1, hover = -1;
};

// ---------------------------------------------------------------------------
// Forma del LFO con un punto que muestra por dónde va
class LfoGraph : public juce::Component, private juce::Timer
{
public:
    LfoGraph (NebulaAudioProcessor& p, const char* shapeId, const char* rateId, const char* syncId, juce::Colour c)
        : proc (p), sId (shapeId), rId (rateId), syId (syncId), colour (c) { startTimerHz (30); setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (r, 6.0f);
        auto plot = r.reduced (8.0f, 8.0f);
        g.setColour (panelEdge);
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

        const int shape = juce::roundToInt (getParamValue (proc.state, sId));
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float ph = (float) i / 120.0f;
            const float x = plot.getX() + plot.getWidth() * ph;
            const float y = plot.getCentreY() - NebLfo::shapeAt (shape, ph) * plot.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.0f));

        const float ph = (float) phase;
        const float x = plot.getX() + plot.getWidth() * ph;
        const float y = plot.getCentreY() - NebLfo::shapeAt (shape, ph) * plot.getHeight() * 0.45f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        g.setColour (colour.withAlpha (0.3f));
        g.fillEllipse (x - 9.0f, y - 9.0f, 18.0f, 18.0f);
    }

private:
    void timerCallback() override
    {
        const int sync = juce::roundToInt (getParamValue (proc.state, syId));
        const double cps = sync > 0 ? 2.0 / syncBeats (sync) : getParamValue (proc.state, rId);   // a 120 bpm de referencia
        phase += cps / 30.0 / juce::jmax (0.05f, getParamValue (proc.state, ParamID::modTime));
        phase -= std::floor (phase);
        repaint();
    }

    NebulaAudioProcessor& proc;
    const char *sId, *rId, *syId;
    juce::Colour colour;
    double phase = 0.0;
};

// ---------------------------------------------------------------------------
// Matriz de modulación: filas = destinos, columnas = fuentes.
// Arrastra arriba/abajo en una celda para asignar cuánto modula. Doble clic: borrar.
class MatrixGrid : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit MatrixGrid (NebulaAudioProcessor& p) : proc (p) { startTimerHz (15); }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        const int cols = ModSrc::count, rows = ModDst::count;
        auto area = grid();

        // Encabezados: íconos de las fuentes
        for (int s = 0; s < cols; ++s)
        {
            auto c = cellRect (0, s).withY (area.getY() - headerH).withHeight (headerH);
            Icons::draw (g, Icons::modSource (s), c.toFloat().reduced (c.getWidth() * 0.25f, 4.0f).withTrimmedBottom (11.0f), sourceColour (s), 1.5f);
            g.setColour (hoverCol == s ? text : textDim);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (shortSrc (s), c.removeFromBottom (12), juce::Justification::centred);
        }
        // Filas: nombres de destinos
        for (int d = 0; d < rows; ++d)
        {
            auto c = cellRect (d, 0).withX (0).withWidth (labelW - 4);
            g.setColour (hoverRow == d ? text : textDim);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (modDstNames()[d], c, juce::Justification::centredRight);
        }

        for (int d = 0; d < rows; ++d)
            for (int s = 0; s < cols; ++s)
            {
                auto c = cellRect (d, s).toFloat().reduced (1.5f);
                const float v = getParamValue (proc.state, matrixID (s, d));
                const bool over = (d == hoverRow && s == hoverCol);
                g.setColour (over ? track.brighter (0.15f) : track.withAlpha (0.7f));
                g.fillRoundedRectangle (c, 3.0f);
                if (std::abs (v) > 0.005f)
                {
                    const auto col = sourceColour (s);
                    const float w = c.getWidth() * std::abs (v) * 0.5f;
                    auto bar = v > 0 ? juce::Rectangle<float> (c.getCentreX(), c.getY(), w, c.getHeight())
                                     : juce::Rectangle<float> (c.getCentreX() - w, c.getY(), w, c.getHeight());
                    g.setColour (col.withAlpha (0.85f));
                    g.fillRoundedRectangle (bar, 3.0f);
                    g.setColour (juce::Colours::white);
                    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                    g.drawText (juce::String (juce::roundToInt (v * 100.0f)), c, juce::Justification::centred);
                }
                else
                {
                    g.setColour (panelEdge.brighter (0.3f));
                    g.fillEllipse (c.getCentreX() - 1.5f, c.getCentreY() - 1.5f, 3.0f, 3.0f);
                }
            }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int r, c; cellAt (e.position, r, c);
        if (r != hoverRow || c != hoverCol)
        {
            hoverRow = r; hoverCol = c;
            setTooltip (r >= 0 && c >= 0 ? modSrcNames()[c] + "  >  " + modDstNames()[r] + "   (arrastra arriba/abajo, doble clic borra)" : juce::String());
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverRow = hoverCol = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        cellAt (e.position, dragRow, dragCol);
        if (dragRow < 0 || dragCol < 0) return;
        startValue = getParamValue (proc.state, matrixID (dragCol, dragRow));
        if (auto* p = proc.state.getParameter (matrixID (dragCol, dragRow))) p->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragRow < 0 || dragCol < 0) return;
        const float v = juce::jlimit (-1.0f, 1.0f, startValue - (float) e.getDistanceFromDragStartY() / 120.0f);
        if (auto* p = proc.state.getParameter (matrixID (dragCol, dragRow))) p->setValueNotifyingHost (p->convertTo0to1 (v));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragRow >= 0 && dragCol >= 0)
            if (auto* p = proc.state.getParameter (matrixID (dragCol, dragRow))) p->endChangeGesture();
        dragRow = dragCol = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int r, c; cellAt (e.position, r, c);
        if (r >= 0 && c >= 0) setParamValue (proc.state, matrixID (c, r), 0.0f);
    }

    static juce::Colour sourceColour (int s)
    {
        static const juce::Colour c[] { juce::Colour (0xff4fd1c5), juce::Colour (0xff6fc3ff), juce::Colour (0xff8b7cf6), juce::Colour (0xffc77dff),
                                        juce::Colour (0xffffc857), juce::Colour (0xffff9f6b), juce::Colour (0xffff6b9a), juce::Colour (0xff9be15d) };
        return c[juce::jlimit (0, 7, s)];
    }

private:
    void timerCallback() override { repaint(); }
    static juce::String shortSrc (int s) { static const char* n[] { "Env 2", "Env 3", "LFO 1", "LFO 2", "Vel", "Rueda", "AT", "Nota" }; return n[s]; }

    static constexpr int labelW = 70, headerH = 34;
    juce::Rectangle<int> grid() const { return getLocalBounds().withTrimmedLeft (labelW).withTrimmedTop (headerH); }
    juce::Rectangle<int> cellRect (int row, int col) const
    {
        auto g = grid();
        const float cw = (float) g.getWidth() / ModSrc::count, ch = (float) g.getHeight() / ModDst::count;
        return { g.getX() + juce::roundToInt (col * cw), g.getY() + juce::roundToInt (row * ch), juce::roundToInt (cw), juce::roundToInt (ch) };
    }
    void cellAt (juce::Point<float> p, int& row, int& col) const
    {
        row = col = -1;
        auto g = grid().toFloat();
        if (! g.contains (p)) return;
        col = juce::jlimit (0, ModSrc::count - 1, (int) ((p.x - g.getX()) / g.getWidth() * ModSrc::count));
        row = juce::jlimit (0, ModDst::count - 1, (int) ((p.y - g.getY()) / g.getHeight() * ModDst::count));
    }

    NebulaAudioProcessor& proc;
    int hoverRow = -1, hoverCol = -1, dragRow = -1, dragCol = -1;
    float startValue = 0.0f;
};

// ---------------------------------------------------------------------------
// Osciloscopio de la salida (estable: se alinea con el cruce por cero)
class Scope : public juce::Component, private juce::Timer
{
public:
    explicit Scope (NebulaAudioProcessor& p) : proc (p) { startTimerHz (30); setInterceptsMouseClicks (false, false); }

    // circular: dibuja la onda alrededor de un círculo (para el centro del Jardín)
    bool circular = false;
    float radius = 60.0f;

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        const int n = 512;
        int start = 0;
        for (int i = 1; i < 1024; ++i) if (buf[(size_t) i - 1] <= 0.0f && buf[(size_t) i] > 0.0f) { start = i; break; }

        float peak = 0.05f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (buf[(size_t) (start + i)]));
        gainSm += (1.0f / peak - gainSm) * 0.15f;
        const float gain = juce::jmin (gainSm, 8.0f);

        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float v = juce::jlimit (-1.2f, 1.2f, buf[(size_t) (start + i)] * gain);
            juce::Point<float> pt;
            if (circular)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
                const float rr = radius * (1.0f + 0.18f * v);
                pt = { r.getCentreX() + rr * std::sin (a), r.getCentreY() - rr * std::cos (a) };
            }
            else
                pt = { r.getX() + r.getWidth() * (float) i / (float) (n - 1), r.getCentreY() - v * r.getHeight() * 0.4f };
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        if (circular) p.closeSubPath();
        const float level = juce::jlimit (0.0f, 1.0f, proc.outputLevel.load() * 3.0f);
        g.setColour (accent2.withAlpha (0.25f + 0.6f * level));
        g.strokePath (p, juce::PathStrokeType (circular ? 1.6f : 1.8f));
    }

private:
    void timerCallback() override
    {
        proc.copyScope (buf.data(), (int) buf.size());
        repaint();
    }

    NebulaAudioProcessor& proc;
    std::array<float, 1536> buf {};
    float gainSm = 1.0f;
};
