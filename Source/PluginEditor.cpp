#include "PluginEditor.h"
#include "Common/Text.h"
#include "Compare/ReportExporter.h"

using namespace ma;
using namespace ma::ui;

//==============================================================================
/** Finestra senza bordi grande quanto il monitor: ospita la dashboard a schermo intero. */
class MasterAgentEditor::FullScreenWindow : public juce::Component
{
public:
    FullScreenWindow (MasterAgentEditor& o, juce::Rectangle<int> area) : owner (o)
    {
        setName ("MasterAgent");
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setBounds (area);
        addAndMakeVisible (owner.content);
        addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override { g.fillAll (colours::background); }
    void resized() override                 { owner.placeContent (getLocalBounds()); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey || key == juce::KeyPress (juce::KeyPress::F11Key))
        {
            owner.setFullScreenAsync (false);
            return true;
        }
        return false;
    }

    void userTriedToCloseWindow() override { owner.setFullScreenAsync (false); }

private:
    MasterAgentEditor& owner;
};

//==============================================================================
MasterAgentEditor::MasterAgentEditor (MasterAgentProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // le preferenze dell'interfaccia sono comuni a tutte le istanze: si caricano una volta sola
    if (getThemeVersion() == 0)
        applyTheme (loadUiSettings());

    content.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (header);
    for (auto* panel : allPanels())
        content.addAndMakeVisible (panel);
    content.addChildComponent (albumView);

    albumView.onClose = [this] { showAlbum (false); };
    albumView.onClear = [this]
    {
        processor.clearAlbum();
        showAlbum (false);
    };
    albumView.onExport = [this] { exportAlbum(); };
    albumView.onShowTrack = [this] (int index)
    {
        processor.useAlbumTrackAsMaster (index);
        showAlbum (false);
        updateComparison();
        pushDataToPanels();
    };

    header.setListening (processor.listenReference.load());
    header.setLiveMode (processor.liveMode.load());
    header.setMixPhase (processor.mixPhase.load());

    header.onTargetChanged = [this] (const juce::String& id)
    {
        processor.setSelectedTargetId (id);
        updateComparison();
        pushDataToPanels();
    };
    header.onLoadReference = [this] { chooseReference(); };
    header.onClearReference = [this]
    {
        processor.listenReference.store (false);
        header.setListening (false);
        processor.getReference().clear();
        updateReferenceStatus();
        layoutDashboard();
        content.repaint();
    };
    header.onSaveProfile = [this] { saveReferenceAsProfile(); };
    header.onReset = [this] { processor.resetAnalysis(); };
    header.onExport = [this] { exportReport(); };
    header.onOptions = [this] { showOptionsMenu(); };
    header.onListenChanged = [this] (bool on) { processor.listenReference.store (on); layoutDashboard(); content.repaint(); };
    header.onHelpModeChanged = [this] (bool on) { setHelpMode (on); };
    header.onToggleFullScreen = [this] { setFullScreenAsync (! isFullScreen()); };
    header.onLiveModeChanged = [this] (bool live) { processor.setLiveMode (live, processor.liveWindowSeconds.load()); };
    header.onMixPhaseChanged = [this] (bool mix)
    {
        processor.mixPhase.store (mix);
        updateComparison();
        pushDataToPanels();
    };

    reportPanel.onHover = [this] (const Finding* finding, juce::Colour colour)
    {
        data.highlightKey = finding != nullptr ? finding->key : juce::String();
        data.highlightColour = colour;
        data.highlightTimeStart = finding != nullptr && finding->hasTime() ? finding->timeStart : -1.0f;
        data.highlightTimeEnd = finding != nullptr && finding->hasTime() ? finding->timeEnd : -1.0f;
        pushDataToPanels();
    };

    reportPanel.onSaveVersion = [this] { saveVersion(); };
    reportPanel.onCompareVersionChanged = [this] (int index)
    {
        processor.setCompareVersion (index);
        updateComparison();
        pushDataToPanels();
    };
    spectrumPanel.onToggleMidSide = [this]
    {
        auto s = getUiSettings();
        s.spectrumMidSide = ! s.spectrumMidSide;
        changeUiSettings (s);
    };

    reportPanel.onIgnoreChanged = [this] (const juce::String& id, bool ignored)
    {
        processor.setFindingIgnored (id, ignored);
        updateComparison();
        pushDataToPanels();
    };

    refreshTargets();
    updateReferenceStatus();

    setResizable (true, true);
    setSize (processor.editorWidth, processor.editorHeight);
    content.toBack();   // l'angolo di ridimensionamento resta sopra la dashboard

    refreshTheme();     // applica anche lo zoom
    startTimerHz (25);
}

MasterAgentEditor::~MasterAgentEditor()
{
    stopTimer();
    fullScreenWindow.reset();
    content.setLookAndFeel (nullptr);
}

std::array<Panel*, 7> MasterAgentEditor::allPanels()
{
    return { &loudnessPanel, &peakPanel, &dynamicsPanel, &spectrumPanel, &stereoPanel, &streamingPanel, &reportPanel };
}

void MasterAgentEditor::paint (juce::Graphics& g)
{
    // a schermo intero la dashboard è nella sua finestra: qui resta un segnaposto
    if (! isFullScreen())
        return;

    g.fillAll (colours::background);
    g.setColour (colours::textDim);
    g.setFont (fonts::value (16.0f));
    g.drawText ("MasterAgent è a schermo intero"_u, getLocalBounds().withTrimmedBottom (30), juce::Justification::centred);
    g.setFont (fonts::label (13.0f));
    g.drawText ("Clicca qui o premi Esc nella finestra a schermo intero per tornare", getLocalBounds().withTrimmedTop (30),
                juce::Justification::centred);
}

