#include "GardenView.h"

using namespace NebulaColours;

namespace
{
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    float easeOutBack (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const float c1 = 1.70158f, c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow (t - 1.0f, 3.0f) + c1 * std::pow (t - 1.0f, 2.0f);
    }
}

//==============================================================================
GardenCanvas::GardenCanvas (NebulaAudioProcessor& p) : processor (p), garden (p.garden)
{
    setTooltip ("Clic en una neurona: escucharla. Doble clic: se vuelve tu sonido y nacen 8 neuronas nuevas. Clic en el cerebro: volver a tu sonido.");
}

void GardenCanvas::resized()
{
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (18.0f);
    centre = b.getCentre();
    const float half = juce::jmin (b.getWidth() * 0.5f, b.getHeight() * 0.5f);
    seedRadius = juce::jlimit (36.0f, 62.0f, half * 0.25f);
    minDist = seedRadius + sproutRadius() + 30.0f;
    maxDist = half - sproutRadius() - 8.0f;
    seedFrom = centre;
}

juce::Point<float> GardenCanvas::sproutPos (int k) const
{
    const float d = garden.distances[(size_t) k];
    const float dist = minDist + (maxDist - minDist) * std::sqrt (d);
    const float angle = -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * (float) k / (float) Garden::numSprouts;
    const float stretch = juce::jlimit (1.0f, 2.0f, (float) getWidth() / (float) juce::jmax (1, getHeight()));
    return { centre.x + std::cos (angle) * dist * stretch, centre.y + std::sin (angle) * dist };
}

int GardenCanvas::hitTest (juce::Point<float> p) const
{
    if (p.getDistanceFrom (centre) <= seedRadius + 4.0f) return seedIndex;
    for (int k = 0; k < Garden::numSprouts; ++k)
        if (p.getDistanceFrom (sproutPos (k)) <= sproutRadius() + 6.0f) return k;
    return none;
}

void GardenCanvas::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h != none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GardenCanvas::mouseExit (const juce::MouseEvent&) { hover = none; repaint(); }

void GardenCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (e.getNumberOfClicks() > 1) return;
    const int h = hitTest (e.position);
    if (h == none) return;
    if (crossMode && h >= 0 && garden.selected >= 0 && h != garden.selected)
    {
        seedFrom = (sproutPos (h) + sproutPos (garden.selected)) * 0.5f;
        garden.cross (garden.selected, h);
        if (onCrossed) onCrossed();
        return;
    }
    if (h == seedIndex) garden.selectSeed();
    else                garden.selectSprout (h);
    if (onSelect) onSelect();
}

void GardenCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h >= 0)
    {
        seedFrom = sproutPos (h);
        garden.plant (h);
        if (onSelect) onSelect();
    }
}

