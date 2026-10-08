#include "LoudnessPanel.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    void drawSmall (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value,
                    juce::Colour colour = colours::text, const juce::String& sub = {})
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::label (11.0f));
        g.drawFittedText (label, r.removeFromTop (14.0f).toNearestInt().withTrimmedRight (6), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (colour);
        g.setFont (fonts::value (17.0f));
        g.drawFittedText (value, r.removeFromTop (22.0f).toNearestInt().withTrimmedRight (6), juce::Justification::centredLeft, 1, 0.8f);
        if (sub.isNotEmpty())
        {
            g.setColour (colours::reference);
            g.setFont (fonts::label (10.5f));
            g.drawText (sub, r.removeFromTop (13.0f), juce::Justification::centredLeft);
        }
    }

    juce::String refText (const DashboardData& d, float refValue, int decimals = 1)
    {
        return d.hasReference ? "ref " + formatDb (refValue, decimals) : juce::String();
    }

    juce::String timeText (double seconds)
    {
        const int s = (int) seconds;
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
void LoudnessPanel::setData (const DashboardData& d)
{
    // il badge va aggiornato prima del paint (viene disegnato prima del contenuto)
    const bool live = d.master.liveMode;
    if (d.masterFromFile)
    {
        badge = "FILE";
        badgeColour = colours::accent2;
    }
    else
    {
        badge = live ? "LIVE " + juce::String (juce::roundToInt (d.master.liveWindowSeconds)) + " s" : "BRANO INTERO"_t;
        badgeColour = live ? colours::warning : colours::accent;
    }
    Panel::setData (d);
}

void LoudnessPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& s = d.master;
    const bool live = s.liveMode;

    auto top = area.removeFromTop (area.getHeight() * 0.52f);

    // Integrated
    auto left = top.removeFromLeft (top.getWidth() * 0.36f);
    auto integratedArea = left.removeFromTop (62.0f);
    markHighlight (g, integratedArea.withWidth (std::min (integratedArea.getWidth(), 150.0f)), highlight::integrated);
    drawValue (g, integratedArea, live ? "INTEGRATED (finestra)" : "INTEGRATED", formatDb (s.integratedLufs), "LUFS",
               d.colourFor (highlight::integrated), 36.0f);

    g.setFont (fonts::label (11.0f));
    if (d.phase == WorkPhase::mix)
    {
        g.setColour (colours::textDim);
        g.drawText ("mix: la decide il mastering", left.removeFromTop (15.0f), juce::Justification::centredLeft);
    }
    else if (d.profile != nullptr)
    {
        if (const auto* r = d.profile->getMetric (metric::integratedLufs))
        {
            g.setColour (colours::target);
            g.drawText ("target " + juce::String (r->min, 1) + " .. " + juce::String (r->max, 1) + " LUFS",
                        left.removeFromTop (15.0f), juce::Justification::centredLeft);
        }
    }
    if (d.hasReference)
    {
        g.setColour (colours::reference);
        g.drawText ("reference " + formatDb (d.reference.integratedLufs) + " LUFS", left.removeFromTop (15.0f), juce::Justification::centredLeft);
    }

    // griglia valori secondari
    const float cw = top.getWidth() / 3.0f, rh = top.getHeight() / 2.0f;
    auto cell = [&] (int c, int r) { return juce::Rectangle<float> (top.getX() + c * cw, top.getY() + r * rh, cw, rh); };

    addHelp (cell (0, 0).withHeight (38.0f), "shortTerm");
    drawSmall (g, cell (0, 0), "Short-term (LUFS)", formatDb (s.shortTermLufs));
    addHelp (cell (1, 0).withHeight (38.0f), "momentary");
    drawSmall (g, cell (1, 0), "Momentary (LUFS)", formatDb (s.momentaryLufs));

    auto lraCell = cell (2, 0);
    markHighlight (g, lraCell.withHeight (38.0f).withTrimmedRight (6.0f), highlight::lra);
    drawSmall (g, lraCell, "LRA", live ? juce::String ("n/d") : juce::String (s.loudnessRange, 1) + " LU",
               live ? colours::textFaint : d.colourFor (highlight::lra), refText (d, d.reference.loudnessRange));

    addHelp (cell (0, 1).withHeight (38.0f), "maxShortTerm");
    addHelp (cell (1, 1).withHeight (38.0f), "maxMomentary");
    drawSmall (g, cell (0, 1), "Max short-term", formatDb (s.maxShortTermLufs), colours::text, refText (d, d.reference.maxShortTermLufs));
    drawSmall (g, cell (1, 1), "Max momentary", formatDb (s.maxMomentaryLufs), colours::text, refText (d, d.reference.maxMomentaryLufs));

    auto timeCell = cell (2, 1);
    markHighlight (g, timeCell.withHeight (38.0f).withTrimmedRight (6.0f), highlight::time);
    drawSmall (g, timeCell, live ? "Finestra" : "Tempo analizzato", timeText (s.secondsAnalyzed));

    drawHistory (g, area.withTrimmedTop (8.0f));
}