void MasterAgentEditor::mouseDown (const juce::MouseEvent&)
{
    if (isFullScreen())
        setFullScreenAsync (false);
}

void MasterAgentEditor::resized()
{
    processor.editorWidth = getWidth();
    processor.editorHeight = getHeight();

    if (! isFullScreen())
        placeContent (getLocalBounds());
}

void MasterAgentEditor::placeContent (juce::Rectangle<int> area)
{
    // la dashboard viene disposta in unità logiche e ingrandita/ridotta con una trasformazione
    const float zoom = getUiSettings().zoom();
    content.setTransform (juce::AffineTransform::scale (zoom).translated ((float) area.getX(), (float) area.getY()));
    content.setBounds (0, 0, juce::roundToInt ((float) area.getWidth() / zoom), juce::roundToInt ((float) area.getHeight() / zoom));
}

void MasterAgentEditor::applyZoom()
{
    const float zoom = getUiSettings().zoom();
    const float previous = appliedZoom > 0.0f ? appliedZoom : zoom;
    appliedZoom = zoom;

    const int minW = juce::roundToInt (minWidth * zoom), minH = juce::roundToInt (minHeight * zoom);
    setResizeLimits (minW, minH, 4000, 2600);

    // stessa dimensione logica di prima, senza superare l'area utile dello schermo
    int w = juce::roundToInt ((float) getWidth() / previous * zoom);
    int h = juce::roundToInt ((float) getHeight() / previous * zoom);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
    {
        const auto user = display->userBounds.toNearestInt();
        w = std::min (w, user.getWidth() - 16);
        h = std::min (h, user.getHeight() - 60);   // spazio per la barra del titolo della finestra della DAW
    }
    w = std::max (w, minW);
    h = std::max (h, minH);

    if (w != getWidth() || h != getHeight())
        setSize (w, h);
    else if (! isFullScreen())
        placeContent (getLocalBounds());

    if (fullScreenWindow != nullptr)
        placeContent (fullScreenWindow->getLocalBounds());
}

