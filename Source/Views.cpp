#include "Views.h"

using namespace NebulaColours;

namespace
{
    juce::Path softClipIcon()
    {
        return Icons::fromFunction ([] (float x) { return std::tanh (2.5f * std::sin (x * juce::MathConstants<float>::twoPi)) / std::tanh (2.5f); });
    }
    juce::Path charIcon (int i) { return i == 0 ? Icons::sine() : i == 1 ? softClipIcon() : Icons::pulse(); }
    juce::Path slopeIcon (int i)
    {
        juce::Path p; p.startNewSubPath (0.08f, 0.35f); p.lineTo (0.5f, 0.35f);
        if (i == 0) p.quadraticTo (0.7f, 0.35f, 0.92f, 0.75f); else p.quadraticTo (0.58f, 0.35f, 0.66f, 0.85f);
        return p;
    }
    const juce::StringArray waveTips { "Seno: suave y puro", "Triangulo: suave con un poco de brillo", "Sierra: brillante, clasica de sintes",
                                       "Pulso: cuadrada; Forma cambia el ancho (PWM)", "Super Saw: 7 sierras desafinadas; Forma = desafinacion",
                                       "Sync: lead agresivo; Forma = cuanto \"chilla\"", "Wavetable: recorre una tabla de ondas; Forma = posicion" };
    const juce::StringArray filterTips { "Pasa bajos: quita agudos", "Pasa banda: deja solo una zona", "Pasa altos: quita graves", "Notch: quita una zona" };
    const juce::StringArray charTips { "Limpio", "Calido: satura suave", "Saturado: sucio y agresivo" };
}

//==============================================================================
NebKnob& EditView::knob (const char* id, const char* name, const char* tip)
{
    knobs.push_back (std::make_unique<NebKnob> (proc.state, id, name, tip));
    addAndMakeVisible (*knobs.back());
    return *knobs.back();
}

