#include "WaveformPanel.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    constexpr float kTransportWidth = 112.0f;
    constexpr float kAxisHeight = 14.0f;

    /** Forma del segno, uguale sulla forma d'onda e nella legenda. */
    void drawMarkerShape (juce::Graphics& g, MarkerType type, juce::Point<float> c, float size, juce::Colour colour, bool hollow)
    {
        const float h = size * 0.5f;
        juce::Path p;
        switch (type)
        {
            case MarkerType::clip:       p.addRectangle (c.x - h * 0.85f, c.y - h * 0.85f, h * 1.7f, h * 1.7f); break;
            case MarkerType::truePeak:   p.addTriangle (c.x, c.y - h, c.x + h, c.y + h * 0.8f, c.x - h, c.y + h * 0.8f); break;
            case MarkerType::dropout:    p.addEllipse (c.x - h, c.y - h, size, size); break;
            case MarkerType::compressed:
            case MarkerType::phase:      p.addRoundedRectangle (c.x - h, c.y - h * 0.55f, size, h * 1.1f, 2.0f); break;
        }
        g.setColour (colour);
        if (hollow || type == MarkerType::dropout)
            g.strokePath (p, juce::PathStrokeType (1.6f));
        else
            g.fillPath (p);
    }

    juce::String shortTypeName (MarkerType type)
    {
        switch (type)
        {
            case MarkerType::clip:       return "Clipping"_t;
            case MarkerType::truePeak:   return "True peak"_t;
            case MarkerType::dropout:    return "Buchi"_t;
            case MarkerType::compressed: return "Compressione"_t;
            case MarkerType::phase:      return "Fase"_t;
        }
        return {};
    }

    juce::Colour typeColour (MarkerType type)
    {
        switch (type)
        {
            case MarkerType::clip:
            case MarkerType::dropout:
            case MarkerType::phase:      return colours::critical;
            case MarkerType::truePeak:
            case MarkerType::compressed: return colours::warning;
        }
        return colours::warning;
    }
}

//==============================================================================
class WaveformPanel::PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("Play") {}

    void setPlaying (bool shouldShowPause)
    {
        if (showPause != shouldShowPause)
        {
            showPause = shouldShowPause;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto c = r.getCentre();
        const float d = std::min (r.getWidth(), r.getHeight());
        const auto circle = juce::Rectangle<float> (d, d).withCentre (c);

        auto fill = isEnabled() ? colours::accent : colours::textFaint;
        if (isButtonDown)
            fill = fill.darker (0.2f);
        else if (isMouseOver)
            fill = hoverTint (fill, 0.15f);
        g.setColour (fill);
        g.fillEllipse (circle);

        g.setColour (isLightTheme() ? juce::Colours::white : colours::background);
        const float s = d * 0.22f;
        if (showPause)
        {
            g.fillRoundedRectangle (c.x - s * 0.95f, c.y - s, s * 0.7f, s * 2.0f, 1.5f);
            g.fillRoundedRectangle (c.x + s * 0.25f, c.y - s, s * 0.7f, s * 2.0f, 1.5f);
        }
        else
        {
            juce::Path p;
            p.addTriangle (c.x - s * 0.7f, c.y - s * 1.1f, c.x + s * 1.2f, c.y, c.x - s * 0.7f, c.y + s * 1.1f);
            g.fillPath (p);
        }
    }

private:
    bool showPause = false;
};

//==============================================================================
WaveformPanel::WaveformPanel() : Panel ("Forma d'onda"_t, "panel:waveform")
{
    playButton = std::make_unique<PlayButton>();
    addAndMakeVisible (*playButton);
    playButton->setEnabled (false);
    playButton->setWantsKeyboardFocus (false);   // la barra spaziatrice resta al contenitore (play/pausa)
    playButton->setTooltip ("Ascolta / pausa (barra spaziatrice)"_t);
    playButton->onClick = [this] { if (onPlayPause) onPlayPause(); };
}

WaveformPanel::~WaveformPanel() = default;

