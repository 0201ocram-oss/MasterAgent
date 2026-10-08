#include "StereoPanel.h"
#include "../Analysis/StreamingPreview.h"
#include "../Common/Text.h"

namespace ma::ui
{

void StereoPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& s = d.master;

    const float gonioSize = std::min (area.getHeight() * 0.6f, area.getWidth() * 0.48f);
    auto top = area.removeFromTop (gonioSize);
    addHelp (top.withWidth (gonioSize), "goniometer");
    drawGoniometer (g, top.removeFromLeft (gonioSize));
    top.removeFromLeft (14.0f);

    // correlazione
    auto corrBlock = top.removeFromTop (44.0f);
    markHighlight (g, corrBlock, highlight::correlation);
    g.setFont (fonts::label (11.0f));
    g.setColour (colours::textDim);
    g.drawText ("Correlazione (istantanea / media)", corrBlock.removeFromTop (15.0f), juce::Justification::centredLeft);
    auto bar = corrBlock.removeFromTop (10.0f);
    const auto corrCol = s.correlationInstant < 0.0f ? colours::critical : (s.correlationInstant < 0.3f ? colours::warning : colours::ok);
    drawHBar (g, bar, s.correlationInstant, -1.0f, 1.0f, corrCol, true);
    {
        auto marker = [&] (float value, juce::Colour c)
        {
            const float x = bar.getX() + (value + 1.0f) * 0.5f * bar.getWidth();
            g.setColour (c);
            g.fillRoundedRectangle (x - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f, 1.5f);
        };
        marker (s.correlation, colours::text);
        if (d.hasReference)
            marker (d.reference.correlation, colours::reference);
    }
    g.setFont (fonts::label (9.0f));
    g.setColour (colours::textFaint);
    auto scale = corrBlock.removeFromTop (12.0f);
    g.drawText ("-1", scale, juce::Justification::centredLeft);
    g.drawText ("0", scale, juce::Justification::centred);
    g.drawText ("+1", scale, juce::Justification::centredRight);

    top.removeFromTop (6.0f);
    auto ref = [&] (float v, int dec) { return d.hasReference ? (dec == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, dec)) : juce::String(); };
    auto row = [&] (const juce::String& label, const juce::String& value, const juce::String& key, const juce::String& refValue = {})
    {
        auto r = top.removeFromTop (18.0f);
        markHighlight (g, r, key);
        drawRow (g, r, label, value, d.colourFor (key), refValue);
    };

    row ("Correlazione media", juce::String (s.correlation, 2), highlight::correlation, ref (d.reference.correlation, 2));
    row ("Larghezza", juce::String (juce::roundToInt (s.widthPercent)) + "%", highlight::width, ref (d.reference.widthPercent, 0));
    row ("Bilanciamento L/R", juce::String (s.balanceDb, 2) + " dB", highlight::balance);
    row ("Perdita in mono", juce::String (s.monoLossDb, 1) + " dB", highlight::monoLoss, ref (d.reference.monoLossDb, 1));
    row ("Side sotto 100 Hz", juce::String (juce::roundToInt (s.lowEndWidthPercent)) + "%   c " + juce::String (s.lowEndCorrelation, 2),
         highlight::lowEndWidth, ref (d.reference.lowEndWidthPercent, 0));

    // tabella per banda: larghezza e correlazione
    area.removeFromTop (10.0f);
    const float bw = area.getWidth() / kNumBands;

    for (int b = 0; b < kNumBands; ++b)
    {
        auto full = juce::Rectangle<float> (area.getX() + b * bw, area.getY(), bw, area.getHeight()).reduced (3.0f, 0.0f);
        auto cell = full;
        auto label = cell.removeFromBottom (14.0f);
        auto corrLabel = cell.removeFromBottom (14.0f);
        auto valueLabel = cell.removeFromTop (15.0f);

        fillWell (g, cell, 5.0f);

        markHighlight (g, full.reduced (2.0f), highlight::bandWidth (b));
        markHighlight (g, corrLabel, highlight::bandCorrelation (b));

        const float w = s.bandWidthPercent[(size_t) b];
        const float corr = s.bandCorrelation[(size_t) b];
        const bool lowBand = b <= 1;
        const bool problem = (lowBand && (corr < 0.7f || w > 15.0f)) || corr < 0.0f;
        const auto col = problem ? colours::warning : colours::accent;

        auto inner = cell.reduced (cell.getWidth() * 0.24f, 3.0f);
        const float h = inner.getHeight() * juce::jlimit (0.0f, 1.0f, w / 60.0f);
        if (h > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (col, 0.0f, inner.getBottom() - h, col.withAlpha (0.45f), 0.0f, inner.getBottom(), false));
            g.fillRoundedRectangle (inner.withTop (inner.getBottom() - h), 3.0f);
        }

        if (d.hasReference)
        {
            const float rh = inner.getHeight() * juce::jlimit (0.0f, 1.0f, d.reference.bandWidthPercent[(size_t) b] / 60.0f);
            g.setColour (colours::reference);
            g.fillRoundedRectangle (cell.getX() + 4.0f, inner.getBottom() - rh - 1.0f, cell.getWidth() - 8.0f, 2.0f, 1.0f);
        }

        g.setFont (fonts::value (11.0f));
        g.setColour (colours::text);
        g.drawText (juce::String (juce::roundToInt (w)) + "%", valueLabel, juce::Justification::centred);
        g.setFont (fonts::label (10.0f));
        g.setColour (corr < 0.0f ? colours::critical : (lowBand && corr < 0.7f ? colours::warning : colours::textDim));
        g.drawText ("c " + juce::String (corr, 2), corrLabel, juce::Justification::centred);
        g.setColour (colours::textDim);
        g.drawText (kBandNames[(size_t) b], label, juce::Justification::centred);
    }
}

