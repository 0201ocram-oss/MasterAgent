#include "AlbumView.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    constexpr std::array<const char*, kNumBands> kBandShort { "Sub", "Bass", "LoMid", "Mid", "HiMid", "Pres.", "Brill.", "Air" };

    juce::String num (float v, int decimals = 1)
    {
        return v <= kSilenceDb + 0.5f ? juce::String ("-inf") : juce::String (v, decimals);
    }

    juce::String signedNum (float v, int decimals = 1)
    {
        const auto rounded = juce::String (v, decimals);
        return (rounded.getFloatValue() > 0.0f ? "+" : "") + rounded;
    }

    /** Colore di sfondo di una cella: tinta leggera per da verificare / critico, niente se nel range. */
    juce::Colour cellTint (Severity s)
    {
        return s >= Severity::warning ? severityColour (s).withAlpha (s == Severity::critical ? 0.26f : 0.18f)
                                      : juce::Colours::transparentBlack;
    }
}

AlbumView::AlbumView() : Panel ("Coerenza album"_t, "panel:album")
{
    showButton.setButtonText ("Mostra nella dashboard"_t);
    showButton.setTooltip ("Apre il brano selezionato nella dashboard, come file master: analisi completa con il target attivo"_t);
    showButton.onClick = [this] { if (onShowTrack && selected >= 0) onShowTrack (selected); };

    exportButton.setButtonText ("Esporta"_t);
    exportButton.setTooltip ("Salva la tabella dell'album come testo, CSV o immagine"_t);
    exportButton.onClick = [this] { if (onExport) onExport(); };

    clearButton.onClick = [this] { if (onClear) onClear(); };

    closeButton.setButtonText ("Chiudi"_t);
    closeButton.setTooltip ("Torna alla dashboard; i risultati dell'album restano disponibili dal menu Opzioni"_t);
    closeButton.onClick = [this] { if (onClose) onClose(); };

    for (auto* b : { &showButton, &exportButton, &clearButton, &closeButton })
        addAndMakeVisible (b);

    updateButtons();
}

void AlbumView::setAlbum (const AlbumResult& result, const juce::StringArray& skippedFiles)
{
    album = result;
    skipped = skippedFiles;
    analysing = false;
    tableScroll = detailScroll = 0.0f;
    hovered = -1;

    // parte dal brano messo peggio: è quello da guardare per primo
    selected = album.tracks.empty() ? -1 : 0;
    for (size_t i = 0; i < album.tracks.size(); ++i)
        if (album.tracks[i].worst > album.tracks[(size_t) selected].worst)
            selected = (int) i;

    badge = album.tracks.empty() ? juce::String() : juce::String ((int) album.tracks.size()) + " " + "BRANI"_t;
    updateButtons();
    repaint();
}

void AlbumView::setProgress (bool isAnalysing, const juce::String& statusText, float progress)
{
    if (isAnalysing == analysing && statusText == status && juce::approximatelyEqual (progress, progressValue))
        return;

    analysing = isAnalysing;
    status = statusText;
    progressValue = progress;
    if (analysing)
        badge = "ANALISI"_t;
    updateButtons();
    repaint();
}

void AlbumView::updateButtons()
{
    const bool ready = ! analysing && album.isValid();
    showButton.setEnabled (ready && selected >= 0);
    exportButton.setEnabled (ready);
    clearButton.setButtonText (analysing ? "Interrompi"_t : "Svuota"_t);
    clearButton.setTooltip (analysing ? "Interrompe l'analisi dei brani"_t : "Elimina i risultati dell'album e chiude il pannello"_t);
}

void AlbumView::resized()
{
    auto bar = getContentArea().removeFromTop (kBarHeight).toNearestInt().withSizeKeepingCentre (getContentArea().toNearestInt().getWidth(), 26);
    for (auto* b : { &closeButton, &clearButton, &exportButton })
    {
        b->setBounds (bar.removeFromRight (b == &closeButton ? 80 : 92));
        bar.removeFromRight (6);
    }
    showButton.setBounds (bar.removeFromRight (178));
}

//==============================================================================
AlbumView::Columns AlbumView::columnsFor (float width) const
{
    Columns c {};
    c.index = 26.0f;
    c.integrated = 58.0f;
    c.loudest = 62.0f;
    c.delta = 54.0f;
    c.gain = 56.0f;
    c.truePeak = 54.0f;
    c.width = 52.0f;
    c.status = 22.0f;
    const float fixed = c.index + c.integrated + c.loudest + c.delta + c.gain + c.truePeak + c.width + c.status;
    c.band = juce::jlimit (34.0f, 54.0f, (width - fixed - 140.0f) / (float) kNumBands);
    c.name = std::max (60.0f, width - fixed - c.band * (float) kNumBands);
    return c;
}