void MasterAgentEditor::paintDashboard (juce::Graphics& g)
{
    const int width = content.getWidth(), height = content.getHeight();

    // le ombre (blur) costano: si disegnano una volta in un'immagine, ridisegnata solo al resize o al cambio di tema.
    // L'immagine ha la risoluzione fisica (zoom x scala dello schermo), così resta nitida.
    const float scale = juce::jlimit (0.5f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int imageW = std::max (1, juce::roundToInt ((float) width * scale));
    const int imageH = std::max (1, juce::roundToInt ((float) height * scale));
    if (backgroundCache.isNull() || backgroundCache.getWidth() != imageW || backgroundCache.getHeight() != imageH
        || ! juce::approximatelyEqual (backgroundScale, scale))
    {
        backgroundScale = scale;
        backgroundCache = juce::Image (juce::Image::RGB, imageW, imageH, false);
        juce::Graphics bg (backgroundCache);
        bg.addTransform (juce::AffineTransform::scale (scale));
        bg.setGradientFill (juce::ColourGradient (colours::backgroundTop, 0.0f, 0.0f, colours::backgroundBottom, 0.0f, (float) height, false));
        bg.fillAll();

        if (getUiSettings().effects)
        {
            const juce::DropShadow shadow (colours::shadow, 14, { 0, 4 });
            for (auto* panel : allPanels())
            {
                juce::Path p;
                p.addRoundedRectangle (panel->getBounds().toFloat(), 10.0f);
                shadow.drawForPath (bg, p);
            }
        }
    }

    g.drawImageTransformed (backgroundCache, juce::AffineTransform::scale (1.0f / backgroundScale));

    if (hasFileBanner())
    {
        const auto banner = bannerArea (0).toFloat();
        const auto state = processor.getMasterFileState();
        const auto name = processor.getMasterFile().getFileName();
        const bool failed = state == MasterAgentProcessor::MasterFileState::failed;

        g.setGradientFill (juce::ColourGradient ((failed ? colours::critical : colours::accent2).darker (0.55f), banner.getX(), 0.0f,
                                                 colours::accent.darker (0.75f), banner.getRight(), 0.0f, false));
        g.fillRect (banner);
        g.setColour (juce::Colours::white);
        g.setFont (fonts::value (13.0f));

        juce::String text;
        if (state == MasterAgentProcessor::MasterFileState::analysing)
            text = "ANALISI DEL FILE"_t + "  " + name + "  " + juce::String (juce::roundToInt (processor.getMasterFileProgress() * 100.0f)) + "%   -   "
                 + "al termine la dashboard mostrerà il file al posto dell'ingresso live (clic per annullare)"_t;
        else if (failed)
            text = "ERRORE NELL'ANALISI DI"_t + "  " + name + ": " + processor.getMasterFileError() + "   -   " + "clic per chiudere"_t;
        else
            text = "FILE"_t + "  " + name + "   -   " + "analisi offline del brano intero al posto dell'ingresso live"_t + "   -   "
                 + "clic qui per tornare al live"_t;
        g.drawText (text, banner.reduced (12.0f, 0.0f), juce::Justification::centred);
    }

    if (processor.listenReference.load())
    {
        auto banner = bannerArea (hasFileBanner() ? 1 : 0).toFloat();
        g.setGradientFill (juce::ColourGradient (colours::reference.darker (0.45f), banner.getX(), 0.0f,
                                                 colours::accent2.darker (0.6f), banner.getRight(), 0.0f, false));
        g.fillRect (banner);
        g.setColour (juce::Colours::white);
        g.setFont (fonts::value (13.0f));
        const float gainDb = juce::Decibels::gainToDecibels (processor.referenceGain.load());
        g.drawText ("ASCOLTO REFERENCE ATTIVO  -  l'uscita del plugin è il brano di riferimento (gain match "_u
                        + juce::String (gainDb, 1) + " dB su "
                        + (processor.abMatchLoudestSection.load() ? "sezione più forte"_u : juce::String ("loudness integrata"))
                        + "). Disattivalo prima dell'export!",
                    banner, juce::Justification::centred);
    }
}

void MasterAgentEditor::paintDragOverlay (juce::Graphics& g)
{
    if (! dragHover)
        return;

    const auto bounds = content.getLocalBounds();
    g.setColour (colours::reference.withAlpha (0.15f));
    g.fillRect (bounds);
    g.setColour (colours::reference);
    g.drawRoundedRectangle (bounds.toFloat().reduced (6.0f), 12.0f, 2.0f);
    g.setFont (fonts::value (24.0f));
    g.drawText ("Rilascia i file: uno come reference o master, più file per l'album o un profilo"_t, bounds, juce::Justification::centred);
}

void MasterAgentEditor::layoutDashboard()
{
    backgroundCache = {};

    auto r = content.getLocalBounds();
    header.setBounds (r.removeFromTop (headerHeight));
    r.removeFromTop (bannerHeight * bannerCount());

    r = r.reduced (12);
    constexpr int gap = 12;
    albumView.setBounds (r);

    reportPanel.setBounds (r.removeFromRight ((int) (r.getWidth() * 0.27f)));
    r.removeFromRight (gap);

    auto row1 = r.removeFromTop ((int) (r.getHeight() * 0.34f));
    r.removeFromTop (gap);
    auto row3 = r.removeFromBottom (std::max (196, (int) (r.getHeight() * 0.3f)));
    r.removeFromBottom (gap);
    auto row2 = r;

    const int w1 = row1.getWidth() - 2 * gap;
    loudnessPanel.setBounds (row1.removeFromLeft ((int) (w1 * 0.40f)));
    row1.removeFromLeft (gap);
    peakPanel.setBounds (row1.removeFromLeft ((int) (w1 * 0.28f)));
    row1.removeFromLeft (gap);
    dynamicsPanel.setBounds (row1);

    spectrumPanel.setBounds (row2.removeFromLeft ((int) ((row2.getWidth() - gap) * 0.62f)));
    row2.removeFromLeft (gap);
    stereoPanel.setBounds (row2);

    streamingPanel.setBounds (row3);
}

bool MasterAgentEditor::hasFileBanner() const
{
    return processor.getMasterFileState() != MasterAgentProcessor::MasterFileState::none;
}

int MasterAgentEditor::bannerCount() const
{
    return (hasFileBanner() ? 1 : 0) + (processor.listenReference.load() ? 1 : 0);
}

juce::Rectangle<int> MasterAgentEditor::bannerArea (int index) const
{
    return { 0, headerHeight + index * bannerHeight, content.getWidth(), bannerHeight };
}

void MasterAgentEditor::contentClicked (juce::Point<int> position)
{
    if (! hasFileBanner() || ! bannerArea (0).contains (position))
        return;

    // clic sul banner del file: annulla l'analisi o torna all'ingresso live
    processor.clearMasterFile();
    layoutDashboard();
    content.repaint();
}

//==============================================================================
void MasterAgentEditor::changeUiSettings (const UiSettings& settings)
{
    if (settings == getUiSettings())
        return;

    applyTheme (settings);
    saveUiSettings (settings);
    refreshTheme();
}

void MasterAgentEditor::refreshTheme()
{
    seenThemeVersion = getThemeVersion();
    lookAndFeel.refreshColours();
    backgroundCache = {};
    if (! juce::approximatelyEqual (appliedZoom, getUiSettings().zoom()))
        applyZoom();
    content.sendLookAndFeelChange();   // header e report rileggono i colori dei loro controlli
    updateReferenceStatus();
    pushDataToPanels();
    content.repaint();
    if (fullScreenWindow != nullptr)
        fullScreenWindow->repaint();
    repaint();
}

void MasterAgentEditor::setHelpMode (bool on)
{
    data.helpMode = on;
    header.setHelpMode (on);
    tooltips.setMillisecondsBeforeTipAppears (on ? helpTooltipDelayMs : normalTooltipDelayMs);
    tooltips.hideTip();
    pushDataToPanels();
}

void MasterAgentEditor::setFullScreenAsync (bool on)
{
    // mai dentro il callback di un componente della finestra che si sta per distruggere
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MasterAgentEditor> (this), on]
    {
        if (safe != nullptr)
            safe->setFullScreen (on);
    });
}

void MasterAgentEditor::setFullScreen (bool on)
{
    if (on == isFullScreen())
        return;

    tooltips.hideTip();

    if (on)
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        const auto area = display != nullptr ? display->logicalBounds.toNearestInt() : getScreenBounds();
        removeChildComponent (&content);
        fullScreenWindow = std::make_unique<FullScreenWindow> (*this, area);
    }
    else
    {
        fullScreenWindow.reset();
        addAndMakeVisible (content);
        content.toBack();
        placeContent (getLocalBounds());

        // altrimenti Windows attiva la finestra di un'altra applicazione
        if (auto* top = getTopLevelComponent())
            top->toFront (true);
    }

    header.setFullScreen (on);
    backgroundCache = {};
    repaint();
}

