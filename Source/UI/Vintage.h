#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Elementi grafici dello stile vintage (frontalini da rack): display a sette segmenti, spie, viti,
    finestre incassate. Colori fissi o passati da chi disegna: non dipendono dal tema.
*/
namespace ma::ui::vintage
{

/** Bahnschrift, il carattere DIN dei frontalini (su Windows dal 2017); se manca, quello di sistema. */
juce::Font font (float size, bool bold = false);

/** Testo serigrafato: maiuscolo con spaziatura larga. */
juce::Font silkscreen (float size);

/**
    Display a sette segmenti (LED). Il testo (cifre, '-', '+', ' ', '.') è allineato a destra su numCells cifre:
    il punto decimale si accende accanto alla cifra precedente, le cifre spente restano appena visibili.
    Restituisce l'area occupata dalle cifre.
*/
juce::Rectangle<float> drawSevenSegment (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                                         juce::Colour colour, int numCells);

/** Larghezza delle cifre per un'altezza data (per dimensionare le finestre dei display). */
float sevenSegmentWidth (float height, int numCells);

/** Finestra incassata di un display, con il vetro. */
void drawDisplayWindow (juce::Graphics& g, juce::Rectangle<float> r);

/** Spia rotonda: accesa con alone, spenta come vetro scuro del suo colore. */
void drawLamp (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, bool lit);

/** Tasto in rilievo: convesso a riposo, incassato quando è premuto o attivo. flat*: lato unito al tasto accanto. */
void drawKey (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour base, bool pressed,
              bool flatLeft = false, bool flatRight = false);

/** Vite a taglio (angolo in radianti). */
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle);

/** Colore della spia per un colore qualsiasi (stessa tinta, piena luminosità). */
juce::Colour lampColour (juce::Colour c);

} // namespace ma::ui::vintage
