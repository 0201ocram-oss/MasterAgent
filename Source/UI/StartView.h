#pragma once

#include "Theme.h"

namespace ma::ui
{

/**
    Schermata iniziale dell'app standalone: analizzare un brano oppure confrontare più brani (album).
    Le due schede si cliccano; i file si possono anche trascinare sulla finestra.
*/
class StartView : public juce::Component
{
public:
    StartView();

    std::function<void()> onAnalyseTrack, onCompareAlbum;

    /** Mostra il collegamento all'album già analizzato. */
    void setAlbumAvailable (bool available, int numTracks);
    std::function<void()> onShowAlbum;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    enum Card { none = -1, track = 0, album = 1, lastAlbum = 2 };

    Card cardAt (juce::Point<int> p) const;
    void paintCard (juce::Graphics& g, juce::Rectangle<float> r, Card card);

    std::array<juce::Rectangle<int>, 2> cards;
    juce::Rectangle<int> albumLink;
    bool albumAvailable = false;
    int albumTracks = 0;
    Card hovered = none;
};

} // namespace ma::ui
