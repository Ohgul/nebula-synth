#include "PluginEditor.h"
#include "Presets.h"

using namespace NebulaColours;

//==============================================================================
NebulaContent::NebulaContent (NebulaAudioProcessor& p)
    : proc (p), garden (p), edit (p), mod (p), fxView (p), macros (p), browser (p), sampleEditor (p), drawPanel (p), recipePanel (p), cleanerPanel (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    const char* names[] { "Cerebro", "Editar", "Modulacion", "Efectos" };
    const juce::Path icons[] { Icons::seed(), Icons::sliders(), Icons::modulation(), Icons::sparkle() };
    const char* tips[] { "Cerebro: suelta un audio, recrealo y haz que tu sonido evolucione",
                         "Editar: osciladores o resintesis, filtros, volumen y voces",
                         "Modulacion: envolventes, LFOs y matriz",
                         "Efectos: EQ, compresor, chorus, delay y reverb" };
    for (int i = 0; i < 4; ++i)
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
    for (auto* b : { &prevButton, &nextButton, &saveButton, &recipeButton, &browseButton, &undoButton, &redoButton }) addAndMakeVisible (b);
    recipeButton.colour = accent2;
    recipeButton.onClick = [this] { recipePanel.open(); };
    browseButton.onClick = [this] { browser.open(); };
    browser.onLoaded = [this] { rebuildPresetMenu(); };
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    juce::ignoreUnused (folderButton);

    zoomBox.addItemList ({ "80 %", "90 %", "100 %", "115 %", "130 %" }, 1);
    zoomBox.setSelectedId (3, juce::dontSendNotification);
    zoomBox.setTooltip ("Tamano de la ventana");
    zoomBox.onChange = [this] { static const float z[] { 0.8f, 0.9f, 1.0f, 1.15f, 1.3f }; if (onZoom) onZoom (z[zoomBox.getSelectedId() - 1]); };
    addAndMakeVisible (zoomBox);

    addChildComponent (garden);
    addChildComponent (edit);
    addChildComponent (mod);
    addChildComponent (fxView);
    addAndMakeVisible (macros);
    addChildComponent (browser);
    addChildComponent (sampleEditor);
    addChildComponent (drawPanel);
    addChildComponent (recipePanel);
    addChildComponent (cleanerPanel);

    garden.onOpenSampleEditor = [this] { sampleEditor.open(); };
    garden.framesForWave = [this] { return sampleEditor.framesChoice(); };
    edit.onOpenSampleEditor = [this] { sampleEditor.open(); };
    edit.onOpenDraw = [this] { drawPanel.setVisible (true); drawPanel.toFront (true); };
    garden.onOpenCleaner = [this] { cleanerPanel.open(); };
    garden.onOpenRecipe  = [this] { recipePanel.open(); };
    edit.onOpenCleaner   = [this] { cleanerPanel.open(); };

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
    addMouseListener (this, true);
    startTimerHz (20);
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
    fxView.setVisible (tab == 3);
    for (int i = 0; i < 4; ++i) tabs[(size_t) i]->setToggleState (i == tab, juce::dontSendNotification);
    repaint();
}

void NebulaContent::mouseDown (const juce::MouseEvent&)
{
    proc.undoManager.beginNewTransaction();
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
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.2, (now - lastTick) / 1000.0) : 0.05f;
    lastTick = now;

    // Locura: qué tan lejos y qué tan loco está el sonido → anima toda la interfaz
    const float before = Neural::level();
    const float c = proc.computeCraziness();
    Neural::craziness = c;
    neural.step (dt, c, juce::jmin (1.0f, proc.outputLevel.load() * 3.0f));
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());

    // El fondo se repinta seguido en el Cerebro; en las demás pestañas, más lento (ahorra CPU)
    ++bgCounter;
    const bool calm = c < 0.08f && std::abs (c - before) < 0.002f;
    if (currentTab == 0 || (! calm && bgCounter % 3 == 0) || bgCounter % 10 == 0)
        repaint();

    if (bgCounter % 10 == 0)
    {
        if (proc.userPresetNames() != userPresets) rebuildPresetMenu();
        const int idx = proc.getPresetIndex();
        if (! presetBox.isPopupActive())
        {
            if (idx >= 0 && presetBox.getSelectedId() != idx + 1)
                presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
            else if (idx < 0 && presetBox.getSelectedId() > 0 && presetBox.getSelectedId() < 1000)
                presetBox.setText ("(sonido propio)", juce::dontSendNotification);
        }
    }
}

void NebulaContent::paint (juce::Graphics& g)
{
    neural.paint (g, getLocalBounds().toFloat());
    // Logo: se calienta con la locura
    g.setGradientFill (juce::ColourGradient (Neural::hot (accent), 18.0f, 0.0f, Neural::hot (accent2, 0.1f), 170.0f, 0.0f, false));
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    const float c = Neural::level();
    const float jx = c > 0.5f ? (float) (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 4.0f * (c - 0.5f) : 0.0f;
    g.drawText ("NEBULA", juce::Rectangle<float> (18.0f + jx, 10.0f, 150.0f, 34.0f), juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("v3", 140, 12, 30, 14, juce::Justification::centredLeft);
}

void NebulaContent::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    auto header = r.removeFromTop (54);
    header.removeFromLeft (160);
    auto tabArea = header.removeFromLeft (452).withSizeKeepingCentre (452, 34);
    for (auto& t : tabs) t->setBounds (tabArea.removeFromLeft (113).reduced (2, 0));

    auto right = header.withSizeKeepingCentre (header.getWidth(), 30);
    zoomBox.setBounds (right.removeFromRight (74));
    right.removeFromRight (8);
    recipeButton.setBounds (right.removeFromRight (30));
    right.removeFromRight (8);
    saveButton.setBounds (right.removeFromRight (30));
    right.removeFromRight (3);
    browseButton.setBounds (right.removeFromRight (30));
    right.removeFromRight (3);
    nextButton.setBounds (right.removeFromRight (26));
    presetBox.setBounds (right.removeFromRight (170).reduced (3, 0));
    prevButton.setBounds (right.removeFromRight (26));
    right.removeFromRight (8);
    redoButton.setBounds (right.removeFromRight (28));
    right.removeFromRight (2);
    undoButton.setBounds (right.removeFromRight (28));

    keyboard.setBounds (r.removeFromBottom (58).withTrimmedBottom (8));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f);
    r.removeFromBottom (6);
    macros.setBounds (r.removeFromBottom (86));
    r.removeFromBottom (8);

    garden.setBounds (r);
    edit.setBounds (r);
    mod.setBounds (r);
    fxView.setBounds (r);
    sampleEditor.setBounds (r.reduced (40, 30));
    drawPanel.setBounds (r.reduced (120, 50));
    recipePanel.setBounds (r.reduced (150, 0).withTrimmedBottom (-60));
    cleanerPanel.setBounds (r.reduced (24, 0).withTrimmedBottom (-40));
    browser.setBounds (r.reduced (200, 10));
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
