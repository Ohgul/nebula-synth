#include "GardenView.h"
#include "PluginEditor.h"

using namespace NebulaColours;

//==============================================================================
GardenCanvas::GardenCanvas (NebulaAudioProcessor& p) : processor (p), garden (p.garden) {}

void GardenCanvas::resized()
{
    const auto b = getLocalBounds().toFloat();
    centre = b.getCentre();
    const float half = juce::jmin (b.getWidth() * 0.5f, b.getHeight() * 0.5f);
    seedRadius = juce::jlimit (38.0f, 64.0f, half * 0.26f);
    minDist = seedRadius + sproutRadius() + 22.0f;
    maxDist = half - sproutRadius() - 10.0f;
}

juce::Point<float> GardenCanvas::sproutPos (int k) const
{
    const float d = garden.distances[(size_t) k];
    const float dist = minDist + (maxDist - minDist) * std::sqrt (d);
    const float angle = -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * (float) k / (float) Garden::numSprouts;
    // El lienzo es más ancho que alto: estiramos el círculo a una elipse
    const float stretch = juce::jlimit (1.0f, 2.2f, (float) getWidth() / (float) juce::jmax (1, getHeight()));
    return { centre.x + std::cos (angle) * dist * stretch, centre.y + std::sin (angle) * dist };
}

int GardenCanvas::hitTest (juce::Point<float> p) const
{
    if (p.getDistanceFrom (centre) <= seedRadius + 4.0f)
        return seedIndex;
    for (int k = 0; k < Garden::numSprouts; ++k)
        if (p.getDistanceFrom (sproutPos (k)) <= sproutRadius() + 6.0f)
            return k;
    return none;
}

void GardenCanvas::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h != none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GardenCanvas::mouseExit (const juce::MouseEvent&)
{
    hover = none;
    repaint();
}

void GardenCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (e.getNumberOfClicks() > 1) return;
    const int h = hitTest (e.position);
    if (h == none) return;

    if (h == seedIndex) garden.selectSeed();
    else                garden.selectSprout (h);

    if (onSelect) onSelect();
}

void GardenCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h >= 0)
    {
        garden.plant (h);
        if (onSelect) onSelect();
    }
}