EditView::EditView (NebulaAudioProcessor& p)
    : proc (p),
      engine (p.state, ParamID::engine, [] (int i) { return i == 0 ? Icons::engineSynth() : Icons::engineSample(); },
              { "Sinte: osciladores", "Sample: toca un audio" }, { "Sinte", "Sample" }),
      wave1 (p.state, ParamID::osc1Wave, [] (int i) { return Icons::wave (i); }, waveTips),
      wave2 (p.state, ParamID::osc2Wave, [] (int i) { return Icons::wave (i); }, waveTips),
      preview1 (p, ParamID::osc1Wave, ParamID::osc1Shape, ParamID::osc1Table),
      preview2 (p, ParamID::osc2Wave, ParamID::osc2Shape, ParamID::osc2Table),
      table1 (p.state, ParamID::osc1Table, {}, "Tabla de ondas"),
      table2 (p.state, ParamID::osc2Table, {}, "Tabla de ondas"),
      regionMini (p, false),
      reverse (p.state, ParamID::smpReverse, "Reversa", Icons::reverseArrow(), "Tocar al reves"),
      loop (p.state, ParamID::smpLoop, "Loop", Icons::loopArrows(), "Repetir la seccion mientras mantienes la nota"),
      playMode (p.state, ParamID::smpPlayMode, [] (int i) { return i == 0 ? Icons::piano() : Icons::metronome(); },
                { "Clasico: el tono cambia la velocidad (como un sampler clasico)",
                  "Tempo: el sample dura los tiempos elegidos al BPM de tu proyecto; el tono no cambia la velocidad" },
                { "Clasico", "Tempo" }),
      beats (p.state, ParamID::smpBeats, "Duracion", "Cuanto dura la seccion en modo Tempo (Auto = la mas cercana)"),
      filterGraph (p),
      f1Type (p.state, ParamID::filterType, [] (int i) { return Icons::filterShape (i); }, filterTips),
      f1Slope (p.state, ParamID::filterSlope, slopeIcon, { "12 dB: suave", "24 dB: mas fuerte" }),
      f1Char (p.state, ParamID::filterChar, charIcon, charTips),
      f2Type (p.state, ParamID::f2Type, [] (int i) { return Icons::filterShape (i); }, filterTips),
      f2Slope (p.state, ParamID::f2Slope, slopeIcon, { "12 dB: suave", "24 dB: mas fuerte" }),
      f2Char (p.state, ParamID::f2Char, charIcon, charTips),
      routing (p.state, ParamID::filterRouting, [] (int i) { return i == 0 ? Icons::serial() : Icons::parallel(); },
               { "Serie: el sonido pasa por el filtro 1 y luego por el 2", "Paralelo: los dos filtros a la vez, mezclados" }),
      f2On (p.state, ParamID::f2On, "", Icons::filterShape (2), "Encender el filtro 2"),
      ampGraph (p, { ParamID::ampA, ParamID::ampD, ParamID::ampS, ParamID::ampR, ParamID::ampCurve }, accent2),
      polyMode (p.state, ParamID::polyMode, [] (int i) { return i == 0 ? Icons::voicesPoly() : i == 1 ? Icons::voicesMono() : Icons::voicesLegato(); },
                { "Poli: acordes", "Mono: una nota a la vez", "Legato: una nota, sin reiniciar al encadenar (ideal para glide)" },
                { "Poli", "Mono", "Legato" })
{
    for (auto* c : std::initializer_list<juce::Component*> { &engine, &wave1, &wave2, &preview1, &preview2, &table1, &table2, &draw1,
                                                             &regionMini, &editRegion, &reverse, &loop, &playMode, &beats,
                                                             &filterGraph, &f1Type, &f1Slope, &f1Char, &f2Type, &f2Slope, &f2Char, &routing, &f2On,
                                                             &ampGraph, &polyMode })
        addAndMakeVisible (c);

    f2Type.colour = f2Slope.colour = f2Char.colour = warm;
    f2On.colour = warm;
    engine.colour = accent2;
    editRegion.horizontal = true;
    editRegion.onClick = [this] { if (onOpenSampleEditor) onOpenSampleEditor(); };
    regionMini.onClick = [this] { if (onOpenSampleEditor) onOpenSampleEditor(); };
    draw1.onClick = [this] { if (onOpenDraw) onOpenDraw(); };

    shape1 = &knob (ParamID::osc1Shape, "Forma", "Cambia segun la onda: ancho, desafinacion, sync o posicion");
    oct1   = &knob (ParamID::osc1Octave, "Octava", "Octava del oscilador 1");
    shape2 = &knob (ParamID::osc2Shape, "Forma", "Cambia segun la onda: ancho, desafinacion, sync o posicion");
    semi2  = &knob (ParamID::osc2Semi, "Semitonos", "Afinacion del oscilador 2");
    fine2  = &knob (ParamID::osc2Fine, "Fino", "Desafinacion fina del oscilador 2");

    smpStart   = &knob (ParamID::smpStart, "Inicio", "Desde donde empieza dentro de la seccion");
    smpTune    = &knob (ParamID::smpTune, "Afinacion", "Transporta el sample en semitonos");
    smpTexture = &knob (ParamID::smpTexture, "Textura", "Granular: estira y difumina el sonido");
    smpGrain   = &knob (ParamID::smpGrain, "Grano", "Tamano de los granos");

    mix   = &knob (ParamID::oscMix, "Mezcla", "Balance entre oscilador 1 y 2");
    sub   = &knob (ParamID::subLevel, "Sub", "Una octava abajo: cuerpo y graves");
    fm    = &knob (ParamID::fmAmount, "FM", "El oscilador 2 modula al 1: sonidos metalicos");
    noise = &knob (ParamID::noise, "Ruido", "Mezcla ruido (aire, golpe)");
    uniV  = &knob (ParamID::uniVoices, "Voces", "Copias de cada nota (unisono): mas grueso");
    uniD  = &knob (ParamID::uniDetune, "Desafinar", "Cuanto se separan las copias");
    uniW  = &knob (ParamID::uniWidth, "Ancho", "Apertura estereo de las copias");

    cut1  = &knob (ParamID::cutoff, "Corte", "Frecuencia del filtro 1");
    res1  = &knob (ParamID::resonance, "Reso", "Resonancia del filtro 1");
    env1  = &knob (ParamID::filterEnv, "Env", "Cuanto mueve la envolvente 2 al filtro 1");
    drive = &knob (ParamID::drive, "Drive", "Saturacion a la salida");
    cut2  = &knob (ParamID::f2Cutoff, "Corte", "Frecuencia del filtro 2");
    res2  = &knob (ParamID::f2Reso, "Reso", "Resonancia del filtro 2");
    env2  = &knob (ParamID::f2Env, "Env", "Cuanto mueve la envolvente 2 al filtro 2");
    for (auto* k : { cut2, res2, env2 }) k->setColour (warm);

    aA = &knob (ParamID::ampA, "Ataque", "Cuanto tarda en sonar");
    aD = &knob (ParamID::ampD, "Decae", "Cuanto tarda en bajar al sostenido");
    aS = &knob (ParamID::ampS, "Sostiene", "Volumen mientras mantienes la nota");
    aR = &knob (ParamID::ampR, "Suelta", "Cuanto dura al soltar la nota");
    aC = &knob (ParamID::ampCurve, "Curva", "Forma de las curvas (lineal, rapida o lenta)");

    voices = &knob (ParamID::voiceCount, "Voces", "Cuantas notas a la vez (modo Poli)");
    glide  = &knob (ParamID::glide, "Glide", "Deslizamiento entre notas");
    bend   = &knob (ParamID::bendRange, "Bend", "Rango del pitch bend (semitonos)");
    vel    = &knob (ParamID::velSens, "Velocidad", "Cuanto afecta la fuerza de la tecla");
    volume = &knob (ParamID::master, "Volumen", "Volumen general");

    lastSampler = samplerEngine();
    updateVisibility();
    startTimerHz (10);
}