void AlbumView::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto bar = area.removeFromTop (kBarHeight);
    bar.removeFromRight ((float) (getContentArea().getRight() - showButton.getX()) + 10.0f);
    addHelp (bar, "album:summary");

    // riepilogo dell'album
    {
        juce::AttributedString as;
        if (analysing)
        {
            as.append ("Analisi dei brani in corso: ogni file viene letto per intero, offline."_t, fonts::label (12.5f), colours::textDim);
        }
        else if (album.isValid())
        {
            const int toFix = album.critical + album.warning;
            as.append (toFix == 0 ? "Album coerente"_t
                                  : (toFix == 1 ? "1 brano da sistemare"_t : "%d brani da sistemare"_t.replace ("%d", juce::String (toFix))),
                       fonts::value (13.5f), toFix == 0 ? colours::ok : (album.critical > 0 ? colours::critical : colours::warning));
            as.append ("    " + "Sezione più forte: mediana %1 LUFS, differenza massima %2 LU"_t
                                    .replace ("%1", num (album.medianLoudestLufs)).replace ("%2", juce::String (album.loudestSpreadLu, 1)),
                       fonts::label (12.0f), colours::textDim);
            as.append ("    " + (album.usesLoudestSection ? "tonale confrontato sui ritornelli"_t : "tonale sul brano intero"_t),
                       fonts::label (12.0f), colours::textFaint);
        }
        if (! skipped.isEmpty())
            as.append ("\n" + "Saltati:"_t + " " + skipped.joinIntoString (", "), fonts::label (11.0f), colours::warning);
        as.setWordWrap (juce::AttributedString::none);
        as.setJustification (juce::Justification::centredLeft);
        juce::TextLayout layout;
        layout.createLayout (as, 10000.0f);
        g.saveState();
        g.reduceClipRegion (bar.toNearestInt());
        layout.draw (g, bar.withTrimmedLeft (2.0f));
        g.restoreState();
    }

    area.removeFromTop (8.0f);
    auto table = area.removeFromLeft (std::floor (area.getWidth() * 0.64f));
    area.removeFromLeft (12.0f);

    if (analysing)
    {
        paintProgress (g, table);
        rowsArea = {};
    }
    else if (! album.isValid())
    {
        fillWell (g, table);
        g.setColour (colours::textDim);
        g.setFont (fonts::label (13.0f));
        g.drawFittedText ("Servono almeno 2 brani analizzati. Trascina i file dell'album sulla finestra o usa Opzioni > Controlla coerenza album."_t,
                          table.reduced (24.0f).toNearestInt(), juce::Justification::centred, 3);
        rowsArea = {};
    }
    else
    {
        paintTable (g, table);
    }

    paintDetails (g, area);
}