// Dibuja un "organismo" cuya forma y color salen del propio sonido
void GardenCanvas::drawBlob (juce::Graphics& g, juce::Point<float> c, float r, const Genome& gen,
                             bool selected, bool isHover, float phase) const
{
    if (! gen.isValid()) return;

    const int   w1     = (int) garden.real (gen, ParamID::osc1Wave);
    const int   w2     = (int) garden.real (gen, ParamID::osc2Wave);
    const float cutoff = garden.real (gen, ParamID::cutoff);
    const float reso   = garden.real (gen, ParamID::resonance);
    const float drive  = garden.real (gen, ParamID::drive);
    const float fm     = garden.real (gen, ParamID::fmAmount);
    const float noise  = garden.real (gen, ParamID::noise);
    const float sub    = garden.real (gen, ParamID::subLevel);
    const float attack = garden.real (gen, ParamID::ampA);
    const float fine   = garden.real (gen, ParamID::osc2Fine);
    const int   ftype  = (int) garden.real (gen, ParamID::filterType);
    const float semi   = garden.real (gen, ParamID::osc2Semi);
    const float fenv   = garden.real (gen, ParamID::filterEnv);
    const float sus    = garden.real (gen, ParamID::ampS);
    const float lfo    = garden.real (gen, ParamID::lfoFilter) + garden.real (gen, ParamID::lfoPitch) / 100.0f;
    r *= 0.85f + 0.25f * sus;

    const float bright = juce::jlimit (0.0f, 1.0f, std::log (cutoff / 20.0f) / std::log (1000.0f));
    const int petals   = 3 + w1 + w2 + (std::abs (semi) > 0.5f ? 1 : 0);
    const float depth  = 0.12f + 0.28f * fm + 0.12f * reso + 0.05f * (float) ftype;
    const float sharp  = 0.6f + reso * 3.5f;
    const float rot    = fine / 50.0f * 0.6f + phase * (0.1f + 0.4f * juce::jmin (1.0f, lfo));

    float hue = juce::jmap (bright, 0.76f, 0.46f) - drive * 0.22f + fm * 0.14f + noise * 0.1f
              + (float) ftype * 0.09f + semi / 24.0f * 0.08f + fenv * 0.06f;
    hue -= std::floor (hue);
    const auto base = juce::Colour::fromHSV (hue, juce::jlimit (0.3f, 0.95f, 0.45f + 0.3f * drive + 0.25f * reso), 0.6f + 0.32f * bright, 1.0f);

    juce::Path shape;
    constexpr int steps = 140;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
        const float petal = std::pow (std::abs (std::cos ((float) petals * 0.5f * (t + rot))), sharp);
        const float jitter = noise * 0.09f * (std::sin (t * 13.0f + phase * 2.0f) + std::sin (t * 29.0f) * 0.6f);
        const float rr = r * (1.0f - depth + depth * petal + jitter);
        const juce::Point<float> pt (c.x + rr * std::cos (t), c.y + rr * std::sin (t));
        if (i == 0) shape.startNewSubPath (pt); else shape.lineTo (pt);
    }
    shape.closeSubPath();

    // Halo (más grande si el ataque es lento: sonido "suave")
    const float glow = (selected ? 0.55f : isHover ? 0.35f : 0.18f);
    const float haloR = r * (1.25f + juce::jmin (0.35f, attack * 0.3f));
    g.setGradientFill (juce::ColourGradient (base.withAlpha (glow), c.x, c.y,
                                             base.withAlpha (0.0f), c.x + haloR, c.y, true));
    g.fillEllipse (c.x - haloR, c.y - haloR, haloR * 2.0f, haloR * 2.0f);

    g.setGradientFill (juce::ColourGradient (base.brighter (0.6f), c.x - r * 0.3f, c.y - r * 0.3f,
                                             base.darker (0.5f), c.x + r, c.y + r, true));
    g.fillPath (shape);
    g.setColour (base.brighter (0.8f).withAlpha (selected ? 1.0f : 0.6f));
    g.strokePath (shape, juce::PathStrokeType (selected ? 2.2f : 1.1f));

    // Núcleo: crece con el sub-oscilador
    const float coreR = r * (0.16f + 0.18f * sub);
    g.setColour (base.darker (0.9f).withAlpha (0.85f));
    g.fillEllipse (c.x - coreR, c.y - coreR, coreR * 2.0f, coreR * 2.0f);
    g.setColour (base.brighter (1.0f).withAlpha (0.7f));
    g.fillEllipse (c.x - coreR * 0.35f, c.y - coreR * 0.35f, coreR * 0.7f, coreR * 0.7f);

    if (selected)
    {
        g.setColour (accent2);
        g.drawEllipse (c.x - r - 9.0f, c.y - r - 9.0f, (r + 9.0f) * 2.0f, (r + 9.0f) * 2.0f, 1.5f);
    }
}