void WaveformPanel::resized()
{
    auto a = getContentArea().removeFromLeft (kTransportWidth);
    playButton->setBounds (a.removeFromTop (44.0f).withSizeKeepingCentre (40.0f, 40.0f).withX (a.getX()).toNearestInt());
}

void WaveformPanel::setTimeline (std::shared_ptr<const TrackTimeline> newTimeline)
{
    if (newTimeline == timeline)
        return;
    timeline = std::move (newTimeline);
    pathTimeline = nullptr;
    hoveredMarker = -1;
    playButton->setEnabled (timeline != nullptr && timeline->isValid());
    repaint();
}

void WaveformPanel::setMarkers (std::vector<Marker> newMarkers)
{
    markers = std::move (newMarkers);
    hoveredMarker = -1;
    repaint();
}

void WaveformPanel::setIgnored (std::set<juce::String> markerIds, std::array<bool, kNumMarkerTypes> types)
{
    ignoredIds = std::move (markerIds);
    ignoredTypes = types;
    repaint();
}

void WaveformPanel::setStatus (const juce::String& text, float progress)
{
    if (text == statusText && juce::approximatelyEqual (progress, statusProgress))
        return;
    statusText = text;
    statusProgress = progress;
    repaint();
}

void WaveformPanel::setPlayback (double positionSeconds, bool isPlaying)
{
    playButton->setPlaying (isPlaying);
    const bool moved = std::abs (timeToX (positionSeconds) - timeToX (position)) >= 0.5f;
    if (! moved && isPlaying == playing)
        return;
    position = positionSeconds;
    playing = isPlaying;
    repaint();
}

int WaveformPanel::getActiveMarkerCount() const
{
    return (int) std::count_if (markers.begin(), markers.end(), [this] (const Marker& m) { return ! isIgnored (m); });
}

bool WaveformPanel::isIgnored (const Marker& m) const
{
    return ignoredTypes[(size_t) m.type] || ignoredIds.count (m.id()) > 0;
}

juce::Colour WaveformPanel::colourFor (const Marker& m) const
{
    if (isIgnored (m))
        return colours::textFaint;
    return m.isCritical() ? colours::critical : colours::warning;
}

float WaveformPanel::timeToX (double seconds) const
{
    const double duration = timeline != nullptr ? timeline->durationSeconds() : 0.0;
    if (duration <= 0.0 || waveArea.isEmpty())
        return waveArea.getX();
    return waveArea.getX() + (float) (juce::jlimit (0.0, 1.0, seconds / duration)) * waveArea.getWidth();
}

double WaveformPanel::xToTime (float x) const
{
    const double duration = timeline != nullptr ? timeline->durationSeconds() : 0.0;
    if (waveArea.getWidth() <= 0.0f)
        return 0.0;
    return juce::jlimit (0.0, duration, (double) ((x - waveArea.getX()) / waveArea.getWidth()) * duration);
}

