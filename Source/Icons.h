#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ===========================================================================
// Íconos vectoriales de Nebula. Todos se dibujan en una caja de 0..1 y se
// escalan al tamaño del botón. Se dibujan como trazo (stroke).
// ===========================================================================
namespace Icons
{
    using juce::Path;

    inline Path fromFunction (const std::function<float (float)>& f, int steps = 64, float yScale = 0.38f)
    {
        Path p;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / steps;
            const float y = 0.5f - f (x) * yScale;
            if (i == 0) p.startNewSubPath (0.08f + x * 0.84f, y); else p.lineTo (0.08f + x * 0.84f, y);
        }
        return p;
    }

    // ---- Formas de onda ----
    inline Path sine()     { return fromFunction ([] (float x) { return std::sin (x * juce::MathConstants<float>::twoPi); }); }
    inline Path triangle() { return fromFunction ([] (float x) { return 1.0f - 4.0f * std::abs (std::fmod (x + 0.25f, 1.0f) - 0.5f); }); }
    inline Path saw()
    {
        Path p; p.startNewSubPath (0.08f, 0.7f); p.lineTo (0.5f, 0.3f); p.lineTo (0.5f, 0.7f); p.lineTo (0.92f, 0.3f); return p;
    }
    inline Path pulse()
    {
        Path p; p.startNewSubPath (0.08f, 0.7f); p.lineTo (0.08f, 0.3f); p.lineTo (0.32f, 0.3f); p.lineTo (0.32f, 0.7f);
        p.lineTo (0.62f, 0.7f); p.lineTo (0.62f, 0.3f); p.lineTo (0.8f, 0.3f); p.lineTo (0.8f, 0.7f); p.lineTo (0.92f, 0.7f); return p;
    }
    inline Path superSaw()
    {
        Path p;
        for (int k = 0; k < 3; ++k)
        {
            const float o = (float) k * 0.07f - 0.07f;
            p.startNewSubPath (0.1f + o, 0.72f - o * 0.5f); p.lineTo (0.5f + o, 0.3f - o * 0.5f); p.lineTo (0.5f + o, 0.72f - o * 0.5f); p.lineTo (0.88f + o, 0.32f - o * 0.5f);
        }
        return p;
    }
    inline Path syncWave()
    {
        Path p; p.startNewSubPath (0.08f, 0.7f);
        for (int c = 0; c < 2; ++c)
        {
            const float x0 = 0.08f + c * 0.42f;
            p.lineTo (x0 + 0.14f, 0.35f); p.lineTo (x0 + 0.14f, 0.65f); p.lineTo (x0 + 0.28f, 0.3f); p.lineTo (x0 + 0.34f, 0.45f);
            p.lineTo (x0 + 0.42f, 0.45f); p.lineTo (x0 + 0.42f, 0.7f);
        }
        return p;
    }
    inline Path wavetable()
    {
        Path p;
        for (int k = 0; k < 3; ++k)
        {
            const float dx = k * 0.06f, dy = -k * 0.09f;
            const float sharp = 1.0f + k * 1.5f;
            for (int i = 0; i <= 40; ++i)
            {
                const float x = (float) i / 40.0f;
                const float s = std::sin (x * juce::MathConstants<float>::twoPi);
                const float y = std::copysign (std::pow (std::abs (s), 1.0f / sharp), s);
                const float px = 0.06f + dx + x * 0.72f, py = 0.62f + dy - y * 0.18f;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
        }
        return p;
    }
    inline Path wave (int index)
    {
        switch (index)
        {
            case 0: return sine();   case 1: return triangle(); case 2: return saw(); case 3: return pulse();
            case 4: return superSaw(); case 5: return syncWave(); default: return wavetable();
        }
    }

    // ---- Filtros ----
    inline Path filterShape (int kind)
    {
        Path p;
        switch (kind)
        {
            case 0: p.startNewSubPath (0.06f, 0.4f); p.lineTo (0.45f, 0.4f); p.quadraticTo (0.6f, 0.4f, 0.62f, 0.28f); p.quadraticTo (0.66f, 0.5f, 0.94f, 0.85f); break;
            case 1: p.startNewSubPath (0.06f, 0.85f); p.quadraticTo (0.4f, 0.75f, 0.5f, 0.28f); p.quadraticTo (0.6f, 0.75f, 0.94f, 0.85f); break;
            case 2: p.startNewSubPath (0.06f, 0.85f); p.quadraticTo (0.34f, 0.5f, 0.38f, 0.28f); p.quadraticTo (0.42f, 0.4f, 0.55f, 0.4f); p.lineTo (0.94f, 0.4f); break;
            default: p.startNewSubPath (0.06f, 0.4f); p.lineTo (0.38f, 0.4f); p.quadraticTo (0.5f, 0.42f, 0.5f, 0.85f); p.quadraticTo (0.5f, 0.42f, 0.62f, 0.4f); p.lineTo (0.94f, 0.4f); break;
        }
        return p;
    }
    inline Path serial()
    {
        Path p; p.addRectangle (0.06f, 0.36f, 0.28f, 0.28f); p.addRectangle (0.66f, 0.36f, 0.28f, 0.28f);
        p.startNewSubPath (0.34f, 0.5f); p.lineTo (0.66f, 0.5f); return p;
    }
    inline Path parallel()
    {
        Path p; p.addRectangle (0.36f, 0.14f, 0.28f, 0.24f); p.addRectangle (0.36f, 0.62f, 0.28f, 0.24f);
        p.startNewSubPath (0.06f, 0.5f); p.lineTo (0.2f, 0.5f); p.lineTo (0.2f, 0.26f); p.lineTo (0.36f, 0.26f);
        p.startNewSubPath (0.2f, 0.5f); p.lineTo (0.2f, 0.74f); p.lineTo (0.36f, 0.74f);
        p.startNewSubPath (0.64f, 0.26f); p.lineTo (0.8f, 0.26f); p.lineTo (0.8f, 0.74f); p.lineTo (0.64f, 0.74f);
        p.startNewSubPath (0.8f, 0.5f); p.lineTo (0.94f, 0.5f); return p;
    }

    // ---- LFO ----
    inline Path lfoShape (int s)
    {
        switch (s)
        {
            case 0: return sine();
            case 1: return triangle();
            case 2: return saw();
            case 3: { Path p; p.startNewSubPath (0.08f, 0.7f); p.lineTo (0.08f, 0.3f); p.lineTo (0.5f, 0.3f); p.lineTo (0.5f, 0.7f); p.lineTo (0.92f, 0.7f); p.lineTo (0.92f, 0.3f); return p; }
            case 4: { Path p; const float y[] { 0.4f, 0.65f, 0.3f, 0.55f, 0.7f, 0.35f }; p.startNewSubPath (0.08f, y[0]);
                      for (int i = 0; i < 6; ++i) { const float x0 = 0.08f + i * 0.14f; p.lineTo (x0, y[i]); p.lineTo (x0 + 0.14f, y[i]); } return p; }
            default: { Path p; p.startNewSubPath (0.08f, 0.45f); p.cubicTo (0.25f, 0.2f, 0.35f, 0.75f, 0.5f, 0.6f); p.cubicTo (0.65f, 0.45f, 0.75f, 0.25f, 0.92f, 0.5f); return p; }
        }
    }

    // ---- Fuentes de modulación ----
    inline Path envelope()
    {
        Path p; p.startNewSubPath (0.06f, 0.82f); p.lineTo (0.22f, 0.2f); p.lineTo (0.42f, 0.5f); p.lineTo (0.7f, 0.5f); p.lineTo (0.94f, 0.82f); return p;
    }
    inline Path velocity()
    {
        Path p; for (int i = 0; i < 4; ++i) p.addRectangle (0.1f + i * 0.21f, 0.8f - (i + 1) * 0.15f, 0.14f, (i + 1) * 0.15f); return p;
    }
    inline Path modWheel()
    {
        Path p; p.addRoundedRectangle (0.3f, 0.08f, 0.4f, 0.84f, 0.12f);
        for (int i = 0; i < 4; ++i) { p.startNewSubPath (0.38f, 0.28f + i * 0.14f); p.lineTo (0.62f, 0.28f + i * 0.14f); } return p;
    }
    inline Path aftertouch()
    {
        Path p; p.addRectangle (0.18f, 0.55f, 0.64f, 0.3f);
        p.startNewSubPath (0.5f, 0.1f); p.lineTo (0.5f, 0.45f); p.startNewSubPath (0.38f, 0.33f); p.lineTo (0.5f, 0.45f); p.lineTo (0.62f, 0.33f); return p;
    }
    inline Path keyTrack()
    {
        Path p; p.addRectangle (0.08f, 0.3f, 0.84f, 0.5f);
        for (int i = 1; i < 4; ++i) { p.startNewSubPath (0.08f + i * 0.21f, 0.3f); p.lineTo (0.08f + i * 0.21f, 0.8f); }
        p.startNewSubPath (0.1f, 0.18f); p.lineTo (0.9f, 0.08f); return p;
    }
    inline Path modSource (int s)
    {
        switch (s)
        {
            case 0: case 1: return envelope();
            case 2: case 3: return sine();
            case 4: return velocity();
            case 5: return modWheel();
            case 6: return aftertouch();
            default: return keyTrack();
        }
    }

    // ---- Botones generales ----
    inline Path seed()
    {
        Path p; p.addEllipse (0.3f, 0.45f, 0.4f, 0.42f);
        p.startNewSubPath (0.5f, 0.45f); p.quadraticTo (0.48f, 0.25f, 0.62f, 0.12f);
        p.startNewSubPath (0.55f, 0.24f); p.quadraticTo (0.75f, 0.12f, 0.85f, 0.22f); p.quadraticTo (0.72f, 0.36f, 0.55f, 0.24f); return p;
    }
    inline Path sprout()
    {
        Path p; p.startNewSubPath (0.5f, 0.92f); p.lineTo (0.5f, 0.42f);
        p.startNewSubPath (0.5f, 0.55f); p.quadraticTo (0.2f, 0.55f, 0.15f, 0.25f); p.quadraticTo (0.45f, 0.25f, 0.5f, 0.55f);
        p.startNewSubPath (0.5f, 0.45f); p.quadraticTo (0.8f, 0.45f, 0.85f, 0.12f); p.quadraticTo (0.55f, 0.15f, 0.5f, 0.45f); return p;
    }
    inline Path dice()
    {
        Path p; p.addRoundedRectangle (0.14f, 0.14f, 0.72f, 0.72f, 0.12f);
        for (auto [x, y] : { std::pair { 0.32f, 0.32f }, { 0.68f, 0.68f }, { 0.5f, 0.5f }, { 0.68f, 0.32f }, { 0.32f, 0.68f } })
            p.addEllipse (x - 0.04f, y - 0.04f, 0.08f, 0.08f);
        return p;
    }
    inline Path sparkle()
    {
        Path p; p.startNewSubPath (0.45f, 0.1f); p.quadraticTo (0.5f, 0.45f, 0.82f, 0.48f); p.quadraticTo (0.5f, 0.52f, 0.45f, 0.9f);
        p.quadraticTo (0.4f, 0.52f, 0.08f, 0.48f); p.quadraticTo (0.4f, 0.45f, 0.45f, 0.1f);
        p.startNewSubPath (0.8f, 0.1f); p.lineTo (0.8f, 0.3f); p.startNewSubPath (0.7f, 0.2f); p.lineTo (0.9f, 0.2f); return p;
    }
    inline Path back()
    {
        Path p; p.startNewSubPath (0.35f, 0.25f); p.lineTo (0.12f, 0.45f); p.lineTo (0.35f, 0.65f);
        p.startNewSubPath (0.12f, 0.45f); p.lineTo (0.62f, 0.45f); p.quadraticTo (0.88f, 0.45f, 0.88f, 0.68f); p.quadraticTo (0.88f, 0.86f, 0.62f, 0.86f); return p;
    }
    inline Path lock()
    {
        Path p; p.addRoundedRectangle (0.2f, 0.45f, 0.6f, 0.45f, 0.08f);
        p.startNewSubPath (0.32f, 0.45f); p.lineTo (0.32f, 0.3f); p.quadraticTo (0.32f, 0.1f, 0.5f, 0.1f); p.quadraticTo (0.68f, 0.1f, 0.68f, 0.3f); p.lineTo (0.68f, 0.45f); return p;
    }
    inline Path speaker()
    {
        Path p; p.startNewSubPath (0.1f, 0.38f); p.lineTo (0.28f, 0.38f); p.lineTo (0.5f, 0.18f); p.lineTo (0.5f, 0.82f); p.lineTo (0.28f, 0.62f); p.lineTo (0.1f, 0.62f); p.closeSubPath();
        p.startNewSubPath (0.64f, 0.34f); p.quadraticTo (0.74f, 0.5f, 0.64f, 0.66f);
        p.startNewSubPath (0.74f, 0.22f); p.quadraticTo (0.92f, 0.5f, 0.74f, 0.78f); return p;
    }
    inline Path wand()
    {
        Path p; p.startNewSubPath (0.12f, 0.88f); p.lineTo (0.62f, 0.38f);
        p.startNewSubPath (0.7f, 0.1f); p.lineTo (0.7f, 0.3f); p.startNewSubPath (0.6f, 0.2f); p.lineTo (0.8f, 0.2f);
        p.startNewSubPath (0.86f, 0.42f); p.lineTo (0.86f, 0.56f); p.startNewSubPath (0.79f, 0.49f); p.lineTo (0.93f, 0.49f);
        p.startNewSubPath (0.42f, 0.12f); p.lineTo (0.42f, 0.22f); p.startNewSubPath (0.37f, 0.17f); p.lineTo (0.47f, 0.17f); return p;
    }
    inline Path waveform()
    {
        Path p; const float h[] { 0.1f, 0.3f, 0.2f, 0.42f, 0.25f, 0.38f, 0.15f, 0.3f, 0.1f };
        for (int i = 0; i < 9; ++i) { const float x = 0.1f + i * 0.1f; p.startNewSubPath (x, 0.5f - h[i]); p.lineTo (x, 0.5f + h[i]); } return p;
    }
    inline Path cycle()
    {
        Path p = sine(); p.addEllipse (0.02f, 0.02f, 0.96f, 0.96f); return p;
    }
    inline Path scissors()
    {
        Path p; p.addEllipse (0.1f, 0.6f, 0.24f, 0.24f); p.addEllipse (0.1f, 0.16f, 0.24f, 0.24f);
        p.startNewSubPath (0.3f, 0.35f); p.lineTo (0.9f, 0.7f); p.startNewSubPath (0.3f, 0.65f); p.lineTo (0.9f, 0.3f); return p;
    }
    inline Path pencil()
    {
        Path p; p.startNewSubPath (0.15f, 0.85f); p.lineTo (0.2f, 0.65f); p.lineTo (0.7f, 0.15f); p.lineTo (0.85f, 0.3f); p.lineTo (0.35f, 0.8f); p.closeSubPath();
        p.startNewSubPath (0.6f, 0.25f); p.lineTo (0.75f, 0.4f); return p;
    }
    inline Path power()
    {
        Path p; p.addCentredArc (0.5f, 0.55f, 0.32f, 0.32f, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        p.startNewSubPath (0.5f, 0.12f); p.lineTo (0.5f, 0.5f); return p;
    }
    inline Path broom()   // Limpiar
    {
        Path p; p.startNewSubPath (0.82f, 0.1f); p.lineTo (0.5f, 0.5f);
        p.startNewSubPath (0.38f, 0.42f); p.lineTo (0.62f, 0.62f); p.lineTo (0.42f, 0.9f); p.lineTo (0.1f, 0.62f); p.closeSubPath();
        p.startNewSubPath (0.22f, 0.72f); p.lineTo (0.32f, 0.8f); return p;
    }
    inline Path recipe()  // libro / receta
    {
        Path p; p.startNewSubPath (0.5f, 0.22f); p.lineTo (0.5f, 0.86f);
        p.startNewSubPath (0.5f, 0.22f); p.quadraticTo (0.3f, 0.12f, 0.1f, 0.18f); p.lineTo (0.1f, 0.8f); p.quadraticTo (0.3f, 0.75f, 0.5f, 0.86f);
        p.quadraticTo (0.7f, 0.75f, 0.9f, 0.8f); p.lineTo (0.9f, 0.18f); p.quadraticTo (0.7f, 0.12f, 0.5f, 0.22f);
        p.startNewSubPath (0.2f, 0.38f); p.lineTo (0.4f, 0.4f); p.startNewSubPath (0.2f, 0.52f); p.lineTo (0.4f, 0.54f);
        p.startNewSubPath (0.6f, 0.4f); p.lineTo (0.8f, 0.38f); p.startNewSubPath (0.6f, 0.54f); p.lineTo (0.8f, 0.52f); return p;
    }
    inline Path history()
    {
        Path p; p.addCentredArc (0.52f, 0.52f, 0.36f, 0.36f, 0.0f, -2.2f, 3.4f, true);
        p.startNewSubPath (0.14f, 0.18f); p.lineTo (0.17f, 0.36f); p.lineTo (0.34f, 0.32f);
        p.startNewSubPath (0.52f, 0.3f); p.lineTo (0.52f, 0.54f); p.lineTo (0.68f, 0.64f); return p;
    }
    inline Path grains()
    {
        Path p; const float pts[][3] { { 0.2f, 0.3f, 0.07f }, { 0.45f, 0.2f, 0.05f }, { 0.7f, 0.32f, 0.08f }, { 0.3f, 0.62f, 0.06f },
                                       { 0.58f, 0.58f, 0.09f }, { 0.82f, 0.7f, 0.05f }, { 0.42f, 0.84f, 0.05f } };
        for (auto& q : pts) p.addEllipse (q[0] - q[2], q[1] - q[2], q[2] * 2.0f, q[2] * 2.0f);
        return p;
    }
    inline Path rescue()  // "rescatar este": check
    {
        Path p; p.startNewSubPath (0.15f, 0.52f); p.lineTo (0.4f, 0.76f); p.lineTo (0.86f, 0.24f); return p;
    }
    inline Path note()
    {
        Path p; p.addEllipse (0.18f, 0.62f, 0.3f, 0.24f); p.startNewSubPath (0.48f, 0.74f); p.lineTo (0.48f, 0.14f); p.lineTo (0.82f, 0.26f); p.lineTo (0.82f, 0.4f); p.lineTo (0.48f, 0.3f); return p;
    }
    inline Path drum()
    {
        Path p; p.addEllipse (0.15f, 0.3f, 0.7f, 0.2f); p.startNewSubPath (0.15f, 0.4f); p.lineTo (0.15f, 0.72f); p.quadraticTo (0.5f, 0.92f, 0.85f, 0.72f); p.lineTo (0.85f, 0.4f);
        p.startNewSubPath (0.3f, 0.1f); p.lineTo (0.5f, 0.36f); p.startNewSubPath (0.78f, 0.08f); p.lineTo (0.6f, 0.36f); return p;
    }
    inline Path play()   { Path p; p.addTriangle (0.3f, 0.2f, 0.3f, 0.8f, 0.8f, 0.5f); return p; }
    inline Path stop()   { Path p; p.addRectangle (0.28f, 0.28f, 0.44f, 0.44f); return p; }
    inline Path save()
    {
        Path p; p.startNewSubPath (0.15f, 0.15f); p.lineTo (0.7f, 0.15f); p.lineTo (0.85f, 0.3f); p.lineTo (0.85f, 0.85f); p.lineTo (0.15f, 0.85f); p.closeSubPath();
        p.addRectangle (0.3f, 0.15f, 0.36f, 0.22f); p.addRectangle (0.28f, 0.55f, 0.44f, 0.3f); return p;
    }
    inline Path folder()
    {
        Path p; p.startNewSubPath (0.1f, 0.25f); p.lineTo (0.4f, 0.25f); p.lineTo (0.48f, 0.35f); p.lineTo (0.9f, 0.35f); p.lineTo (0.9f, 0.8f); p.lineTo (0.1f, 0.8f); p.closeSubPath(); return p;
    }
    inline Path magnet()  // "auto"
    {
        Path p; p.startNewSubPath (0.2f, 0.15f); p.lineTo (0.2f, 0.55f); p.quadraticTo (0.2f, 0.85f, 0.5f, 0.85f); p.quadraticTo (0.8f, 0.85f, 0.8f, 0.55f); p.lineTo (0.8f, 0.15f);
        p.startNewSubPath (0.38f, 0.15f); p.lineTo (0.38f, 0.55f); p.quadraticTo (0.38f, 0.67f, 0.5f, 0.67f); p.quadraticTo (0.62f, 0.67f, 0.62f, 0.55f); p.lineTo (0.62f, 0.15f); return p;
    }
    inline Path expand()
    {
        Path p; p.startNewSubPath (0.1f, 0.5f); p.lineTo (0.9f, 0.5f);
        p.startNewSubPath (0.25f, 0.35f); p.lineTo (0.1f, 0.5f); p.lineTo (0.25f, 0.65f);
        p.startNewSubPath (0.75f, 0.35f); p.lineTo (0.9f, 0.5f); p.lineTo (0.75f, 0.65f); return p;
    }
    inline Path loopArrows()
    {
        Path p; p.startNewSubPath (0.2f, 0.55f); p.quadraticTo (0.2f, 0.3f, 0.45f, 0.3f); p.lineTo (0.8f, 0.3f);
        p.startNewSubPath (0.7f, 0.2f); p.lineTo (0.8f, 0.3f); p.lineTo (0.7f, 0.4f);
        p.startNewSubPath (0.8f, 0.45f); p.quadraticTo (0.8f, 0.7f, 0.55f, 0.7f); p.lineTo (0.2f, 0.7f);
        p.startNewSubPath (0.3f, 0.6f); p.lineTo (0.2f, 0.7f); p.lineTo (0.3f, 0.8f); return p;
    }
    inline Path reverseArrow()
    {
        Path p; p.startNewSubPath (0.9f, 0.5f); p.lineTo (0.15f, 0.5f); p.startNewSubPath (0.35f, 0.3f); p.lineTo (0.15f, 0.5f); p.lineTo (0.35f, 0.7f); return p;
    }
    inline Path metronome()
    {
        Path p; p.startNewSubPath (0.3f, 0.88f); p.lineTo (0.42f, 0.12f); p.lineTo (0.58f, 0.12f); p.lineTo (0.7f, 0.88f); p.closeSubPath();
        p.startNewSubPath (0.5f, 0.7f); p.lineTo (0.75f, 0.25f); return p;
    }
    inline Path piano()
    {
        Path p; p.addRectangle (0.1f, 0.2f, 0.8f, 0.6f);
        for (int i = 1; i < 4; ++i) { p.startNewSubPath (0.1f + i * 0.2f, 0.2f); p.lineTo (0.1f + i * 0.2f, 0.8f); }
        for (int i : { 0, 1, 3 }) p.addRectangle (0.24f + i * 0.2f, 0.2f, 0.1f, 0.34f);
        return p;
    }
    inline Path voicesPoly()
    {
        Path p; for (int i = 0; i < 3; ++i) p.addEllipse (0.12f + i * 0.26f, 0.38f - (i % 2) * 0.12f, 0.2f, 0.2f);
        for (int i = 0; i < 3; ++i) { p.startNewSubPath (0.3f + i * 0.26f, 0.48f - (i % 2) * 0.12f); p.lineTo (0.3f + i * 0.26f, 0.12f); } return p;
    }
    inline Path voicesMono() { Path p; p.addEllipse (0.3f, 0.6f, 0.26f, 0.24f); p.startNewSubPath (0.56f, 0.72f); p.lineTo (0.56f, 0.15f); p.lineTo (0.75f, 0.28f); return p; }
    inline Path voicesLegato()
    {
        Path p; p.addEllipse (0.08f, 0.6f, 0.2f, 0.2f); p.addEllipse (0.62f, 0.38f, 0.2f, 0.2f);
        p.startNewSubPath (0.18f, 0.55f); p.quadraticTo (0.45f, 0.15f, 0.72f, 0.32f); return p;
    }
    inline Path garden()   { return sprout(); }
    inline Path sliders()
    {
        Path p; for (int i = 0; i < 3; ++i) { const float x = 0.22f + i * 0.28f; p.startNewSubPath (x, 0.12f); p.lineTo (x, 0.88f); }
        p.addRectangle (0.14f, 0.3f, 0.16f, 0.1f); p.addRectangle (0.42f, 0.6f, 0.16f, 0.1f); p.addRectangle (0.7f, 0.22f, 0.16f, 0.1f); return p;
    }
    inline Path modulation()
    {
        Path p = sine(); p.startNewSubPath (0.08f, 0.85f); p.lineTo (0.92f, 0.85f);
        p.startNewSubPath (0.5f, 0.85f); p.lineTo (0.5f, 0.65f); return p;
    }
    inline Path engineSynth() { return saw(); }
    inline Path engineSample() { return waveform(); }

    // Dibuja un ícono dentro de un rectángulo
    inline void draw (juce::Graphics& g, const Path& icon, juce::Rectangle<float> area, juce::Colour c, float stroke = 1.8f, bool fill = false)
    {
        const float s = juce::jmin (area.getWidth(), area.getHeight());
        auto box = area.withSizeKeepingCentre (s, s);
        auto p = icon;
        p.applyTransform (juce::AffineTransform::scale (s).translated (box.getX(), box.getY()));
        g.setColour (c);
        if (fill) g.fillPath (p);
        g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
