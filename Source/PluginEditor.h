#pragma once

#include "PluginProcessor.h"
#include "UI/AlbumView.h"
#include "UI/HeaderBar.h"
#include "UI/LoudnessPanel.h"
#include "UI/ReportPanel.h"
#include "UI/SpectrumPanel.h"
#include "UI/StartView.h"
#include "UI/StereoPanel.h"
#include "UI/WaveformPanel.h"

#include <set>

class MasterAgentEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit MasterAgentEditor (MasterAgentProcessor&);
    ~MasterAgentEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    /**
        Contenitore della dashboard. Sta dentro l'editor, oppure (a schermo intero) in una finestra
        propria che copre il monitor: così lo schermo intero funziona in ogni host.
    */
    class Content : public juce::Component,
                    public juce::FileDragAndDropTarget
    {
    public:
        explicit Content (MasterAgentEditor& o) : owner (o) {}

        void paint (juce::Graphics& g) override                 { owner.paintDashboard (g); }
        void paintOverChildren (juce::Graphics& g) override     { owner.paintDragOverlay (g); }
        void resized() override                                 { owner.layoutDashboard(); }

        bool isInterestedInFileDrag (const juce::StringArray& files) override { return owner.isInterestedInFileDrag (files); }
        void fileDragEnter (const juce::StringArray&, int, int) override       { owner.dragHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override                  { owner.dragHover = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int x, int y) override { owner.filesDropped (files, { x, y }); }
        bool keyPressed (const juce::KeyPress& key) override                  { return owner.contentKeyPressed (key); }

    private:
        MasterAgentEditor& owner;
    };

    class FullScreenWindow;

    /** Mette la dashboard in area (coordinate del padre) con lo zoom dell'interfaccia. */
    void placeContent (juce::Rectangle<int> area);
    /** Limiti e dimensione della finestra per lo zoom attuale (la dimensione logica resta uguale). */
    void applyZoom();

    void paintDashboard (juce::Graphics&);
    void paintDragOverlay (juce::Graphics&);
    void layoutDashboard();
    bool isInterestedInFileDrag (const juce::StringArray& files) const;
    void filesDropped (const juce::StringArray& files, juce::Point<int> position);
    bool contentKeyPressed (const juce::KeyPress& key);

    // banner sotto la barra in alto: ascolto del reference
    int bannerCount() const;
    juce::Rectangle<int> bannerArea (int index) const;

    void timerCallback() override;
    void refreshTargets();
    void updateReferenceStatus();
    void updateComparison();
    void pushDataToPanels();
    void chooseReference();
    void saveReferenceAsProfile();
    void exportReport();

    // standalone: schermata iniziale, brano con forma d'onda e lettore, album
    enum class Screen { start, track, album };
    void showScreen (Screen newScreen);
    void startTrackAnalysis (const juce::File& file);
    void updateWaveform();
    ma::MarkerSettings currentMarkerSettings() const;
    void resetIgnoredMarkers();
    void togglePlayback();

    // brano da analizzare e versioni
    void chooseMasterFile();
    void saveVersion();
    void renameComparedVersion();
    void showOptionsMenu();

    // interfaccia: tema, guida, schermo intero
    void changeUiSettings (const ma::ui::UiSettings& settings);
    void refreshTheme();
    void setHelpMode (bool on);
    void setFullScreen (bool on);
    void setFullScreenAsync (bool on);
    bool isFullScreen() const noexcept { return fullScreenWindow != nullptr; }

    // profilo da più brani di riferimento
    void createProfileFromFiles();
    void startProfileJob (const juce::Array<juce::File>& files);
    void pollMultiReference();
    void askProfileName (std::vector<ma::AnalysisSnapshot> snapshots, const juce::StringArray& names, const juce::StringArray& skipped);

    // coerenza album
    void chooseAlbumFiles();
    void startAlbumCheck (const juce::Array<juce::File>& files);
    void updateAlbumView();
    void exportAlbum();

    std::array<ma::ui::Panel*, 7> allPanels();

    MasterAgentProcessor& processor;
    const bool standalone;
    juce::SharedResourcePointer<ma::ui::SharedLookAndFeel> sharedLookAndFeel;
    ma::ui::LookAndFeel& lookAndFeel { sharedLookAndFeel->lookAndFeel };
    Content content { *this };
    juce::TooltipWindow tooltips { &content, normalTooltipDelayMs };

    ma::ui::HeaderBar header;
    ma::ui::LoudnessPanel loudnessPanel;
    ma::ui::PeakPanel peakPanel;
    ma::ui::DynamicsPanel dynamicsPanel;
    ma::ui::SpectrumPanel spectrumPanel;
    ma::ui::StereoPanel stereoPanel;
    ma::ui::StreamingPanel streamingPanel;
    ma::ui::ReportPanel reportPanel;
    ma::ui::WaveformPanel waveformPanel;   // solo standalone, sopra la dashboard
    ma::ui::AlbumView albumView;           // solo standalone, al posto della dashboard
    ma::ui::StartView startView;           // solo standalone: schermata iniziale
    Screen screen = Screen::track;

    std::shared_ptr<const ma::TrackTimeline> shownTimeline;
    ma::MarkerSettings shownMarkerSettings;
    bool markersDirty = true;
    std::set<juce::String> ignoredMarkerIds;                  // non salvati: valgono finché il brano resta aperto
    std::array<bool, ma::kNumMarkerTypes> ignoredMarkerTypes {};

    ma::ui::DashboardData data;
    ma::TargetProfile referenceProfile;
    juce::File referenceProfileSource;
    ma::ReferenceTrack::State lastRefState = ma::ReferenceTrack::State::empty;

    juce::Image backgroundCache;   // gradiente + ombre dei pannelli, rigenerato al resize
    float backgroundScale = 0.0f;  // pixel fisici per unità logica della cache (zoom x scala dello schermo)
    float appliedZoom = 0.0f;
    std::unique_ptr<juce::FileChooser> fileChooser;
    ma::BatchAnalysisJob profileJob;
    std::unique_ptr<juce::AlertWindow> nameDialog, renameDialog;
    int seenVersionsRevision = -1;
    int seenAlbumRevision = -1;
    int lastBannerCount = 0;
    std::unique_ptr<FullScreenWindow> fullScreenWindow;   // dopo i componenti: viene distrutto prima di loro
    int tick = 0;
    int seenThemeVersion = -1;
    bool dragHover = false;

    static constexpr int minWidth = 1400, minHeight = 820;   // dimensione minima della dashboard (unità logiche)
    static constexpr int minHeightStandalone = 940;          // con la forma d'onda sopra la dashboard
    static constexpr int headerHeight = 60;
    static constexpr int bannerHeight = 26;
    static constexpr int normalTooltipDelayMs = 600;
    static constexpr int helpTooltipDelayMs = 1000;   // guida: il mouse resta fermo per un secondo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterAgentEditor)
};