void StereoPanel::drawGoniometer (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& s = data->master;
    r = r.reduced (2.0f);
    fillWell (g, r, 8.0f);

    const auto c = r.getCentre();
    const float radius = r.getWidth() * 0.5f - 6.0f;

    g.setColour (colours::grid);
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 1.0f);
    g.drawEllipse (juce::Rectangle<float> (radius, radius).withCentre (c), 1.0f);
    g.drawLine (c.x, c.y - radius, c.x, c.y + radius);
    g.drawLine (c.x - radius, c.y, c.x + radius, c.y);
    g.drawLine (c.x - radius * 0.707f, c.y - radius * 0.707f, c.x + radius * 0.707f, c.y + radius * 0.707f);
    g.drawLine (c.x + radius * 0.707f, c.y - radius * 0.707f, c.x - radius * 0.707f, c.y + radius * 0.707f);

    g.setColour (colours::textFaint);
    g.setFont (fonts::label (9.0f));
    g.drawText ("M", juce::Rectangle<float> (c.x + 3.0f, c.y - radius, 12.0f, 10.0f), juce::Justification::left);
    g.drawText ("L", juce::Rectangle<float> (c.x - radius * 0.707f - 12.0f, c.y - radius * 0.707f - 10.0f, 10.0f, 10.0f), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (c.x + radius * 0.707f + 2.0f, c.y - radius * 0.707f - 10.0f, 10.0f, 10.0f), juce::Justification::centred);

    // auto-gain: normalizza al picco dei punti visualizzati
    float peak = 1e-6f;
    for (auto v : s.goniometer) peak = std::max (peak, std::abs (v));
    const float scale = radius / std::max (peak * 1.414f, 0.05f);

    for (int i = 0; i < kGoniometerPoints; ++i)
    {
        const int idx = (s.goniometerWritePos + i) % kGoniometerPoints;   // dal più vecchio
        const float l = s.goniometer[(size_t) idx * 2], rr = s.goniometer[(size_t) idx * 2 + 1];
        const float x = c.x + (rr - l) * 0.707f * scale;
        const float y = c.y - (l + rr) * 0.707f * scale;
        const float age = (float) i / kGoniometerPoints;
        g.setColour (colours::accent.interpolatedWith (colours::accent2, 1.0f - age).withAlpha (0.12f + 0.65f * age));
        g.fillEllipse (x - 0.9f, y - 0.9f, 1.8f, 1.8f);
    }
}

