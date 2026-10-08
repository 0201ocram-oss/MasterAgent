#pragma once

#include "Theme.h"
#include "../Compare/AlbumCheck.h"

namespace ma::ui
{

/**
    Coerenza album: pannello sopra la dashboard (dentro il contenitore, quindi segue tema, zoom e schermo intero).
    A sinistra la tabella dei brani con le celle colorate per gravità, a destra le diagnosi del brano selezionato.
    Clic su una riga per selezionarla, doppio clic per aprirla nella dashboard come file master.
*/
class AlbumView : public Panel
{
public:
    AlbumView();

    void setAlbum (const AlbumResult& result, const juce::StringArray& skipped);
    void setProgress (bool isAnalysing, const juce::String& statusText, float progress);

    int getSelectedTrack() const noexcept { return selected; }

    std::function<void()> onClose, onClear, onExport;
    std::function<void (int index)> onShowTrack;

    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    struct Columns
    {
        float index, name, integrated, loudest, delta, gain, truePeak, width, band, status;
    };
    Columns columnsFor (float width) const;

    void paintTable (juce::Graphics& g, juce::Rectangle<float> area);
    void paintDetails (juce::Graphics& g, juce::Rectangle<float> area);
    void paintProgress (juce::Graphics& g, juce::Rectangle<float> area);
    int rowAt (juce::Point<float> p) const;
    void select (int index);
    void updateButtons();

    AlbumResult album;
    juce::StringArray skipped;
    bool analysing = false;
    juce::String status;
    float progressValue = 0.0f;

    int selected = -1, hovered = -1;
    float tableScroll = 0.0f, detailScroll = 0.0f;
    float tableContentHeight = 0.0f, detailContentHeight = 0.0f;

    // geometria dell'ultimo paint, per il mouse
    juce::Rectangle<float> rowsArea, detailArea;

    juce::TextButton showButton, exportButton, clearButton, closeButton;

    static constexpr float kRowHeight = 26.0f;
    static constexpr float kBarHeight = 30.0f;
};

} // namespace ma::ui