void GardenCanvas::paint (juce::Graphics& g)
{
    const juce::ScopedLock sl (garden.lock);
    const float phase = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1000.0, 1000.0);
    const float stretch = juce::jlimit (1.0f, 2.2f, (float) getWidth() / (float) juce::jmax (1, getHeight()));

    // Fondo: anillos de crecimiento
    g.setColour (panelEdge.withAlpha (0.35f));
    for (int i = 1; i <= 4; ++i)
    {
        const float d = minDist + (maxDist - minDist) * std::sqrt ((float) i / 4.0f);
        g.drawEllipse (centre.x - d * stretch, centre.y - d, d * 2.0f * stretch, d * 2.0f, 0.8f);
    }

    // Tallos
    for (int k = 0; k < Garden::numSprouts; ++k)
    {
        const auto p = sproutPos (k);
        const auto dir = (p - centre);
        const juce::Point<float> perp (-dir.y, dir.x);
        const float bend = 0.18f * std::sin ((float) k * 2.1f + phase * 0.6f);
        const auto ctrl = centre + dir * 0.5f + perp * bend;

        juce::Path stem;
        stem.startNewSubPath (centre);
        stem.quadraticTo (ctrl, p);
        const bool active = (garden.selected == k);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.7f), centre.x, centre.y,
                                                 accent2.withAlpha (active ? 0.9f : 0.35f), p.x, p.y, false));
        g.strokePath (stem, juce::PathStrokeType (active ? 3.0f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Brotes
    for (int k = 0; k < Garden::numSprouts; ++k)
        drawBlob (g, sproutPos (k), sproutRadius(), garden.sprouts[(size_t) k],
                  garden.selected == k, hover == k, phase + (float) k);

    // Semilla
    drawBlob (g, centre, seedRadius, garden.seed, garden.selected == -1, hover == seedIndex, phase * 0.5f);

    g.setColour (text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SEMILLA", juce::Rectangle<float> (centre.x - 60.0f, centre.y + seedRadius + 8.0f, 120.0f, 14.0f), juce::Justification::centred);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("generacion " + juce::String (garden.generation),
                juce::Rectangle<float> (centre.x - 60.0f, centre.y + seedRadius + 22.0f, 120.0f, 14.0f), juce::Justification::centred);

    g.setColour (textDim.withAlpha (0.8f));
    g.drawText ("clic: escuchar   |   doble clic: plantar y germinar   |   clic en la semilla: volver a ella",
                getLocalBounds().removeFromBottom (18), juce::Justification::centred);
}

//==============================================================================
SamplePanel::SamplePanel (NebulaAudioProcessor& p) : processor (p)
{
    imitateButton.setTooltip ("El sinte busca el patch que mas se parece al sample");
    useButton.setTooltip ("Toca el sample como instrumento; los brotes lo transforman");
    imitateButton.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.55f));
    useButton.setColour (juce::TextButton::buttonColourId, accent2.withAlpha (0.35f));

    imitateButton.onClick = [this]
    {
        processor.startImitation();
        message = {};
        update();
    };
    useButton.onClick = [this]
    {
        processor.useSampleAsInstrument();
        message = "Modo sample: los brotes son versiones de tu audio.";
        if (onSoundChanged) onSoundChanged();
        update();
    };
    cancelButton.onClick = [this] { processor.cancelImitation(); message = "Busqueda cancelada."; };

    for (auto* b : { &imitateButton, &useButton, &cancelButton })
        addAndMakeVisible (b);

    seenImitations = processor.imitationCount.load();
    update();
}

void SamplePanel::rebuildWaveform()
{
    waveform.clear();
    if (shown == nullptr || getWidth() <= 0) return;

    const auto area = dropArea().reduced (8, 10).toFloat();
    const int cols = juce::jmax (1, (int) area.getWidth());
    const int n = shown->length();
    const float* d = shown->data();
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((juce::int64) n * c / cols), b = (int) ((juce::int64) n * (c + 1) / cols);
        float mx = 0.0f;
        for (int i = a; i < b; ++i) mx = juce::jmax (mx, std::abs (d[i]));
        const float h = juce::jmin (1.0f, mx) * area.getHeight() * 0.5f;
        waveform.addRectangle (area.getX() + (float) c, area.getCentreY() - h, 1.0f, juce::jmax (0.5f, h * 2.0f));
    }
}

juce::String SamplePanel::noteName (float midi)
{
    return juce::MidiMessage::getMidiNoteName (juce::roundToInt (midi), true, true, 3);
}

void SamplePanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Elige un audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadFile (fc.getResult());
                          });
}

void SamplePanel::loadFile (const juce::File& f)
{
    const auto error = processor.loadSample (f);
    message = error.isNotEmpty() ? error : "Ahora elige: imitar con el sinte o usar el sample.";
    update();
}

void SamplePanel::mouseUp (const juce::MouseEvent& e)
{
    if (dropArea().contains (e.getPosition()) && ! processor.isImitating())
        chooseFile();
}

void SamplePanel::update()
{
    auto current = processor.sampleSource.get();
    if (current != shown)
    {
        shown = current;
        rebuildWaveform();
    }

    const bool imitating = processor.isImitating();
    imitateButton.setEnabled (shown != nullptr && ! imitating);
    useButton.setEnabled (shown != nullptr && ! imitating);
    cancelButton.setVisible (imitating);

    if (const int count = processor.imitationCount.load(); count != seenImitations)
    {
        seenImitations = count;
        message = "Listo: parecido " + juce::String (juce::roundToInt (processor.lastSimilarity.load() * 100.0f))
                + " %. Los brotes son versiones de ese sonido.";
        if (onSoundChanged) onSoundChanged();
    }
    wasImitating = imitating;
    repaint();
}

