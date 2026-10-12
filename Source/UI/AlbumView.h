#pragma once

#include "Theme.h"
#include "../Compare/AlbumCheck.h"

namespace ma::ui
{

/**
    Coerenza album: pannello sopra la dashboard (dentro il contenitore, quindi segue tema, zoom e schermo intero).
    A sinistra la tabella dei brani con le celle colorate per gravità, a destra le diagnosi del brano selezionato.
    Clic su una riga per selezionarla, doppio clic per aprirla nella dashboard come file master, × o Canc per
    toglierla dall'album. In fondo all'elenco i file in analisi e la riga "+ Aggiungi brano".
*/
class AlbumView : public Panel
{
public:
    AlbumView();

    void setAlbum (const AlbumResult& result, const juce::StringArray& skipped);
    /** File in attesa di analisi: il primo è quello in corso, con il suo avanzamento (0..1). */
    void setPending (const juce::StringArray& names, float currentProgress);

    int getSelectedTrack() const noexcept { return selected; }
    juce::Component& getExportButton() noexcept { return exportButton; }
    /** Immagine della tabella per l'export: tutti i brani, senza diagnosi, selezione, × e riga per aggiungere. */
    juce::Image createTableImage();

    std::function<void()> onClose, onClear, onStop, onExport, onAddTracks;
    std::function<void (int index)> onShowTrack, onRemoveTrack;

    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    struct Columns
    {
        float index, name, integrated, loudest, delta, gain, truePeak, width, band, status, remove;
    };
    Columns columnsFor (float width) const;

    void paintTable (juce::Graphics& g, juce::Rectangle<float> area);
    void paintDetails (juce::Graphics& g, juce::Rectangle<float> area);

    /** Elementi dell'elenco sotto il mouse: brano (indice), sua × di rimozione, riga "+ Aggiungi brano". */
    struct Hit { int row = -1; bool remove = false; bool add = false; };
    Hit hitTest (juce::Point<float> p) const;
    juce::Rectangle<float> rowBounds (int index) const;
    bool canAdd() const noexcept { return (int) (album.tracks.size()) + pending.size() < album::maxTracks; }

    void select (int index);
    void removeSelected();
    void updateButtons();

    AlbumResult album;
    juce::StringArray skipped, pending;
    float pendingProgress = 0.0f;
    bool analysing() const noexcept { return ! pending.isEmpty(); }

    int selected = -1;
    Hit hovered;
    bool exporting = false;   // paint della tabella per l'immagine esportata
    float tableScroll = 0.0f, detailScroll = 0.0f;
    float tableContentHeight = 0.0f, detailContentHeight = 0.0f;

    // geometria dell'ultimo paint, per il mouse
    juce::Rectangle<float> rowsArea, detailArea, tableArea;
    float removeColumnX = 0.0f;

    juce::TextButton showButton, removeButton, exportButton, clearButton, closeButton;

    static constexpr float kRowHeight = 26.0f;
    static constexpr float kBarHeight = 30.0f;
};

} // namespace ma::ui
