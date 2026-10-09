#include "StartView.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    /** Icona della scheda "brano": una forma d'onda con un segno di problema. */
    void drawTrackIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        juce::Path wave;
        const int bars = 15;
        const float w = r.getWidth() / (float) bars;
        for (int i = 0; i < bars; ++i)
        {
            const float h = r.getHeight() * (0.25f + 0.75f * std::abs (std::sin ((float) i * 0.9f + 0.4f)) * (i % 4 == 2 ? 1.0f : 0.7f));
            wave.addRoundedRectangle (r.getX() + (float) i * w + w * 0.2f, r.getCentreY() - h * 0.5f, w * 0.6f, h, w * 0.3f);
        }
        g.setColour (colour);
        g.fillPath (wave);

        const float x = r.getX() + 10.5f * w;
        g.setColour (colours::critical);
        g.fillRect (x - 1.0f, r.getY() - 4.0f, 2.0f, r.getHeight() + 8.0f);
        g.fillRect (x - 4.0f, r.getY() - 8.0f, 8.0f, 8.0f);
    }

    /** Icona della scheda "album": più brani affiancati con livelli diversi. */
    void drawAlbumIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        const float levels[] { 0.7f, 0.82f, 0.62f, 0.78f, 0.74f };
        const float w = r.getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float h = r.getHeight() * levels[i];
            auto bar = juce::Rectangle<float> (r.getX() + (float) i * w + w * 0.15f, r.getBottom() - h, w * 0.7f, h);
            g.setColour (colour.withAlpha (i == 2 ? 1.0f : 0.55f));
            g.fillRoundedRectangle (bar, 3.0f);
        }
        g.setColour (colours::target);
        const float y = r.getBottom() - r.getHeight() * 0.75f;
        g.fillRect (r.getX(), y - 0.75f, r.getWidth(), 1.5f);
    }
}

StartView::StartView()
{
    setInterceptsMouseClicks (true, false);
}

void StartView::setAlbumAvailable (bool available, int numTracks)
{
    if (available == albumAvailable && numTracks == albumTracks)
        return;
    albumAvailable = available;
    albumTracks = numTracks;
    resized();
    repaint();
}

void StartView::resized()
{
    const auto r = getLocalBounds();
    const int cardW = std::min (400, (r.getWidth() - 3 * 40) / 2), cardH = 250, gap = 36;
    const int x = r.getCentreX() - cardW - gap / 2;
    const int y = r.getCentreY() - cardH / 2 + 10;
    cards[0] = { x, y, cardW, cardH };
    cards[1] = { x + cardW + gap, y, cardW, cardH };
    albumLink = albumAvailable ? juce::Rectangle<int> (r.getCentreX() - 160, cards[0].getBottom() + 64, 320, 28) : juce::Rectangle<int>();
}

void StartView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    g.setColour (colours::text);
    g.setFont (fonts::value (26.0f));
    g.drawText ("Cosa vuoi analizzare?"_t, juce::Rectangle<float> (r.getX(), (float) cards[0].getY() - 92.0f, r.getWidth(), 34.0f),
                juce::Justification::centred);
    g.setColour (colours::textDim);
    g.setFont (fonts::label (14.0f));
    g.drawText ("Scegli una modalità, oppure trascina i file audio sulla finestra"_t,
                juce::Rectangle<float> (r.getX(), (float) cards[0].getY() - 54.0f, r.getWidth(), 20.0f), juce::Justification::centred);

    paintCard (g, cards[0].toFloat(), track);
    paintCard (g, cards[1].toFloat(), album);

    g.setColour (colours::textFaint);
    g.setFont (fonts::label (12.0f));
    g.drawText ("WAV, AIFF, FLAC, MP3, OGG - fino a 20 minuti per brano"_t,
                juce::Rectangle<float> (r.getX(), (float) cards[0].getBottom() + 22.0f, r.getWidth(), 18.0f), juce::Justification::centred);

    if (albumAvailable)
    {
        const auto link = albumLink.toFloat();
        g.setColour (hovered == lastAlbum ? colours::accent : colours::accent.withAlpha (0.8f));
        g.setFont (fonts::value (13.0f));
        const auto text = "Torna all'album analizzato (%d brani)"_t.replace ("%d", juce::String (albumTracks));
        g.drawText (text, link, juce::Justification::centred);
        if (hovered == lastAlbum)
        {
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            g.fillRect (link.getCentreX() - w * 0.5f, link.getBottom() - 5.0f, w, 1.0f);
        }
    }
}

void StartView::paintCard (juce::Graphics& g, juce::Rectangle<float> r, Card card)
{
    const bool isHovered = hovered == card;

    if (getUiSettings().effects)
    {
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (r, 14.0f);
        juce::DropShadow (colours::shadow, isHovered ? 22 : 14, { 0, isHovered ? 8 : 4 }).drawForPath (g, shadowPath);
        g.setGradientFill (juce::ColourGradient (colours::panelTop, 0.0f, r.getY(), colours::panelBottom, 0.0f, r.getBottom(), false));
    }
    else
    {
        g.setColour (colours::panel);
    }
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (isHovered ? colours::accent : colours::panelBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, isHovered ? 2.0f : 1.0f);

    auto inner = r.reduced (28.0f, 24.0f);
    auto icon = inner.removeFromTop (58.0f).withSizeKeepingCentre (card == track ? 150.0f : 110.0f, 50.0f);
    if (card == track)
        drawTrackIcon (g, icon, colours::accent);
    else
        drawAlbumIcon (g, icon, colours::accent2);

    inner.removeFromTop (18.0f);
    g.setColour (colours::text);
    g.setFont (fonts::value (19.0f));
    g.drawText (card == track ? "Analizza un brano"_t : "Confronta più brani (album)"_t, inner.removeFromTop (26.0f),
                juce::Justification::centred);

    inner.removeFromTop (8.0f);
    g.setColour (colours::textDim);
    g.setFont (fonts::label (13.0f));
    const auto text = card == track
        ? "Report completo del master, forma d'onda con i problemi segnati nel punto in cui avvengono e ascolto dal punto che ti interessa."_t
        : "Da 2 a 30 brani confrontati con il resto dell'album: loudness, tono, larghezza e picchi, per un disco coerente."_t;
    g.drawFittedText (text, inner.removeFromTop (54.0f).toNearestInt(), juce::Justification::centredTop, 3, 1.0f);

    g.setColour (isHovered ? colours::accent : colours::accent.withAlpha (0.75f));
    g.setFont (fonts::value (13.0f));
    g.drawText (card == track ? "Scegli il file..."_t : "Scegli i file..."_t, inner.removeFromBottom (18.0f), juce::Justification::centred);
}

StartView::Card StartView::cardAt (juce::Point<int> p) const
{
    if (cards[0].contains (p)) return track;
    if (cards[1].contains (p)) return album;
    if (albumAvailable && albumLink.contains (p)) return lastAlbum;
    return none;
}

void StartView::mouseMove (const juce::MouseEvent& e)
{
    const auto card = cardAt (e.getPosition());
    setMouseCursor (card != none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (card != hovered)
    {
        hovered = card;
        repaint();
    }
}

void StartView::mouseExit (const juce::MouseEvent&)
{
    hovered = none;
    repaint();
}

void StartView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;

    switch (cardAt (e.getPosition()))
    {
        case track:     if (onAnalyseTrack) onAnalyseTrack(); break;
        case album:     if (onCompareAlbum) onCompareAlbum(); break;
        case lastAlbum: if (onShowAlbum) onShowAlbum(); break;
        case none:      break;
    }
}

} // namespace ma::ui