//==============================================================================
void MasterAgentEditor::refreshTargets()
{
    std::vector<std::pair<juce::String, juce::String>> items;
    items.emplace_back (MasterAgentProcessor::referenceTargetId, "Brano di riferimento (reference)");
    for (const auto& p : processor.getProfiles().getProfiles())
        items.emplace_back (p.id, p.name);

    header.setTargets (items, processor.getSelectedTargetId());
}

void MasterAgentEditor::updateReferenceStatus()
{
    auto& ref = processor.getReference();
    const auto state = ref.getState();
    const auto name = ref.getFile().getFileName();

    switch (state)
    {
        case ReferenceTrack::State::empty:
            header.setReferenceStatus ("Nessun reference", colours::textFaint, false, false);
            break;
        case ReferenceTrack::State::loading:
            header.setReferenceStatus ("Analisi " + name + "... " + juce::String (juce::roundToInt (ref.getProgress() * 100.0f)) + "%",
                                       colours::warning, false, true);
            break;
        case ReferenceTrack::State::ready:
            header.setReferenceStatus (name, colours::reference, true, true);
            break;
        case ReferenceTrack::State::failed:
            header.setReferenceStatus ("Errore: " + ref.getError(), colours::critical, false, true);
            break;
    }

    if (state != ReferenceTrack::State::ready && processor.listenReference.load())
    {
        processor.listenReference.store (false);
        header.setListening (false);
        layoutDashboard();
        content.repaint();
    }
}

void MasterAgentEditor::updateComparison()
{
    auto& ref = processor.getReference();
    data.hasReference = ref.getSnapshot (data.reference);

    // versioni salvate: elenco e versione confrontata si aggiornano solo quando cambiano
    if (const int revision = processor.getVersionsRevision(); revision != seenVersionsRevision)
    {
        seenVersionsRevision = revision;
        const int index = processor.getCompareVersion();
        reportPanel.setVersions (processor.getVersionNames(), index);

        MasterAgentProcessor::SavedVersion v;
        data.hasVersion = processor.getVersion (index, v);
        data.version = data.hasVersion ? std::move (v.snapshot) : AnalysisSnapshot();
        data.versionName = data.hasVersion ? v.name : juce::String();
        data.versionComparison = {};
    }

    // profilo attivo
    const auto targetId = processor.getSelectedTargetId();
    const TargetProfile* profile = nullptr;

    if (targetId == MasterAgentProcessor::referenceTargetId)
    {
        if (data.hasReference)
        {
            if (referenceProfileSource != ref.getFile())
            {
                referenceProfile = TargetProfile::fromSnapshot (data.reference, "Reference: " + ref.getFile().getFileNameWithoutExtension());
                referenceProfileSource = ref.getFile();
            }
            profile = &referenceProfile;
        }
    }
    else
    {
        profile = processor.getProfiles().findById (targetId);
        if (profile == nullptr && ! processor.getProfiles().getProfiles().empty())
            profile = &processor.getProfiles().getProfiles().front();
    }

    data.profile = profile;
    data.phase = processor.getWorkPhase();

    if (profile != nullptr)
    {
        CompareOptions options;
        options.phase = data.phase;
        options.tonalBasis = processor.tonalLoudestSection.load() ? TonalBasis::loudestSection : TonalBasis::automatic;
        options.fifoOverflow = processor.getLiveAnalysis().hasOverflowed();
        options.reference = data.hasReference ? &data.reference : nullptr;
        options.ignoredIds = processor.getIgnoredFindings();

        data.targetName = profile->name;
        data.comparison = compare (data.master, *profile, options);

        // versione salvata: confrontata con lo stesso profilo e le stesse opzioni
        if (data.hasVersion)
        {
            auto versionOptions = options;
            versionOptions.fifoOverflow = false;
            data.versionComparison = compareVersions (compare (data.version, *profile, versionOptions), data.comparison);
        }
    }
    else
    {
        data.targetName = "Reference (non caricato)";
        data.comparison = {};
        data.comparison.findings.push_back ({ "Analisi", "Reference", "-", "-", 0.0f, Severity::info,
                                              "Seleziona un profilo di mercato o carica un brano di riferimento (pulsante o drag & drop).", {} });
    }

    // gain match per l'ascolto A/B: integrated contro integrated, oppure sezione più forte contro sezione più forte
    if (data.hasReference && data.reference.integratedLufs > -70.0f)
    {
        const bool loudest = processor.abMatchLoudestSection.load();
        const float refLevel = loudest ? data.reference.maxShortTermLufs : data.reference.integratedLufs;
        const float masterLevel = loudest ? data.master.maxShortTermLufs : data.master.integratedLufs;
        const float target = masterLevel > -70.0f ? masterLevel : -14.0f;
        const float gainDb = juce::jlimit (-30.0f, 30.0f, target - refLevel);
        processor.referenceGain.store (juce::Decibels::decibelsToGain (gainDb));
    }
}

void MasterAgentEditor::pushDataToPanels()
{
    for (auto* panel : allPanels())
        panel->setData (data);
    albumView.setData (data);
}

