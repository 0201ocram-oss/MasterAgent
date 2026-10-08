#include "SpectrumPanel.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    constexpr float kMinF = 20.0f, kMaxF = 20000.0f;

    float xForFreq (juce::Rectangle<float> r, float f)
    {
        return r.getX() + r.getWidth() * std::log (f / kMinF) / std::log (kMaxF / kMinF);
    }

    float freqForX (juce::Rectangle<float> r, float x)
    {
        return kMinF * std::pow (kMaxF / kMinF, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
    }

    /** Interpola una curva a terzi d'ottava alla frequenza f (in scala logaritmica). */
    float interpolateThirdOctave (const std::array<float, kNumThirdOctaves>& curve, float f)
    {
        const float pos = 17.0f + 3.0f * std::log2 (f / 1000.0f);
        const int i0 = juce::jlimit (0, kNumThirdOctaves - 2, (int) std::floor (pos));
        const float frac = juce::jlimit (0.0f, 1.0f, pos - (float) i0);
        return curve[(size_t) i0] + (curve[(size_t) i0 + 1] - curve[(size_t) i0]) * frac;
    }

    /** Valore della griglia di visualizzazione (1/12 ott.) interpolato a f. */
    float interpolateDisplay (const std::array<float, kNumDisplayPoints>& curve, float f)
    {
        const float pos = 12.0f * std::log2 (f / 20.0f);
        const int i0 = juce::jlimit (0, kNumDisplayPoints - 2, (int) std::floor (pos));
        const float frac = juce::jlimit (0.0f, 1.0f, pos - (float) i0);
        return curve[(size_t) i0] + (curve[(size_t) i0 + 1] - curve[(size_t) i0]) * frac;
    }

    juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    int bandForFrequency (float f)
    {
        for (int b = 0; b < kNumBands; ++b)
            if (f >= kBandEdges[(size_t) b] && f < kBandEdges[(size_t) b + 1])
                return b;
        return f < kBandEdges[0] ? 0 : kNumBands - 1;
    }
}

int SpectrumPanel::bandToHighlight() const
{
    if (hoveredBand >= 0)
        return hoveredBand;

    if (data != nullptr && data->highlightKey.startsWith ("band:"))
        return data->highlightKey.fromFirstOccurrenceOf (":", false, false).getIntValue();

    return -1;
}

void SpectrumPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    int band = -1;
    float freq = 0.0f;

    for (int b = 0; b < kNumBands; ++b)
        if (bandCells[(size_t) b].contains (p))
            band = b;

    if (plotArea.contains (p))
    {
        freq = freqForX (plotArea, p.x);
        band = bandForFrequency (freq);
    }

    if (band != hoveredBand || freq != cursorFrequency)
    {
        hoveredBand = band;
        cursorFrequency = freq;
        repaint();
    }
}

void SpectrumPanel::mouseDown (const juce::MouseEvent& e)
{
    if (viewToggle.contains (e.position) && onToggleMidSide)
        onToggleMidSide();
}

void SpectrumPanel::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
    cursorFrequency = 0.0f;
    repaint();
}

void SpectrumPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto bands = area.removeFromBottom (std::min (118.0f, area.getHeight() * 0.38f));
    area.removeFromBottom (8.0f);
    drawSpectrum (g, area);
    drawBandDeltas (g, bands);
}