// ---------------------------------------------------------------------------
// La forma y el color de cada organismo salen de su sonido
GardenCanvas::Blob GardenCanvas::target (const Genome& gen) const
{
    Blob b;
    if (! gen.isValid()) return b;
    auto v = [&] (const char* id) { return garden.real (gen, id); };

    const bool sampler = juce::roundToInt (v (ParamID::engine)) != Engine::Synth;
    const int   w1     = (int) v (ParamID::osc1Wave);
    const int   w2     = (int) v (ParamID::osc2Wave);
    const float shape  = v (ParamID::osc1Shape);
    const float cutoff = v (ParamID::cutoff);
    const float reso   = v (ParamID::resonance);
    const float drive  = v (ParamID::drive);
    const float fm     = v (ParamID::fmAmount);
    const float sub    = v (ParamID::subLevel);
    const float attack = v (ParamID::ampA);
    const int   ftype  = (int) v (ParamID::filterType);
    const float semi   = v (ParamID::osc2Semi);
    const float sus    = v (ParamID::ampS);
    const float uni    = v (ParamID::uniVoices);
    const float tex    = v (ParamID::smpTexture);

    auto norm = [&] (const char* id) { if (auto* p = processor.state.getParameter (id)) return p->convertTo0to1 (v (id)); return 0.5f; };
    const float g1 = norm (ParamID::ampD) + norm (ParamID::smpStart),
                g2 = norm (ParamID::ampR) + norm (ParamID::lfoRate) + norm (ParamID::smpTune),
                g3 = norm (ParamID::fltA) + norm (ParamID::fltD) + norm (ParamID::smpGrain);

    const float bright = juce::jlimit (0.0f, 1.0f, std::log (cutoff / 20.0f) / std::log (1000.0f));
    const int petals   = sampler ? 5 + (int) (tex * 6.0f) : 3 + (w1 % 5) + (w2 % 4) + (std::abs (semi) > 0.5f ? 1 : 0);
    const float depth  = 0.1f + 0.28f * fm + 0.12f * reso + 0.05f * (float) ftype + 0.06f * (uni - 1.0f) / 4.0f;
    const float sharp  = 0.6f + reso * 3.5f + (w1 == Wave::Sync ? 1.5f : 0.0f);
    const float size   = 0.85f + 0.25f * sus;

    for (int i = 0; i < profileSize; ++i)
    {
        const float t = juce::MathConstants<float>::twoPi * (float) i / (float) profileSize;
        float petal = std::pow (std::abs (std::cos ((float) petals * 0.5f * t)), sharp);
        if (w1 == Wave::Table || w2 == Wave::Table) petal = 0.5f + 0.5f * std::sin ((float) petals * t + shape * 6.0f) * std::sin (2.0f * t);
        if (w1 == Wave::SuperSaw) petal = juce::jmax (petal, std::abs (std::sin ((float) petals * 1.5f * t)) * 0.6f);
        // "Huella" propia de cada sonido: pequeñas ondulaciones según otros genes,
        // para que dos brotes distintos nunca se vean iguales
        const float wobble = 0.045f * std::sin (2.0f * t + g1 * 6.28f) + 0.035f * std::sin (3.0f * t + g2 * 6.28f)
                           + 0.025f * std::sin (5.0f * t + g3 * 6.28f);
        b.prof[(size_t) i] = size * (1.0f - depth + depth * petal) * (1.0f + wobble);
    }

    float hue = juce::jmap (bright, 0.76f, 0.46f) - drive * 0.22f + fm * 0.14f + (float) ftype * 0.09f + semi / 24.0f * 0.08f + (sampler ? 0.18f + 0.25f * (norm (ParamID::smpTune) - 0.5f) + 0.1f * tex : 0.0f);
    hue -= std::floor (hue);
    b.colour = juce::Colour::fromHSV (hue, juce::jlimit (0.3f, 0.95f, 0.45f + 0.3f * drive + 0.25f * reso), 0.6f + 0.32f * bright, 1.0f);
    b.core = 0.16f + 0.18f * sub;
    b.halo = 1.25f + juce::jmin (0.35f, attack * 0.3f);
    b.rot = v (ParamID::osc2Fine) / 50.0f * 0.6f;
    b.noise = v (ParamID::noise) + tex * 0.5f;
    b.lfo = juce::jmin (1.0f, v (ParamID::lfoFilter) + v (ParamID::lfoPitch) / 100.0f);
    b.init = true;
    return b;
}

void GardenCanvas::approach (Blob& cur, const Blob& tgt, float k)
{
    if (! cur.init) { cur = tgt; return; }
    for (size_t i = 0; i < cur.prof.size(); ++i) cur.prof[i] += (tgt.prof[i] - cur.prof[i]) * k;
    cur.colour = cur.colour.interpolatedWith (tgt.colour, k);
    cur.core += (tgt.core - cur.core) * k;
    cur.halo += (tgt.halo - cur.halo) * k;
    cur.rot += (tgt.rot - cur.rot) * k;
    cur.noise += (tgt.noise - cur.noise) * k;
    cur.lfo += (tgt.lfo - cur.lfo) * k;
}

void GardenCanvas::animate()
{
    {
        const juce::ScopedLock sl (garden.lock);
        if (garden.generation != lastGeneration)
        {
            lastGeneration = garden.generation;
            growStart = nowSeconds();
            for (auto& b : sproutBlobs) b.init = false;    // los brotes nuevos nacen con su forma
        }
        approach (seedBlob, target (garden.seed), 0.12f);
        for (int k = 0; k < Garden::numSprouts; ++k)
            approach (sproutBlobs[(size_t) k], target (garden.sprouts[(size_t) k]), 0.2f);
    }
    processor.copyScope (scopeBuf.data(), (int) scopeBuf.size());
    level = juce::jlimit (0.0f, 1.0f, processor.outputLevel.load() * 3.0f);
    seedFrom += (centre - seedFrom) * 0.18f;
    repaint();
}

