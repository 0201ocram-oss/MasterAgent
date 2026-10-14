#pragma once

#include "Theme.h"

namespace ma::ui
{

/**
    Pannelli della dashboard del brano e loro disposizione, comuni all'editor e all'anteprima PNG.
    I pannelli classici e quelli vintage si alternano secondo lo stile; streaming e report si possono nascondere
    dal menu: la finestra resta della stessa dimensione e gli altri pannelli si allargano a occupare lo spazio.
*/
struct DashboardPanels
{
    // stile classico
    Panel* loudness = nullptr;
    Panel* peak = nullptr;
    Panel* dynamics = nullptr;
    Panel* spectrum = nullptr;
    Panel* stereo = nullptr;

    // stile vintage
    Panel* meters = nullptr;
    Panel* tonalEq = nullptr;
    Panel* analyzer = nullptr;
    Panel* scope = nullptr;

    // comuni
    Panel* streaming = nullptr;
    Panel* report = nullptr;
    Panel* waveform = nullptr;   // solo standalone

    /** Tutti i pannelli presenti (per dati, ombre e immagini). */
    std::vector<Panel*> all() const;

    /** Visibilità: dashboard del brano mostrata o no, stile e pannelli scelti dal menu. */
    void setVisibility (bool trackScreen, const UiSettings& settings) const;

    /** Dispone i pannelli in area (sotto la barra in alto e i banner). */
    void layout (juce::Rectangle<int> area, const UiSettings& settings) const;
};

} // namespace ma::ui