void AlbumView::paintProgress (juce::Graphics& g, juce::Rectangle<float> area)
{
    fillWell (g, area);
    auto centre = area.withSizeKeepingCentre (std::min (480.0f, area.getWidth() - 40.0f), 60.0f);
    g.setColour (colours::text);
    g.setFont (fonts::value (14.0f));
    g.drawFittedText (status.isNotEmpty() ? status : "Preparazione..."_t, centre.removeFromTop (24.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
    centre.removeFromTop (8.0f);
    drawHBar (g, centre.removeFromTop (8.0f), progressValue, 0.0f, 1.0f, colours::accent);
    centre.removeFromTop (8.0f);
    g.setColour (colours::textFaint);
    g.setFont (fonts::label (11.5f));
    g.drawText ("Puoi chiudere il pannello: l'analisi continua e i risultati restano nel plugin."_t, centre, juce::Justification::centredLeft);
}

void AlbumView::paintTable (juce::Graphics& g, juce::Rectangle<float> area)
{
    fillWell (g, area);
    auto inner = area.reduced (8.0f, 4.0f);
    const auto c = columnsFor (inner.getWidth());

    // una riga: celle da sinistra a destra con le larghezze delle colonne
    struct Cell { juce::String text; juce::Colour colour; juce::Colour tint; bool bold = false; };
    auto drawRow = [&] (juce::Rectangle<float> r, const std::vector<Cell>& cells, const std::vector<juce::String>& helpKeys)
    {
        const std::vector<float> widths { c.index, c.name, c.integrated, c.loudest, c.delta, c.gain, c.truePeak, c.width,
                                          c.band, c.band, c.band, c.band, c.band, c.band, c.band, c.band, c.status };
        float x = r.getX();
        for (size_t i = 0; i < widths.size() && i < cells.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (x, r.getY(), widths[i], r.getHeight());
            x += widths[i];
            if (i < helpKeys.size())
                addHelp (cell, helpKeys[i]);

            const auto& cl = cells[i];
            if (! cl.tint.isTransparent())
            {
                g.setColour (cl.tint);
                g.fillRoundedRectangle (cell.reduced (2.0f, 3.0f), 3.0f);
            }
            if (i == 16 && cl.text == "*")
            {
                g.setColour (cl.colour);
                g.fillEllipse (cell.withSizeKeepingCentre (8.0f, 8.0f));
                continue;
            }
            g.setColour (cl.colour);
            g.setFont (cl.bold ? fonts::value (12.0f) : fonts::label (12.0f));
            g.drawFittedText (cl.text, cell.reduced (4.0f, 0.0f).toNearestInt(),
                              i == 1 ? juce::Justification::centredLeft : juce::Justification::centredRight, 1, 0.8f);
        }
    };

    // intestazione
    {
        auto header = inner.removeFromTop (30.0f);
        std::vector<Cell> cells;
        for (const auto& label : { juce::String ("#"), "Brano"_t, juce::String ("Integr."), "Forte"_t, "vs album"_t, "Gain"_t,
                                   juce::String ("TP"), "Largh."_t })
            cells.push_back ({ label, colours::textFaint, {} });
        for (auto* band : kBandShort)
            cells.push_back ({ band, colours::textFaint, {} });
        cells.push_back ({ {}, colours::textFaint, {} });

        std::vector<juce::String> keys { {}, "album:row", "album:integrated", "album:loudest", "album:delta", "album:gain",
                                         "album:tp", "album:width" };
        for (int b = 0; b < kNumBands; ++b)
            keys.push_back ("album:band:" + juce::String (b));
        keys.push_back ("album:status");
        drawRow (header, cells, keys);
    }

    // riferimento: la mediana dell'album
    {
        auto row = inner.removeFromTop (kRowHeight);
        g.setColour (colours::accent.withAlpha (0.07f));
        g.fillRoundedRectangle (row, 4.0f);
        std::vector<Cell> cells {
            { {}, colours::textDim, {} },
            { "Mediana album"_t, colours::accent, {}, true },
            { num (album.medianIntegratedLufs), colours::textDim, {} },
            { num (album.medianLoudestLufs), colours::accent, {}, true },
            { "0.0", colours::textFaint, {} },
            { {}, colours::textFaint, {} },
            { num (album.medianTruePeakDb), colours::textDim, {} },
            { juce::String (juce::roundToInt (album.medianWidthPercent)) + "%", colours::textDim, {} },
        };
        for (int b = 0; b < kNumBands; ++b)
            cells.push_back ({ "0.0", colours::textFaint, {} });
        drawRow (row, cells, { {}, "album:median" });
        inner.removeFromTop (3.0f);
        g.setColour (colours::line);
        g.fillRect (inner.removeFromTop (1.0f));
        inner.removeFromTop (2.0f);
    }

    rowsArea = inner;
    tableContentHeight = (float) album.tracks.size() * kRowHeight;
    tableScroll = juce::jlimit (0.0f, std::max (0.0f, tableContentHeight - rowsArea.getHeight()), tableScroll);

    g.saveState();
    g.reduceClipRegion (rowsArea.toNearestInt());
    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& t = album.tracks[i];
        const auto row = juce::Rectangle<float> (rowsArea.getX(), rowsArea.getY() + (float) i * kRowHeight - tableScroll,
                                                 rowsArea.getWidth(), kRowHeight);
        if (row.getBottom() < rowsArea.getY() || row.getY() > rowsArea.getBottom())
            continue;

        const bool isSelected = (int) i == selected;
        if (isSelected || (int) i == hovered)
        {
            g.setColour (isSelected ? colours::accent.withAlpha (0.16f) : colours::hover);
            g.fillRoundedRectangle (row, 4.0f);
        }
        else if (i % 2 == 1)
        {
            g.setColour (colours::stripe);
            g.fillRect (row);
        }

        const bool flagged = t.loudness != Severity::ok;
        std::vector<Cell> cells {
            { juce::String ((int) i + 1), colours::textFaint, {} },
            { t.name, isSelected ? colours::text : colours::text.withAlpha (0.9f), {}, isSelected },
            { num (t.integratedLufs), colours::textDim, {} },
            { num (t.loudestLufs), colours::text, {} },
            { signedNum (t.loudestDeltaLu), flagged ? severityColour (t.loudness) : colours::textDim, cellTint (t.loudness), flagged },
            { flagged ? signedNum (t.gainDb) + " dB" : juce::String::charToString (0x2013), flagged ? colours::text : colours::textFaint, {}, flagged },
            { num (t.truePeakDb), std::abs (t.truePeakDeltaDb) > album::truePeakInfoDb ? colours::info : colours::textDim, {} },
            { juce::String (juce::roundToInt (t.widthPercent)) + "%",
              std::abs (t.widthDelta) > album::widthInfoPercent ? colours::info : colours::textDim, {} },
        };
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto s = t.bandSeverity[(size_t) b];
            cells.push_back ({ signedNum (t.bandDelta[(size_t) b]), s >= Severity::warning ? severityColour (s) : colours::textFaint,
                               cellTint (s), s >= Severity::warning });
        }
        cells.push_back ({ "*", severityColour (t.worst), {} });
        drawRow (row, cells, {});
    }
    g.restoreState();
    addHelp (rowsArea, "album:row");

    // barra di scorrimento quando i brani non ci stanno
    if (tableContentHeight > rowsArea.getHeight())
    {
        const float h = rowsArea.getHeight() * rowsArea.getHeight() / tableContentHeight;
        const float y = rowsArea.getY() + (rowsArea.getHeight() - h) * tableScroll / (tableContentHeight - rowsArea.getHeight());
        g.setColour (colours::textFaint.withAlpha (0.5f));
        g.fillRoundedRectangle ({ rowsArea.getRight() - 3.0f, y, 3.0f, h }, 1.5f);
    }
}