void LoudnessPanel::drawHistory (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = *data;
    const auto& hist = d.master.shortTermHistory;

    fillWell (g, r);
    addHelp (r, "history");
    r = r.reduced (2.0f);

    constexpr float minL = -36.0f, maxL = 0.0f;
    auto yFor = [&] (float l) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, (l - minL) / (maxL - minL)) * r.getHeight(); };

    g.setFont (fonts::label (9.5f));
    for (float l = -30.0f; l <= -6.0f; l += 6.0f)
    {
        g.setColour (colours::grid);
        g.drawHorizontalLine ((int) yFor (l), r.getX(), r.getRight());
        g.setColour (colours::textFaint);
        g.drawText (juce::String ((int) l), juce::Rectangle<float> (r.getX() + 4.0f, yFor (l) - 11.0f, 30.0f, 10.0f), juce::Justification::left);
    }

    // fascia target del profilo (solo sul master: nel mix la loudness non è un obiettivo)
    if (d.profile != nullptr && d.phase == WorkPhase::master)
        if (const auto* range = d.profile->getMetric (metric::integratedLufs))
        {
            g.setColour (colours::target.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), yFor (range->max), r.getRight(), yFor (range->min)));
        }

    if (d.hasReference && d.reference.integratedLufs > -70.0f)
    {
        g.setColour (colours::reference.withAlpha (0.7f));
        g.drawHorizontalLine ((int) yFor (d.reference.integratedLufs), r.getX(), r.getRight());
    }

    if (hist.size() < 2)
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::label (11.0f));
        g.drawText ("Short-term loudness nel tempo", r, juce::Justification::centred);
        return;
    }

    const size_t n = hist.size();

    // versione salvata: stessa scala dei tempi (dall'inizio dell'analisi), tratto tenue
    const auto& versionHist = d.version.shortTermHistory;
    const bool showVersion = d.hasVersion && versionHist.size() > 1;
    const size_t span = showVersion ? std::max (n, versionHist.size()) : n;
    if (showVersion)
    {
        juce::Path v;
        for (size_t i = 0; i < versionHist.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (span - 1);
            const float y = yFor (std::max (minL, versionHist[i]));
            if (i == 0) v.startNewSubPath (x, y); else v.lineTo (x, y);
        }
        g.setColour (colours::text.withAlpha (0.35f));
        g.strokePath (v, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
    }

    // tratto del brano collegato alla diagnosi evidenziata (la cronologia ha un valore ogni 100 ms)
    if (d.highlightTimeStart >= 0.0f && d.highlightTimeEnd > d.highlightTimeStart && d.master.historyAligned)
    {
        auto xForTime = [&] (double t)
        {
            const double index = (t - d.master.historyStartSongSec) / 0.1;
            return r.getX() + r.getWidth() * (float) juce::jlimit (0.0, 1.0, index / (double) (span - 1));
        };
        const float x1 = xForTime (d.highlightTimeStart), x2 = xForTime (d.highlightTimeEnd);
        if (x2 - x1 > 1.0f)
        {
            const auto zone = juce::Rectangle<float>::leftTopRightBottom (x1, r.getY(), x2, r.getBottom());
            g.setColour (d.highlightColour.withAlpha (0.16f));
            g.fillRect (zone);
            g.setColour (d.highlightColour.withAlpha (0.8f));
            g.drawVerticalLine ((int) x1, zone.getY(), zone.getBottom());
            g.drawVerticalLine ((int) x2, zone.getY(), zone.getBottom());
            g.setFont (fonts::value (10.5f));
            g.drawText (formatSongTime (d.highlightTimeStart) + " - " + formatSongTime (d.highlightTimeEnd),
                        zone.withHeight (14.0f).expanded (30.0f, 0.0f), juce::Justification::centred);
        }
    }

    juce::Path line, fill;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) (span - 1);
        const float y = yFor (std::max (minL, hist[i]));
        if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, r.getBottom()); fill.lineTo (x, y); }
        else        { line.lineTo (x, y); fill.lineTo (x, y); }
    }
    fill.lineTo (r.getX() + r.getWidth() * (float) (n - 1) / (float) (span - 1), r.getBottom());
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.25f), 0.0f, r.getY(), colours::accent.withAlpha (0.0f), 0.0f, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colours::accent);
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

    if (d.master.integratedLufs > -70.0f)
    {
        g.setColour (colours::text.withAlpha (0.5f));
        const float y = yFor (d.master.integratedLufs);
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ r.getX(), y, r.getRight(), y }, dashes, 2, 1.0f);
    }
}

