#include "PluginEditor.h"
#include "Presets.h"

using namespace NebulaColours;

//==============================================================================
NebulaLookAndFeel::NebulaLookAndFeel()
{
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
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.6f));
    setColour (juce::TextButton::buttonColourId, track);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextEditor::backgroundColourId, track);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.5f));
    setColour (juce::CaretComponent::caretColourId, accent2);
}

void NebulaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (3.0f, radius * 0.13f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Pista
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (NebulaColours::track);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Valor (bipolar si el rango cruza el cero)
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float fromAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                    : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setGradientFill (juce::ColourGradient (accent, centre.x - radius, centre.y + radius,
                                             accent2, centre.x + radius, centre.y - radius, false));
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cuerpo
    const float bodyR = arcR - lineW * 1.3f;
    g.setColour (slider.isMouseOverOrDragging() ? panelEdge.brighter (0.15f) : panelEdge);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Indicador
    const juce::Point<float> tip (centre.x + (bodyR - 3.0f) * std::sin (angle), centre.y - (bodyR - 3.0f) * std::cos (angle));
    const juce::Point<float> base (centre.x + bodyR * 0.35f * std::sin (angle), centre.y - bodyR * 0.35f * std::cos (angle));
    g.setColour (text);
    g.drawLine ({ base, tip }, 2.2f);
}

void NebulaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.isMouseOver() ? accent : panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (accent2);
    g.fillPath (arrow);
}

//==============================================================================
KnobCell::KnobCell (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
    slider.setColour (juce::Slider::textBoxTextColourId, text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);

    // Doble clic = valor por defecto
    if (auto* p = s.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void KnobCell::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

ChoiceCell::ChoiceCell (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    box.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (box);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
}

void ChoiceCell::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    box.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 100), 26).translated (0, -8));
}

//==============================================================================
NebulaAudioProcessorEditor::NebulaAudioProcessorEditor (NebulaAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    using namespace ParamID;
    oscSection    = { "OSCILADORES", { choice (engine, "Motor"), choice (osc1Wave, "Onda 1"), knob (osc1Octave, "Octava 1"),
                                       choice (osc2Wave, "Onda 2"), knob (osc2Semi, "Semitonos 2"),
                                       knob (osc2Fine, "Fino 2"), knob (oscMix, "Mezcla"), knob (subLevel, "Sub"),
                                       knob (fmAmount, "FM"), knob (noise, "Ruido") } };
    sampleSection = { "SAMPLE", { choice (engine, "Motor"), knob (smpStart, "Inicio"), knob (smpTune, "Afinacion"),
                                  choice (smpReverse, "Reversa"), choice (smpLoop, "Loop"), knob (smpTexture, "Textura"),
                                  knob (smpGrain, "Grano"), knob (subLevel, "Sub"), knob (noise, "Ruido") } };
    filterSection = { "FILTRO", { choice (filterType, "Tipo"), knob (cutoff, "Corte"), knob (resonance, "Resonancia"),
                                  knob (filterEnv, "Env Cant."), knob (drive, "Drive") } };
    ampSection    = { "ENVOLVENTE AMP", { knob (ampA, "Ataque"), knob (ampD, "Decaim."),
                                          knob (ampS, "Sostenido"), knob (ampR, "Liberacion") } };
    fltEnvSection = { "ENVOLVENTE FILTRO", { knob (fltA, "Ataque"), knob (fltD, "Decaim."),
                                             knob (fltS, "Sostenido"), knob (fltR, "Liberacion") } };
    lfoSection    = { "LFO", { knob (lfoRate, "Velocidad"), knob (lfoFilter, "A Filtro"), knob (lfoPitch, "A Tono") } };
    masterSection = { "SALIDA", { knob (master, "Volumen") } };

    // Presets
    const auto& presets = factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        presetBox.addItem (presets[(size_t) i].name, i + 1);
    presetBox.setSelectedId (processor.getPresetIndex() + 1, juce::dontSendNotification);
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.onChange = [this]
    {
        if (const int id = presetBox.getSelectedId(); id > 0)
            processor.loadPreset (id - 1);
    };
    addAndMakeVisible (presetBox);

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    keyboard.setAvailableRange (24, 108);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent2.withAlpha (0.3f));
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd9dbe6));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1a1c26));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff8d92ab));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setLowestVisibleKey (36);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    gardenView = std::make_unique<GardenView> (processor);
    addChildComponent (*gardenView);

    for (auto* t : { &gardenTab, &editTab })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (1001);
        t->setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
        t->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (*t);
    }
    gardenTab.onClick = [this] { if (gardenTab.getToggleState()) showTab (true); };
    editTab.onClick   = [this] { if (editTab.getToggleState()) showTab (false); };

    setSize (1100, 600);
    gardenTab.setToggleState (true, juce::dontSendNotification);
    lastEngineSampler = samplerEngine();
    showTab (true);
    startTimerHz (10);
}

