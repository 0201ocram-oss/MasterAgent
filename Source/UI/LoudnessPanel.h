#pragma once

#include "Theme.h"

namespace ma::ui
{

class LoudnessPanel : public Panel
{
public:
    LoudnessPanel() : Panel ("Loudness  (EBU R128 / BS.1770-4)", "panel:loudness") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void setData (const DashboardData& d) override;

private:
    void drawHistory (juce::Graphics& g, juce::Rectangle<float> r);
};

class PeakPanel : public Panel
{
public:
    PeakPanel() : Panel ("Peak & tecnici", "panel:peak") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

class DynamicsPanel : public Panel
{
public:
    DynamicsPanel() : Panel ("Dinamica", "panel:dynamics") {}
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

} // namespace ma::ui