//==============================================================================
void WaveformPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    // titolo: legenda dei tipi di problema a destra
    auto titleRow = getLocalBounds().toFloat().reduced (14.0f, 10.0f).removeFromTop (16.0f);
    paintLegend (g, titleRow);

    paintTransport (g, area.removeFromLeft (kTransportWidth));
    area.removeFromLeft (10.0f);

    auto axis = area.removeFromBottom (kAxisHeight);
    waveArea = area;
    fillWell (g, area, 6.0f);
    addHelp (area, "wave:overview");

    if (timeline == nullptr || ! timeline->isValid())
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::value (14.0f));
        auto text = area.reduced (16.0f);
        g.drawText (statusText.isNotEmpty() ? statusText : "Nessun brano analizzato"_t, text.removeFromTop (text.getHeight() * 0.5f + 8.0f),
                    juce::Justification::centredBottom);
        if (statusProgress >= 0.0f)
        {
            auto bar = text.withTrimmedTop (10.0f).removeFromTop (6.0f).withSizeKeepingCentre (std::min (360.0f, text.getWidth()), 6.0f);
            drawHBar (g, bar, statusProgress, 0.0f, 1.0f, colours::accent);
        }
        return;
    }

    // tratto collegato alla diagnosi sotto il mouse nel report
    if (data != nullptr && data->highlightTimeStart >= 0.0f && data->highlightTimeEnd > data->highlightTimeStart)
    {
        const float x0 = timeToX (data->highlightTimeStart), x1 = timeToX (data->highlightTimeEnd);
        g.setColour (data->highlightColour.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, area.getY(), std::max (x1, x0 + 2.0f), area.getBottom()));
    }

    // zone (sezioni) sotto la forma d'onda, segni puntuali sopra
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        if (! m.isZone())
            continue;
        const bool hovered = (int) i == hoveredMarker;
        const auto colour = colourFor (m);
        const float x0 = timeToX (m.start), x1 = std::max (timeToX (m.end), x0 + 2.0f);
        const auto band = juce::Rectangle<float>::leftTopRightBottom (x0, area.getY(), x1, area.getBottom());
        g.setColour (colour.withAlpha (isIgnored (m) ? 0.05f : (hovered ? 0.24f : 0.13f)));
        g.fillRect (band);
        g.setColour (colour.withAlpha (isIgnored (m) ? 0.35f : 0.85f));
        g.fillRect (band.withHeight (3.0f));
        if (band.getWidth() > 70.0f)
        {
            g.setFont (fonts::label (10.0f));
            g.drawText (shortTypeName (m.type), band.withTrimmedTop (4.0f).withHeight (13.0f).reduced (5.0f, 0.0f),
                        juce::Justification::centredLeft, true);
        }
    }

    paintWave (g);

    // posizione del lettore
    {
        const float x = timeToX (position);
        g.setColour (colours::text.withAlpha (0.9f));
        g.fillRect (x - 0.75f, area.getY(), 1.5f, area.getHeight());
        juce::Path tip;
        tip.addTriangle (x - 5.0f, area.getY(), x + 5.0f, area.getY(), x, area.getY() + 6.0f);
        g.fillPath (tip);
    }

    paintMarkers (g);

    // cursore del mouse con il tempo
    if (hoverX >= area.getX() && hoverX <= area.getRight() && hoveredMarker < 0)
    {
        g.setColour (colours::textDim.withAlpha (0.6f));
        g.fillRect (hoverX - 0.5f, area.getY(), 1.0f, area.getHeight());
        const auto text = formatTimelineTime (xToTime (hoverX));
        g.setFont (fonts::value (11.0f));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 10.0f;
        auto box = juce::Rectangle<float> (w, 16.0f).withPosition (hoverX + 4.0f, area.getBottom() - 20.0f);
        if (box.getRight() > area.getRight())
            box.setX (hoverX - 4.0f - w);
        g.setColour (colours::readout);
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colours::text);
        g.drawText (text, box, juce::Justification::centred);
    }

    // asse dei tempi
    const double duration = timeline->durationSeconds();
    double step = 600.0;
    for (double candidate : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
        if (duration > 0.0 && area.getWidth() * (float) (candidate / duration) >= 70.0f)
        {
            step = candidate;
            break;
        }
    g.setFont (fonts::label (10.0f));
    for (double t = 0.0; t <= duration; t += step)
    {
        const float x = timeToX (t);
        g.setColour (colours::grid);
        g.fillRect (x - 0.5f, axis.getY(), 1.0f, 3.0f);
        g.setColour (colours::textFaint);
        g.drawText (formatTimelineTime (t, false), juce::Rectangle<float> (x - 30.0f, axis.getY() + 2.0f, 60.0f, 12.0f),
                    juce::Justification::centred);
    }
}

