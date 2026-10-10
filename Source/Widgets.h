#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Icons.h"

// ===========================================================================
// Colores y piezas de interfaz reutilizables (perillas, botones con ícono…)
// ===========================================================================
namespace NebulaColours
{
    inline const juce::Colour background { 0xff12141c };
    inline const juce::Colour panel      { 0xff1b1e2b };
    inline const juce::Colour panelEdge  { 0xff2a2e40 };
    inline const juce::Colour track      { 0xff2e3247 };
    inline const juce::Colour accent     { 0xff8b7cf6 }; // violeta
    inline const juce::Colour accent2    { 0xff4fd1c5 }; // turquesa
    inline const juce::Colour warm       { 0xffff9f6b }; // naranja (filtro 2, avisos)
    inline const juce::Colour text       { 0xffe6e8f2 };
    inline const juce::Colour textDim    { 0xff8d92ab };
}

// Estado "neuronal" de la interfaz: qué tan loco está el sonido (0..1) y el reloj de animación
namespace Neural
{
    inline std::atomic<float> craziness { 0.0f };
    inline float level() { return craziness.load(); }
    // Los colores se calientan con la locura: turquesa/violeta -> magenta -> naranja
    inline juce::Colour hot (juce::Colour base, float extra = 0.0f)
    {
        const float c = juce::jlimit (0.0f, 1.0f, level() + extra);
        if (c < 0.02f) return base;
        const auto mid = juce::Colour (0xffff3fb4), top = juce::Colour (0xffff8a3d);
        return c < 0.6f ? base.interpolatedWith (mid, c / 0.6f) : mid.interpolatedWith (top, (c - 0.6f) / 0.4f);
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

    // Perilla estilo Moog: tapa negra estriada, falda metálica y anillo de LEDs
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace NebulaColours;
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool custom = slider.isColourSpecified (juce::Slider::rotarySliderFillColourId);
        const auto led = custom ? slider.findColour (juce::Slider::rotarySliderFillColourId) : Neural::hot (accent2);
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float zero = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;

        // Anillo de LEDs
        constexpr int numLeds = 15;
        const float ledR = R - 2.2f, dot = juce::jmax (1.5f, R * 0.075f);
        for (int i = 0; i < numLeds; ++i)
        {
            const float t = (float) i / (numLeds - 1);
            const float a = startAngle + t * (endAngle - startAngle);
            const juce::Point<float> p (c.x + ledR * std::sin (a), c.y - ledR * std::cos (a));
            const bool lit = bipolar ? ((t >= juce::jmin (zero, pos) - 0.01f) && (t <= juce::jmax (zero, pos) + 0.01f) && std::abs (pos - zero) > 0.02f)
                                     : (t <= pos + 0.01f && pos > 0.005f);
            if (lit)
            {
                g.setColour (led.withAlpha (0.25f));
                g.fillEllipse (p.x - dot * 2.0f, p.y - dot * 2.0f, dot * 4.0f, dot * 4.0f);
                g.setColour (led.brighter (0.3f));
            }
            else g.setColour (juce::Colour (0xff2a2c36));
            g.fillEllipse (p.x - dot, p.y - dot, dot * 2.0f, dot * 2.0f);
        }

        // Falda metálica
        const float skirt = R * 0.74f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d58), c.x, c.y - skirt, juce::Colour (0xff15161b), c.x, c.y + skirt, false));
        g.fillEllipse (c.x - skirt, c.y - skirt, skirt * 2.0f, skirt * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - skirt, c.y - skirt, skirt * 2.0f, skirt * 2.0f, 1.0f);

        // Tapa negra con estrías
        const float cap = R * 0.58f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (slider.isMouseOverOrDragging() ? 0xff34363f : 0xff2a2b31), c.x, c.y - cap,
                                                 juce::Colour (0xff0b0b0e), c.x, c.y + cap, false));
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (int i = 0; i < 24; ++i)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) i / 24.0f;
            g.drawLine (c.x + cap * 0.86f * std::sin (a), c.y - cap * 0.86f * std::cos (a), c.x + cap * std::sin (a), c.y - cap * std::cos (a), 1.0f);
        }
        // Línea indicadora blanca
        g.setColour (juce::Colour (0xfff2f2f4));
        g.drawLine (c.x + cap * 0.2f * std::sin (angle), c.y - cap * 0.2f * std::cos (angle),
                    c.x + cap * 0.95f * std::sin (angle), c.y - cap * 0.95f * std::cos (angle), juce::jmax (1.6f, R * 0.07f));
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
            g.setColour (sel ? colour.withAlpha (0.85f) : (over ? track.brighter (0.15f) : track));
            g.fillRoundedRectangle (r, 5.0f);
            auto iconArea = r.reduced (r.getWidth() * 0.16f, 3.0f);
            if (caps.size() > i) iconArea = iconArea.withTrimmedBottom (12.0f);
            Icons::draw (g, iconFor (i), iconArea, sel ? juce::Colours::white : textDim.brighter (over ? 0.4f : 0.0f), 1.6f);
            if (caps.size() > i)
            {
                g.setColour (sel ? juce::Colours::white : textDim);
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
        g.setColour ((on ? col.withAlpha (0.85f) : (down ? track.brighter (0.25f) : over ? track.brighter (0.12f) : track)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 6.0f);
        if (filled && ! on)
        {
            g.setColour (col.withAlpha (0.35f * alpha));
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour ((on ? col : panelEdge.brighter (over ? 0.4f : 0.1f)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        const auto ink = (on || filled ? juce::Colours::white : NebulaColours::text).withMultipliedAlpha (alpha);
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
    using namespace NebulaColours;
    const auto r = area.toFloat();
    const float cr = Neural::level();
    g.setColour (panel.withAlpha (0.94f));
    g.fillRoundedRectangle (r, 8.0f);
    // Borde que "brilla" más entre más loco está el sonido
    const auto edge = Neural::hot (panelEdge.brighter (0.15f), cr * 0.3f);
    if (cr > 0.05f)
    {
        g.setColour (Neural::hot (accentColour).withAlpha (0.12f + 0.25f * cr));
        g.drawRoundedRectangle (r.expanded (1.5f), 9.0f, 2.0f + 2.0f * cr);
    }
    g.setColour (edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    float x = r.getX() + 10.0f;
    const auto ac = Neural::hot (accentColour, 0.0f);
    if (icon != nullptr)
    {
        Icons::draw (g, *icon, { x, r.getY() + 7.0f, 16.0f, 16.0f }, ac, 1.5f);
        x += 21.0f;
    }
    else
    {
        g.setColour (ac);
        g.fillRoundedRectangle (x, r.getY() + 10.0f, 3.0f, 11.0f, 1.5f);
        x += 9.0f;
    }
    g.setColour (text);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (title, (int) x, (int) r.getY() + 6, (int) r.getWidth() - 40, 20, juce::Justification::centredLeft);
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
