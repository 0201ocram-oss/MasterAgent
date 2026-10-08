#include "ReportPanel.h"
#include "HelpTexts.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    constexpr float kScoreHeight = 78.0f;     // anello + pagella
    constexpr float kCountersHeight = 20.0f;
    constexpr float kHeaderHeight = kScoreHeight + 6.0f + kCountersHeight;
    constexpr float kPad = 10.0f;
    constexpr float kGap = 6.0f;

    juce::Colour scoreColour (int score)
    {
        return score >= 85 ? colours::ok : (score >= 60 ? colours::warning : colours::critical);
    }
}

ReportPanel::ReportPanel() : Panel ("Report e diagnosi", "panel:report")
{
    addAndMakeVisible (showOkButton);
    showOkButton.setButtonText ("Mostra anche i parametri nel range"_t);
    showOkButton.setTooltip ("Elenca anche le misure che rispettano il target (in verde)"_t);
    showOkButton.setColour (juce::ToggleButton::textColourId, colours::textDim);
    showOkButton.onClick = [this] { if (data != nullptr) setData (*data); };

    addAndMakeVisible (saveVersionButton);
    saveVersionButton.setButtonText ("Salva versione"_t);
    saveVersionButton.setTooltip ("Salva l'analisi attuale: dopo le correzioni vedi cosa è migliorato e cosa è peggiorato"_t);
    saveVersionButton.onClick = [this] { if (onSaveVersion) onSaveVersion(); };

    addAndMakeVisible (versionBox);
    versionBox.setTooltip ("Versione salvata con cui confrontare il master attuale"_t);
    versionBox.onChange = [this]
    {
        if (onCompareVersionChanged)
            onCompareVersionChanged (versionBox.getSelectedId() - 2);   // id 1 = nessuna
    };
    setVersions ({}, -1);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

void ReportPanel::lookAndFeelChanged()
{
    showOkButton.setColour (juce::ToggleButton::textColourId, colours::textDim);
    if (data != nullptr)
        setData (*data);
}

void ReportPanel::resized()
{
    auto area = getContentArea().toNearestInt();
    area.removeFromTop ((int) kHeaderHeight + 4);
    auto versionRow = area.removeFromTop (26);
    saveVersionButton.setBounds (versionRow.removeFromLeft (118));
    versionRow.removeFromLeft (6);
    versionBox.setBounds (versionRow);
    area.removeFromTop (2);
    showOkButton.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    viewport.setBounds (area);
    if (data != nullptr)
        setData (*data);
}

void ReportPanel::setData (const DashboardData& d)
{
    badge = d.phase == WorkPhase::mix ? "MIX" : "MASTER";
    badgeColour = d.phase == WorkPhase::mix ? colours::accent2 : colours::accent;
    data = &d;

    if (areaFilter && ! d.comparison.areas[(size_t) *areaFilter].evaluated)
        areaFilter.reset();

    list.setFindings (d.comparison, showOkButton.getToggleState(), viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    repaint();
}

void ReportPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& c = data->comparison;
    auto header = area.removeFromTop (kScoreHeight);

    // punteggio totale ad anello
    const bool hasSignal = data->master.valid && data->master.integratedLufs > -70.0f;
    auto ringArea = header.removeFromLeft (std::min (header.getHeight(), 70.0f)).withSizeKeepingCentre (64.0f, 64.0f);
    addHelp (ringArea, "score");
    {
        const auto centre = ringArea.getCentre();
        const float radius = ringArea.getWidth() * 0.5f - 3.0f;
        juce::Path bg, arc;
        bg.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (colours::grid);
        g.strokePath (bg, juce::PathStrokeType (5.0f));
        if (hasSignal)
        {
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * c.score / 100.0f, true);
            g.setColour (scoreColour (c.score));
            g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (hasSignal ? colours::text : colours::textFaint);
        g.setFont (fonts::value (19.0f));
        g.drawText (hasSignal ? juce::String (c.score) : juce::String ("--"), ringArea, juce::Justification::centred);
    }

    // pagella: una riga per area, cliccabile per filtrare le diagnosi
    header.removeFromLeft (12.0f);
    const float rowH = header.getHeight() / (float) kNumAreas;
    for (int a = 0; a < kNumAreas; ++a)
    {
        const auto& sc = c.areas[(size_t) a];
        auto row = header.removeFromTop (rowH);
        areaRows[(size_t) a] = row;
        addHelp (row, "area:" + juce::String (a));

        const bool selected = areaFilter && (int) *areaFilter == a;
        if (selected || hoveredArea == a)
        {
            g.setColour (colours::accent.withAlpha (selected ? 0.16f : 0.08f));
            g.fillRoundedRectangle (row.expanded (4.0f, 0.0f), 4.0f);
        }

        auto name = row.removeFromLeft (std::min (118.0f, row.getWidth() * 0.45f));
        g.setColour (selected ? colours::text : colours::textDim);
        g.setFont (selected ? fonts::value (11.5f) : fonts::label (11.5f));
        g.drawFittedText (areaName ((Area) a), name.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

        auto value = row.removeFromRight (30.0f);
        const bool show = hasSignal && sc.evaluated;
        g.setColour (show ? scoreColour (sc.score) : colours::textFaint);
        g.setFont (fonts::value (11.5f));
        g.drawText (show ? juce::String (sc.score) : juce::String ("n/d"), value, juce::Justification::centredRight);

        auto bar = row.reduced (6.0f, 0.0f).withSizeKeepingCentre (row.getWidth() - 12.0f, 5.0f);
        g.setColour (colours::grid);
        g.fillRoundedRectangle (bar, 2.5f);
        if (show)
        {
            g.setColour (scoreColour (sc.score));
            g.fillRoundedRectangle (bar.withWidth (std::max (5.0f, bar.getWidth() * (float) sc.score / 100.0f)), 2.5f);
        }
    }

    // contatori su una riga
    area.removeFromTop (6.0f);
    auto counters = area.removeFromTop (kCountersHeight);
    addHelp (counters, "counters");
    auto counter = [&] (const juce::String& label, int n, juce::Colour col)
    {
        g.setFont (fonts::value (12.0f));
        const auto number = juce::String (n);
        g.setFont (fonts::label (12.0f));
        const float w = 22.0f + juce::GlyphArrangement::getStringWidth (fonts::label (12.0f), label)
                        + juce::GlyphArrangement::getStringWidth (fonts::value (12.0f), number);
        auto r = counters.removeFromLeft (w);
        counters.removeFromLeft (10.0f);
        g.setColour (col);
        g.fillEllipse (r.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f));
        r.removeFromLeft (5.0f);
        g.setColour (colours::text);
        g.setFont (fonts::value (12.0f));
        g.drawText (number, r, juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (fonts::label (12.0f));
        g.drawText (label, r.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (fonts::value (12.0f), number) + 4.0f),
                    juce::Justification::centredLeft);
    };
    counter ("critici"_t, c.count (Severity::critical), colours::critical);
    counter ("da verificare"_t, c.count (Severity::warning), colours::warning);
    counter ("nel range"_t, c.count (Severity::ok), colours::ok);
    if (const int ignored = c.countIgnored(); ignored > 0)
        counter ("ignorate"_t, ignored, colours::textFaint);
}

void ReportPanel::setVersions (const juce::StringArray& names, int selected)
{
    versionBox.clear (juce::dontSendNotification);
    versionBox.addItem (names.isEmpty() ? "Nessuna versione salvata"_t : "Confronta con: nessuna"_t, 1);
    for (int i = 0; i < names.size(); ++i)
        versionBox.addItem ("Confronta con "_t + names[i], i + 2);
    versionBox.setSelectedId (juce::isPositiveAndBelow (selected, names.size()) ? selected + 2 : 1, juce::dontSendNotification);
    versionBox.setEnabled (! names.isEmpty());
}

int ReportPanel::areaRowAt (juce::Point<float> p) const
{
    for (int a = 0; a < kNumAreas; ++a)
        if (areaRows[(size_t) a].expanded (4.0f, 0.0f).contains (p))
            return a;
    return -1;
}

void ReportPanel::mouseMove (const juce::MouseEvent& e)
{
    const int a = areaRowAt (e.position);
    const bool clickable = a >= 0 && data != nullptr && data->comparison.areas[(size_t) a].evaluated;
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (a != hoveredArea)
    {
        hoveredArea = a;
        repaint();
    }
}

void ReportPanel::mouseExit (const juce::MouseEvent&)
{
    hoveredArea = -1;
    repaint();
}

void ReportPanel::mouseDown (const juce::MouseEvent& e)
{
    const int a = areaRowAt (e.position);
    if (a < 0 || data == nullptr || ! data->comparison.areas[(size_t) a].evaluated)
        return;

    if (areaFilter && (int) *areaFilter == a)
        areaFilter.reset();
    else
        areaFilter = (Area) a;

    setData (*data);
    viewport.setViewPosition (0, 0);
}

//==============================================================================
void ReportPanel::FindingList::setFindings (const ComparisonResult& result, bool showOk, int width)
{
    width = std::max (100, width);
    const auto filter = owner.areaFilter;
    auto passes = [&filter] (const Finding& f) { return ! filter || f.area == *filter; };

    // ricostruisce i layout solo se il contenuto è cambiato
    juce::String sig (width);
    sig << (showOk ? "1" : "0") << (showIgnored ? "1" : "0") << "a" << (filter ? (int) *filter : -1) << "t" << getThemeVersion();
    for (const auto& f : result.findings)
        sig << (int) f.severity << (f.ignored ? "i" : "") << f.metric << f.value << f.message.length() << f.action.length();
    for (const auto& f : result.priorities)
        sig << f.metric;
    if (const auto* d = owner.data; d != nullptr && d->hasVersion)
    {
        sig << "v" << d->versionName << d->versionComparison.scoreBefore << d->versionComparison.scoreNow;
        for (const auto& c : d->versionComparison.changes)
            sig << c.id << (int) c.verdict << c.valueNow;
    }
    if (sig == signature)
        return;
    signature = sig;

    // mantiene l'hover sulla stessa diagnosi anche se i valori cambiano
    juce::String hoveredId;
    if (juce::isPositiveAndBelow (hovered, (int) items.size()))
        hoveredId = items[(size_t) hovered].finding.id;

    items.clear();
    float y = 0.0f;
    const float textWidth = (float) width - 2.0f * kPad - 8.0f;

    auto addHeader = [&] (const juce::String& text, ItemType type = ItemType::header)
    {
        Item item;
        item.type = type;
        item.headerText = text;
        item.y = y;
        item.height = 22.0f;
        y += item.height;
        items.push_back (std::move (item));
    };

    auto addFinding = [&] (const Finding& f, bool compact, int number)
    {
        const float alpha = f.ignored ? 0.55f : 1.0f;
        juce::AttributedString as;
        if (number > 0)
            as.append (juce::String (number) + ".  ", fonts::value (13.0f), colours::accent);
        as.append (f.category.toUpperCase() + "   ", fonts::value (9.5f), colours::textFaint);
        as.append (f.metric + "\n", fonts::value (13.0f), colours::text.withMultipliedAlpha (alpha));
        as.append (f.value, fonts::value (12.5f), f.ignored ? colours::textDim : severityColour (f.severity));
        as.append ("    target " + f.target, fonts::label (11.5f), colours::target.withAlpha (0.8f * alpha));
        if (f.ignored)
        {
            as.append ("\n" + "Ignorata: scelta voluta. Clic destro per ripristinarla."_t, fonts::label (11.5f), colours::textFaint);
        }
        else if (! compact && f.severity != Severity::ok)
        {
            as.append ("\n" + f.message, fonts::label (12.0f), colours::text.withAlpha (0.82f));
            if (f.action.isNotEmpty())
                as.append ("\n" + juce::String::charToString (0x2192) + " " + f.action, fonts::label (12.0f), colours::accent.withAlpha (0.95f));
        }
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (2.0f);

        Item item;
        item.finding = f;
        item.layout.createLayout (as, textWidth);
        item.y = y;
        item.height = item.layout.getHeight() + 2.0f * kPad;
        y += item.height + kGap;
        items.push_back (std::move (item));
    };

    // confronto con la versione salvata, in cima
    if (const auto* d = owner.data; d != nullptr && d->hasVersion && ! filter)
    {
        const auto& vc = d->versionComparison;
        addHeader ("RISPETTO A "_t + d->versionName.toUpperCase());

        juce::AttributedString as;
        const auto up = juce::String::charToString (0x25B2), down = juce::String::charToString (0x25BC);
        as.append (up + " " + juce::String (vc.improved) + " " + "migliorate"_t + "   ", fonts::value (12.5f), colours::ok);
        as.append (down + " " + juce::String (vc.worsened) + " " + "peggiorate"_t + "   ", fonts::value (12.5f),
                   vc.worsened > 0 ? colours::critical : colours::textDim);
        as.append (juce::String (vc.unchanged) + " " + "invariate"_t + "\n", fonts::label (12.0f), colours::textDim);
        as.append ("Punteggio"_t + " " + juce::String (vc.scoreBefore) + " " + juce::String::charToString (0x2192) + " "
                       + juce::String (vc.scoreNow), fonts::value (12.0f),
                   vc.scoreNow > vc.scoreBefore ? colours::ok : (vc.scoreNow < vc.scoreBefore ? colours::critical : colours::text));

        constexpr int maxRows = 8;
        int shown = 0;
        for (const auto& c : vc.changes)
        {
            if (shown++ >= maxRows)
            {
                as.append ("\n" + "... e altre "_t + juce::String ((int) vc.changes.size() - maxRows), fonts::label (11.5f), colours::textFaint);
                break;
            }
            const bool better = c.verdict == Verdict::better;
            as.append ("\n" + (better ? up : down) + " ", fonts::value (11.5f), better ? colours::ok : colours::critical);
            as.append (c.metric + "  ", fonts::label (11.5f), colours::text);
            as.append (c.valueBefore + " " + juce::String::charToString (0x2192) + " " + c.valueNow, fonts::value (11.5f),
                       severityColour (c.severityNow));
        }
        if (vc.changes.empty())
            as.append ("\n" + "Nessuna differenza rilevante rispetto alla versione salvata."_t, fonts::label (11.5f), colours::textDim);

        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (2.0f);

        Item item;
        item.type = ItemType::text;
        item.layout.createLayout (as, textWidth);
        item.y = y;
        item.height = item.layout.getHeight() + 2.0f * kPad;
        y += item.height + kGap + 4.0f;
        items.push_back (std::move (item));
    }

    int number = 1;
    bool hasPriorities = false;
    for (const auto& f : result.priorities)
    {
        if (! passes (f))
            continue;
        if (! hasPriorities)
            addHeader ("DA FARE PRIMA, IN QUEST'ORDINE"_t);
        hasPriorities = true;
        addFinding (f, true, number++);
    }

    if (hasPriorities)
        y += 4.0f;
    if (filter)
        addHeader ("DIAGNOSI: "_t + areaName (*filter).toUpperCase() + "  " + "(clic sull'area per vederle tutte)"_t);
    else if (hasPriorities)
        addHeader ("TUTTE LE DIAGNOSI"_t);

    for (const auto& f : result.findings)
        if (! f.ignored && passes (f) && (f.severity != Severity::ok || showOk))
            addFinding (f, false, 0);

    // scelte volute: in fondo, richiudibili
    int ignoredCount = 0;
    for (const auto& f : result.findings)
        ignoredCount += f.ignored && passes (f) ? 1 : 0;

    if (ignoredCount > 0)
    {
        y += 4.0f;
        addHeader ((showIgnored ? juce::String::charToString (0x25BE) : juce::String::charToString (0x25B8)) + " "
                       + "IGNORATE"_t + " (" + juce::String (ignoredCount) + ")",
                   ItemType::ignoredToggle);
        if (showIgnored)
            for (const auto& f : result.findings)
                if (f.ignored && passes (f))
                    addFinding (f, true, 0);
    }

    hovered = -1;
    if (hoveredId.isNotEmpty())
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].type == ItemType::finding && items[i].finding.id == hoveredId)
            {
                hovered = (int) i;
                break;
            }

    setSize (width, (int) std::ceil (y));
    repaint();
}