void GardenCanvas::drawBlob (juce::Graphics& g, juce::Point<float> c, float r, const Blob& b,
                             bool selected, bool isHover, float phase, float glowBoost) const
{
    if (! b.init || r <= 1.0f) return;
    const float rot = b.rot + phase * (0.1f + 0.4f * b.lfo);

    juce::Path shape;
    for (int i = 0; i <= profileSize; ++i)
    {
        const int idx = i % profileSize;
        const float t = juce::MathConstants<float>::twoPi * (float) idx / (float) profileSize;
        const float jitter = (b.noise * 0.08f + 0.12f * Neural::level()) * (std::sin (t * 13.0f + phase * 3.0f) + 0.6f * std::sin (t * 29.0f - phase * 2.0f));
        const float rr = r * (b.prof[(size_t) idx] + jitter);
        const juce::Point<float> pt (c.x + rr * std::cos (t + rot), c.y + rr * std::sin (t + rot));
        if (i == 0) shape.startNewSubPath (pt); else shape.lineTo (pt);
    }
    shape.closeSubPath();

    const float glow = (selected ? 0.55f : isHover ? 0.35f : 0.18f) + glowBoost * 0.4f;
    Blob hb = b; hb.colour = Neural::hot (b.colour);
    const auto& bb = hb;
    const float haloR = r * (b.halo + glowBoost * 0.25f);
    g.setGradientFill (juce::ColourGradient (bb.colour.withAlpha (juce::jmin (0.9f, glow)), c.x, c.y, bb.colour.withAlpha (0.0f), c.x + haloR, c.y, true));
    g.fillEllipse (c.x - haloR, c.y - haloR, haloR * 2.0f, haloR * 2.0f);

    g.setGradientFill (juce::ColourGradient (bb.colour.brighter (0.6f), c.x - r * 0.3f, c.y - r * 0.3f, bb.colour.darker (0.5f), c.x + r, c.y + r, true));
    g.fillPath (shape);
    g.setColour (bb.colour.brighter (0.8f).withAlpha (selected ? 1.0f : 0.6f));
    g.strokePath (shape, juce::PathStrokeType (selected ? 2.2f : 1.1f));

    const float coreR = r * b.core;
    g.setColour (bb.colour.darker (0.9f).withAlpha (0.85f));
    g.fillEllipse (c.x - coreR, c.y - coreR, coreR * 2.0f, coreR * 2.0f);
    g.setColour (bb.colour.brighter (1.0f).withAlpha (0.7f));
    g.fillEllipse (c.x - coreR * 0.35f, c.y - coreR * 0.35f, coreR * 0.7f, coreR * 0.7f);

    if (selected)
    {
        g.setColour (accent2);
        g.drawEllipse (c.x - r - 9.0f, c.y - r - 9.0f, (r + 9.0f) * 2.0f, (r + 9.0f) * 2.0f, 1.5f);
    }
}

void GardenCanvas::drawDendrites (juce::Graphics& g, juce::Point<float> c, float r, const Blob& b, int seed, float phase) const
{
    if (! b.init || r <= 2.0f) return;
    const float cz = Neural::level();
    const int n = 5 + seed % 3;
    const auto col = Neural::hot (b.colour);
    for (int k = 0; k < n; ++k)
    {
        const float a = b.rot + juce::MathConstants<float>::twoPi * ((float) k + 0.3f * (float) ((seed * 7 + k * 3) % 5) / 5.0f) / (float) n
                        + 0.15f * cz * std::sin (phase * 3.0f + (float) k);
        const float len = r * (1.15f + 0.5f * (float) ((seed + k * 5) % 4) / 3.0f) * (1.0f + 0.25f * cz);
        const juce::Point<float> d (std::cos (a), std::sin (a));
        const auto p0 = c + d * (r * 0.8f), p1 = c + d * len;
        g.setColour (col.withAlpha (0.45f));
        g.drawLine ({ p0, p1 }, 1.3f);
        for (int s = -1; s <= 1; s += 2)
        {
            const float a2 = a + (float) s * (0.45f + 0.2f * cz * std::sin (phase * 2.0f + (float) (k * s)));
            const auto p2 = p1 + juce::Point<float> (std::cos (a2), std::sin (a2)) * (len * 0.35f);
            g.setColour (col.withAlpha (0.3f));
            g.drawLine ({ p1, p2 }, 1.0f);
            g.fillEllipse (p2.x - 1.5f, p2.y - 1.5f, 3.0f, 3.0f);
        }
    }
}

