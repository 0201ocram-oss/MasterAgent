#pragma once

#include "Theme.h"
#include "../Analysis/Timeline.h"

#include <set>

namespace ma::ui
{

/**
    Forma d'onda del brano analizzato (app standalone) con i problemi segnati nel punto in cui avvengono.
    Clic sulla forma d'onda: ascolta da lì. Clic su un segno: ascolta, ignora questo o tutti quelli dello stesso tipo.
    Clic su una voce della legenda: ignora (o ripristina) tutti i segni di quel tipo.
*/
class WaveformPanel : public Panel
{
public:
    WaveformPanel();
    ~WaveformPanel() override;

    void setTimeline (std::shared_ptr<const TrackTimeline> timeline);
    void setMarkers (std::vector<Marker> markers);
    void setIgnored (std::set<juce::String> markerIds, std::array<bool, kNumMarkerTypes> types);
    /** Testo al posto della forma d'onda (analisi in corso, errore); progress < 0 = senza barra. */
    void setStatus (const juce::String& text, float progress);
    void setPlayback (double positionSeconds, bool isPlaying);

    std::function<void (double seconds)> onSeek;
    std::function<void()> onPlayPause;
    std::function<void (double seconds)> onListenFrom;
    std::function<void (const Marker&, bool ignore)> onIgnoreMarker;
    std::function<void (MarkerType, bool ignore)> onIgnoreType;

    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    /** Segni attivi (non ignorati). */
    int getActiveMarkerCount() const;

private:
    class PlayButton;

    bool isIgnored (const Marker& m) const;
    juce::Colour colourFor (const Marker& m) const;
    float timeToX (double seconds) const;
    double xToTime (float x) const;
    int markerAt (juce::Point<float> p) const;
    int legendAt (juce::Point<float> p) const;
    void showMarkerMenu (int index);
    void rebuildWavePaths();

    void paintWave (juce::Graphics& g);
    void paintMarkers (juce::Graphics& g);
    void paintLegend (juce::Graphics& g, juce::Rectangle<float> row);
    void paintTransport (juce::Graphics& g, juce::Rectangle<float> area);

    std::shared_ptr<const TrackTimeline> timeline;
    std::vector<Marker> markers;
    std::set<juce::String> ignoredIds;
    std::array<bool, kNumMarkerTypes> ignoredTypes {};

    juce::String statusText;
    float statusProgress = -1.0f;
    double position = 0.0;
    bool playing = false;

    int hoveredMarker = -1;
    float hoverX = -1.0f;

    // geometria dell'ultimo paint, per il mouse
    juce::Rectangle<float> waveArea;
    std::array<juce::Rectangle<float>, kNumMarkerTypes> legendChips;

    // forma d'onda in pixel, rigenerata quando cambiano brano o dimensione
    juce::Path peakPath, rmsPath;
    juce::Rectangle<float> pathArea;
    const TrackTimeline* pathTimeline = nullptr;

    std::unique_ptr<PlayButton> playButton;

    static constexpr float kFlagHeight = 13.0f;
};

} // namespace ma::ui
