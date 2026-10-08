#pragma once

#include "Theme.h"

namespace ma::ui
{

/**
    Spettro long-term + istantaneo con overlay della curva target/reference e scostamenti per banda.
    Hover su una banda (o sullo spettro) evidenzia la zona di frequenze corrispondente.
*/
class SpectrumPanel : public Panel
{
public:
    SpectrumPanel() : Panel ("Spettro e bilanciamento tonale", "panel:spectrum") {}

    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

    /** Clic sul selettore L+R | M/S. */
    std::function<void()> onToggleMidSide;

private:
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r);
    void drawBandDeltas (juce::Graphics& g, juce::Rectangle<float> r);

    /** Vista Mid/Side: spettro del Mid e del Side, bassi troppo larghi evidenziati. */
    template <typename YFor, typename BuildPath>
    void drawMidSide (juce::Graphics& g, juce::Rectangle<float> plot, YFor yFor, BuildPath buildPath);
    int bandToHighlight() const;

    // geometria dell'ultimo paint, per l'hit-test del mouse
    juce::Rectangle<float> plotArea;
    juce::Rectangle<float> viewToggle;   // selettore L+R | M/S
    std::array<juce::Rectangle<float>, kNumBands> bandCells;

    int hoveredBand = -1;
    float cursorFrequency = 0.0f;   // > 0 se il mouse è sullo spettro
};

} // namespace ma::ui