void EditView::updateVisibility()
{
    const bool s = samplerEngine();
    for (auto* c : std::initializer_list<juce::Component*> { &wave1, &wave2, &preview1, &preview2, shape1, oct1, shape2, semi2, fine2 })
        c->setVisible (! s);
    for (auto* c : std::initializer_list<juce::Component*> { &regionMini, &editRegion, &reverse, &loop, &playMode, smpStart, smpTune, smpTexture, smpGrain })
        c->setVisible (s);
    beats.setVisible (s && getParamValue (proc.state, ParamID::smpPlayMode) > 0.5f);

    const bool wt1 = ! s && juce::roundToInt (getParamValue (proc.state, ParamID::osc1Wave)) == Wave::Table;
    const bool wt2 = ! s && juce::roundToInt (getParamValue (proc.state, ParamID::osc2Wave)) == Wave::Table;
    table1.setVisible (wt1);
    table2.setVisible (wt2);
    draw1.setVisible (wt1);

    const bool f2 = getParamValue (proc.state, ParamID::f2On) > 0.5f;
    for (auto* c : std::initializer_list<juce::Component*> { &f2Type, &f2Slope, &f2Char, &routing, cut2, res2, env2 })
        c->setAlpha (f2 ? 1.0f : 0.35f);

    const int sig = (s ? 1 : 0) | (wt1 ? 2 : 0) | (wt2 ? 4 : 0);
    if (sig != layoutSig) { layoutSig = sig; resized(); }
}

void EditView::timerCallback()
{
    updateVisibility();
    if (samplerEngine() != lastSampler) { lastSampler = samplerEngine(); repaint(); }
}

void EditView::paint (juce::Graphics& g)
{
    auto iconSaw = Icons::saw(), iconSample = Icons::waveform(), iconMix = Icons::sliders(), iconUni = Icons::superSaw(),
         iconFilter = Icons::filterShape (0), iconEnv = Icons::envelope(), iconVoice = Icons::piano();
    if (samplerEngine())
        drawSection (g, rSample, "SAMPLE", &iconSample);
    else
    {
        drawSection (g, rOsc1, "OSCILADOR 1", &iconSaw);
        drawSection (g, rOsc2, "OSCILADOR 2", &iconSaw);
    }
    drawSection (g, rMix, "MEZCLA", &iconMix);
    drawSection (g, rUni, "UNISONO", &iconUni);
    drawSection (g, rFilter, "FILTROS", &iconFilter);
    drawSection (g, rAmp, "ENVOLVENTE DE VOLUMEN", &iconEnv);
    drawSection (g, rVoice, "VOCES Y SALIDA", &iconVoice);

    // Etiquetas de filtro 1 / 2
    auto badge = [&g] (juce::Rectangle<int> r, const juce::String& t, juce::Colour c)
    {
        g.setColour (c);
        g.fillEllipse (r.toFloat());
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (t, r, juce::Justification::centred);
    };
    badge (f1Type.getBounds().withWidth (18).withHeight (18).translated (-24, 6), "1", accent2);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MOTOR", rEngine.withWidth (60), juce::Justification::centredLeft);
}