void MasterAgentEditor::timerCallback()
{
    // sorgente del master: un file analizzato offline (se pronto) oppure l'ingresso live
    processor.updateMasterFile();
    const bool fromFile = processor.getMasterFileSnapshot (data.master);
    if (! fromFile)
        processor.getLiveAnalysis().getSnapshot (data.master);
    data.masterFromFile = fromFile;
    data.masterFileName = fromFile ? processor.getMasterFile().getFileName() : juce::String();
    header.setModeEnabled (! fromFile);

    if (const int banners = bannerCount(); banners != lastBannerCount)
    {
        lastBannerCount = banners;
        layoutDashboard();
        content.repaint();
    }

    const auto refState = processor.getReference().getState();
    if (refState != lastRefState || refState == ReferenceTrack::State::loading)
    {
        lastRefState = refState;
        updateReferenceStatus();
        referenceProfileSource = juce::File();   // forza la rigenerazione del profilo reference
    }

    // il tema può essere cambiato da un'altra istanza del plugin
    if (seenThemeVersion != getThemeVersion())
        refreshTheme();

    // la modalità può cambiare anche dal ripristino dello stato del progetto
    header.setLiveMode (processor.liveMode.load());
    header.setMixPhase (processor.mixPhase.load());

    updateAlbumView();

    if (tick++ % 4 == 0)
    {
        pollMultiReference();
        updateComparison();
        if (! albumVisible)
        {
            reportPanel.setData (data);
            streamingPanel.setData (data);
        }
        if (bannerCount() > 0)
            content.repaint (0, headerHeight, content.getWidth(), bannerHeight * bannerCount());
    }

    // con il pannello album aperto la dashboard è coperta: non serve ridisegnarla
    if (! albumVisible)
        for (auto* panel : { (Panel*) &loudnessPanel, (Panel*) &peakPanel, (Panel*) &dynamicsPanel, (Panel*) &spectrumPanel, (Panel*) &stereoPanel })
            panel->setData (data);
}

//==============================================================================
void MasterAgentEditor::updateAlbumView()
{
    processor.updateAlbum();
    const auto state = processor.getAlbumState();

    if (const int revision = processor.getAlbumRevision(); revision != seenAlbumRevision)
    {
        seenAlbumRevision = revision;
        if (state == MasterAgentProcessor::AlbumState::ready)
        {
            const auto album = processor.getAlbum();
            albumView.setAlbum (checkAlbum (album.snapshots, album.names), album.skipped);
        }
        else
        {
            albumView.setAlbum ({}, {});
        }

        if (state == MasterAgentProcessor::AlbumState::none && albumVisible)
            showAlbum (false);
    }

    if (state == MasterAgentProcessor::AlbumState::analysing)
        albumView.setProgress (true, processor.getAlbumStatusText(), processor.getAlbumProgress());
}

void MasterAgentEditor::showAlbum (bool on)
{
    albumVisible = on;
    albumView.setVisible (on);
    for (auto* panel : allPanels())
        panel->setVisible (! on);

    if (on)
    {
        albumView.toFront (false);
        albumView.setData (data);
    }
    else
    {
        pushDataToPanels();
    }
    content.repaint();
}

void MasterAgentEditor::chooseAlbumFiles()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Scegli i brani dell'album (da 2 a 30)"_t,
                                                       processor.getMasterFile().getParentDirectory(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto files = fc.getResults();
                                  if (! files.isEmpty())
                                      startAlbumCheck (files);
                              });
}

void MasterAgentEditor::startAlbumCheck (const juce::Array<juce::File>& files)
{
    if (files.size() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Coerenza album"_t,
            "Seleziona almeno 2 brani: ognuno viene confrontato con il resto dell'album."_t);
        return;
    }

    // l'ordine di scelta del selettore non è affidabile: la tabella segue il nome del file (01_, 02_, ...)
    auto sorted = files;
    std::sort (sorted.begin(), sorted.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    if (sorted.size() > album::maxTracks)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Coerenza album"_t,
            "Verranno analizzati i primi %d brani."_t.replace ("%d", juce::String (album::maxTracks)));

    processor.startAlbumCheck (sorted);
    updateAlbumView();
    showAlbum (true);
}

void MasterAgentEditor::exportAlbum()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Report testuale (.txt)"_t);
    menu.addItem (2, "Tabella per foglio di calcolo (.csv)"_t);
    menu.addItem (3, "Immagine del pannello (.png)"_t);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (albumView), [this] (int choice)
    {
        if (choice == 0)
            return;

        const auto album = processor.getAlbum();
        const auto result = checkAlbum (album.snapshots, album.names);
        if (! result.isValid())
            return;

        const juce::String ext = choice == 1 ? ".txt" : (choice == 2 ? ".csv" : ".png");
        const auto text = choice == 1 ? buildAlbumTextReport (result) : (choice == 2 ? buildAlbumCsv (result) : juce::String());
        const auto image = choice == 3 ? albumView.createComponentSnapshot (albumView.getLocalBounds(), true, 1.0f) : juce::Image();
        const auto folder = album.files.isEmpty() ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                  : album.files.getFirst().getParentDirectory();
        const auto defaultFile = folder.getChildFile ("MasterAgent_album_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ext);

        fileChooser = std::make_unique<juce::FileChooser> ("Esporta coerenza album"_t, defaultFile, "*" + ext);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [text, image] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            bool ok = false;
            if (image.isNull())
            {
                ok = file.replaceWithText (text, false, false, "\r\n");
            }
            else
            {
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat png;
                ok = out.openedOk() && png.writeImageToStream (image, out);
            }

            if (! ok)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Errore",
                                                        "Impossibile scrivere " + file.getFullPathName());
        });
    });
}