void SamplePanel::paint (juce::Graphics& g)
{
    g.setColour (accent2);
    g.fillRoundedRectangle (0.0f, 6.0f, 3.0f, 12.0f, 1.5f);
    g.setColour (text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SAMPLE", 9, 0, getWidth() - 9, 24, juce::Justification::centredLeft);

    const auto box = dropArea().toFloat();
    g.setColour (dragHover ? accent.withAlpha (0.25f) : track.withAlpha (0.6f));
    g.fillRoundedRectangle (box, 6.0f);
    juce::Path border;
    border.addRoundedRectangle (box.reduced (0.5f), 6.0f);
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (border, border, dashes, 2);
    g.setColour (dragHover ? accent2 : panelEdge.brighter (0.4f));
    g.fillPath (border);

    if (shown == nullptr)
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText ("Arrastra aqui un audio\n(WAV, AIFF, MP3...)\no haz clic para buscarlo", dropArea().reduced (8), juce::Justification::centred, 3);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (accent, box.getX(), 0.0f, accent2, box.getRight(), 0.0f, false));
        g.fillPath (waveform);

        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (shown->name, 0, 128, getWidth(), 18, juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (12.0f));
        const auto info = (shown->hasPitch ? "Nota detectada: " + noteName (shown->rootNote) : juce::String ("Sin tono claro (suena en C3)"))
                        + "  |  " + juce::String (shown->length() / shown->sampleRate, 1) + " s";
        g.drawText (info, 0, 146, getWidth(), 16, juce::Justification::centredLeft, true);
    }

    // Estado
    auto status = juce::Rectangle<int> (0, 246, getWidth(), 70);
    if (processor.isImitating())
    {
        const float prog = processor.imitationProgress();
        auto bar = status.removeFromTop (8).toFloat();
        g.setColour (track);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setGradientFill (juce::ColourGradient (accent, bar.getX(), 0.0f, accent2, bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * prog)), 4.0f);
        status.removeFromTop (6);
        g.setColour (text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Buscando el parecido... " + juce::String (juce::roundToInt (prog * 100.0f)) + " %",
                    status.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (textDim);
        g.drawText ("Mejor hasta ahora: " + juce::String (juce::roundToInt (processor.imitationSimilarity() * 100.0f)) + " % parecido",
                    status.removeFromTop (16), juce::Justification::centredLeft);
    }
    else if (message.isNotEmpty())
    {
        g.setColour (text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (message, status, juce::Justification::topLeft, 4, 1.0f);
    }

    // Ayuda
    g.setColour (textDim.withAlpha (0.75f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText ("Imitar: el sinte busca un sonido parecido y lo usa como semilla.\n"
                      "Usar el sample: tocas tu audio y los brotes cambian inicio, tono, reversa, loop y textura.",
                      juce::Rectangle<int> (0, getHeight() - 64, getWidth(), 64), juce::Justification::bottomLeft, 5, 1.0f);
}

void SamplePanel::resized()
{
    rebuildWaveform();
    imitateButton.setBounds (0, 172, getWidth(), 30);
    useButton.setBounds (0, 208, getWidth(), 30);
    cancelButton.setBounds (0, 298, 90, 24);
}

//==============================================================================
GardenView::GardenView (NebulaAudioProcessor& p) : processor (p), garden (p.garden), canvas (p), samplePanel (p)
{
    addAndMakeVisible (canvas);
    addAndMakeVisible (samplePanel);
    samplePanel.onSoundChanged = [this] { audition(); refreshButtons(); };
    canvas.onSelect = [this] { audition(); refreshButtons(); };

    mutationLabel.setText ("Mutacion", juce::dontSendNotification);
    mutationLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (mutationLabel);

    mutationSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    mutationSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    mutationSlider.setRange (0.05, 1.0, 0.01);
    mutationSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    mutationSlider.setValue (garden.mutation, juce::dontSendNotification);
    mutationSlider.onValueChange = [this] { garden.mutation = (float) mutationSlider.getValue(); };
    mutationSlider.setColour (juce::Slider::trackColourId, accent);
    mutationSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    mutationSlider.setColour (juce::Slider::backgroundColourId, track);
    mutationSlider.setColour (juce::Slider::thumbColourId, accent2);
    addAndMakeVisible (mutationSlider);

    germinateButton.onClick = [this] { garden.germinate(); refreshButtons(); };
    rerollButton.onClick    = [this] { garden.reroll(); refreshButtons(); };
    randomButton.onClick    = [this] { garden.newRandomSeed(); audition(); refreshButtons(); };
    backButton.onClick      = [this] { garden.back(); refreshButtons(); };
    germinateButton.setTooltip ("Crea 8 variaciones a partir del sonido que suena ahora");
    rerollButton.setTooltip ("Nuevas variaciones de la misma semilla");
    randomButton.setTooltip ("Empieza desde un sonido aleatorio");

    lockEnvButton.setToggleState (garden.lockEnvelope, juce::dontSendNotification);
    lockEnvButton.onClick = [this] { garden.lockEnvelope = lockEnvButton.getToggleState(); };
    listenButton.setToggleState (true, juce::dontSendNotification);

    for (auto* b : { &lockEnvButton, &listenButton })
    {
        b->setColour (juce::ToggleButton::textColourId, textDim);
        b->setColour (juce::ToggleButton::tickColourId, accent2);
        b->setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &germinateButton, &rerollButton, &randomButton, &backButton, &lockEnvButton, &listenButton })
        addAndMakeVisible (c);

    refreshButtons();
    startTimerHz (24);
}

GardenView::~GardenView()
{
    if (noteIsOn)
        processor.keyboardState.noteOff (1, auditionNoteOn, 0.0f);
}

bool GardenView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void GardenView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    samplePanel.setDragHover (false);
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
        {
            samplePanel.loadFile (juce::File (f));
            break;
        }
    repaint();
}