void ReportPanel::FindingList::paint (juce::Graphics& g)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        auto r = juce::Rectangle<float> (0.0f, item.y, (float) getWidth(), item.height);

        if (item.type == ItemType::text)
        {
            g.setColour (colours::well);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (colours::accent.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
            item.layout.draw (g, r.reduced (kPad));
            continue;
        }

        if (item.type != ItemType::finding)
        {
            const bool toggle = item.type == ItemType::ignoredToggle;
            g.setColour (toggle && (int) i == hovered ? colours::textDim : colours::textFaint);
            g.setFont (fonts::title());
            g.drawText (item.headerText, r.withTrimmedLeft (2.0f), juce::Justification::centredLeft);
            continue;
        }

        const auto col = item.finding.ignored ? colours::textFaint : severityColour (item.finding.severity);
        const bool isHovered = (int) i == hovered;

        g.setColour (isHovered ? colours::hover : colours::well);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (isHovered ? col.withAlpha (0.7f) : colours::line.withMultipliedAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

        g.setColour (col);
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 8.0f).translated (5.0f, 0.0f), 1.5f);

        item.layout.draw (g, r.reduced (kPad).withTrimmedLeft (6.0f));
    }
}

juce::String ReportPanel::FindingList::getTooltip()
{
    return owner.data != nullptr && owner.data->helpMode ? helpTextFor ("report:list") : juce::String();
}