// El núcleo: un cerebro que se deforma y se calienta con la locura
void GardenCanvas::drawBrain (juce::Graphics& g, juce::Point<float> c, float r, const Blob& b, bool selected, bool isHover, float phase) const
{
    if (! b.init) return;
    const float cz = Neural::level();
    const auto col = Neural::hot (b.colour, 0.05f);

    // Aura
    const float haloR = r * (1.7f + 0.4f * level + 0.3f * cz);
    g.setGradientFill (juce::ColourGradient (col.withAlpha ((selected ? 0.45f : isHover ? 0.35f : 0.25f) + 0.3f * level), c.x, c.y,
                                             col.withAlpha (0.0f), c.x + haloR, c.y, true));
    g.fillEllipse (c.x - haloR, c.y - haloR, haloR * 2.0f, haloR * 2.0f);

    // Silueta: dos hemisferios con la fisura arriba; con locura se deforma y "late"
    juce::Path brain;
    const int steps = 120;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = juce::MathConstants<float>::twoPi * (float) (i % steps) / (float) steps;
        float rr = r * (1.0f + 0.05f * std::sin (7.0f * t + phase * (0.5f + 4.0f * cz))
                             + (0.04f + 0.14f * cz) * std::sin (13.0f * t - phase * (1.0f + 6.0f * cz))
                             + 0.04f * level * std::sin (5.0f * t + phase * 9.0f));
        const float top = std::exp (-std::pow ((t - 1.5f * juce::MathConstants<float>::pi) / 0.22f, 2.0f));
        rr *= 1.0f - 0.16f * top;
        const juce::Point<float> p (c.x + rr * 1.25f * std::cos (t), c.y + rr * 0.92f * std::sin (t));
        if (i == 0) brain.startNewSubPath (p); else brain.lineTo (p);
    }
    brain.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col.brighter (0.5f), c.x - r * 0.6f, c.y - r * 0.6f, col.darker (0.7f), c.x + r, c.y + r, true));
    g.fillPath (brain);

    // Circunvoluciones (pliegues)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (brain);
        g.setColour (col.darker (0.6f).withAlpha (0.75f));
        for (int k = 0; k < 9; ++k)
        {
            juce::Path fold;
            const float y0 = c.y - r * 0.85f + (float) k * r * 0.21f;
            for (int j = 0; j <= 40; ++j)
            {
                const float u = (float) j / 40.0f;
                const float x = c.x - r * 1.3f + u * r * 2.6f;
                const float y = y0 + r * 0.07f * std::sin (u * 18.0f + (float) k * 1.7f + phase * cz * 2.5f)
                                   + r * 0.04f * std::sin (u * 41.0f - (float) k + phase * cz * 5.0f);
                if (j == 0) fold.startNewSubPath (x, y); else fold.lineTo (x, y);
            }
            g.strokePath (fold, juce::PathStrokeType (1.6f + 0.6f * cz));
        }
        // Fisura central
        juce::Path fiss;
        for (int j = 0; j <= 30; ++j)
        {
            const float u = (float) j / 30.0f;
            const float y = c.y - r * 0.95f + u * r * 1.9f;
            const float x = c.x + r * 0.04f * std::sin (u * 9.0f + phase * cz * 3.0f);
            if (j == 0) fiss.startNewSubPath (x, y); else fiss.lineTo (x, y);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.strokePath (fiss, juce::PathStrokeType (2.2f));
    }
    g.setColour (col.brighter (0.9f).withAlpha (selected ? 1.0f : 0.65f));
    g.strokePath (brain, juce::PathStrokeType (selected ? 2.2f : 1.3f));
}