void WaveformPanel::rebuildWavePaths()
{
    pathArea = waveArea;
    pathTimeline = timeline.get();
    peakPath.clear();
    rmsPath.clear();

    const auto& wave = timeline->wave;
    const int columns = std::max (1, (int) waveArea.getWidth());
    const auto points = wave.size();
    const float mid = waveArea.getCentreY(), half = waveArea.getHeight() * 0.46f;

    std::vector<WavePoint> cols ((size_t) columns);
    for (int x = 0; x < columns; ++x)
    {
        const auto p0 = (size_t) ((double) x / columns * (double) points);
        const auto p1 = std::max (p0 + 1, (size_t) ((double) (x + 1) / columns * (double) points));
        WavePoint c { 0.0f, 0.0f, 0.0f };
        for (size_t p = p0; p < std::min (p1, points); ++p)
        {
            c.min = std::min (c.min, wave[p].min);
            c.max = std::max (c.max, wave[p].max);
            c.rms = std::max (c.rms, wave[p].rms);
        }
        cols[(size_t) x] = c;
    }

    auto y = [mid, half] (float v) { return mid - juce::jlimit (-1.0f, 1.0f, v) * half; };
    const float left = waveArea.getX();

    peakPath.startNewSubPath (left, y (cols[0].max));
    for (int x = 1; x < columns; ++x)
        peakPath.lineTo (left + (float) x, y (cols[(size_t) x].max));
    for (int x = columns - 1; x >= 0; --x)
        peakPath.lineTo (left + (float) x, y (cols[(size_t) x].min));
    peakPath.closeSubPath();

    rmsPath.startNewSubPath (left, y (cols[0].rms));
    for (int x = 1; x < columns; ++x)
        rmsPath.lineTo (left + (float) x, y (cols[(size_t) x].rms));
    for (int x = columns - 1; x >= 0; --x)
        rmsPath.lineTo (left + (float) x, y (-cols[(size_t) x].rms));
    rmsPath.closeSubPath();
}

void WaveformPanel::paintWave (juce::Graphics& g)
{
    if (pathTimeline != timeline.get() || pathArea != waveArea)
        rebuildWavePaths();

    g.setColour (colours::grid);
    g.fillRect (waveArea.getX(), waveArea.getCentreY() - 0.5f, waveArea.getWidth(), 1.0f);

    // la parte già ascoltata è più accesa
    const float playX = timeToX (position);
    for (int part = 0; part < 2; ++part)
    {
        const bool played = part == 0;
        const auto clip = played ? waveArea.withRight (playX) : waveArea.withLeft (playX);
        if (clip.isEmpty())
            continue;
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (clip.toNearestIntEdges());
        g.setColour (colours::accent.withAlpha (played ? 0.55f : 0.32f));
        g.fillPath (peakPath);
        g.setColour (colours::accent.withAlpha (played ? 0.95f : 0.62f));
        g.fillPath (rmsPath);
    }
}

void WaveformPanel::paintMarkers (juce::Graphics& g)
{
    const float flagY = waveArea.getY() + 4.0f + kFlagHeight * 0.5f;

    // prima gli ignorati, così quelli attivi restano sopra
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t i = 0; i < markers.size(); ++i)
        {
            const auto& m = markers[i];
            if (m.isZone() || isIgnored (m) != (pass == 0))
                continue;

            const bool hovered = (int) i == hoveredMarker;
            const auto colour = colourFor (m);
            const float x0 = timeToX (m.start), x1 = timeToX (m.end);

            if (x1 - x0 > 3.0f)
            {
                g.setColour (colour.withAlpha (isIgnored (m) ? 0.06f : 0.2f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, waveArea.getY(), x1, waveArea.getBottom()));
            }

            g.setColour (colour.withAlpha (isIgnored (m) ? 0.4f : (hovered ? 1.0f : 0.8f)));
            g.fillRect (x0 - (hovered ? 1.0f : 0.6f), waveArea.getY() + kFlagHeight, hovered ? 2.0f : 1.2f,
                        waveArea.getHeight() - kFlagHeight);
            drawMarkerShape (g, m.type, { x0, flagY }, hovered ? kFlagHeight : kFlagHeight - 3.0f,
                             colour.withAlpha (isIgnored (m) ? 0.6f : 1.0f), isIgnored (m));
        }
    }
}

