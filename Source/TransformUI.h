#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

// ===========================================================================
// Control ABISMO <-> ORIGINAL <-> COSMOS: una barra bipolar (brasa a la izquierda, hielo a la derecha)
// con muesca en el centro (doble clic = original), más Identidad y Salida.
// ===========================================================================
class TransformSlider : public juce::Slider
{
public:
    explicit TransformSlider (juce::AudioProcessorValueTreeState& s)
    {
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setDoubleClickReturnValue (true, 0.0);
        setMouseDragSensitivity (300);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, ParamID::transform, *this);
        setTooltip ("Izquierda: ABISMO (fragmenta, deforma, caos reproducible). Derecha: COSMOS (expande, mueve, abre el espacio). "
                    "Centro: el sonido original. Doble clic: volver al centro.");
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NebulaColours;
        auto r = getLocalBounds().toFloat();
        auto labels = r.removeFromTop (16.0f);
        const float v = (float) getValue();

        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (v < -0.01f ? warm : textDim);
        g.drawText ("ABISMO", labels, juce::Justification::centredLeft);
        g.setColour (v > 0.01f ? accent : textDim);
        g.drawText ("COSMOS", labels, juce::Justification::centredRight);
        g.setColour (std::abs (v) <= 0.01f ? text : textDim);
        g.setFont (juce::FontOptions (10.5f));
        const juce::String mid = std::abs (v) <= 0.01f ? "ORIGINAL"
                               : juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + " %";
        g.drawText (mid, labels, juce::Justification::centred);

        auto bar = r.withSizeKeepingCentre (r.getWidth() - 8.0f, 6.0f);
        g.setColour (track);
        g.fillRoundedRectangle (bar, 3.0f);
        const float cx = bar.getCentreX();
        const float x = bar.getX() + bar.getWidth() * (v + 1.0f) * 0.5f;
        // tramo activo desde el centro, con el color de su lado
        auto active = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), bar.getY(), juce::jmax (cx, x), bar.getBottom());
        g.setGradientFill (juce::ColourGradient (warm, bar.getX(), 0.0f, accent, bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle (active, 3.0f);
        // muesca central y marcador fino
        g.setColour (textDim);
        g.fillRect (cx - 0.5f, bar.getY() - 4.0f, 1.0f, bar.getHeight() + 8.0f);
        g.setColour (text);
        g.fillRoundedRectangle (x - 1.5f, bar.getY() - 6.0f, 3.0f, bar.getHeight() + 12.0f, 1.5f);
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class TransformStrip : public juce::Component, private juce::Timer
{
public:
    explicit TransformStrip (NebulaAudioProcessor& p, bool compact = false)
        : proc (p), slider (p.state),
          identity (p.state, ParamID::identity, "Identidad", "Cuanto del sonido original se conserva al transformar (100 % = sin cambio)"),
          output (p.state, ParamID::transformOut, "Salida", "Nivel de salida de Cosmos/Abismo (la proteccion limita los picos igual)"),
          small (compact)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (identity);
        addAndMakeVisible (output);
        identity.maxKnob = output.maxKnob = compact ? 30 : 38;
        startTimerHz (10);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (small)
        {
            // columna angosta: la barra arriba a todo el ancho, las perillas debajo
            slider.setBounds (r.removeFromTop (36));
            r.removeFromTop (2);
            identity.setBounds (r.removeFromLeft (r.getWidth() / 2));
            output.setBounds (r);
            return;
        }
        const int kw = 76;
        output.setBounds (r.removeFromRight (kw));
        identity.setBounds (r.removeFromRight (kw));
        r.removeFromRight (8);
        slider.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 44)));
    }

private:
    void timerCallback() override
    {
        // Identidad y Salida sólo tienen efecto fuera del centro
        const bool on = std::abs (getParamValue (proc.state, ParamID::transform)) > 0.001f;
        identity.setAlpha (on ? 1.0f : 0.45f);
        output.setAlpha (on ? 1.0f : 0.45f);
        slider.repaint();
    }

    NebulaAudioProcessor& proc;
    TransformSlider slider;
    NebKnob identity, output;
    bool small = false;
};