void AlbumView::paintDetails (juce::Graphics& g, juce::Rectangle<float> area)
{
    fillWell (g, area);
    detailArea = area;
    addHelp (area, "album:details");
    auto inner = area.reduced (12.0f, 10.0f);

    if (analysing || ! juce::isPositiveAndBelow (selected, (int) album.tracks.size()))
    {
        g.setColour (colours::textFaint);
        g.setFont (fonts::label (12.5f));
        g.drawFittedText (analysing ? "Le diagnosi di ogni brano compaiono qui al termine dell'analisi."_t
                                    : "Seleziona un brano nella tabella."_t,
                          inner.toNearestInt(), juce::Justification::centred, 3);
        return;
    }

    const auto& t = album.tracks[(size_t) selected];

    // titolo e misure principali
    {
        auto titleRow = inner.removeFromTop (22.0f);
        g.setColour (severityColour (t.worst));
        g.fillEllipse (titleRow.removeFromLeft (9.0f).withSizeKeepingCentre (9.0f, 9.0f));
        titleRow.removeFromLeft (8.0f);
        g.setColour (colours::text);
        g.setFont (fonts::value (14.5f));
        g.drawFittedText (juce::String (selected + 1) + ".  " + t.name, titleRow.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        g.setColour (colours::textDim);
        g.setFont (fonts::label (11.5f));
        g.drawFittedText ("Integrated %1 LUFS   Sezione forte %2 LUFS   TP %3 dBTP   PLR %4 dB   LRA %5 LU"_t
                              .replace ("%1", num (t.integratedLufs)).replace ("%2", num (t.loudestLufs))
                              .replace ("%3", num (t.truePeakDb)).replace ("%4", num (t.plr)).replace ("%5", num (t.lra)),
                          inner.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        inner.removeFromTop (6.0f);
    }

    // bilanciamento tonale rispetto all'album: barre dal centro, fascia = tolleranza
    {
        auto chart = inner.removeFromTop (70.0f);
        addHelp (chart, "album:tonalChart");
        auto labels = chart.removeFromBottom (14.0f);
        constexpr float range = 6.0f;
        const float slot = chart.getWidth() / (float) kNumBands;
        const float centreY = chart.getCentreY();
        auto yOf = [&] (float db) { return centreY - juce::jlimit (-range, range, db) / range * chart.getHeight() * 0.5f; };

        g.setColour (colours::grid);
        g.fillRect (chart.getX(), centreY - 0.5f, chart.getWidth(), 1.0f);
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto col = juce::Rectangle<float> (chart.getX() + slot * (float) b, chart.getY(), slot, chart.getHeight()).reduced (slot * 0.18f, 0.0f);
            const float tol = t.bandTolerance[(size_t) b];
            g.setColour (colours::ok.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (col.getX(), yOf (tol), col.getRight(), yOf (-tol)));

            const float d = t.bandDelta[(size_t) b];
            const auto s = t.bandSeverity[(size_t) b];
            g.setColour (s >= Severity::warning ? severityColour (s) : colours::accent.withAlpha (0.7f));
            const float y0 = std::min (centreY, yOf (d)), y1 = std::max (centreY, yOf (d));
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (col.getX() + 3.0f, y0, col.getRight() - 3.0f, std::max (y0 + 1.5f, y1)), 2.0f);

            g.setColour (colours::textFaint);
            g.setFont (fonts::label (10.0f));
            g.drawText (kBandShort[(size_t) b], juce::Rectangle<float> (chart.getX() + slot * (float) b, labels.getY(), slot, labels.getHeight()),
                        juce::Justification::centred);
        }
        inner.removeFromTop (8.0f);
    }

    auto hint = inner.removeFromBottom (18.0f);
    g.setColour (colours::textFaint);
    g.setFont (fonts::label (11.0f));
    g.drawFittedText ("Doppio clic sulla riga o \"Mostra nella dashboard\" per l'analisi completa del brano."_t,
                      hint.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    inner.removeFromBottom (4.0f);

    // diagnosi del brano, come schede del report
    const float textWidth = inner.getWidth() - 26.0f;
    std::vector<std::pair<juce::TextLayout, Severity>> cards;
    for (const auto& f : t.findings)
    {
        juce::AttributedString as;
        as.append (f.category.toUpperCase() + "   ", fonts::value (9.5f), colours::textFaint);
        as.append (f.metric + "\n", fonts::value (13.0f), colours::text);
        as.append (f.value, fonts::value (12.5f), severityColour (f.severity));
        as.append ("    " + f.target, fonts::label (11.5f), colours::target.withAlpha (0.8f));
        as.append ("\n" + f.message, fonts::label (12.0f), colours::text.withAlpha (0.82f));
        if (f.action.isNotEmpty())
            as.append ("\n" + juce::String::charToString (0x2192) + " " + f.action, fonts::label (12.0f), colours::accent.withAlpha (0.95f));
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (2.0f);
        juce::TextLayout layout;
        layout.createLayout (as, textWidth);
        cards.emplace_back (std::move (layout), f.severity);
    }

    if (cards.empty())
    {
        g.setColour (colours::ok);
        g.setFont (fonts::value (13.0f));
        g.drawFittedText ("In linea con il resto dell'album: loudness della sezione più forte e bilanciamento tonale coerenti."_t,
                          inner.removeFromTop (40.0f).toNearestInt(), juce::Justification::topLeft, 2);
        return;
    }

    detailContentHeight = 0.0f;
    for (const auto& [layout, sev] : cards)
        detailContentHeight += layout.getHeight() + 20.0f + 6.0f;
    detailScroll = juce::jlimit (0.0f, std::max (0.0f, detailContentHeight - inner.getHeight()), detailScroll);

    g.saveState();
    g.reduceClipRegion (inner.toNearestInt());
    float y = inner.getY() - detailScroll;
    for (const auto& [layout, sev] : cards)
    {
        const auto r = juce::Rectangle<float> (inner.getX(), y, inner.getWidth(), layout.getHeight() + 20.0f);
        y += r.getHeight() + 6.0f;
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (colours::line.withMultipliedAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (severityColour (sev));
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 8.0f).translated (5.0f, 0.0f), 1.5f);
        layout.draw (g, r.reduced (10.0f).withTrimmedLeft (6.0f));
    }
    g.restoreState();
}