void WaveformPanel::paintLegend (juce::Graphics& g, juce::Rectangle<float> row)
{
    std::array<int, kNumMarkerTypes> counts {};
    for (const auto& m : markers)
        if (! isIgnored (m))
            ++counts[(size_t) m.type];

    // da destra a sinistra, nell'ordine dei tipi
    g.setFont (fonts::label (11.0f));
    const bool hasTrack = timeline != nullptr && timeline->isValid();
    for (int t = kNumMarkerTypes - 1; t >= 0; --t)
    {
        const auto type = (MarkerType) t;
        const auto text = shortTypeName (type) + " " + (hasTrack ? juce::String (counts[(size_t) t]) : juce::String ("-"));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 26.0f;
        auto chip = row.removeFromRight (w);
        row.removeFromRight (6.0f);
        legendChips[(size_t) t] = chip;

        const bool ignoredType = ignoredTypes[(size_t) t];
        const bool active = hasTrack && counts[(size_t) t] > 0 && ! ignoredType;
        const auto colour = ignoredType ? colours::textFaint : (active ? typeColour (type) : colours::ok);

        g.setColour (colour.withAlpha (active ? 0.16f : 0.08f));
        g.fillRoundedRectangle (chip, 8.0f);
        drawMarkerShape (g, type, { chip.getX() + 11.0f, chip.getCentreY() }, 8.0f, colour, ignoredType);
        g.setColour (active ? colours::text : colours::textDim);
        g.drawText (text, chip.withTrimmedLeft (19.0f), juce::Justification::centredLeft);
        if (ignoredType)
            g.fillRect (chip.getX() + 19.0f, chip.getCentreY(), w - 26.0f, 1.0f);
    }
    addHelp (legendChips[0].getUnion (legendChips[(size_t) kNumMarkerTypes - 1]), "wave:legend");

    // nome del file dopo il titolo
    if (hasTrack && data != nullptr && data->masterFileName.isNotEmpty())
    {
        const float titleWidth = juce::GlyphArrangement::getStringWidth (fonts::title(), title.toUpperCase()) + 28.0f;
        g.setColour (colours::textDim);
        g.setFont (fonts::label (12.0f));
        g.drawText (data->masterFileName, row.withTrimmedLeft (titleWidth).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
    }
}

void WaveformPanel::paintTransport (juce::Graphics& g, juce::Rectangle<float> area)
{
    addHelp (area, "wave:transport");
    area.removeFromTop (48.0f);
    const bool hasTrack = timeline != nullptr && timeline->isValid();

    g.setColour (hasTrack ? colours::text : colours::textFaint);
    g.setFont (fonts::value (16.0f));
    g.drawText (formatTimelineTime (hasTrack ? position : 0.0), area.removeFromTop (20.0f), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (fonts::label (11.0f));
    g.drawText ("/ " + formatTimelineTime (hasTrack ? timeline->durationSeconds() : 0.0), area.removeFromTop (14.0f),
                juce::Justification::centredLeft);

    if (hasTrack && area.getHeight() >= 30.0f)
    {
        area.removeFromTop (8.0f);
        const int active = getActiveMarkerCount();
        g.setColour (active > 0 ? colours::warning : colours::ok);
        g.setFont (fonts::label (11.0f));
        g.drawFittedText (active > 0 ? "%d da controllare"_t.replace ("%d", juce::String (active)) : "Nessun problema"_t,
                          area.removeFromTop (28.0f).toNearestInt(), juce::Justification::topLeft, 2);
    }
}

//==============================================================================
int WaveformPanel::markerAt (juce::Point<float> p) const
{
    if (timeline == nullptr || ! waveArea.expanded (4.0f, 0.0f).contains (p))
        return -1;

    // i segni puntuali vincono sulle zone; tra più segni vicini il più vicino (gli attivi prima degli ignorati)
    int best = -1;
    float bestDistance = 6.0f;
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        if (m.isZone())
            continue;
        const float x0 = timeToX (m.start), x1 = std::max (timeToX (m.end), x0);
        const float distance = (p.x < x0 ? x0 - p.x : (p.x > x1 ? p.x - x1 : 0.0f)) + (isIgnored (m) ? 2.0f : 0.0f);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = (int) i;
        }
    }
    if (best >= 0)
        return best;

    // zone: solo nella striscia in alto, così il resto della forma d'onda resta cliccabile per ascoltare
    if (p.y > waveArea.getY() + 18.0f)
        return -1;
    for (size_t i = 0; i < markers.size(); ++i)
        if (markers[i].isZone() && p.x >= timeToX (markers[i].start) && p.x <= timeToX (markers[i].end))
            return (int) i;
    return -1;
}