//==============================================================================
void PeakPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& s = d.master;

    // meter verticali true peak L/R
    auto meters = area.removeFromLeft (66.0f);
    constexpr float minDb = -24.0f, maxDb = 3.0f;
    auto yFor = [&] (juce::Rectangle<float> r, float db) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)) * r.getHeight(); };

    addHelp (meters, "tpMeters");
    auto meterArea = meters.withTrimmedBottom (14.0f);
    const auto scaleArea = meterArea;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto m = meterArea.removeFromLeft (22.0f).reduced (3.0f, 0.0f);
        fillWell (g, m, 3.0f);

        const float tp = s.truePeakDb[(size_t) ch];
        auto bar = m.reduced (2.0f).withTop (yFor (m.reduced (2.0f), tp));
        juce::ColourGradient grad (colours::critical, 0.0f, yFor (m, 1.0f), colours::ok, 0.0f, yFor (m, -18.0f), false);
        grad.addColour (0.25, colours::warning);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, 2.0f);

        g.setColour (colours::textDim);
        g.setFont (fonts::label (10.0f));
        g.drawText (ch == 0 ? "L" : "R", juce::Rectangle<float> (m.getX(), meters.getBottom() - 12.0f, m.getWidth(), 12.0f), juce::Justification::centred);
    }
    g.setFont (fonts::label (9.0f));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f })
    {
        const float y = yFor (scaleArea, db);
        g.setColour (db == 0.0f ? colours::critical.withAlpha (0.8f) : colours::textFaint);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (meterArea.getX() + 2.0f, y - 5.0f, 20.0f, 10.0f), juce::Justification::left);
    }

    area.removeFromLeft (10.0f);
    auto tpArea = area.removeFromTop (54.0f);
    markHighlight (g, tpArea.withWidth (std::min (tpArea.getWidth(), 170.0f)), highlight::truePeak);
    drawValue (g, tpArea, "TRUE PEAK MAX", formatDb (s.truePeakMaxDb, 2), "dBTP",
               severityColour (std::max (d.severityOf (highlight::truePeak) == Severity::info ? Severity::ok : d.severityOf (highlight::truePeak),
                                         d.severityOf (highlight::overs))), 30.0f);

    if (d.phase == WorkPhase::mix)
    {
        g.setColour (colours::target);
        g.setFont (fonts::label (11.0f));
        g.drawText ("margine per il mastering: -6 .. -3 dBTP", area.removeFromTop (15.0f), juce::Justification::centredLeft);
    }
    else if (d.profile != nullptr)
    {
        if (const auto* r = d.profile->getMetric (metric::truePeakMax))
        {
            g.setColour (colours::target);
            g.setFont (fonts::label (11.0f));
            g.drawText ("max consigliato " + juce::String (r->max, 1) + " dBTP", area.removeFromTop (15.0f), juce::Justification::centredLeft);
        }
    }

    const float rowH = juce::jlimit (15.0f, 19.0f, area.getHeight() / 9.0f);
    auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c, const juce::String& key = {})
    {
        auto r = area.removeFromTop (rowH);
        markHighlight (g, r, key);
        drawRow (g, r, label, value, c);
    };

    area.removeFromTop (4.0f);
    row ("Sample peak L / R", formatDb (s.samplePeakDb[0], 2) + " / " + formatDb (s.samplePeakDb[1], 2) + " dBFS", colours::text, "samplePeak");
    row ("TP recente (3 s)", formatDb (s.recentTruePeakDb, 2) + " dBTP", colours::text, "recentTp");
    row ("Eventi > -1 dBTP", juce::String (s.overs1dB), s.overs1dB > 0 ? colours::warning : colours::ok, "overs1");
    row ("Eventi > 0 dBTP", juce::String (s.overs0dB), s.overs0dB > 0 ? colours::critical : colours::ok, highlight::overs);
    row ("Clipping", juce::String (s.clipEvents) + " eventi", s.clipEvents > 0 ? colours::warning : colours::ok, highlight::clip);
    const float dc = std::max (s.dcOffsetDb[0], s.dcOffsetDb[1]);
    row ("DC offset max", formatDb (dc) + " dBFS", dc > -60.0f ? colours::warning : colours::ok, highlight::dc);
    row ("Noise floor", formatDb (s.noiseFloorDb) + " dBFS", colours::text, "noiseFloor");
    row ("Sample rate", s.sampleRate > 0.0 ? juce::String (s.sampleRate / 1000.0, 1) + " kHz" : juce::String ("--"), colours::text, "sampleRate");
    if (d.hasReference)
        row ("Reference TP max", formatDb (d.reference.truePeakMaxDb, 2) + " dBTP", colours::reference, "refTp");
}

