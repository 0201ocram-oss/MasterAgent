#include "Vintage.h"

namespace ma::ui::vintage
{

namespace
{
    bool hasBahnschrift()
    {
        static const bool found = juce::Font::findAllTypefaceNames().contains ("Bahnschrift", true);
        return found;
    }

    // segmenti: bit 0..6 = a b c d e f g (a in alto, poi in senso orario, g al centro); bit 7 = barra verticale del '+'
    int segmentsFor (juce::juce_wchar c)
    {
        switch (c)
        {
            case '0': return 0x3f;
            case '1': return 0x06;
            case '2': return 0x5b;
            case '3': return 0x4f;
            case '4': return 0x66;
            case '5': return 0x6d;
            case '6': return 0x7d;
            case '7': return 0x07;
            case '8': return 0x7f;
            case '9': return 0x6f;
            case '-': return 0x40;
            case '+': return 0xc0;
            case 'L': return 0x38;
            default:  return 0;
        }
    }

    struct Cell
    {
        juce::juce_wchar ch = ' ';
        bool point = false;
    };

    constexpr float kCellWidth = 0.5f;    // larghezza di una cifra rispetto all'altezza
    constexpr float kCellGap = 0.17f;     // spazio tra le cifre (ospita il punto decimale)
    constexpr float kSkew = 0.09f;        // inclinazione delle cifre

    /** Segmenti di una cifra in (x, y, w, h); i segmenti accesi in lit, tutti in ghost. */
    void addCell (juce::Path& ghost, juce::Path& lit, float x, float y, float w, float h, const Cell& cell)
    {
        const float t = std::max (1.2f, w * 0.2f);
        const float half = t * 0.5f, gap = t * 0.16f;
        const float xl = half, xr = w - half, yt = half, ym = h * 0.5f, yb = h - half;

        const auto point = [&] (float px, float py) { return juce::Point<float> (x + px + (h - py) * kSkew, y + py); };
        const auto horizontal = [&] (float xa, float xb, float yc)
        {
            juce::Path p;
            p.startNewSubPath (point (xa + gap, yc));
            p.lineTo (point (xa + gap + half, yc - half));
            p.lineTo (point (xb - gap - half, yc - half));
            p.lineTo (point (xb - gap, yc));
            p.lineTo (point (xb - gap - half, yc + half));
            p.lineTo (point (xa + gap + half, yc + half));
            p.closeSubPath();
            return p;
        };
        const auto vertical = [&] (float xc, float ya, float yb2)
        {
            juce::Path p;
            p.startNewSubPath (point (xc, ya + gap));
            p.lineTo (point (xc + half, ya + gap + half));
            p.lineTo (point (xc + half, yb2 - gap - half));
            p.lineTo (point (xc, yb2 - gap));
            p.lineTo (point (xc - half, yb2 - gap - half));
            p.lineTo (point (xc - half, ya + gap + half));
            p.closeSubPath();
            return p;
        };

        const juce::Path segments[] = {
            horizontal (xl, xr, yt),    // a
            vertical (xr, yt, ym),      // b
            vertical (xr, ym, yb),      // c
            horizontal (xl, xr, yb),    // d
            vertical (xl, ym, yb),      // e
            vertical (xl, yt, ym),      // f
            horizontal (xl, xr, ym),    // g
        };

        const int on = segmentsFor (cell.ch);
        for (int i = 0; i < 7; ++i)
        {
            ghost.addPath (segments[i]);
            if ((on >> i) & 1)
                lit.addPath (segments[i]);
        }

        if (on & 0x80)
        {
            lit.addPath (vertical (w * 0.5f, ym - h * 0.24f, ym));
            lit.addPath (vertical (w * 0.5f, ym, ym + h * 0.24f));
        }

        // punto decimale, nello spazio dopo la cifra
        const float dp = t * 1.1f;
        juce::Path dot;
        const auto centre = point (w + h * kCellGap * 0.5f, yb);
        dot.addRectangle (centre.x - dp * 0.5f, centre.y - dp * 0.5f, dp, dp);
        ghost.addPath (dot);
        if (cell.point)
            lit.addPath (dot);
    }
}

juce::Font font (float size, bool bold)
{
    const int style = bold ? juce::Font::bold : juce::Font::plain;
    if (hasBahnschrift())
        return juce::Font (juce::FontOptions ("Bahnschrift", size, style));
    return juce::Font (juce::FontOptions (size, style));
}

juce::Font silkscreen (float size)
{
    return font (size, true).withExtraKerningFactor (0.1f);
}

float sevenSegmentWidth (float height, int numCells)
{
    return (float) numCells * height * kCellWidth + (float) std::max (0, numCells - 1) * height * kCellGap + height * kSkew;
}

juce::Rectangle<float> drawSevenSegment (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                                         juce::Colour colour, int numCells)
{
    std::vector<Cell> cells;
    for (auto c : text)
    {
        if (c == '.' || c == ',')
        {
            if (! cells.empty() && ! cells.back().point)
                cells.back().point = true;
            else
                cells.push_back ({ ' ', true });
        }
        else
        {
            cells.push_back ({ c, false });
        }
    }

    // allineato a destra: le cifre mancanti restano spente
    numCells = std::max (numCells, (int) cells.size());
    while ((int) cells.size() < numCells)
        cells.insert (cells.begin(), Cell());

    float h = area.getHeight();
    if (const float w = sevenSegmentWidth (h, numCells); w > area.getWidth())
        h *= area.getWidth() / w;

    const float cellW = h * kCellWidth, step = cellW + h * kCellGap;
    const float total = sevenSegmentWidth (h, numCells);
    const float x0 = area.getRight() - total, y0 = area.getCentreY() - h * 0.5f;

    juce::Path ghost, lit;
    for (int i = 0; i < numCells; ++i)
        addCell (ghost, lit, x0 + (float) i * step, y0, cellW, h, cells[(size_t) i]);

    g.setColour (colour.withAlpha (0.07f));
    g.fillPath (ghost);
    g.setColour (colour.withAlpha (0.22f));
    g.strokePath (lit, juce::PathStrokeType (std::max (1.0f, h * 0.06f)));
    g.setColour (colour);
    g.fillPath (lit);

    return { x0, y0, total, h };
}

void drawDisplayWindow (juce::Graphics& g, juce::Rectangle<float> r)
{
    constexpr float radius = 2.5f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff060504), 0.0f, r.getY(), juce::Colour (0xff15110d), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);

    // incasso: ombra in alto, luce sul bordo inferiore del frontalino
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + radius, r.getRight() - radius);

    // vetro
    auto glass = r.reduced (1.5f).removeFromTop (r.getHeight() * 0.45f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0.0f, glass.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, glass.getBottom(), false));
    g.fillRoundedRectangle (glass, radius);
}

