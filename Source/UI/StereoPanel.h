#pragma once

#include "Theme.h"

namespace ma::ui
{

class StereoPanel : public Panel
{
public:
    StereoPanel() : Panel ("Stereo e fase", "panel:stereo") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;

private:
    void drawGoniometer (juce::Graphics& g, juce::Rectangle<float> r);
};

class StreamingPanel : public Panel
{
public:
    StreamingPanel() : Panel ("Anteprima normalizzazione streaming", "panel:streaming") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

} // namespace ma::ui