void EditView::resized()
{
    auto r = getLocalBounds();
    constexpr int gap = 10;

    // ---- Fila 1 ----
    auto row1 = r.removeFromTop (272);
    r.removeFromTop (gap);
    auto left = row1.removeFromLeft (610);
    rEngine = left.removeFromTop (34);
    engine.setBounds (rEngine.withTrimmedLeft (56).withWidth (190).reduced (0, 1));
    left.removeFromTop (4);
    rOsc1 = left.removeFromLeft ((left.getWidth() - gap) / 2);
    left.removeFromLeft (gap);
    rOsc2 = left;
    rSample = rOsc1.getUnion (rOsc2);

    auto layoutOsc = [] (juce::Rectangle<int> area, IconChoice& wave, WavePreview& prev, ParamCombo& table, std::vector<NebKnob*> ks, IconButton* draw)
    {
        auto a = area.reduced (10).withTrimmedTop (22);
        wave.setBounds (a.removeFromTop (30));
        a.removeFromTop (6);
        auto pr = a.removeFromTop (54);
        if (table.isVisible())
        {
            auto side = pr.removeFromRight (104);
            table.setBounds (side.removeFromTop (28).reduced (4, 0));
            if (draw != nullptr) draw->setBounds (side.removeFromTop (26).withSizeKeepingCentre (26, 24));
        }
        prev.setBounds (pr);
        a.removeFromTop (4);
        const int w = a.getWidth() / (int) ks.size();
        for (auto* k : ks) k->setBounds (a.removeFromLeft (w));
    };
    layoutOsc (rOsc1, wave1, preview1, table1, { shape1, oct1 }, &draw1);
    layoutOsc (rOsc2, wave2, preview2, table2, { shape2, semi2, fine2 }, nullptr);

    {
        auto a = rSample.reduced (10).withTrimmedTop (22);
        auto leftCol = a.removeFromLeft (270);
        regionMini.setBounds (leftCol.removeFromTop (118));
        leftCol.removeFromTop (6);
        editRegion.setBounds (leftCol.removeFromTop (30).withWidth (160));
        a.removeFromLeft (12);
        auto top = a.removeFromTop (48);
        playMode.setBounds (top.removeFromLeft (140));
        top.removeFromLeft (8);
        reverse.setBounds (top.removeFromLeft (70).reduced (2));
        loop.setBounds (top.removeFromLeft (70).reduced (2));
        a.removeFromTop (4);
        beats.setBounds (a.removeFromTop (40).withWidth (140));
        const int w = a.getWidth() / 4;
        for (auto* k : { smpStart, smpTune, smpTexture, smpGrain }) k->setBounds (a.removeFromLeft (w));
    }

    row1.removeFromLeft (gap);
    rMix = row1.removeFromLeft (row1.getWidth() / 2 - gap / 2);
    row1.removeFromLeft (gap);
    rUni = row1;
    {
        auto a = rMix.reduced (8).withTrimmedTop (24);
        auto top = a.removeFromTop (a.getHeight() / 2), bottom = a;
        mix->setBounds (top.removeFromLeft (top.getWidth() / 2)); sub->setBounds (top);
        fm->setBounds (bottom.removeFromLeft (bottom.getWidth() / 2)); noise->setBounds (bottom);
    }
    {
        auto a = rUni.reduced (8).withTrimmedTop (24);
        auto top = a.removeFromTop (a.getHeight() / 2), bottom = a;
        uniV->setBounds (top.withSizeKeepingCentre (top.getWidth() / 2, top.getHeight()));
        uniD->setBounds (bottom.removeFromLeft (bottom.getWidth() / 2)); uniW->setBounds (bottom);
    }

    // ---- Fila 2 ----
    rFilter = r.removeFromLeft (620);
    r.removeFromLeft (gap);
    rAmp = r.removeFromLeft (310);
    r.removeFromLeft (gap);
    rVoice = r;

    {
        auto a = rFilter.reduced (10).withTrimmedTop (22);
        filterGraph.setBounds (a.removeFromTop (118));
        a.removeFromTop (6);
        auto row = [&] (juce::Rectangle<int> rowArea, IconChoice& type, IconChoice& slope, IconChoice& ch, std::vector<NebKnob*> ks, juce::Component* lead)
        {
            auto icons = rowArea.removeFromLeft (330);
            if (lead != nullptr) lead->setBounds (icons.removeFromLeft (30).withSizeKeepingCentre (28, 28));
            else icons.removeFromLeft (30);
            icons = icons.withSizeKeepingCentre (icons.getWidth(), 30);
            type.setBounds (icons.removeFromLeft (140));
            icons.removeFromLeft (6);
            slope.setBounds (icons.removeFromLeft (64));
            icons.removeFromLeft (6);
            ch.setBounds (icons.removeFromLeft (90));
            const int w = rowArea.getWidth() / 4;
            for (auto* k : ks) k->setBounds (rowArea.removeFromLeft (w));
            return rowArea;
        };
        const int rowH = a.getHeight() / 2;
        row (a.removeFromTop (rowH), f1Type, f1Slope, f1Char, { cut1, res1, env1, drive }, nullptr);
        auto rest = row (a, f2Type, f2Slope, f2Char, { cut2, res2, env2 }, &f2On);
        routing.setBounds (rest.withSizeKeepingCentre (juce::jmin (rest.getWidth() - 4, 76), 32).translated (0, 6));
    }

    {
        auto a = rAmp.reduced (10).withTrimmedTop (22);
        ampGraph.setBounds (a.removeFromTop (a.getHeight() - 86));
        a.removeFromTop (6);
        const int w = a.getWidth() / 5;
        for (auto* k : { aA, aD, aS, aR, aC }) k->setBounds (a.removeFromLeft (w));
    }

    {
        auto a = rVoice.reduced (10).withTrimmedTop (22);
        polyMode.setBounds (a.removeFromTop (46));
        a.removeFromTop (6);
        const int rowH = a.getHeight() / 3;
        auto r1 = a.removeFromTop (rowH), r2 = a.removeFromTop (rowH), r3 = a;
        voices->setBounds (r1.removeFromLeft (r1.getWidth() / 2)); glide->setBounds (r1);
        bend->setBounds (r2.removeFromLeft (r2.getWidth() / 2)); vel->setBounds (r2);
        volume->setBounds (r3.withSizeKeepingCentre (r3.getWidth() / 2, r3.getHeight()));
    }
}