//==============================================================================
void StreamingPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& s = data->master;
    const auto results = computeStreamingPreview (s.integratedLufs, s.truePeakMaxDb);

    const float colW[] = { 0.24f, 0.14f, 0.18f, 0.2f, 0.24f };
    const juce::String headers[] = { "Piattaforma", "Target", "Guadagno", "In riproduzione", "TP risultante" };

    auto header = area.removeFromTop (18.0f);
    g.setFont (fonts::label (10.5f));
    g.setColour (colours::textFaint);
    {
        // guida: una zona per colonna (intestazione + righe)
        const char* columnHelp[] = { "streaming:platform", "streaming:target", "streaming:gain", "streaming:playback", "streaming:tp" };
        auto columns = header.withBottom (area.getBottom() - 30.0f);
        for (int i = 0; i < 5; ++i)
            addHelp (columns.removeFromLeft (header.getWidth() * colW[i]), columnHelp[i]);

        auto h = header;
        for (int i = 0; i < 5; ++i)
            g.drawText (headers[i].toUpperCase(), h.removeFromLeft (header.getWidth() * colW[i]),
                        i == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }

    if (results.empty())
    {
        g.setFont (fonts::label (12.0f));
        g.drawText ("In attesa del loudness integrato...", area, juce::Justification::centred);
        return;
    }

    const float rowH = std::min (22.0f, (area.getHeight() - 18.0f) / (float) results.size());
    int index = 0;
    for (const auto& r : results)
    {
        auto row = area.removeFromTop (rowH);
        if (index++ % 2 == 0)
        {
            g.setColour (colours::stripe);
            g.fillRoundedRectangle (row.expanded (6.0f, 0.0f), 4.0f);
        }
        auto cell = [&] (int i) { return row.removeFromLeft (header.getWidth() * colW[i]); };

        g.setFont (fonts::value (12.5f));
        g.setColour (colours::text);
        g.drawText (r.name, cell (0), juce::Justification::centredLeft);
        g.setFont (fonts::label (12.5f));
        g.setColour (colours::textDim);
        g.drawText (juce::String (juce::roundToInt (r.targetLufs)) + " LUFS", cell (1), juce::Justification::centredRight);

        const auto gainCol = r.tooLoud ? (r.gainDb < -3.0f ? colours::warning : colours::info) : (r.tooQuiet ? colours::warning : colours::ok);
        g.setFont (fonts::value (12.5f));
        g.setColour (gainCol);
        g.drawText ((r.gainDb >= 0 ? "+" : "") + juce::String (r.gainDb, 1) + " dB", cell (2), juce::Justification::centredRight);
        g.setFont (fonts::label (12.5f));
        g.setColour (colours::text);
        g.drawText (juce::String (r.playbackLufs, 1) + " LUFS", cell (3), juce::Justification::centredRight);
        g.setColour (r.playbackTruePeak > -1.0f ? colours::warning : colours::textDim);
        g.drawText (juce::String (r.playbackTruePeak, 1) + " dBTP", cell (4), juce::Justification::centredRight);
    }

    g.setFont (fonts::label (10.5f));
    g.setColour (data->phase == WorkPhase::mix ? colours::warning : colours::textFaint);
    g.drawFittedText (data->phase == WorkPhase::mix
                          ? "Fase di mix: anteprima solo indicativa, la loudness finale la decide il mastering."_u
                          : "Guadagno negativo = la piattaforma abbasserà il brano: il loudness oltre il target non dà vantaggi e costa dinamica."_u,
                      area.toNearestInt(), juce::Justification::bottomLeft, 2);
}

} // namespace ma::ui