void GardenCanvas::paint (juce::Graphics& g)
{
    const juce::ScopedLock sl (garden.lock);
    const double now = nowSeconds();
    const float phase = (float) std::fmod (now, 1000.0);
    const float grow = (float) (now - growStart);
    const float cz = Neural::level();

    // Memoria: las generaciones pasadas, como neuronas tenues en un anillo exterior
    {
        const int past = juce::jmin (32, garden.historySize());
        const float stretch = juce::jlimit (1.0f, 2.0f, (float) getWidth() / (float) juce::jmax (1, getHeight()));
        const float rr = maxDist + sproutRadius() * 0.9f;
        juce::Point<float> prev;
        for (int i = 0; i < past; ++i)
        {
            const float a = -juce::MathConstants<float>::halfPi - juce::MathConstants<float>::twoPi * (float) (i + 1) / 34.0f;
            const juce::Point<float> p (centre.x + std::cos (a) * rr * stretch, centre.y + std::sin (a) * rr);
            g.setColour (Neural::hot (accent).withAlpha (0.25f));
            if (i > 0) g.drawLine ({ prev, p }, 0.8f);
            g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            prev = p;
        }
    }

    // Axones (crecen al evolucionar) con pulsos que viajan
    for (int k = 0; k < Garden::numSprouts; ++k)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (grow - 0.05f * (float) k) / 0.45f);
        if (t <= 0.0f) continue;
        const auto p = sproutPos (k);
        const auto dir = (p - centre);
        const juce::Point<float> perp (-dir.y, dir.x);
        const float bend = (0.18f + 0.25f * cz) * std::sin ((float) k * 2.1f + phase * (0.6f + 2.0f * cz));
        const auto ctrl = centre + dir * 0.5f + perp * bend;
        auto at = [&] (float u) { const auto a = centre + (ctrl - centre) * u, bpt = ctrl + (p - ctrl) * u; return a + (bpt - a) * u; };
        juce::Path stem;
        stem.startNewSubPath (centre);
        const int steps = 24;
        for (int i = 1; i <= (int) (steps * t); ++i) stem.lineTo (at ((float) i / steps));
        const bool active = garden.selected == k;
        const auto c1 = Neural::hot (accent), c2 = Neural::hot (accent2);
        g.setGradientFill (juce::ColourGradient (c1.withAlpha (0.7f), centre.x, centre.y, c2.withAlpha (active ? 0.9f : 0.35f), p.x, p.y, false));
        g.strokePath (stem, juce::PathStrokeType (active ? 3.0f : 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (t >= 1.0f)
        {
            const int pulses = active ? 3 : 1 + (cz > 0.4f ? 1 : 0);
            for (int q = 0; q < pulses; ++q)
            {
                const float u = std::fmod (phase * (0.35f + 1.2f * cz + (active ? 0.4f : 0.0f)) + (float) k * 0.17f + (float) q / (float) pulses, 1.0f);
                const auto pp = at (u);
                g.setColour (c2.withAlpha (0.85f * std::sin (u * juce::MathConstants<float>::pi)));
                g.fillEllipse (pp.x - 2.5f, pp.y - 2.5f, 5.0f, 5.0f);
            }
        }
    }

    // Neuronas: dendritas + soma (su forma sale del sonido)
    for (int k = 0; k < Garden::numSprouts; ++k)
    {
        const float s = easeOutBack ((grow - 0.25f - 0.05f * (float) k) / 0.45f);
        const bool sel = garden.selected == k;
        const auto p = sproutPos (k);
        drawDendrites (g, p, sproutRadius() * s, sproutBlobs[(size_t) k], k * 13 + garden.generation, phase);
        drawBlob (g, p, sproutRadius() * s * 0.85f, sproutBlobs[(size_t) k], sel, hover == k, phase + (float) k, sel ? level : 0.0f);
        if (crossMode && garden.selected >= 0 && k != garden.selected && hover == k)
        {
            g.setColour (warm);
            g.drawEllipse (p.x - sproutRadius() - 8.0f, p.y - sproutRadius() - 8.0f, (sproutRadius() + 8.0f) * 2.0f, (sproutRadius() + 8.0f) * 2.0f, 2.0f);
        }
    }

    // El cerebro (tu sonido) + osciloscopio circular alrededor
    const bool seedSel = garden.selected == -1;
    drawBrain (g, seedFrom, seedRadius, seedBlob, seedSel, hover == seedIndex, phase);
    {
        int start = 0;
        for (int i = 1; i < 256; ++i) if (scopeBuf[(size_t) i - 1] <= 0.0f && scopeBuf[(size_t) i] > 0.0f) { start = i; break; }
        float peak = 0.02f;
        for (int i = 0; i < 256; ++i) peak = juce::jmax (peak, std::abs (scopeBuf[(size_t) (start + i)]));
        juce::Path ring;
        const float baseR = seedRadius * 1.6f;
        for (int i = 0; i < 256; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 256.0f;
            const float v = scopeBuf[(size_t) (start + i)] / peak;
            const float rr = baseR * (1.0f + (0.12f + 0.15f * cz) * v * juce::jmin (1.0f, level * 4.0f));
            const juce::Point<float> pt (seedFrom.x + rr * 1.15f * std::sin (a), seedFrom.y - rr * std::cos (a));
            if (i == 0) ring.startNewSubPath (pt); else ring.lineTo (pt);
        }
        ring.closeSubPath();
        g.setColour (Neural::hot (accent2).withAlpha (0.12f + 0.6f * level));
        g.strokePath (ring, juce::PathStrokeType (1.5f));
    }

    g.setColour (text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("TU SONIDO", juce::Rectangle<float> (centre.x - 60.0f, centre.y + seedRadius * 1.6f + 6.0f, 120.0f, 14.0f), juce::Justification::centred);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("generacion " + juce::String (garden.generation) + (cz > 0.05f ? "  |  locura " + juce::String (juce::roundToInt (cz * 100.0f)) + " %" : juce::String()),
                juce::Rectangle<float> (centre.x - 100.0f, centre.y + seedRadius * 1.6f + 20.0f, 200.0f, 14.0f), juce::Justification::centred);

    // Ayuda con íconos
    auto help = getLocalBounds().removeFromBottom (18).toFloat();
    const juce::String items[] { "clic: escuchar", "doble clic: usarla", "cerebro: volver" };
    const juce::Path icons[] { Icons::speaker(), Icons::sprout(), Icons::seed() };
    float x = help.getCentreX() - 230.0f;
    for (int i = 0; i < 3; ++i)
    {
        Icons::draw (g, icons[i], { x, help.getY() + 2.0f, 14.0f, 14.0f }, textDim, 1.3f);
        g.setColour (textDim);
        g.drawText (items[i], juce::Rectangle<float> (x + 18.0f, help.getY(), 130.0f, 18.0f), juce::Justification::centredLeft);
        x += 160.0f;
    }
    if (crossMode)
    {
        g.setColour (warm);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Toca otra neurona para cruzarla con la elegida", getLocalBounds().removeFromTop (22), juce::Justification::centred);
    }
}

//==============================================================================
SamplePanel::SamplePanel (NebulaAudioProcessor& p) : processor (p), region (p, false)
{
    addAndMakeVisible (region);
    region.onClick = [this] { if (processor.sampleSource.get() == nullptr) chooseFile(); else if (onOpenEditor) onOpenEditor(); };
    region.setInterceptsMouseClicks (true, false);

    for (auto* b : { &recreateButton, &editButton, &cleanButton, &recipeButton, &cancelButton })
    {
        b->horizontal = true;
        addAndMakeVisible (b);
    }
    addAndMakeVisible (loadButton);
    recreateButton.colour = accent; recreateButton.filled = true;
    cleanButton.colour = accent2;
    recipeButton.colour = accent;

    recreateButton.onClick = [this]
    {
        message = "Analizando...";
        repaint();
        const auto r = processor.recreateFromSample();
        if (r == NebulaAudioProcessor::RecreateResult::Failed) message = "No pude analizar esa seccion. Prueba eligiendo otra parte.";
        else if (onSoundChanged) onSoundChanged();
        update();
    };
    editButton.onClick = [this] { if (onOpenEditor) onOpenEditor(); };
    cleanButton.onClick = [this] { if (onOpenCleaner) onOpenCleaner(); };
    recipeButton.onClick = [this] { if (onOpenRecipe) onOpenRecipe(); };
    loadButton.onClick = [this] { chooseFile(); };
    cancelButton.onClick = [this] { processor.cancelImitation(); message = "Cancelado."; };

    seenImitations = processor.imitationCount.load();
    update();
}

void SamplePanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Elige un audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
}