//==============================================================================
void MasterAgentEditor::showOptionsMenu()
{
    const double window = processor.liveWindowSeconds.load();

    juce::PopupMenu windowMenu;
    for (double seconds : { 10.0, 20.0, 30.0, 60.0 })
        windowMenu.addItem (juce::String (juce::roundToInt (seconds)) + " secondi", true, std::abs (window - seconds) < 0.5,
                            [this, seconds] { processor.setLiveMode (processor.liveMode.load(), seconds); });

    juce::PopupMenu menu;
    menu.addSectionHeader ("Analisi");
    menu.addItem ("Auto-reset quando il play parte dall'inizio", true, processor.autoResetOnPlay.load(),
                  [this] { processor.autoResetOnPlay.store (! processor.autoResetOnPlay.load()); });
    menu.addItem ("Analizza solo durante la riproduzione", true, processor.analyseOnlyWhilePlaying.load(),
                  [this] { processor.analyseOnlyWhilePlaying.store (! processor.analyseOnlyWhilePlaying.load()); });
    menu.addSubMenu ("Finestra modalità Live"_u, windowMenu);
    menu.addSectionHeader ("Confronto tonale");
    menu.addItem ("Brano intero (in Live: sezione più forte del target)"_u, true, ! processor.tonalLoudestSection.load(),
                  [this] { processor.tonalLoudestSection.store (false); });
    menu.addItem ("Solo le sezioni più forti (ritornello/drop)"_u, true, processor.tonalLoudestSection.load(),
                  [this] { processor.tonalLoudestSection.store (true); });
    menu.addSectionHeader ("Ascolto A/B");
    menu.addItem ("Allinea il livello sulla loudness integrata", true, ! processor.abMatchLoudestSection.load(),
                  [this] { processor.abMatchLoudestSection.store (false); });
    menu.addItem ("Allinea il livello sulla sezione più forte (ritornello/drop)"_u, true, processor.abMatchLoudestSection.load(),
                  [this] { processor.abMatchLoudestSection.store (true); });
    menu.addSectionHeader ("Master"_t);
    menu.addItem ("Analizza un file come master..."_t, [this] { chooseMasterFile(); });
    if (hasFileBanner())
        menu.addItem ("Torna all'ingresso live"_t, [this] { contentClicked (bannerArea (0).getCentre()); });

    {
        const int numVersions = processor.getNumVersions();
        const bool compared = processor.getCompareVersion() >= 0;
        juce::PopupMenu versionsMenu;
        versionsMenu.addItem ("Salva la versione attuale"_t, [this] { saveVersion(); });
        versionsMenu.addItem ("Rinomina la versione confrontata..."_t, compared, false, [this] { renameComparedVersion(); });
        versionsMenu.addItem ("Elimina la versione confrontata"_t, compared, false, [this]
        {
            processor.removeVersion (processor.getCompareVersion());
            updateComparison();
            pushDataToPanels();
        });
        versionsMenu.addItem ("Elimina tutte le versioni"_t, numVersions > 0, false, [this]
        {
            processor.clearVersions();
            updateComparison();
            pushDataToPanels();
        });
        menu.addSubMenu ("Versioni"_t + " (" + juce::String (numVersions) + ")", versionsMenu);
    }

    menu.addSectionHeader ("Album"_t);
    menu.addItem ("Controlla coerenza album..."_t, [this] { chooseAlbumFiles(); });
    if (processor.getAlbumState() != MasterAgentProcessor::AlbumState::none)
        menu.addItem (albumVisible ? "Nascondi coerenza album"_t : "Mostra coerenza album"_t, [this] { showAlbum (! albumVisible); });

    menu.addSeparator();
    menu.addItem ("Crea profilo da più brani di riferimento..."_u, profileJob.getState() != BatchAnalysisJob::State::running,
                  false, [this] { createProfileFromFiles(); });
    if (profileJob.getState() == BatchAnalysisJob::State::running)
        menu.addItem ("Interrompi la creazione del profilo", [this] { profileJob.cancel(); updateReferenceStatus(); });
    menu.addItem ("Apri cartella profili utente", []
    {
        const auto dir = ProfileLibrary::getUserProfileDirectory();
        dir.createDirectory();
        dir.startAsProcess();
    });
    menu.addItem ("Ricarica profili", [this] { processor.getProfiles().reload(); refreshTargets(); });
    if (const int ignored = processor.getIgnoredFindings().size(); ignored > 0)
        menu.addItem ("Ripristina le diagnosi ignorate"_t + " (" + juce::String (ignored) + ")", [this]
        {
            processor.clearIgnoredFindings();
            updateComparison();
            pushDataToPanels();
        });

    // interfaccia: preferenze comuni a tutte le istanze, salvate su disco
    const auto ui = getUiSettings();
    juce::PopupMenu themeMenu, accentMenu;
    for (auto theme : { ThemeId::dark, ThemeId::light, ThemeId::graphite, ThemeId::highContrast })
        themeMenu.addItem (themeName (theme), true, ui.theme == theme, [this, ui, theme]
        {
            auto s = ui;
            s.theme = theme;
            changeUiSettings (s);
        });
    for (auto accentId : { AccentId::blue, AccentId::teal, AccentId::indigo, AccentId::silver })
        accentMenu.addItem (accentName (accentId), true, ui.accent == accentId, [this, ui, accentId]
        {
            auto s = ui;
            s.accent = accentId;
            changeUiSettings (s);
        });

    menu.addSectionHeader ("Interfaccia");
    menu.addSubMenu ("Tema: " + themeName (ui.theme), themeMenu);
    menu.addSubMenu ("Colore accento: " + accentName (ui.accent), accentMenu);

    juce::PopupMenu zoomMenu;
    for (int step : kZoomSteps)
        zoomMenu.addItem (juce::String (step) + "%", true, ui.zoomPercent == step, [this, ui, step]
        {
            auto s = ui;
            s.zoomPercent = step;
            changeUiSettings (s);
        });
    menu.addSubMenu ("Dimensione interfaccia: "_t + juce::String (ui.zoomPercent) + "%", zoomMenu);
    menu.addItem ("Ombre e sfumature", true, ui.effects, [this, ui]
    {
        auto s = ui;
        s.effects = ! s.effects;
        changeUiSettings (s);
    });
    menu.addItem ("Guida ai parametri (?)", true, data.helpMode, [this] { setHelpMode (! data.helpMode); });
    menu.addItem ("Schermo intero", true, isFullScreen(), [this] { setFullScreenAsync (! isFullScreen()); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.getOptionsButton()));
}