template <typename YFor, typename BuildPath>
void SpectrumPanel::drawMidSide (juce::Graphics& g, juce::Rectangle<float> plot, YFor yFor, BuildPath buildPath)
{
    const auto& d = *data;
    const auto& s = d.master;
    addHelp (plot.withTrimmedTop (20.0f), "spectrum:ms");

    // sotto 120 Hz un Side vicino al Mid rende il low-end instabile e debole in mono
    constexpr float lowEndLimit = 120.0f, sideMargin = 6.0f;
    for (int i = 0; i < kNumDisplayPoints; ++i)
    {
        const float f = displayFrequency (i);
        if (f > lowEndLimit)
            break;
        if (s.spectrumMidDb[(size_t) i] > -100.0f && s.spectrumSideDb[(size_t) i] > s.spectrumMidDb[(size_t) i] - sideMargin)
        {
            const float x1 = xForFreq (plot, std::max (kMinF, f * std::pow (2.0f, -1.0f / 24.0f)));
            const float x2 = xForFreq (plot, f * std::pow (2.0f, 1.0f / 24.0f));
            g.setColour (colours::warning.withAlpha (0.2f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, plot.getY() + 20.0f, x2, plot.getBottom()));
        }
    }

    auto mid = [&] (int i) { return s.spectrumMidDb[(size_t) i]; };
    auto side = [&] (int i) { return s.spectrumSideDb[(size_t) i]; };

    g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.30f), 0.0f, plot.getY(), colours::accent.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
    g.fillPath (buildPath (mid, true));
    g.setColour (colours::accent);
    g.strokePath (buildPath (mid, false), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colours::accent2.withAlpha (0.16f));
    g.fillPath (buildPath (side, true));
    g.setColour (colours::accent2);
    g.strokePath (buildPath (side, false), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Side del reference, portato al livello del master (mediana dello scarto del Mid tra 63 Hz e 12.5 kHz)
    if (d.hasReference && d.reference.valid)
    {
        std::vector<float> offsets;
        for (int i = 0; i < kNumDisplayPoints; ++i)
        {
            const float f = displayFrequency (i);
            if (f >= 63.0f && f <= 12500.0f)
                offsets.push_back (s.spectrumMidDb[(size_t) i] - d.reference.spectrumMidDb[(size_t) i]);
        }
        float offset = 0.0f;
        if (! offsets.empty())
        {
            std::nth_element (offsets.begin(), offsets.begin() + (long) (offsets.size() / 2), offsets.end());
            offset = offsets[offsets.size() / 2];
        }

        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, buildPath ([&] (int i) { return d.reference.spectrumSideDb[(size_t) i] + offset; }, false),
                                                        dashes, 2);
        g.setColour (colours::reference);
        g.fillPath (dashed);
    }

    if (d.hasVersion && d.version.valid)
    {
        juce::Path dashed;
        const float dashes[] = { 2.0f, 3.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, buildPath ([&] (int i) { return d.version.spectrumSideDb[(size_t) i]; }, false),
                                                        dashes, 2);
        g.setColour (colours::text.withAlpha (0.5f));
        g.fillPath (dashed);
    }

    // legenda
    const int legendItems = 3 + (d.hasReference ? 1 : 0) + (d.hasVersion ? 1 : 0);
    // a destra del selettore L+R | M/S, voci più strette se lo spazio non basta
    const float itemW = std::min (104.0f, (plot.getWidth() - 90.0f) / (float) legendItems);
    auto legend = plot.withTrimmedLeft (90.0f).removeFromTop (14.0f).removeFromRight (itemW * (float) legendItems);
    g.setFont (fonts::label (10.5f));
    auto item = [&] (const juce::String& text, juce::Colour c)
    {
        auto it = legend.removeFromLeft (itemW);
        g.setColour (c);
        g.fillRoundedRectangle (it.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f), 1.5f);
        g.setColour (colours::textDim);
        g.drawFittedText (text, it.withTrimmedLeft (5.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    };
    item ("Mid", colours::accent);
    item ("Side", colours::accent2);
    if (d.hasReference) item ("Side reference"_t, colours::reference);
    if (d.hasVersion) item ("Side versione"_t, colours::text.withAlpha (0.5f));
    item ("bassi larghi"_t, colours::warning.withAlpha (0.6f));

    // cursore: livelli di Mid e Side e la loro differenza
    if (cursorFrequency > 0.0f)
    {
        const float x = xForFreq (plotArea, cursorFrequency);
        const float m = interpolateDisplay (s.spectrumMidDb, cursorFrequency);
        const float sd = interpolateDisplay (s.spectrumSideDb, cursorFrequency);
        g.setColour (colours::text.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
        g.setColour (colours::accent);
        g.fillEllipse (x - 3.5f, yFor (m) - 3.5f, 7.0f, 7.0f);
        g.setColour (colours::accent2);
        g.fillEllipse (x - 3.5f, yFor (sd) - 3.5f, 7.0f, 7.0f);

        juce::String text = hzText (cursorFrequency) + "   Mid " + juce::String (m, 1) + " dB   Side " + juce::String (sd, 1) + " dB   (S-M "
                          + juce::String (sd - m, 1) + " dB)";
        g.setFont (fonts::value (11.0f));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
        auto box = juce::Rectangle<float> (x + 8.0f, plotArea.getY() + 22.0f, w, 20.0f);
        if (box.getRight() > plotArea.getRight())
            box.setX (x - 8.0f - w);
        g.setColour (colours::readout);
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (colours::text);
        g.drawText (text, box, juce::Justification::centred);
    }
}

void SpectrumPanel::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = *data;
    const auto& s = d.master;

    fillWell (g, r);

    auto plot = r.reduced (6.0f).withTrimmedBottom (12.0f).withTrimmedLeft (24.0f);
    plotArea = plot;
    addHelp (r, "spectrum");

    // range verticale: segue il massimo del long-term
    float top = -20.0f;
    for (auto v : s.spectrumLongTermDb) top = std::max (top, v);
    top = std::ceil ((top + 3.0f) / 6.0f) * 6.0f;
    const float range = 66.0f, bottom = top - range;
    auto yFor = [&] (float db) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db - bottom) / range) * plot.getHeight(); };

    // zona della banda evidenziata (hover sulla banda, sullo spettro o su una diagnosi)
    const int hb = bandToHighlight();
    const bool fromReport = hoveredBand < 0 && hb >= 0;
    if (hb >= 0)
    {
        const auto col = fromReport ? d.highlightColour : colours::accent;
        const float x1 = xForFreq (plot, std::max (kMinF, kBandEdges[(size_t) hb]));
        const float x2 = xForFreq (plot, std::min (kMaxF, kBandEdges[(size_t) hb + 1]));
        auto zone = juce::Rectangle<float>::leftTopRightBottom (x1, plot.getY(), x2, plot.getBottom());
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.20f), 0.0f, zone.getY(), col.withAlpha (0.04f), 0.0f, zone.getBottom(), false));
        g.fillRect (zone);
        g.setColour (col.withAlpha (0.7f));
        g.drawVerticalLine ((int) x1, zone.getY(), zone.getBottom());
        g.drawVerticalLine ((int) x2, zone.getY(), zone.getBottom());
        g.setFont (fonts::value (11.0f));
        g.drawText (juce::String (kBandNames[(size_t) hb]).toUpperCase(), zone.removeFromBottom (18.0f).reduced (2.0f, 0.0f),
                    juce::Justification::centred);
    }

    // zona sub-sonica evidenziata
    if (isHighlighted (highlight::rumble))
    {
        markHighlight (g, juce::Rectangle<float>::leftTopRightBottom (plot.getX(), plot.getY() + 4.0f, xForFreq (plot, 30.0f), plot.getBottom() - 4.0f),
                       highlight::rumble);
    }

    // griglia
    g.setFont (fonts::label (9.0f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (plot, f);
        g.setColour (colours::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (colours::textFaint);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 15.0f, plot.getBottom() + 1.0f, 30.0f, 10.0f), juce::Justification::centred);
    }
    for (float db = top; db >= bottom; db -= 12.0f)
    {
        g.setColour (colours::grid);
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() + 4.0f, yFor (db) - 5.0f, 24.0f, 10.0f), juce::Justification::left);
    }

    // selettore della vista: L+R (spettro del master) o Mid/Side
    const bool midSide = getUiSettings().spectrumMidSide;
    {
        viewToggle = juce::Rectangle<float> (plot.getX() + 6.0f, plot.getY() + 2.0f, 76.0f, 16.0f);
        addHelp (viewToggle, "spectrum:view");
        auto left = viewToggle.withWidth (viewToggle.getWidth() * 0.5f), right = left.withX (left.getRight());
        g.setColour (colours::panel.withAlpha (0.9f));
        g.fillRoundedRectangle (viewToggle, 8.0f);
        g.setColour (colours::accent.withAlpha (0.28f));
        g.fillRoundedRectangle (midSide ? right : left, 8.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (viewToggle, 8.0f, 1.0f);
        g.setFont (fonts::value (10.0f));
        g.setColour (midSide ? colours::textDim : colours::text);
        g.drawText ("L+R", left, juce::Justification::centred);
        g.setColour (midSide ? colours::text : colours::textDim);
        g.drawText ("M/S", right, juce::Justification::centred);
    }

    if (! s.valid)
        return;

    auto buildPath = [&] (auto getDb, bool closed)
    {
        juce::Path p;
        for (int i = 0; i < kNumDisplayPoints; ++i)
        {
            const float f = displayFrequency (i);
            if (f > kMaxF) break;
            const float x = xForFreq (plot, f), y = yFor (getDb (i));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (xForFreq (plot, displayFrequency (kNumDisplayPoints - 1)), plot.getBottom());
            p.lineTo (plot.getX(), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };

    if (midSide)
    {
        drawMidSide (g, plot, yFor, buildPath);
        return;
    }

    // long-term (area con gradiente)
    g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.32f), 0.0f, plot.getY(), colours::accent2.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
    g.fillPath (buildPath ([&] (int i) { return s.spectrumLongTermDb[(size_t) i]; }, true));
    g.setColour (colours::accent);
    g.strokePath (buildPath ([&] (int i) { return s.spectrumLongTermDb[(size_t) i]; }, false),
                  juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // istantaneo
    g.setColour (colours::text.withAlpha (0.3f));
    g.strokePath (buildPath ([&] (int i) { return s.spectrumInstantDb[(size_t) i]; }, false), juce::PathStrokeType (1.0f));

    // curve target/reference allineate al master nel range 63 Hz - 12.5 kHz
    // mediana degli scostamenti: una zona molto diversa (es. sibilanti) non sposta tutta la curva
    auto alignOffset = [&] (const std::array<float, kNumThirdOctaves>& curve)
    {
        std::vector<float> offsets;
        for (int i = 0; i < kNumDisplayPoints; ++i)
        {
            const float f = displayFrequency (i);
            if (f < 63.0f || f > 12500.0f) continue;
            offsets.push_back (s.spectrumLongTermDb[(size_t) i] - interpolateThirdOctave (curve, f));
        }
        if (offsets.empty())
            return 0.0f;
        std::nth_element (offsets.begin(), offsets.begin() + (long) (offsets.size() / 2), offsets.end());
        return offsets[offsets.size() / 2];
    };

    auto overlay = [&] (const std::array<float, kNumThirdOctaves>& curve, juce::Colour colour, bool dashed)
    {
        const float offset = alignOffset (curve);
        const auto path = buildPath ([&] (int i) { return interpolateThirdOctave (curve, displayFrequency (i)) + offset; }, false);
        g.setColour (colour);
        if (dashed)
        {
            juce::Path dashedPath;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashedPath, path, dashes, 2);
            g.fillPath (dashedPath);
        }
        else
        {
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
        }
    };

    // curva target effettivamente usata nel confronto (brano intero o sezione più forte),
    // salvo quando il target è il reference caricato, già disegnato in viola
    const bool targetIsLoadedReference = d.hasReference && d.profile != nullptr && d.profile->id.startsWith ("ref_");
    const bool showTarget = d.comparison.hasTonalTarget && ! targetIsLoadedReference;
    if (showTarget)
        overlay (d.comparison.tonalTarget, colours::target.withAlpha (0.85f), true);
    if (d.hasReference)
        overlay (d.comparison.tonalUsesLoudest && d.reference.loudestSectionSeconds > 0.0f ? d.reference.thirdOctaveLoudestDb
                                                                                          : d.reference.thirdOctaveDb,
                 colours::reference, false);

    // versione salvata con cui si confronta il master (stessa scala, nessun allineamento: si vedono anche i livelli)
    if (d.hasVersion && d.version.valid)
    {
        juce::Path dashed;
        const float dashes[] = { 2.0f, 3.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, buildPath ([&] (int i) { return d.version.spectrumLongTermDb[(size_t) i]; }, false),
                                                        dashes, 2);
        g.setColour (colours::text.withAlpha (0.55f));
        g.fillPath (dashed);
    }

    // curva EQ suggerita, con scala propria (±6 dB attorno alla metà del grafico)
    const auto& moves = d.comparison.eqMoves;
    if (! moves.empty())
    {
        const float midY = plot.getCentreY();
        const float pxPerDb = plot.getHeight() * 0.25f / 6.0f;
        auto shownResponse = [&] (float f)
        {
            float total = 0.0f;
            for (auto m : moves)
            {
                m.gainDb = m.shownGainDb;
                total += eqMoveResponseDb (m, f);
            }
            return total;
        };

        g.setColour (colours::accent2.withAlpha (0.25f));
        g.drawHorizontalLine ((int) midY, plot.getX(), plot.getRight());

        juce::Path eq;
        for (int i = 0; i < kNumDisplayPoints; ++i)
        {
            const float f = displayFrequency (i);
            if (f > kMaxF) break;
            const float y = midY - juce::jlimit (-6.0f, 6.0f, shownResponse (f)) * pxPerDb;
            if (i == 0) eq.startNewSubPath (xForFreq (plot, f), y); else eq.lineTo (xForFreq (plot, f), y);
        }
        juce::Path dashedEq;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashedEq, eq, dashes, 2);
        g.setColour (colours::accent2);
        g.fillPath (dashedEq);

        for (int i = 0; i < (int) moves.size(); ++i)
        {
            const auto& m = moves[(size_t) i];
            const float x = xForFreq (plot, juce::jlimit (kMinF, kMaxF, m.frequency));
            const float y = midY - juce::jlimit (-6.0f, 6.0f, shownResponse (m.frequency)) * pxPerDb;
            const auto node = juce::Rectangle<float> (x - 8.0f, y - 8.0f, 16.0f, 16.0f);
            markHighlight (g, node.expanded (4.0f), highlight::eqMove (i));
            g.setColour (colours::well);
            g.fillEllipse (node);
            g.setColour (colours::accent2);
            g.drawEllipse (node, 1.5f);
            g.setFont (fonts::value (10.0f));
            g.drawText (juce::String (i + 1), node, juce::Justification::centred);
        }
    }

    // legenda
    const int legendItems = 1 + (showTarget ? 1 : 0) + (d.hasReference ? 1 : 0) + (moves.empty() ? 0 : 1) + (d.hasVersion ? 1 : 0);
    const float itemW = std::min (100.0f, (plot.getWidth() - 90.0f) / (float) legendItems);
    auto legend = plot.withTrimmedLeft (90.0f).removeFromTop (14.0f).removeFromRight (itemW * (float) legendItems);
    g.setFont (fonts::label (10.5f));
    auto item = [&] (const juce::String& text, juce::Colour c)
    {
        auto it = legend.removeFromLeft (itemW);
        g.setColour (c);
        g.fillRoundedRectangle (it.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f), 1.5f);
        g.setColour (colours::textDim);
        g.drawFittedText (text, it.withTrimmedLeft (5.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    };
    item ("master (media)", colours::accent);
    if (showTarget) item (d.comparison.tonalUsesLoudest ? "target (forte)" : "target", colours::target);
    if (d.hasReference) item ("reference", colours::reference);
    if (! moves.empty()) item ("EQ suggerito", colours::accent2);
    if (d.hasVersion) item ("versione"_t, colours::text.withAlpha (0.55f));

    // picchi stretti: note del brano (grigio, con il nome) o risonanze (arancio)
    for (const auto& res : s.resonances)
    {
        if (res.excessDb <= 6.0f) continue;
        const float x = xForFreq (plotArea, res.frequency);
        const auto key = highlight::resonance (res.frequency);
        if (isHighlighted (key))
            markHighlight (g, juce::Rectangle<float> (x - 6.0f, plotArea.getY() + 18.0f, 12.0f, plotArea.getHeight() - 22.0f), key);
        addHelp (juce::Rectangle<float> (x - 8.0f, plotArea.getY() + 14.0f, 16.0f, 24.0f), key);
        g.setColour (res.musical ? colours::textDim : colours::warning);
        g.fillEllipse (x - 3.0f, plotArea.getY() + 18.0f, 6.0f, 6.0f);
        if (res.musical && res.midiNote >= 0)
        {
            g.setFont (fonts::label (9.5f));
            g.drawText (juce::String (kNoteNamesEn[(size_t) (res.midiNote % 12)]) + juce::String (res.midiNote / 12 - 1),
                        juce::Rectangle<float> (x - 15.0f, plotArea.getY() + 25.0f, 30.0f, 11.0f), juce::Justification::centred);
        }
    }

    // cursore con lettura di frequenza e livello
    if (cursorFrequency > 0.0f)
    {
        const float x = xForFreq (plotArea, cursorFrequency);
        const float lt = interpolateDisplay (s.spectrumLongTermDb, cursorFrequency);
        g.setColour (colours::text.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
        g.setColour (colours::accent);
        g.fillEllipse (x - 3.5f, yFor (lt) - 3.5f, 7.0f, 7.0f);

        juce::String text = hzText (cursorFrequency) + "   " + juce::String (lt, 1) + " dB";
        if (d.comparison.hasTonalTarget)
        {
            const float delta = interpolateThirdOctave (d.comparison.tonalDelta, cursorFrequency);
            text << "   (" << (delta >= 0 ? "+" : "") << juce::String (delta, 1) << " vs target)";
        }

        g.setFont (fonts::value (11.0f));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
        auto box = juce::Rectangle<float> (x + 8.0f, plotArea.getY() + 22.0f, w, 20.0f);
        if (box.getRight() > plotArea.getRight())
            box.setX (x - 8.0f - w);
        g.setColour (colours::readout);
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (colours::text);
        g.drawText (text, box, juce::Justification::centred);
    }
}

void SpectrumPanel::drawBandDeltas (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = *data;
    const auto& c = d.comparison;

    auto info = r.removeFromTop (18.0f);
    g.setFont (fonts::label (11.5f));

    auto infoItem = [&] (const juce::String& text, const juce::String& key, juce::Colour colour)
    {
        g.setFont (fonts::label (11.5f));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 18.0f;
        auto rr = info.removeFromLeft (w);
        markHighlight (g, rr.reduced (2.0f, 1.0f).withTrimmedRight (10.0f), key);
        g.setColour (colour);
        g.drawText (text, rr, juce::Justification::centredLeft);
    };

    infoItem ("Tilt " + juce::String (d.master.spectralTiltDbPerOct, 2) + " dB/oct", highlight::tilt, d.colourFor (highlight::tilt, colours::textDim));
    infoItem ("Centroide " + juce::String (juce::roundToInt (d.master.spectralCentroidHz)) + " Hz", "centroid", colours::textDim);
    infoItem ("<30 Hz " + formatDb (d.master.subRumbleDb) + " dB", highlight::rumble, d.colourFor (highlight::rumble, colours::textDim));
    if (d.hasReference)
        infoItem ("ref tilt " + juce::String (d.reference.spectralTiltDbPerOct, 2), {}, colours::reference);

    g.setColour (colours::textFaint);
    g.setFont (fonts::label (11.0f));
    g.drawText (! c.hasTonalTarget ? juce::String ("nessuna curva target")
                                   : (c.tonalUsesLoudest ? "scostamento dalla sezione più forte del target (dB)"_u : juce::String ("scostamento dal target (dB)")),
                info, juce::Justification::centredRight);

    r.removeFromTop (4.0f);
    const float bw = r.getWidth() / kNumBands;
    constexpr float maxDelta = 6.0f;
    const int hb = bandToHighlight();

    for (int b = 0; b < kNumBands; ++b)
    {
        auto full = juce::Rectangle<float> (r.getX() + b * bw, r.getY(), bw, r.getHeight()).reduced (3.0f, 0.0f);
        bandCells[(size_t) b] = full;
        auto cell = full;
        auto label = cell.removeFromBottom (15.0f);
        auto valueLabel = cell.removeFromTop (15.0f);

        const bool hovered = b == hb;
        fillWell (g, cell, 5.0f);
        if (hovered)
        {
            const auto col = hoveredBand >= 0 ? colours::accent : d.highlightColour;
            g.setColour (col.withAlpha (0.12f));
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (col.withAlpha (0.8f));
            g.drawRoundedRectangle (cell, 5.0f, 1.2f);
        }
        markHighlight (g, full.reduced (2.0f), highlight::band (b));

        const float tol = c.hasTonalTarget ? c.bandTolerance[(size_t) b] : 2.0f;
        const float midY = cell.getCentreY();
        const float tolH = cell.getHeight() * 0.5f * tol / maxDelta;
        g.setColour (colours::ok.withAlpha (0.07f));
        g.fillRect (cell.reduced (1.0f, 0.0f).withY (midY - tolH).withHeight (tolH * 2.0f));
        g.setColour (colours::axis);
        g.drawHorizontalLine ((int) midY, cell.getX() + 4.0f, cell.getRight() - 4.0f);

        if (c.hasTonalTarget)
        {
            const float delta = c.bandTonalDelta[(size_t) b];
            const float h = cell.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, delta / maxDelta);
            const auto col = d.colourFor (highlight::band (b));
            auto bar = juce::Rectangle<float>::leftTopRightBottom (cell.getX() + cell.getWidth() * 0.28f, std::min (midY, midY - h),
                                                                 cell.getRight() - cell.getWidth() * 0.28f, std::max (midY, midY - h));
            g.setColour (col);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setFont (fonts::value (11.5f));
            g.drawText ((delta >= 0 ? "+" : "") + juce::String (delta, 1), valueLabel, juce::Justification::centred);
        }

        g.setColour (hovered ? colours::text : colours::textDim);
        g.setFont (hovered ? fonts::value (11.0f) : fonts::label (11.0f));
        g.drawText (kBandNames[(size_t) b], label, juce::Justification::centred);
    }
}

} // namespace ma::ui