//==============================================================================
NebKnob& ModView::knob (const char* id, const char* name, const char* tip)
{
    knobs.push_back (std::make_unique<NebKnob> (proc.state, id, name, tip));
    addAndMakeVisible (*knobs.back());
    return *knobs.back();
}

ModView::ModView (NebulaAudioProcessor& p)
    : proc (p),
      envGraph (p, { ParamID::fltA, ParamID::fltD, ParamID::fltS, ParamID::fltR, ParamID::fltCurve }, MatrixGrid::sourceColour (0)),
      env2Loop (p.state, ParamID::fltMode, "Loop", Icons::loopArrows(), "Loop: la envolvente se repite mientras mantienes la nota"),
      env3Loop (p.state, ParamID::env3Mode, "Loop", Icons::loopArrows(), "Loop: la envolvente se repite mientras mantienes la nota"),
      lfo1Shape (p.state, ParamID::lfoShape, [] (int i) { return Icons::lfoShape (i); }, lfoShapeNames()),
      lfo2Shape (p.state, ParamID::lfo2Shape, [] (int i) { return Icons::lfoShape (i); }, lfoShapeNames()),
      lfo1Graph (p, ParamID::lfoShape, ParamID::lfoRate, ParamID::lfoSync, MatrixGrid::sourceColour (2)),
      lfo2Graph (p, ParamID::lfo2Shape, ParamID::lfo2Rate, ParamID::lfo2Sync, MatrixGrid::sourceColour (3)),
      lfo1Sync (p.state, ParamID::lfoSync, "Sync", "Libre (Hz) o sincronizado al tempo del proyecto"),
      lfo2Sync (p.state, ParamID::lfo2Sync, "Sync", "Libre (Hz) o sincronizado al tempo del proyecto"),
      lfo1Retrig (p.state, ParamID::lfoRetrig, "Reinicio", Icons::back(), "Reiniciar el LFO con cada nota"),
      lfo2Retrig (p.state, ParamID::lfo2Retrig, "Reinicio", Icons::back(), "Reiniciar el LFO con cada nota"),
      matrix (p)
{
    const char* names[] { "Env 1 (volumen)", "Env 2", "Env 3" };
    const juce::Colour cols[] { NebulaColours::accent2, MatrixGrid::sourceColour (0), MatrixGrid::sourceColour (1) };
    for (int i = 0; i < 3; ++i)
    {
        envTabs[(size_t) i] = std::make_unique<IconButton> (names[i], Icons::envelope(), i == 0 ? "Envolvente de volumen" : "Fuente de modulacion");
        envTabs[(size_t) i]->horizontal = true;
        envTabs[(size_t) i]->colour = cols[i];
        envTabs[(size_t) i]->setClickingTogglesState (false);
        envTabs[(size_t) i]->onClick = [this, i] { showEnv (i); };
        addAndMakeVisible (*envTabs[(size_t) i]);
    }

    const std::array<std::array<const char*, 5>, 3> ids {{
        { ParamID::ampA, ParamID::ampD, ParamID::ampS, ParamID::ampR, ParamID::ampCurve },
        { ParamID::fltA, ParamID::fltD, ParamID::fltS, ParamID::fltR, ParamID::fltCurve },
        { ParamID::env3A, ParamID::env3D, ParamID::env3S, ParamID::env3R, ParamID::env3Curve } }};
    const char* kn[] { "Ataque", "Decae", "Sostiene", "Suelta", "Curva" };
    for (int e = 0; e < 3; ++e)
        for (int k = 0; k < 5; ++k)
        {
            envKnobs[(size_t) e][(size_t) k] = &knob (ids[(size_t) e][(size_t) k], kn[k], "");
            envKnobs[(size_t) e][(size_t) k]->setColour (cols[e]);
        }

    for (auto* c : std::initializer_list<juce::Component*> { &envGraph, &env2Loop, &env3Loop, &lfo1Shape, &lfo2Shape, &lfo1Graph, &lfo2Graph,
                                                             &lfo1Sync, &lfo2Sync, &lfo1Retrig, &lfo2Retrig, &matrix })
        addAndMakeVisible (c);
    lfo1Shape.colour = MatrixGrid::sourceColour (2);
    lfo2Shape.colour = MatrixGrid::sourceColour (3);
    env2Loop.colour = MatrixGrid::sourceColour (0);
    env3Loop.colour = MatrixGrid::sourceColour (1);

    l1Rate   = &knob (ParamID::lfoRate, "Velocidad", "Velocidad en Hz (si Sync esta en Libre)");
    l1Fade   = &knob (ParamID::lfoFade, "Entrada", "Tiempo para que el LFO entre poco a poco");
    l1Pitch  = &knob (ParamID::lfoPitch, "A Tono", "Vibrato directo");
    l1Filter = &knob (ParamID::lfoFilter, "A Filtro", "Movimiento directo del filtro 1");
    l2Rate   = &knob (ParamID::lfo2Rate, "Velocidad", "Velocidad en Hz (si Sync esta en Libre)");
    l2Fade   = &knob (ParamID::lfo2Fade, "Entrada", "Tiempo para que el LFO entre poco a poco");
    for (auto* k : { l1Rate, l1Fade, l1Pitch, l1Filter }) k->setColour (MatrixGrid::sourceColour (2));
    for (auto* k : { l2Rate, l2Fade }) k->setColour (MatrixGrid::sourceColour (3));

    modTime   = &knob (ParamID::modTime, "Tiempo", "Acelera (<1) o frena (>1) todas las envolventes y LFOs");
    modAmount = &knob (ParamID::modAmount, "Cantidad", "Escala toda la modulacion de la matriz");

    showEnv (1);
}