bool MasterAgentEditor::isInterestedInFileDrag (const juce::StringArray& files) const
{
    if (files.isEmpty())
        return false;
    for (const auto& f : files)
        if (! juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return false;
    return true;
}

void MasterAgentEditor::filesDropped (const juce::StringArray& files, juce::Point<int> position)
{
    dragHover = false;
    content.repaint();
    if (! isInterestedInFileDrag (files))
        return;

    juce::Array<juce::File> dropped;
    for (const auto& f : files)
        dropped.add (juce::File (f));

    // un file può essere il reference o il master da analizzare; più file diventano un profilo
    juce::PopupMenu menu;
    if (dropped.size() == 1)
    {
        menu.addSectionHeader (dropped[0].getFileName());
        menu.addItem ("Usa come brano di riferimento"_t, [this, file = dropped[0]] { processor.loadReference (file); });
        menu.addItem ("Analizza come master (brano intero, offline)"_t, [this, file = dropped[0]] { processor.analyseMasterFile (file); });
    }
    else
    {
        menu.addSectionHeader (juce::String (dropped.size()) + " " + "file"_t);
        menu.addItem ("Controlla la coerenza dell'album"_t, [this, dropped] { startAlbumCheck (dropped); });
        menu.addItem ("Crea un profilo da questi brani"_t, profileJob.getState() != BatchAnalysisJob::State::running, false,
                      [this, dropped] { startProfileJob (dropped); });
    }

    const auto screen = content.localPointToGlobal (position);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&content)
                                                  .withTargetScreenArea ({ screen.x, screen.y, 1, 1 }));
}

void MasterAgentEditor::chooseMasterFile()
{
    const auto current = processor.getMasterFile();
    fileChooser = std::make_unique<juce::FileChooser> ("Scegli il file del master da analizzare"_t,
                                                       current.existsAsFile() ? current.getParentDirectory() : juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (const auto file = fc.getResult(); file.existsAsFile())
                                      processor.analyseMasterFile (file);
                              });
}

void MasterAgentEditor::saveVersion()
{
    if (! data.master.valid || data.master.integratedLufs <= -70.0f)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Salva versione"_t,
            "Non c'è ancora un'analisi da salvare: riproduci il brano o analizza un file."_t);
        return;
    }

    const int index = processor.addVersion (data.master, data.masterFromFile ? data.masterFileName : juce::String ("live"));
    processor.setCompareVersion (index);   // da qui in poi il report mostra cosa cambia rispetto a questa versione
    updateComparison();
    pushDataToPanels();
}

void MasterAgentEditor::renameComparedVersion()
{
    const int index = processor.getCompareVersion();
    MasterAgentProcessor::SavedVersion v;
    if (! processor.getVersion (index, v))
        return;

    renameDialog = std::make_unique<juce::AlertWindow> ("Rinomina versione"_t, juce::String(), juce::MessageBoxIconType::NoIcon, this);
    renameDialog->addTextEditor ("name", v.name, "Nome:"_t);
    renameDialog->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    renameDialog->addButton ("Annulla"_t, 0, juce::KeyPress (juce::KeyPress::escapeKey));
    renameDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MasterAgentEditor> (this), index] (int result)
        {
            if (safe == nullptr || safe->renameDialog == nullptr)
                return;
            const auto name = safe->renameDialog->getTextEditorContents ("name");
            safe->renameDialog->setVisible (false);
            if (result == 1)
            {
                safe->processor.renameVersion (index, name);
                safe->updateComparison();
                safe->pushDataToPanels();
            }
        }));
}

void MasterAgentEditor::chooseReference()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Seleziona il brano di riferimento",
                                                       processor.getReference().getFile().getParentDirectory(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      processor.loadReference (file);
                                      if (processor.getSelectedTargetId() != MasterAgentProcessor::referenceTargetId)
                                      {
                                          processor.setSelectedTargetId (MasterAgentProcessor::referenceTargetId);
                                          refreshTargets();
                                      }
                                  }
                              });
}

void MasterAgentEditor::saveReferenceAsProfile()
{
    AnalysisSnapshot refSnap;
    if (! processor.getReference().getSnapshot (refSnap))
        return;

    auto profile = TargetProfile::fromSnapshot (refSnap, processor.getReference().getFile().getFileNameWithoutExtension());
    juce::String error;

    if (processor.getProfiles().saveUserProfile (profile, error))
    {
        refreshTargets();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Profilo salvato",
            "Il profilo \"" + profile.name + "\" è ora disponibile nel selettore Target.\nCartella: "_u
                + ProfileLibrary::getUserProfileDirectory().getFullPathName());
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Errore", error);
    }
}

