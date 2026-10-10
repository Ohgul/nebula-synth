#include "PluginEditor.h"
#include "Presets.h"

using namespace NebulaColours;

//==============================================================================
NebulaContent::NebulaContent (NebulaAudioProcessor& p)
    : proc (p), garden (p), edit (p), mod (p), sampleEditor (p), drawPanel (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    const char* names[] { "Jardin", "Editar", "Modulacion" };
    const juce::Path icons[] { Icons::garden(), Icons::sliders(), Icons::modulation() };
    const char* tips[] { "Jardin: cultiva variaciones del sonido y arrastra audios",
                         "Editar: osciladores, filtros, volumen y voces",
                         "Modulacion: envolventes, LFOs y matriz" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[(size_t) i] = std::make_unique<IconButton> (names[i], icons[i], tips[i]);
        tabs[(size_t) i]->horizontal = true;
        tabs[(size_t) i]->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*tabs[(size_t) i]);
    }

    rebuildPresetMenu();
    presetBox.onChange = [this] { presetChosen(); };
    presetBox.setTooltip ("Presets de fabrica y tuyos");
    addAndMakeVisible (presetBox);
    prevButton.onClick = [this] { const int n = presetBox.getNumItems(); if (n > 0) presetBox.setSelectedItemIndex ((juce::jmax (0, presetBox.getSelectedItemIndex()) - 1 + n) % n); };
    nextButton.onClick = [this] { const int n = presetBox.getNumItems(); if (n > 0) presetBox.setSelectedItemIndex ((juce::jmax (0, presetBox.getSelectedItemIndex()) + 1) % n); };
    saveButton.onClick = [this] { savePreset(); };
    folderButton.onClick = [] { auto f = NebulaAudioProcessor::userPresetFolder(); f.createDirectory(); f.revealToUser(); };
    for (auto* b : { &prevButton, &nextButton, &saveButton, &folderButton }) addAndMakeVisible (b);

    zoomBox.addItemList ({ "80 %", "90 %", "100 %", "115 %", "130 %" }, 1);
    zoomBox.setSelectedId (3, juce::dontSendNotification);
    zoomBox.setTooltip ("Tamano de la ventana");
    zoomBox.onChange = [this] { static const float z[] { 0.8f, 0.9f, 1.0f, 1.15f, 1.3f }; if (onZoom) onZoom (z[zoomBox.getSelectedId() - 1]); };
    addAndMakeVisible (zoomBox);

    addChildComponent (garden);
    addChildComponent (edit);
    addChildComponent (mod);
    addChildComponent (sampleEditor);
    addChildComponent (drawPanel);

    garden.onOpenSampleEditor = [this] { sampleEditor.open(); };
    garden.framesForWave = [this] { return sampleEditor.framesChoice(); };
    edit.onOpenSampleEditor = [this] { sampleEditor.open(); };
    edit.onOpenDraw = [this] { drawPanel.setVisible (true); drawPanel.toFront (true); };

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

    setSize (designW, designH);
    showTab (0);
    startTimerHz (2);
}

void NebulaContent::setZoomChoice (float z)
{
    static const float zs[] { 0.8f, 0.9f, 1.0f, 1.15f, 1.3f };
    for (int i = 0; i < 5; ++i)
        if (std::abs (zs[i] - z) < 0.01f) zoomBox.setSelectedId (i + 1, juce::dontSendNotification);
}

void NebulaContent::showTab (int tab)
{
    currentTab = tab;
    garden.setVisible (tab == 0);
    edit.setVisible (tab == 1);
    mod.setVisible (tab == 2);
    for (int i = 0; i < 3; ++i) tabs[(size_t) i]->setToggleState (i == tab, juce::dontSendNotification);
}

