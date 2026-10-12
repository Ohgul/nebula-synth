#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Icons.h"

// ===========================================================================
// Colores y piezas de interfaz reutilizables (perillas, botones con ícono…)
// ===========================================================================
namespace NebulaColours
{
    // Tokens de diseño 0.4.0 (NEBULA-CORE-040): fondo, superficie, Cosmos y Abismo
    inline const juce::Colour tokenBackground { 0xff090a0d };
    inline const juce::Colour tokenSurface    { 0xff15171d };
    inline const juce::Colour cosmos          { 0xff789dd8 };
    inline const juce::Colour abyss           { 0xffa84742 };

    // Grafito: fondo casi negro, paneles apenas más claros, un solo acento frío y uno cálido
    inline const juce::Colour background = tokenBackground;
    inline const juce::Colour panel      = tokenSurface;
    inline const juce::Colour panelEdge  { 0xff262a32 };
    inline const juce::Colour track      { 0xff1d2027 };
    inline const juce::Colour accent     = cosmos;       // acción principal, Cosmos
    inline const juce::Colour accent2    { 0xff8cc4bb }; // verde agua apagado (estado, señal)
    inline const juce::Colour warm       { 0xffd98a6a }; // brasa (filtro 2, avisos, Abismo)
    inline const juce::Colour text       { 0xffe4e6ea };
    inline const juce::Colour textDim    { 0xff858b95 };
}

// Estado "neuronal" de la interfaz: qué tan loco está el sonido (0..1) y el reloj de animación
namespace Neural
{
    inline std::atomic<float> craziness { 0.0f };
    inline float level() { return craziness.load(); }
    // Los colores se calientan con la locura: turquesa/violeta -> magenta -> naranja
    inline juce::Colour hot (juce::Colour base, float extra = 0.0f)
    {
        // (interfaz minimalista: el calor tiñe, no pinta; como mucho 55 % hacia el color caliente)
        const float c = juce::jlimit (0.0f, 1.0f, level() + extra);
        if (c < 0.02f) return base;
        const auto mid = juce::Colour (0xffd877b0), top = juce::Colour (0xffe0895c);
        const auto hotC = c < 0.6f ? mid : mid.interpolatedWith (top, (c - 0.6f) / 0.4f);
        return base.interpolatedWith (hotC, 0.55f * juce::jmin (1.0f, c / 0.6f));
    }
}

class NebulaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NebulaLookAndFeel()
    {
        using namespace NebulaColours;
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, textDim);
        setColour (juce::ComboBox::backgroundColourId, track);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, panelEdge);
        setColour (juce::ComboBox::arrowColourId, accent2);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.6f));
        setColour (juce::TextButton::buttonColourId, track);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextEditor::backgroundColourId, track);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.5f));
        setColour (juce::CaretComponent::caretColourId, accent2);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff242838));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, accent.withAlpha (0.6f));
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, accent);
    }

    // Minimal rotary control: quiet track, single accent arc and precise indicator.
    // Functional slider mapping remains unchanged.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace NebulaColours;
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto active = slider.isColourSpecified (juce::Slider::rotarySliderFillColourId)
                            ? slider.findColour (juce::Slider::rotarySliderFillColourId)
                            : Neural::hot (accent);
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float zero = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
        auto arc = [&] (float from, float to)
        {
            juce::Path path;
            path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                startAngle + from * (endAngle - startAngle),
                                startAngle + to * (endAngle - startAngle), true);
            return path;
        };
        g.setColour (panelEdge);
        g.strokePath (arc (0.0f, 1.0f), juce::PathStrokeType (2.4f));
        const float from = bipolar ? juce::jmin (zero, pos) : 0.0f;
        const float to = bipolar ? juce::jmax (zero, pos) : pos;
        if (to - from > 0.001f)
        {
            g.setColour (active);
            g.strokePath (arc (from, to), juce::PathStrokeType (2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (panel);
        g.fillEllipse (centre.x - radius * 0.74f, centre.y - radius * 0.74f,
                       radius * 1.48f, radius * 1.48f);
        g.setColour (text.withAlpha (slider.isEnabled() ? 0.95f : 0.35f));
        g.drawLine (centre.x + radius * 0.16f * std::sin (angle),
                    centre.y - radius * 0.16f * std::cos (angle),
                    centre.x + radius * 0.64f * std::sin (angle),
                    centre.y - radius * 0.64f * std::cos (angle), 2.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.isMouseOver() ? NebulaColours::accent : NebulaColours::panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (NebulaColours::accent2);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (12.5f); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 20, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getLabelFont (juce::Label&) override { return juce::FontOptions (11.5f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.5f); }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int w = juce::jmin (300, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (13.0f), tip) + 20);
        const int lines = 1 + juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (13.0f), tip) / 280;
        const int h = 10 + lines * 17;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const juce::Rectangle<float> r (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (findColour (juce::TooltipWindow::textColourId));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (10, 4), juce::Justification::centredLeft, 4, 1.0f);
    }
};

