#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

// Fondo y título comunes de las ventanas que se abren encima
inline void drawOverlayFrame (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::Path& icon, const juce::String& title, const juce::String& subtitle)
{
    using namespace NebulaColours;
    g.setColour (juce::Colour (0xf4151824));
    g.fillRoundedRectangle (bounds.toFloat(), 10.0f);
    g.setColour (accent.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 10.0f, 1.2f);
    Icons::draw (g, icon, { 16.0f, 12.0f, 18.0f, 18.0f }, accent2, 1.5f);
    g.setColour (text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, 42, 10, bounds.getWidth() - 60, 22, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (subtitle, 42, 30, bounds.getWidth() - 60, 18, juce::Justification::centredLeft);
}

// ===========================================================================
// Receta: qué es el sonido y cómo está hecho
// ===========================================================================
class RecipeText : public juce::Component
{
public:
    void setText (const juce::String& t) { text = t; layoutFor (getWidth()); }

    int heightFor (int width) { layoutFor (width); return (int) std::ceil (layout.getHeight()) + 20; }

    void paint (juce::Graphics& g) override { layout.draw (g, getLocalBounds().toFloat().reduced (4.0f, 6.0f)); }
    void resized() override { layoutFor (getWidth()); }

    juce::String text;

private:
    void layoutFor (int width)
    {
        using namespace NebulaColours;
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (3.0f);
        juce::StringArray lines;
        lines.addLines (text);
        for (auto line : lines)
        {
            if (line.startsWith ("# "))
                as.append (line.substring (2) + "\n", juce::FontOptions (19.0f, juce::Font::bold), accent2);
            else if (line.startsWith ("## "))
                as.append ("\n" + line.substring (3) + "\n", juce::FontOptions (15.0f, juce::Font::bold), accent2.interpolatedWith (juce::Colours::white, 0.3f));
            else if (line.startsWith ("- "))
                as.append (juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\xa2  ")) + line.substring (2) + "\n", juce::FontOptions (13.0f), NebulaColours::text);
            else if (line.isNotEmpty() && juce::CharacterFunctions::isDigit (line[0]) && line.containsChar ('.'))
            {
                const auto num = line.upToFirstOccurrenceOf (".", true, false);
                as.append ("  " + num + " ", juce::FontOptions (13.0f, juce::Font::bold), accent);
                as.append (line.fromFirstOccurrenceOf (".", false, false).trimStart() + "\n", juce::FontOptions (13.0f), NebulaColours::text);
            }
            else
                as.append (line + "\n", juce::FontOptions (13.0f), textDim.brighter (0.3f));
        }
        layout.createLayout (as, (float) juce::jmax (100, width - 8));
    }

    juce::TextLayout layout;
};

class RecipePanel : public juce::Component
{
public:
    explicit RecipePanel (NebulaAudioProcessor& p) : proc (p)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        for (auto* b : { &copyButton, &closeButton }) { addAndMakeVisible (b); b->horizontal = true; }
        copyButton.onClick = [this]
        {
            // Copia sin los símbolos de formato
            juce::StringArray lines; lines.addLines (content.text);
            for (auto& l : lines) if (l.startsWith ("# ")) l = l.substring (2).toUpperCase(); else if (l.startsWith ("## ")) l = "\n" + l.substring (3).toUpperCase();
            juce::SystemClipboard::copyTextToClipboard (lines.joinIntoString ("\n"));
            copyButton.setButtonText ("Copiado");
        };
        closeButton.onClick = [this] { setVisible (false); };
    }

    void open()
    {
        content.setText (proc.recipeText());
        copyButton.setButtonText ("Copiar");
        setVisible (true);
        toFront (true);
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        drawOverlayFrame (g, getLocalBounds(), Icons::recipe(), "RECETA DEL SONIDO",
                          "Que es, como lo reconstruyo Nebula y como hacerlo en cualquier sinte");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (44);
        auto bar = r.removeFromBottom (36);
        closeButton.setBounds (bar.removeFromRight (120).reduced (2));
        copyButton.setBounds (bar.removeFromRight (120).reduced (2));
        r.removeFromBottom (6);
        viewport.setBounds (r);
        const int w = r.getWidth() - viewport.getScrollBarThickness() - 4;
        content.setSize (w, juce::jmax (r.getHeight(), content.heightFor (w)));
    }

private:
    NebulaAudioProcessor& proc;
    juce::Viewport viewport;
    RecipeText content;
    IconButton copyButton  { "Copiar", Icons::save(), "Copia la receta como texto" },
               closeButton { "Cerrar", Icons::stop(), "Cerrar" };
};

// ===========================================================================
// Limpiar: opciones para rescatar una parte del audio + historial
// ===========================================================================
class CleanCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    CleanCard (NebulaAudioProcessor& p, int k) : proc (p), kind (k)
    {
        setTooltip (Cleaner::tips()[k]);
        for (auto* b : { &playButton, &rescueButton }) { addAndMakeVisible (b); b->horizontal = true; }
        rescueButton.filled = true;
        rescueButton.colour = NebulaColours::accent2;
        playButton.onClick = [this]
        {
            if (proc.isPreviewing()) { proc.stopPreview(); return; }
            if (auto r = proc.cleaner.result (kind)) proc.startPreview (r);
        };
        rescueButton.onClick = [this] { if (onRescue) onRescue (kind); };
    }

    static juce::Path iconFor (int k)
    {
        switch (k)
        {
            case Cleaner::MainNote: return Icons::note();
            case Cleaner::Tonal:    return Icons::sine();
            case Cleaner::Hits:     return Icons::drum();
            case Cleaner::Denoise:  return Icons::broom();
            case Cleaner::Lows:     return Icons::filterShape (0);
            case Cleaner::Mids:     return Icons::filterShape (1);
            default:                return Icons::filterShape (2);
        }
    }

    std::function<void (int)> onRescue;

    void refresh()
    {
        auto r = proc.cleaner.result (kind);
        if (r != shown)
        {
            shown = r;
            wave.clear();
            if (r != nullptr)
            {
                const int w = 160;
                const float* x = r->data();
                const int n = r->length();
                std::vector<float> mx ((size_t) w, 0.0f);
                for (int i = 0; i < n; ++i) { auto& m = mx[(size_t) juce::jmin (w - 1, i * w / n)]; m = juce::jmax (m, std::abs (x[i])); }
                wave.startNewSubPath (0.0f, 0.5f);
                for (int i = 0; i < w; ++i) wave.lineTo ((float) i / (w - 1), 0.5f - 0.5f * mx[(size_t) i]);
                for (int i = w - 1; i >= 0; --i) wave.lineTo ((float) i / (w - 1), 0.5f + 0.5f * mx[(size_t) i]);
                wave.closeSubPath();
            }
        }
        const bool ready = r != nullptr;
        playButton.setEnabled (ready);
        rescueButton.setEnabled (ready);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto b = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
        Icons::draw (g, iconFor (kind), { 10.0f, 9.0f, 18.0f, 18.0f }, accent2, 1.5f);
        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (Cleaner::names()[kind], 34, 8, getWidth() - 40, 20, juce::Justification::centredLeft);

        auto w = waveArea().toFloat();
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (w, 5.0f);
        if (shown != nullptr)
        {
            auto p = wave;
            p.applyTransform (juce::AffineTransform::scale (w.getWidth(), w.getHeight()).translated (w.getX(), w.getY()));
            g.setGradientFill (juce::ColourGradient (accent, w.getX(), 0.0f, accent2, w.getRight(), 0.0f, false));
            g.fillPath (p);
        }
        else
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (proc.cleaner.isRunning() ? "calculando..." : "no quedo nada", w.toNearestInt(), juce::Justification::centred);
        }
        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText (Cleaner::tips()[kind], getLocalBounds().reduced (10, 0).withTrimmedTop (30).withHeight (28), juce::Justification::topLeft, 2, 0.9f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto bar = r.removeFromBottom (28);
        playButton.setBounds (bar.removeFromLeft (bar.getWidth() / 2 - 3));
        bar.removeFromLeft (6);
        rescueButton.setBounds (bar);
    }

private:
    juce::Rectangle<int> waveArea() const { return getLocalBounds().reduced (8).withTrimmedTop (56).withTrimmedBottom (34); }

    NebulaAudioProcessor& proc;
    int kind;
    std::shared_ptr<const SampleData> shown;
    juce::Path wave;
    IconButton playButton   { "Escuchar", Icons::play(), "Escuchar esta opcion" },
               rescueButton { "Rescatar este", Icons::rescue(), "Usar esta version: pasa a ser tu sample (queda en el historial)" };
};