//==============================================================================
int AlbumView::rowAt (juce::Point<float> p) const
{
    if (analysing || ! rowsArea.contains (p))
        return -1;
    const int index = (int) std::floor ((p.y - rowsArea.getY() + tableScroll) / kRowHeight);
    return juce::isPositiveAndBelow (index, (int) album.tracks.size()) ? index : -1;
}

void AlbumView::select (int index)
{
    if (index == selected)
        return;
    selected = index;
    detailScroll = 0.0f;
    updateButtons();
    repaint();
}

void AlbumView::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position);
    setMouseCursor (row >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (row != hovered)
    {
        hovered = row;
        repaint();
    }
}

void AlbumView::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void AlbumView::mouseDown (const juce::MouseEvent& e)
{
    if (const int row = rowAt (e.position); row >= 0)
        select (row);
}

void AlbumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int row = rowAt (e.position); row >= 0 && onShowTrack)
        onShowTrack (row);
}

void AlbumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float step = -wheel.deltaY * 120.0f;
    if (rowsArea.contains (e.position))
        tableScroll = juce::jlimit (0.0f, std::max (0.0f, tableContentHeight - rowsArea.getHeight()), tableScroll + step);
    else if (detailArea.contains (e.position))
        detailScroll = std::max (0.0f, detailScroll + step);   // limitato al paint, dove l'altezza è nota
    else
        return;
    repaint();
}

} // namespace ma::ui