// ---------------------------------------------------------------------------
// Perilla con nombre arriba y valor abajo
class NebKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    NebKnob (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& name, const juce::String& tip = {})
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        label.setFont (juce::FontOptions (10.5f));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
        slider.setMouseDragSensitivity (170);   // menos recorrido de mouse para ir de punta a punta
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramID, slider);
        if (auto* p = s.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        if (tip.isNotEmpty()) { setTooltip (tip); slider.setTooltip (tip); }
    }

    void setColour (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    // Perillas compactas: como mucho "maxKnob" px, con el nombre arriba y el valor abajo, todo centrado
    void resized() override
    {
        auto r = getLocalBounds();
        const int knob = juce::jmax (18, juce::jmin (maxKnob, r.getWidth() - 4, r.getHeight() - 32));
        r = r.withSizeKeepingCentre (r.getWidth(), knob + 31);
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }

    int maxKnob = 42;
    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ---------------------------------------------------------------------------
// Fila de íconos conectada a un parámetro de opciones (o entero)
class IconChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    using IconFn = std::function<juce::Path (int)>;

    IconChoice (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, IconFn icon,
                juce::StringArray tips, juce::StringArray captions = {}, int firstValue = 0)
        : iconFor (std::move (icon)), tooltips (std::move (tips)), caps (std::move (captions)), offset (firstValue)
    {
        param = s.getParameter (paramID);
        jassert (param != nullptr);
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { current = juce::roundToInt (v) - offset; repaint(); });
        attachment->sendInitialUpdate();
    }

    int count() const { return tooltips.size(); }
    juce::Colour colour = NebulaColours::accent;

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        for (int i = 0; i < count(); ++i)
        {
            auto r = cell (i).toFloat().reduced (1.5f);
            const bool sel = i == current, over = i == hover;
            g.setColour (sel ? colour.withAlpha (0.16f) : (over ? track.brighter (0.12f) : track));
            g.fillRoundedRectangle (r, 4.0f);
            if (sel) { g.setColour (colour.withAlpha (0.75f)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); }
            auto iconArea = r.reduced (r.getWidth() * 0.16f, 3.0f);
            if (caps.size() > i) iconArea = iconArea.withTrimmedBottom (12.0f);
            Icons::draw (g, iconFor (i), iconArea, sel ? colour.brighter (0.3f) : textDim.brighter (over ? 0.4f : 0.0f), 1.5f);
            if (caps.size() > i)
            {
                g.setColour (sel ? text : textDim);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (caps[i], r.removeFromBottom (13.0f), juce::Justification::centred);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = cellAt (e.position);
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int c = cellAt (e.position);
        if (c >= 0) attachment->setValueAsCompleteGesture ((float) (c + offset));
    }

    juce::String getTooltip() override { return hover >= 0 && hover < tooltips.size() ? tooltips[hover] : juce::String(); }

private:
    juce::Rectangle<int> cell (int i) const
    {
        const float w = (float) getWidth() / (float) juce::jmax (1, count());
        return { juce::roundToInt (w * i), 0, juce::roundToInt (w * (i + 1)) - juce::roundToInt (w * i), getHeight() };
    }
    int cellAt (juce::Point<float> p) const
    {
        for (int i = 0; i < count(); ++i) if (cell (i).toFloat().contains (p)) return i;
        return -1;
    }

    IconFn iconFor;
    juce::StringArray tooltips, caps;
    int offset = 0, current = 0, hover = -1;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ---------------------------------------------------------------------------
// Botón con ícono y texto corto debajo (o a la derecha)
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& caption, juce::Path iconPath, const juce::String& tip = {})
        : juce::Button (caption), icon (std::move (iconPath))
    {
        setTooltip (tip.isNotEmpty() ? tip : caption);
    }

    juce::Colour colour = NebulaColours::accent;
    bool horizontal = false;
    bool filled = false;

    void setIcon (juce::Path p) { icon = std::move (p); repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        const float alpha = isEnabled() ? 1.0f : 0.35f;
        const auto col = Neural::hot (colour, 0.0f);
        // plano: fondo apenas marcado; encendido = tinte del color + borde fino; "filled" = acción principal
        g.setColour ((down ? track.brighter (0.2f) : over ? track.brighter (0.1f) : track).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 5.0f);
        if (on || filled)
        {
            g.setColour (col.withAlpha ((on ? 0.22f : 0.14f) * alpha));
            g.fillRoundedRectangle (r, 5.0f);
        }
        g.setColour ((on ? col.withAlpha (0.9f) : filled ? col.withAlpha (0.45f) : panelEdge.brighter (over ? 0.35f : 0.0f)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        const auto ink = (on ? col.brighter (0.35f) : filled ? NebulaColours::text : NebulaColours::text.withAlpha (0.88f)).withMultipliedAlpha (alpha);
        const auto caption = getButtonText();
        if (caption.isEmpty())
            Icons::draw (g, icon, r.reduced (r.getHeight() * 0.2f), ink, 1.7f);
        else if (icon.isEmpty())
        {
            g.setColour (ink);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (caption, r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
        }
        else if (horizontal)
        {
            auto iconArea = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.2f);
            Icons::draw (g, icon, iconArea, ink, 1.7f);
            g.setColour (ink);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (caption, r.withTrimmedRight (6.0f), juce::Justification::centredLeft);
        }
        else
        {
            auto textArea = r.removeFromBottom (14.0f);
            Icons::draw (g, icon, r.reduced (r.getHeight() * 0.12f), ink, 1.7f);
            g.setColour (ink);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (caption, textArea, juce::Justification::centred);
        }
    }

private:
    juce::Path icon;
};

// Botón con ícono conectado a un parámetro booleano o de 2 opciones
class IconToggle : public IconButton
{
public:
    IconToggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& caption, juce::Path icon, const juce::String& tip)
        : IconButton (caption, std::move (icon), tip)
    {
        setClickingTogglesState (true);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, paramID, *this);
    }
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Menú desplegable conectado a un parámetro
class ParamCombo : public juce::Component
{
public:
    ParamCombo (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& name, const juce::String& tip = {})
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (paramID)))
            box.addItemList (p->choices, 1);
        box.setTooltip (tip);
        addAndMakeVisible (box);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, paramID, box);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        if (label.getText().isNotEmpty()) label.setBounds (r.removeFromTop (15));
        box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (24, r.getHeight())));
    }
    juce::ComboBox box;
private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// ---------------------------------------------------------------------------
// Dibuja un panel con título e ícono
inline void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::Path* icon = nullptr,
                         juce::Colour accentColour = NebulaColours::accent2)
{
    // Panel discreto: fondo plano, borde de 1 px y título pequeño espaciado. Sin brillos.
    using namespace NebulaColours;
    juce::ignoreUnused (icon);
    const auto r = area.toFloat();
    g.setColour (panel.withAlpha (0.96f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    const float x = r.getX() + 12.0f;
    g.setColour (Neural::hot (accentColour).withAlpha (0.9f));
    g.fillEllipse (x, r.getY() + 13.0f, 4.0f, 4.0f);
    g.setColour (textDim.brighter (0.25f));
    juce::Font f (juce::FontOptions (10.5f, juce::Font::bold));
    f.setExtraKerningFactor (0.08f);
    g.setFont (f);
    g.drawText (title.toUpperCase(), (int) x + 10, (int) r.getY() + 5, (int) r.getWidth() - 60, 20, juce::Justification::centredLeft);
}

// Ayuda: asigna un valor real a un parámetro como gesto completo
inline void setParamValue (juce::AudioProcessorValueTreeState& s, const juce::String& id, float realValue)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}
inline float getParamValue (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* v = s.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}