void NebulaAudioProcessorEditor::timerCallback()
{
    // Al cambiar de motor (Sinte/Sample) se cambia la sección de arriba en Editar
    if (samplerEngine() != lastEngineSampler)
    {
        lastEngineSampler = samplerEngine();
        resized();
        updateVisibility();
        repaint();
    }
}

void NebulaAudioProcessorEditor::updateVisibility()
{
    for (auto& cell : cells)
        cell->setVisible (false);
    if (showingGarden)
        return;
    for (auto* s : { &topSection(), &filterSection, &ampSection, &fltEnvSection, &lfoSection, &masterSection })
        for (auto* c : s->cells)
            c->setVisible (true);
}

NebulaAudioProcessorEditor::~NebulaAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

juce::Component* NebulaAudioProcessorEditor::knob (const char* id, const char* name)
{
    auto& c = cells.emplace_back (std::make_unique<KnobCell> (processor.state, id, name));
    addAndMakeVisible (*c);
    return c.get();
}

juce::Component* NebulaAudioProcessorEditor::choice (const char* id, const char* name)
{
    auto& c = cells.emplace_back (std::make_unique<ChoiceCell> (processor.state, id, name));
    addAndMakeVisible (*c);
    return c.get();
}

void NebulaAudioProcessorEditor::showTab (bool garden)
{
    showingGarden = garden;
    gardenView->setVisible (garden);
    updateVisibility();
    repaint();
}

void NebulaAudioProcessorEditor::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    const int current = juce::jmax (0, presetBox.getSelectedItemIndex());
    presetBox.setSelectedItemIndex ((current + delta + n) % n, juce::sendNotification);
}

void NebulaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Cabecera
    g.setGradientFill (juce::ColourGradient (accent, 20.0f, 0.0f, accent2, 200.0f, 0.0f, false));
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("NEBULA", 20, 12, 200, 32, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("PRESET", getWidth() - 376, 16, 60, 24, juce::Justification::centredRight);

    if (showingGarden)
        return;

    for (auto* s : { &topSection(), &filterSection, &ampSection, &fltEnvSection, &lfoSection, &masterSection })
    {
        const auto r = s->area.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (accent2);
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 13.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (s->title, (int) r.getX() + 21, (int) r.getY() + 8, (int) r.getWidth() - 30, 22, juce::Justification::centredLeft);
    }
}

void NebulaAudioProcessorEditor::layoutRow (std::vector<Section*> row, juce::Rectangle<int> area)
{
    constexpr int gap = 12;
    // Los selectores (combo) ocupan un poco más que los knobs
    auto weight = [] (juce::Component* c) { return dynamic_cast<ChoiceCell*> (c) != nullptr ? 1.45f : 1.0f; };
    float totalUnits = 0.0f;
    for (auto* s : row) for (auto* c : s->cells) totalUnits += weight (c);

    const float unit = (float) (area.getWidth() - gap * ((int) row.size() - 1) - 16 * (int) row.size()) / totalUnits;
    float x = (float) area.getX();

    for (auto* s : row)
    {
        float units = 0.0f;
        for (auto* c : s->cells) units += weight (c);
        const int w = juce::roundToInt (unit * units) + 16;
        s->area = { juce::roundToInt (x), area.getY(), w, area.getHeight() };

        auto inner = s->area.reduced (8).withTrimmedTop (26);
        float cx = (float) inner.getX();
        for (auto* c : s->cells)
        {
            const float cw = unit * weight (c);
            c->setBounds (juce::roundToInt (cx), inner.getY(), juce::roundToInt (cw), inner.getHeight());
            cx += cw;
        }

        x += (float) w + gap;
    }
}

void NebulaAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14, 0);

    auto header = r.removeFromTop (56);
    auto tabs = header.withTrimmedLeft (170).removeFromLeft (220).withSizeKeepingCentre (220, 28);
    gardenTab.setBounds (tabs.removeFromLeft (108));
    tabs.removeFromLeft (4);
    editTab.setBounds (tabs);
    auto presetArea = header.removeFromRight (300).withSizeKeepingCentre (300, 28);
    prevButton.setBounds (presetArea.removeFromLeft (28));
    nextButton.setBounds (presetArea.removeFromRight (28));
    presetBox.setBounds (presetArea.reduced (6, 0));

    keyboard.setBounds (r.removeFromBottom (78).withTrimmedBottom (14));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f); // 5 octavas visibles
    r.removeFromBottom (12);

    gardenView->setBounds (r);

    const int rowH = (r.getHeight() - 12) / 2;
    layoutRow ({ &topSection(), &filterSection }, r.removeFromTop (rowH));
    r.removeFromTop (12);
    layoutRow ({ &ampSection, &fltEnvSection, &lfoSection, &masterSection }, r.removeFromTop (rowH));
}