juce::Colour lampColour (juce::Colour c)
{
    return juce::Colour::fromHSV (c.getHue(), juce::jlimit (0.45f, 0.95f, c.getSaturation() + 0.15f), 1.0f, 1.0f);
}

void drawLamp (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, bool lit)
{
    const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    if (lit)
    {
        const float glow = radius * 2.8f;
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.42f), centre.x, centre.y,
                                                 colour.withAlpha (0.0f), centre.x + glow, centre.y, true));
        g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (centre));

        juce::ColourGradient fill (colour.brighter (0.8f), centre.x - radius * 0.3f, centre.y - radius * 0.3f,
                                   colour.darker (0.15f), centre.x + radius, centre.y + radius, true);
        g.setGradientFill (fill);
    }
    else
    {
        const auto off = colour.withMultipliedBrightness (0.3f).withMultipliedSaturation (0.75f);
        g.setGradientFill (juce::ColourGradient (off.brighter (0.25f), centre.x - radius * 0.3f, centre.y - radius * 0.3f,
                                                 off.darker (0.4f), centre.x + radius, centre.y + radius, true));
    }
    g.fillEllipse (body);

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (body, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.55f : 0.16f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.55f).withCentre ({ centre.x - radius * 0.3f, centre.y - radius * 0.38f }));
}

void drawKey (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour base, bool pressed, bool flatLeft, bool flatRight)
{
    constexpr float radius = 3.0f;
    juce::Path p;
    p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius, radius,
                           ! flatLeft, ! flatRight, ! flatLeft, ! flatRight);

    g.setGradientFill (juce::ColourGradient (pressed ? base.darker (0.45f) : base.brighter (0.22f), 0.0f, bounds.getY(),
                                             pressed ? base.darker (0.1f) : base.darker (0.32f), 0.0f, bounds.getBottom(), false));
    g.fillPath (p);
    if (! pressed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawHorizontalLine ((int) bounds.getY() + 1, bounds.getX() + radius, bounds.getRight() - radius);
    }
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffaaa296), centre.x - radius * 0.4f, centre.y - radius * 0.5f,
                                             juce::Colour (0xff47423b), centre.x + radius, centre.y + radius, true));
    g.fillEllipse (head);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (head, 0.8f);

    const auto dir = juce::Point<float> (std::cos (angle), std::sin (angle)) * (radius * 0.78f);
    g.setColour (juce::Colour (0xff24201b));
    g.drawLine ({ centre - dir, centre + dir }, std::max (1.0f, radius * 0.38f));
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine ({ centre - dir + juce::Point<float> (0.0f, 0.8f), centre + dir + juce::Point<float> (0.0f, 0.8f) }, 0.6f);
}

} // namespace ma::ui::vintage