void GardenView::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    g.setColour (accent.withAlpha (0.12f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (accent2);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f, 2.0f);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.setColour (text);
    g.drawText ("Suelta el audio", canvas.getBounds(), juce::Justification::centred);
}

void GardenView::refreshButtons()
{
    backButton.setEnabled (garden.canGoBack());
    germinateButton.setButtonText (garden.selected >= 0 ? "Plantar este brote" : "Germinar desde aqui");
}

void GardenView::audition()
{
    if (! listenButton.getToggleState()) return;
    auto& ks = processor.keyboardState;
    if (noteIsOn) ks.noteOff (1, auditionNoteOn, 0.0f);

    // Si hay un sample con tono, escuchamos en su nota; si no, en C3
    auditionNoteOn = NebulaAudioProcessor::auditionNote;
    if (auto s = processor.sampleSource.get(); s != nullptr && s->hasPitch)
        auditionNoteOn = juce::jlimit (36, 84, juce::roundToInt (s->rootNote));

    ks.noteOn (1, auditionNoteOn, 0.8f);
    noteIsOn = true;
    noteOffTime = juce::Time::getMillisecondCounterHiRes() + 700.0;
}

void GardenView::timerCallback()
{
    if (noteIsOn && juce::Time::getMillisecondCounterHiRes() >= noteOffTime)
    {
        processor.keyboardState.noteOff (1, auditionNoteOn, 0.0f);
        noteIsOn = false;
    }

    if (const int v = garden.version.load(); v != lastVersion)
    {
        lastVersion = v;
        mutationSlider.setValue (garden.mutation, juce::dontSendNotification);
        lockEnvButton.setToggleState (garden.lockEnvelope, juce::dontSendNotification);
        refreshButtons();
    }
    samplePanel.update();
    canvas.repaint();
}

void GardenView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

void GardenView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto bar = r.removeFromBottom (32);
    r.removeFromBottom (8);
    samplePanel.setBounds (r.removeFromLeft (230).reduced (2, 0));
    r.removeFromLeft (14);
    canvas.setBounds (r);

    mutationLabel.setBounds (bar.removeFromLeft (70));
    mutationSlider.setBounds (bar.removeFromLeft (180).reduced (4, 4));
    bar.removeFromLeft (10);
    for (auto* b : { &germinateButton, &rerollButton, &randomButton, &backButton })
    {
        b->setBounds (bar.removeFromLeft (b == &germinateButton ? 150 : (b == &backButton ? 70 : 120)).reduced (3, 2));
    }
    bar.removeFromLeft (8);
    lockEnvButton.setBounds (bar.removeFromLeft (135));
    listenButton.setBounds (bar);
}