void SamplePanel::loadFile (const juce::File& f)
{
    const auto error = processor.loadSample (f);
    message = error.isNotEmpty() ? error : "Dale RECREAR: Nebula lo reconstruye con su sinte.";
    recreated = false;
    update();
}

void SamplePanel::update()
{
    shown = processor.sampleSource.get();
    const bool imitating = processor.isImitating();
    const bool has = shown != nullptr;
    for (auto* b : { &recreateButton, &editButton, &cleanButton }) b->setEnabled (has && ! imitating);
    recipeButton.setVisible (recreated || processor.recipeOrigin != NebulaAudioProcessor::RecipeOrigin::None);
    cancelButton.setVisible (imitating);
    loadButton.setVisible (has);

    if (const int count = processor.imitationCount.load(); count != seenImitations)
    {
        seenImitations = count;
        recreated = true;
        message = "Toca cualquier tecla: en C suena C. Usa A/B (abajo) para comparar con el original.";
        recipeButton.setVisible (true);
        if (onSoundChanged) onSoundChanged();
    }
    repaint();
}

void SamplePanel::paint (juce::Graphics& g)
{
    auto icon = Icons::waveform();
    Icons::draw (g, icon, { 0.0f, 3.0f, 16.0f, 16.0f }, Neural::hot (accent2), 1.5f);
    g.setColour (text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SAMPLE", 22, 0, getWidth() - 22, 22, juce::Justification::centredLeft);

    const auto box = dropArea().toFloat();
    if (shown == nullptr || dragHover)
    {
        g.setColour (dragHover ? accent.withAlpha (0.25f) : track.withAlpha (0.6f));
        g.fillRoundedRectangle (box, 6.0f);
        juce::Path border;
        border.addRoundedRectangle (box.reduced (0.5f), 6.0f);
        const float dashes[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (border, border, dashes, 2);
        g.setColour (dragHover ? accent2 : panelEdge.brighter (0.4f));
        g.fillPath (border);
        Icons::draw (g, Icons::folder(), box.withSizeKeepingCentre (26.0f, 26.0f).translated (0.0f, -12.0f), textDim, 1.5f);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("Arrastra un audio aqui\no haz clic para buscarlo", dropArea().reduced (6).withTrimmedTop (34), juce::Justification::centred, 2);
    }
    else
    {
        g.setColour (text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (shown->name, 0, 112, getWidth() - 30, 16, juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.0f));
        const float root = shown->rootNote + processor.rootOffset();
        juce::String tone;
        if (shown->melodic && shown->keyTonic >= 0)
            tone = "Frase en " + juce::String (SoundProfiler::noteNames (shown->keyTonic)) + (shown->keyMinor ? " menor" : " mayor") + "  ->  en C";
        else if (shown->hasPitch)
            tone = "Original: " + SoundProfiler::noteWithCents (root) + "  ->  afinado a C";
        else
            tone = "Sin nota fija (golpe o textura): C = original";
        g.drawText (tone, 0, 128, getWidth(), 16, juce::Justification::centredLeft, true);
    }

    // Parecido (grande) después de recrear
    if (recreated && processor.lastSimilarity.load() >= 0.0f && ! processor.isImitating())
    {
        const auto r = juce::Rectangle<int> (0, recreateButton.getBottom() + 10, getWidth(), 40);
        g.setColour (Neural::hot (accent2));
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText (juce::String (juce::roundToInt (processor.lastSimilarity.load() * 100.0f)) + " %", r.withWidth (96), juce::Justification::centredLeft);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("de parecido con\nel original", r.withTrimmedLeft (96), juce::Justification::centredLeft, 2);
    }

    // Estado
    auto status = juce::Rectangle<int> (0, recipeButton.getBottom() + 10, getWidth(), 60);
    if (processor.isImitating())
    {
        const float prog = processor.imitationProgress();
        auto bar = juce::Rectangle<float> (0.0f, (float) recreateButton.getBottom() + 8.0f, (float) getWidth(), 5.0f);
        g.setColour (track);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setGradientFill (juce::ColourGradient (accent, bar.getX(), 0.0f, accent2, bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * prog)), 3.0f);
    }
    else if (message.isNotEmpty())
    {
        g.setColour (text.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (message, status, juce::Justification::topLeft, 4, 1.0f);
    }
}

void SamplePanel::resized()
{
    region.setBounds (dropArea());
    loadButton.setBounds (getWidth() - 26, 110, 26, 22);
    recreateButton.setBounds (0, 154, getWidth(), 48);
    recipeButton.setBounds (0, 262, getWidth(), 28);
    cancelButton.setBounds (0, 212, 110, 26);
    // Lo secundario, chiquito y abajo
    const int y = getHeight() - 30;
    editButton.setBounds (0, y, getWidth() / 2 - 3, 28);
    cleanButton.setBounds (getWidth() / 2 + 3, y, getWidth() - getWidth() / 2 - 3, 28);
}

//==============================================================================
GardenView::GardenView (NebulaAudioProcessor& p) : processor (p), garden (p.garden), canvas (p), samplePanel (p)
{
    addAndMakeVisible (canvas);
    addAndMakeVisible (samplePanel);
    canvas.onSelect = [this] { morphSlider.setValue (1.0, juce::dontSendNotification); audition(); refreshButtons(); };
    samplePanel.onSoundChanged = [this] { audition(); refreshButtons(); };
    samplePanel.onOpenEditor = [this] { if (onOpenSampleEditor) onOpenSampleEditor(); };
    samplePanel.framesForWave = [this] { return framesForWave ? framesForWave() : 32; };
    samplePanel.onOpenCleaner = [this] { if (onOpenCleaner) onOpenCleaner(); };
    samplePanel.onOpenRecipe = [this] { if (onOpenRecipe) onOpenRecipe(); };

    mutationSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    mutationSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    mutationSlider.setRange (0.05, 1.0, 0.01);
    mutationSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    mutationSlider.setValue (garden.mutation, juce::dontSendNotification);
    mutationSlider.onValueChange = [this] { garden.mutation = (float) mutationSlider.getValue(); };
    mutationSlider.setColour (juce::Slider::trackColourId, accent);
    mutationSlider.setColour (juce::Slider::backgroundColourId, track);
    mutationSlider.setColour (juce::Slider::thumbColourId, accent2);
    mutationSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    mutationSlider.setTooltip ("Mutacion: cuanto cambian las neuronas nuevas respecto al nucleo");
    addAndMakeVisible (mutationSlider);

    morphSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    morphSlider.setRange (0.0, 1.0, 0.001);
    morphSlider.setValue (1.0, juce::dontSendNotification);
    morphSlider.setColour (juce::Slider::trackColourId, accent2);
    morphSlider.setColour (juce::Slider::backgroundColourId, track);
    morphSlider.setColour (juce::Slider::thumbColourId, accent);
    morphSlider.setTooltip ("Mezclar: de tu sonido (izquierda) a la neurona elegida (derecha), poco a poco");
    morphSlider.onValueChange = [this] { if (garden.selected >= 0) garden.blend (garden.selected, (float) morphSlider.getValue()); };
    addAndMakeVisible (morphSlider);

    crossButton.horizontal = true;
    crossButton.setClickingTogglesState (true);
    crossButton.colour = warm;
    crossButton.onClick = [this] { canvas.crossMode = crossButton.getToggleState(); };
    canvas.onCrossed = [this] { crossButton.setToggleState (false, juce::dontSendNotification); canvas.crossMode = false; audition(); refreshButtons(); };
    addAndMakeVisible (crossButton);

    germinateButton.onClick = [this] { garden.germinate(); refreshButtons(); };
    rerollButton.onClick    = [this] { garden.reroll(); refreshButtons(); };
    randomButton.onClick    = [this] { garden.newRandomSeed(); audition(); refreshButtons(); };
    backButton.onClick      = [this] { garden.back(); refreshButtons(); };
    germinateButton.filled = true;

    lockEnvButton.setClickingTogglesState (true);
    lockEnvButton.setToggleState (garden.lockEnvelope, juce::dontSendNotification);
    lockEnvButton.onClick = [this] { garden.lockEnvelope = lockEnvButton.getToggleState(); };
    lockFilterButton.setClickingTogglesState (true);
    lockFilterButton.setToggleState (garden.lockFilter, juce::dontSendNotification);
    lockFilterButton.onClick = [this] { garden.lockFilter = lockFilterButton.getToggleState(); };
    lockFilterButton.colour = accent2;
    listenButton.setClickingTogglesState (true);
    listenButton.setToggleState (true, juce::dontSendNotification);
    lockEnvButton.colour = listenButton.colour = accent2;

    for (auto* b : { &germinateButton, &rerollButton, &randomButton, &backButton, &lockEnvButton, &lockFilterButton, &listenButton })
        addAndMakeVisible (b);

    refreshButtons();
    startTimerHz (30);
}

GardenView::~GardenView()
{
    if (noteIsOn) processor.keyboardState.noteOff (1, auditionNoteOn, 0.0f);
}

bool GardenView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
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
    germinateButton.setButtonText (garden.selected >= 0 ? "Usar esta" : "Evolucionar");
    morphSlider.setEnabled (garden.selected >= 0);
    crossButton.setEnabled (garden.selected >= 0);
}

void GardenView::audition()
{
    if (! listenButton.getToggleState()) return;
    auto& ks = processor.keyboardState;
    if (noteIsOn) ks.noteOff (1, auditionNoteOn, 0.0f);
    auditionNoteOn = processor.homeKey();   // todo se escucha en C
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
        lockFilterButton.setToggleState (garden.lockFilter, juce::dontSendNotification);
        refreshButtons();
    }
    samplePanel.update();
    canvas.animate();
}

