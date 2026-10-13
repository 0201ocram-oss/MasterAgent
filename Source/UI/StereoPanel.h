#pragma once

#include "Theme.h"

namespace ma::ui
{

class StereoPanel : public Panel
{
public:
    StereoPanel() : Panel ("Stereo e fase", "panel:stereo") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    /** Selettore d'ascolto Stereo | Mono | Side (Stereo = monitor spento). */
    std::function<void (MonitorChoice)> onMonitorChanged;

private:
    void drawGoniometer (juce::Graphics& g, juce::Rectangle<float> r);
    void drawMonitorSelector (juce::Graphics& g, juce::Rectangle<float> area);

    std::array<juce::Rectangle<float>, 3> monitorButtons;   // Stereo, Mono, Side (geometria dell'ultimo paint)
};

class StreamingPanel : public Panel
{
public:
    StreamingPanel() : Panel ("Anteprima normalizzazione streaming", "panel:streaming") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

} // namespace ma::ui