void MasterAgentEditor::createProfileFromFiles()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Scegli 3-5 brani di riferimento con l'estetica che cerchi",
                                                       processor.getReference().getFile().getParentDirectory(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto files = fc.getResults();
                                  if (files.isEmpty())
                                      return;
                                  if (files.size() < 2)
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Profilo da più brani"_u,
                                          "Seleziona almeno 2 brani (consigliati 3-5). Per un solo brano usa \"Carica reference\" e poi \"Salva profilo\"."_u);
                                      return;
                                  }
                                  startProfileJob (files);
                              });
}

void MasterAgentEditor::startProfileJob (const juce::Array<juce::File>& files)
{
    if (files.size() < 2)
        return;
    profileJob.start (files, "Profilo da %d brani"_t.replace ("%d", juce::String (files.size())));
    pollMultiReference();
}

void MasterAgentEditor::pollMultiReference()
{
    const auto state = profileJob.getState();

    if (state == BatchAnalysisJob::State::running)
    {
        const auto& ref = processor.getReference();
        header.setReferenceStatus (profileJob.getStatusText(), colours::warning,
                                   ref.getState() == ReferenceTrack::State::ready, ref.getState() != ReferenceTrack::State::empty);
        return;
    }

    if (state != BatchAnalysisJob::State::done)
        return;

    auto results = profileJob.takeResults();
    auto& snapshots = results.snapshots;
    const auto& names = results.names;
    const auto& skipped = results.skipped;
    updateReferenceStatus();

    if (snapshots.size() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Profilo da più brani"_u,
            "Servono almeno 2 brani analizzati."_u + (skipped.isEmpty() ? juce::String() : "\nSaltati: " + skipped.joinIntoString (", ")));
        return;
    }

    askProfileName (std::move (snapshots), names, skipped);
}

void MasterAgentEditor::askProfileName (std::vector<AnalysisSnapshot> snapshots, const juce::StringArray& names, const juce::StringArray& skipped)
{
    juce::String text;
    text << "Analizzati " << (int) snapshots.size() << " brani: " << names.joinIntoString (", ");
    if (! skipped.isEmpty())
        text << "\nSaltati: " << skipped.joinIntoString (", ");
    text << "\n\nIl target sarà la loro media; le tolleranze di ogni banda vengono dalla loro dispersione."_u;

    nameDialog = std::make_unique<juce::AlertWindow> ("Profilo da più brani"_u, text, juce::MessageBoxIconType::QuestionIcon, this);
    nameDialog->addTextEditor ("name", "Genere (" + juce::String ((int) snapshots.size()) + " brani)", "Nome del profilo:");
    nameDialog->addButton ("Salva", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto shared = std::make_shared<std::vector<AnalysisSnapshot>> (std::move (snapshots));
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MasterAgentEditor> (this), shared, names] (int result)
        {
            if (safe == nullptr || safe->nameDialog == nullptr)
                return;

            auto& dialog = *safe->nameDialog;
            dialog.exitModalState (result);
            dialog.setVisible (false);

            const auto name = dialog.getTextEditorContents ("name").trim();
            if (result != 1 || name.isEmpty())
                return;

            const auto profile = TargetProfile::fromSnapshots (*shared, name, names);
            juce::String error;
            if (safe->processor.getProfiles().saveUserProfile (profile, error))
            {
                safe->processor.setSelectedTargetId ("user_" + profile.id);
                safe->refreshTargets();
                safe->updateComparison();
                safe->pushDataToPanels();
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Profilo salvato",
                    "Il profilo \"" + profile.name + "\" è ora il target attivo.\nCartella: "_u
                        + ProfileLibrary::getUserProfileDirectory().getFullPathName());
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Errore", error);
            }
        }));
}

void MasterAgentEditor::exportReport()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Report testuale (.txt)");
    menu.addItem (2, "Dati completi (.json)");
    menu.addItem (3, "Screenshot dashboard (.png)");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header), [this] (int choice)
    {
        if (choice == 0)
            return;

        const juce::String ext = choice == 1 ? ".txt" : (choice == 2 ? ".json" : ".png");
        const auto defaultFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                     .getChildFile ("MasterAgent_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ext);

        // cattura i dati ora, così il file corrisponde a ciò che si vede
        const auto snapshotCopy = data.master;
        const auto comparisonCopy = data.comparison;
        const auto targetName = data.targetName;
        const auto image = choice == 3 ? content.createComponentSnapshot (content.getLocalBounds(), true, 1.0f) : juce::Image();

        fileChooser = std::make_unique<juce::FileChooser> ("Esporta report", defaultFile, "*" + ext);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [choice, snapshotCopy, comparisonCopy, targetName, image] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            bool ok = false;
            if (choice == 1)
                ok = file.replaceWithText (buildTextReport (snapshotCopy, comparisonCopy, targetName));
            else if (choice == 2)
                ok = file.replaceWithText (buildJsonReport (snapshotCopy, comparisonCopy, targetName));
            else
            {
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat png;
                ok = out.openedOk() && png.writeImageToStream (image, out);
            }

            if (! ok)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Errore",
                                                        "Impossibile scrivere " + file.getFullPathName());
        });
    });
}