class CleanerPanel : public juce::Component, private juce::Timer
{
public:
    explicit CleanerPanel (NebulaAudioProcessor& p) : proc (p)
    {
        for (int k = 0; k < Cleaner::count; ++k)
        {
            cards[(size_t) k] = std::make_unique<CleanCard> (p, k);
            cards[(size_t) k]->onRescue = [this] (int kind)
            {
                proc.stopPreview();
                if (proc.rescueCleaned (kind)) { rebuildHistory(); if (onSampleChanged) onSampleChanged(); }
            };
            addAndMakeVisible (*cards[(size_t) k]);
        }
        for (auto* b : { &playCurrent, &closeButton }) { addAndMakeVisible (b); b->horizontal = true; }
        playCurrent.onClick = [this] { if (proc.isPreviewing()) proc.stopPreview(); else if (auto s = proc.sampleSource.get()) proc.startPreview (s); };
        closeButton.onClick = [this] { proc.stopPreview(); setVisible (false); };
    }

    std::function<void()> onSampleChanged;

    void open()
    {
        auto s = proc.sampleSource.get();
        if (s == nullptr) return;
        if (proc.cleaner.input() != s || (! proc.cleaner.isRunning() && proc.cleaner.result (Cleaner::Tonal) == nullptr))
            proc.startCleaning();
        rebuildHistory();
        setVisible (true);
        toFront (true);
        startTimerHz (12);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto s = proc.sampleSource.get();
        drawOverlayFrame (g, getLocalBounds(), Icons::broom(), "LIMPIAR EL AUDIO",
                          "Escucha cada opcion y rescata la que quieres. Puedes seguir limpiando sobre lo rescatado.");
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("HISTORIAL", 16, historyY, 90, 26, juce::Justification::centredLeft);
        if (proc.cleaner.isRunning())
        {
            auto bar = juce::Rectangle<float> (16.0f, (float) historyY + 34.0f, (float) getWidth() - 32.0f, 4.0f);
            g.setColour (track);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setGradientFill (juce::ColourGradient (accent, bar.getX(), 0.0f, accent2, bar.getRight(), 0.0f, false));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (4.0f, bar.getWidth() * proc.cleaner.progress.load())), 2.0f);
        }
        juce::ignoreUnused (s);

        // Ayuda en el hueco que queda libre de la cuadrícula
        if (Cleaner::count < 8)
        {
            auto last = cards[Cleaner::count - 1]->getBounds();
            auto help = last.withX (last.getRight() + 10).reduced (6, 10);
            g.setColour (textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("Como se usa:\n1. Escucha las opciones.\n2. \"Rescatar este\" vuelve esa version tu sample.\n"
                              "3. Puedes seguir limpiando sobre lo rescatado (por ejemplo Parte tonal y luego Sin ruido).\n"
                              "4. Arriba, en el historial, regresas a cualquier version.",
                              help, juce::Justification::topLeft, 8, 1.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (44);
        historyY = r.getY();
        auto hist = r.removeFromTop (26).withTrimmedLeft (90);
        for (auto& c : chips)
        {
            const int w = juce::jmin (170, 24 + juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (12.5f)), c->getButtonText()) + 26);
            c->setBounds (hist.removeFromLeft (w));
            hist.removeFromLeft (16);
        }
        r.removeFromTop (16);
        auto bar = r.removeFromBottom (36);
        closeButton.setBounds (bar.removeFromRight (120).reduced (2));
        playCurrent.setBounds (bar.removeFromLeft (180).reduced (2));
        r.removeFromBottom (8);

        const int cols = 4, rows = 2, gap = 10;
        const int cw = (r.getWidth() - gap * (cols - 1)) / cols, ch = (r.getHeight() - gap) / rows;
        for (int k = 0; k < Cleaner::count; ++k)
            cards[(size_t) k]->setBounds (r.getX() + (k % cols) * (cw + gap), r.getY() + (k / cols) * (ch + gap), cw, ch);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // Flechas entre versiones del historial
        g.setColour (NebulaColours::textDim);
        for (size_t i = 1; i < chips.size(); ++i)
        {
            const auto a = chips[i - 1]->getBounds(), b = chips[i]->getBounds();
            g.drawArrow ({ (float) a.getRight() + 2.0f, (float) a.getCentreY(), (float) b.getX() - 2.0f, (float) b.getCentreY() }, 1.2f, 6.0f, 6.0f);
        }
    }