int ReportPanel::FindingList::itemAt (float y) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if ((items[i].type == ItemType::finding || items[i].type == ItemType::ignoredToggle) && y >= items[i].y && y < items[i].y + items[i].height)
            return (int) i;
    return -1;
}

void ReportPanel::FindingList::setHovered (int index)
{
    if (index == hovered)
        return;

    hovered = index;
    repaint();

    const bool isFinding = juce::isPositiveAndBelow (index, (int) items.size()) && items[(size_t) index].type == ItemType::finding;
    setMouseCursor (isFinding || index < 0 ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);

    if (owner.onHover)
    {
        if (isFinding && ! items[(size_t) index].finding.ignored)
            owner.onHover (&items[(size_t) index].finding, severityColour (items[(size_t) index].finding.severity));
        else
            owner.onHover (nullptr, colours::accent);
    }
}

void ReportPanel::FindingList::mouseMove (const juce::MouseEvent& e)
{
    setHovered (itemAt (e.position.y));
}

void ReportPanel::FindingList::mouseExit (const juce::MouseEvent&)
{
    setHovered (-1);
}

void ReportPanel::FindingList::mouseDown (const juce::MouseEvent& e)
{
    const int index = itemAt (e.position.y);
    if (index < 0)
        return;

    const auto& item = items[(size_t) index];
    if (item.type == ItemType::ignoredToggle)
    {
        showIgnored = ! showIgnored;
        if (owner.data != nullptr)
            owner.setData (*owner.data);
        return;
    }

    if (! e.mods.isPopupMenu() || item.finding.id.isEmpty() || item.finding.area == Area::none)
        return;

    const auto id = item.finding.id;
    const bool ignored = item.finding.ignored;
    juce::PopupMenu menu;
    menu.addSectionHeader (item.finding.metric);
    menu.addItem (ignored ? "Ripristina la diagnosi"_t : "Ignora: è una scelta voluta"_t,
                  [safe = juce::Component::SafePointer<ReportPanel> (&owner), id, ignored]
                  {
                      if (safe != nullptr && safe->onIgnoreChanged)
                          safe->onIgnoreChanged (id, ! ignored);
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

} // namespace ma::ui