void ModView::showEnv (int index)
{
    currentEnv = index;
    for (int e = 0; e < 3; ++e)
    {
        envTabs[(size_t) e]->setToggleState (e == index, juce::dontSendNotification);
        for (auto* k : envKnobs[(size_t) e]) k->setVisible (e == index);
    }
    const std::array<std::array<const char*, 5>, 3> ids {{
        { ParamID::ampA, ParamID::ampD, ParamID::ampS, ParamID::ampR, ParamID::ampCurve },
        { ParamID::fltA, ParamID::fltD, ParamID::fltS, ParamID::fltR, ParamID::fltCurve },
        { ParamID::env3A, ParamID::env3D, ParamID::env3S, ParamID::env3R, ParamID::env3Curve } }};
    const juce::Colour cols[] { NebulaColours::accent2, MatrixGrid::sourceColour (0), MatrixGrid::sourceColour (1) };
    envGraph.setIds (ids[(size_t) index], cols[index]);
    env2Loop.setVisible (index == 1);
    env3Loop.setVisible (index == 2);
    repaint();
}

void ModView::paint (juce::Graphics& g)
{
    auto iconEnv = Icons::envelope(), iconLfo = Icons::sine(), iconMatrix = Icons::modulation();
    drawSection (g, rEnv, "ENVOLVENTES", &iconEnv);
    drawSection (g, rLfo1, "LFO 1", &iconLfo, MatrixGrid::sourceColour (2));
    drawSection (g, rLfo2, "LFO 2", &iconLfo, MatrixGrid::sourceColour (3));
    drawSection (g, rMatrix, "MATRIZ DE MODULACION", &iconMatrix);

    g.setColour (NebulaColours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    const char* hint[] { "Env 1 controla el volumen de cada nota.",
                         "Env 2 mueve los filtros (perillas Env) y se puede usar en la matriz.",
                         "Env 3 es libre: conectala en la matriz a lo que quieras." };
    g.drawFittedText (hint[currentEnv], rEnv.reduced (12).removeFromBottom (18), juce::Justification::centredLeft, 1);
}

void ModView::resized()
{
    auto r = getLocalBounds();
    constexpr int gap = 10;
    rEnv = r.removeFromLeft (420);
    r.removeFromLeft (gap);
    auto mid = r.removeFromLeft (330);
    r.removeFromLeft (gap);
    rMatrix = r;
    rLfo1 = mid.removeFromTop ((mid.getHeight() - gap) / 2);
    mid.removeFromTop (gap);
    rLfo2 = mid;

    {
        auto a = rEnv.reduced (12).withTrimmedTop (22);
        a.removeFromBottom (20);
        auto tabs = a.removeFromTop (34);
        const int tw = tabs.getWidth() / 3;
        for (auto& t : envTabs) t->setBounds (tabs.removeFromLeft (tw).reduced (2, 0));
        a.removeFromTop (8);
        auto knobsRow = a.removeFromBottom (88);
        auto loopArea = a.removeFromBottom (34);
        env2Loop.setBounds (loopArea.removeFromRight (90).reduced (2));
        env3Loop.setBounds (env2Loop.getBounds());
        a.removeFromBottom (6);
        envGraph.setBounds (a);
        const int w = knobsRow.getWidth() / 5;
        for (int e = 0; e < 3; ++e)
        {
            auto kr = knobsRow;
            for (auto* k : envKnobs[(size_t) e]) k->setBounds (kr.removeFromLeft (w));
        }
    }

    auto layoutLfo = [] (juce::Rectangle<int> area, IconChoice& shape, LfoGraph& graph, ParamCombo& sync, IconToggle& retrig, std::vector<NebKnob*> ks)
    {
        auto a = area.reduced (10).withTrimmedTop (22);
        shape.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        graph.setBounds (a.removeFromTop (60));
        a.removeFromTop (6);
        auto side = a.removeFromRight (96);
        sync.setBounds (side.removeFromTop (42));
        side.removeFromTop (4);
        retrig.setBounds (side.removeFromTop (40).reduced (8, 0));
        const int w = a.getWidth() / (int) ks.size();
        for (auto* k : ks) k->setBounds (a.removeFromLeft (w));
    };
    layoutLfo (rLfo1, lfo1Shape, lfo1Graph, lfo1Sync, lfo1Retrig, { l1Rate, l1Fade, l1Pitch, l1Filter });
    layoutLfo (rLfo2, lfo2Shape, lfo2Graph, lfo2Sync, lfo2Retrig, { l2Rate, l2Fade });

    {
        auto a = rMatrix.reduced (10).withTrimmedTop (24);
        auto bottom = a.removeFromBottom (86);
        matrix.setBounds (a);
        modTime->setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).withSizeKeepingCentre (90, 86));
        modAmount->setBounds (bottom.withSizeKeepingCentre (90, 86));
    }
}