//==============================================================================
void DynamicsPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& s = d.master;

    auto top = area.removeFromTop (56.0f);
    const float w = top.getWidth() / 3.0f;
    auto big = [&] (juce::Rectangle<float> r, const juce::String& label, const juce::String& value, const juce::String& unit, const juce::String& key)
    {
        markHighlight (g, r.withTrimmedRight (8.0f), key);
        drawValue (g, r, label, value, unit, d.colourFor (key), 26.0f);
    };
    big (top.removeFromLeft (w), "PLR", juce::String (s.plr, 1), "dB", highlight::plr);
    big (top.removeFromLeft (w), "PSR MIN", juce::String (s.minPsr, 1), "dB", highlight::psr);
    big (top, "DR", (s.drValid && ! s.liveMode) ? juce::String (s.drValue, 1) : juce::String ("--"), "", highlight::dr);

    auto ref = [&] (float v) { return d.hasReference ? juce::String (v, 1) : juce::String(); };
    const float rowH = 17.0f;
    auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c, const juce::String& refValue = {}, const juce::String& key = {})
    {
        auto r = area.removeFromTop (rowH);
        markHighlight (g, r, key);
        drawRow (g, r, label, value, c, refValue);
    };

    area.removeFromTop (4.0f);
    row ("PSR attuale", juce::String (s.psr, 1) + " dB", colours::text, {}, "psrNow");
    row ("Crest factor", juce::String (s.crestDb, 1) + " dB", colours::text, ref (d.reference.crestDb), "crestFactor");
    row ("RMS", formatDb (s.rmsDb) + " dBFS", colours::text, ref (d.reference.rmsDb), "rms");
    for (int b = 0; b < kNumCrestBands; ++b)
        row (juce::String ("Crest ") + kCrestBandNames[(size_t) b], juce::String (s.bandCrestDb[(size_t) b], 1) + " dB",
             d.colourFor (highlight::crest (b)), ref (d.reference.bandCrestDb[(size_t) b]), highlight::crest (b));

    // istogramma loudness short-term
    area.removeFromTop (8.0f);
    auto hist = area;
    fillWell (g, hist);
    addHelp (hist, "stHistogram");

    float maxV = 0.0f;
    for (auto v : s.stHistogram) maxV = std::max (maxV, v);
    if (d.hasReference)
        for (auto v : d.reference.stHistogram) maxV = std::max (maxV, v);

    if (maxV <= 0.0f)
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::label (11.0f));
        g.drawText ("Distribuzione short-term loudness", hist, juce::Justification::centred);
        return;
    }

    auto plot = hist.reduced (6.0f).withTrimmedBottom (12.0f);
    const float bw = plot.getWidth() / kHistogramBins;
    for (int i = 0; i < kHistogramBins; ++i)
    {
        const float h = plot.getHeight() * s.stHistogram[(size_t) i] / maxV;
        if (h > 0.0f)
        {
            g.setGradientFill (juce::ColourGradient (colours::accent, 0.0f, plot.getBottom() - h, colours::accent2.withAlpha (0.6f), 0.0f, plot.getBottom(), false));
            g.fillRoundedRectangle (plot.getX() + i * bw + 1.0f, plot.getBottom() - h, bw - 2.0f, h, 1.5f);
        }

        if (d.hasReference)
        {
            const float rh = plot.getHeight() * d.reference.stHistogram[(size_t) i] / maxV;
            g.setColour (colours::reference);
            g.fillRect (plot.getX() + i * bw, plot.getBottom() - rh - 1.0f, bw, 1.5f);
        }
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::label (9.0f));
    for (int l = -40; l <= 0; l += 10)
    {
        const float x = plot.getX() + (l - kHistogramMinLufs) * bw;
        g.drawText (juce::String (l), juce::Rectangle<float> (x - 15.0f, plot.getBottom() + 1.0f, 30.0f, 10.0f), juce::Justification::centred);
    }
}

} // namespace ma::ui