private:
    void timerCallback() override
    {
        if (! isVisible()) { stopTimer(); return; }
        for (auto& c : cards) c->refresh();
        playCurrent.setButtonText (proc.isPreviewing() ? "Detener" : "Escuchar el actual");
        if ((int) chips.size() != (int) proc.history.size() || shownIndex != proc.historyIndex) rebuildHistory();
        repaint (0, historyY + 30, getWidth(), 10);
    }

    void rebuildHistory()
    {
        chips.clear();
        for (int i = 0; i < (int) proc.history.size(); ++i)
        {
            auto c = std::make_unique<IconButton> (proc.history[(size_t) i].label, i == 0 ? Icons::waveform() : Icons::rescue(),
                                                   "Volver a esta version");
            c->horizontal = true;
            c->setToggleState (i == proc.historyIndex, juce::dontSendNotification);
            c->colour = NebulaColours::accent2;
            c->onClick = [this, i]
            {
                proc.stopPreview();
                proc.restoreVersion (i);
                rebuildHistory();
                if (onSampleChanged) onSampleChanged();
            };
            addAndMakeVisible (*c);
            chips.push_back (std::move (c));
        }
        shownIndex = proc.historyIndex;
        resized();
        repaint();
    }

    NebulaAudioProcessor& proc;
    std::array<std::unique_ptr<CleanCard>, Cleaner::count> cards;
    std::vector<std::unique_ptr<IconButton>> chips;
    int historyY = 58, shownIndex = -1;
    IconButton playCurrent { "Escuchar el actual", Icons::play(), "Escuchar la version actual completa" },
               closeButton { "Listo", Icons::rescue(), "Cerrar" };
};