void GardenView::paint (juce::Graphics& g)
{
    // Panel translúcido: detrás se ve la red neuronal
    const auto r = getLocalBounds().toFloat();
    g.setColour (panel.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Neural::hot (panelEdge.brighter (0.15f), 0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (panel.withAlpha (0.7f));
    g.fillRoundedRectangle (samplePanel.getBounds().toFloat().expanded (6.0f), 6.0f);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MUTACION", mutationSlider.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
    g.drawText ("MEZCLAR", morphSlider.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
}

void GardenView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto bar = r.removeFromBottom (46);
    r.removeFromBottom (8);
    samplePanel.setBounds (r.removeFromLeft (220));
    r.removeFromLeft (14);
    canvas.setBounds (r);

    auto left = bar.removeFromLeft (200);
    mutationSlider.setBounds (left.withTrimmedTop (16).reduced (0, 2));
    bar.removeFromLeft (12);
    for (auto* b : { &germinateButton, &rerollButton, &randomButton, &backButton })
        b->setBounds (bar.removeFromLeft (80).reduced (3, 0));
    bar.removeFromLeft (14);
    morphSlider.setBounds (bar.removeFromLeft (150).withTrimmedTop (16).reduced (0, 4));
    bar.removeFromLeft (6);
    crossButton.setBounds (bar.removeFromLeft (92).withSizeKeepingCentre (92, 32));
    bar.removeFromLeft (14);
    lockEnvButton.setBounds (bar.removeFromLeft (76).reduced (3, 0));
    lockFilterButton.setBounds (bar.removeFromLeft (76).reduced (3, 0));
    listenButton.setBounds (bar.removeFromLeft (76).reduced (3, 0));
}