void NebulaContent::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& factory = factoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
        presetBox.addItem (factory[(size_t) i].name, i + 1);
    userPresets = proc.userPresetNames();
    if (! userPresets.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Tus presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i], 1000 + i);
    }
    const int idx = proc.getPresetIndex();
    if (idx >= 0) presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
    else presetBox.setText ("(sonido propio)", juce::dontSendNotification);
}

void NebulaContent::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id <= 0) return;
    if (id >= 1000) proc.loadUserPreset (userPresets[id - 1000]);
    else proc.loadPreset (id - 1);
}

void NebulaContent::savePreset()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Guardar preset", "Ponle nombre a tu sonido:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", "Mi sonido");
    saveDialog->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto name = saveDialog->getTextEditorContents ("name");
            if (proc.saveUserPreset (name))
            {
                rebuildPresetMenu();
                const int i = userPresets.indexOf (juce::File::createLegalFileName (name.trim()));
                if (i >= 0) presetBox.setSelectedId (1000 + i, juce::dontSendNotification);
            }
        }
        saveDialog.reset();
    }), false);
}

void NebulaContent::timerCallback()
{
    // Si cambiaron los presets de usuario en disco (por ejemplo desde otra instancia)
    if (proc.userPresetNames() != userPresets) rebuildPresetMenu();

    // Si el preset cambió desde fuera (proyecto abierto, otra ventana), mostrarlo
    const int idx = proc.getPresetIndex();
    if (idx >= 0 && presetBox.getSelectedId() != idx + 1 && ! presetBox.isPopupActive())
        presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

void NebulaContent::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setGradientFill (juce::ColourGradient (accent, 18.0f, 0.0f, accent2, 170.0f, 0.0f, false));
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("NEBULA", 18, 10, 150, 34, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("v2", 140, 12, 30, 14, juce::Justification::centredLeft);
}

void NebulaContent::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    auto header = r.removeFromTop (54);
    header.removeFromLeft (160);
    auto tabArea = header.removeFromLeft (390).withSizeKeepingCentre (390, 34);
    for (auto& t : tabs) t->setBounds (tabArea.removeFromLeft (128).reduced (2, 0));

    auto right = header.withSizeKeepingCentre (header.getWidth(), 30);
    zoomBox.setBounds (right.removeFromRight (78));
    right.removeFromRight (14);
    folderButton.setBounds (right.removeFromRight (30));
    right.removeFromRight (4);
    saveButton.setBounds (right.removeFromRight (30));
    right.removeFromRight (8);
    nextButton.setBounds (right.removeFromRight (30));
    presetBox.setBounds (right.removeFromRight (240).reduced (4, 0));
    prevButton.setBounds (right.removeFromRight (30));

    keyboard.setBounds (r.removeFromBottom (74).withTrimmedBottom (12));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f);
    r.removeFromBottom (10);

    garden.setBounds (r);
    edit.setBounds (r);
    mod.setBounds (r);
    sampleEditor.setBounds (r.reduced (40, 50));
    drawPanel.setBounds (r.reduced (120, 70));
}

//==============================================================================
NebulaAudioProcessorEditor::NebulaAudioProcessorEditor (NebulaAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), content (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.onZoom = [this] (float z) { setZoom (z); };
    zoom = (float) p.state.state.getProperty ("uiZoom", 1.0f);
    content.setZoomChoice (zoom);
    setZoom (zoom);
}

NebulaAudioProcessorEditor::~NebulaAudioProcessorEditor() { setLookAndFeel (nullptr); }

void NebulaAudioProcessorEditor::setZoom (float z)
{
    zoom = juce::jlimit (0.6f, 1.6f, z);
    processor.state.state.setProperty ("uiZoom", zoom, nullptr);
    content.setTransform (juce::AffineTransform::scale (zoom));
    setSize (juce::roundToInt (NebulaContent::designW * zoom), juce::roundToInt (NebulaContent::designH * zoom));
}

void NebulaAudioProcessorEditor::resized()
{
    content.setBounds (0, 0, NebulaContent::designW, NebulaContent::designH);
}