int WaveformPanel::legendAt (juce::Point<float> p) const
{
    for (int t = 0; t < kNumMarkerTypes; ++t)
        if (legendChips[(size_t) t].contains (p))
            return t;
    return -1;
}

void WaveformPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const int marker = markerAt (p);
    const float x = waveArea.contains (p) ? p.x : -1.0f;
    const bool clickable = marker >= 0 || legendAt (p) >= 0 || (x >= 0.0f && timeline != nullptr);
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

    if (marker != hoveredMarker || ! juce::approximatelyEqual (x, hoverX))
    {
        hoveredMarker = marker;
        hoverX = x;
        repaint();
    }
}

void WaveformPanel::mouseExit (const juce::MouseEvent&)
{
    hoveredMarker = -1;
    hoverX = -1.0f;
    repaint();
}

void WaveformPanel::mouseDown (const juce::MouseEvent& e)
{
    if (timeline == nullptr || ! timeline->isValid())
        return;

    if (const int type = legendAt (e.position); type >= 0)
    {
        if (onIgnoreType)
            onIgnoreType ((MarkerType) type, ! ignoredTypes[(size_t) type]);
        return;
    }

    if (const int marker = markerAt (e.position); marker >= 0)
    {
        showMarkerMenu (marker);
        return;
    }

    if (waveArea.contains (e.position) && onSeek)
        onSeek (xToTime (e.position.x));
}

void WaveformPanel::mouseDrag (const juce::MouseEvent& e)
{
    // trascinando sulla forma d'onda si sposta la posizione di ascolto
    if (timeline == nullptr || ! waveArea.contains (e.mouseDownPosition) || markerAt (e.mouseDownPosition) >= 0)
        return;
    hoverX = juce::jlimit (waveArea.getX(), waveArea.getRight(), e.position.x);
    if (onSeek)
        onSeek (xToTime (hoverX));
}

void WaveformPanel::showMarkerMenu (int index)
{
    const auto m = markers[(size_t) index];
    const bool ignoredOne = ignoredIds.count (m.id()) > 0;
    const bool ignoredType = ignoredTypes[(size_t) m.type];
    const int sameType = (int) std::count_if (markers.begin(), markers.end(), [&] (const Marker& other) { return other.type == m.type; });

    juce::PopupMenu menu;
    menu.addSectionHeader (markerTypeName (m.type) + "  " + formatTimelineTime (m.start));
    menu.addItem ("Ascolta da qui (2 s prima)"_t, [this, m] { if (onListenFrom) onListenFrom (std::max (0.0, m.start - 2.0)); });
    menu.addSeparator();
    if (ignoredOne)
        menu.addItem ("Ripristina questo segno"_t, [this, m] { if (onIgnoreMarker) onIgnoreMarker (m, false); });
    else
        menu.addItem ("Ignora questo segno (scelta voluta)"_t, ! ignoredType, false, [this, m] { if (onIgnoreMarker) onIgnoreMarker (m, true); });

    const auto typeName = markerTypeName (m.type);
    if (ignoredType)
        menu.addItem ("Ripristina tutti"_t + ": " + typeName, [this, m] { if (onIgnoreType) onIgnoreType (m.type, false); });
    else
        menu.addItem ("Ignora tutti"_t + ": " + typeName + " (" + juce::String (sameType) + ")",
                      [this, m] { if (onIgnoreType) onIgnoreType (m.type, true); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::String WaveformPanel::getTooltip()
{
    if (data != nullptr && data->helpMode)
        return Panel::getTooltip();

    const auto p = getMouseXYRelative().toFloat();
    if (const int marker = markerAt (p); marker >= 0)
    {
        const auto& m = markers[(size_t) marker];
        return describeMarker (m) + "\n\n" + (isIgnored (m) ? "Ignorato: clic per ripristinarlo"_t : "Clic: ascolta da qui o ignora"_t);
    }
    if (legendAt (p) >= 0)
        return "Clic per ignorare o ripristinare tutti i segni di questo tipo"_t;
    return {};
}

} // namespace ma::ui
