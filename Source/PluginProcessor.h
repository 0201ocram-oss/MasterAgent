#pragma once

#include "Analysis/AnalysisEngine.h"
#include "Analysis/ReferenceAnalyzer.h"
#include "Compare/AlbumCheck.h"
#include "Compare/Comparator.h"
#include "Compare/TargetProfile.h"
#include "Playback/FilePlayer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/**
    MasterAgent: plugin di sola analisi per il master bus.
    L'audio passa inalterato (bit-transparent) salvo quando l'utente attiva l'ascolto del reference.
*/
class MasterAgentProcessor : public juce::AudioProcessor
{
public:
    MasterAgentProcessor();
    ~MasterAgentProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /**
        App standalone: niente ingresso audio. Si analizzano file (un brano o un album) e il brano
        si ascolta dal lettore interno. Nella DAW: analisi dell'ingresso (Brano intero / Live).
    */
    bool isStandalone() const noexcept { return standalone; }
    ma::FilePlayer& getPlayer() noexcept { return player; }

    ma::LiveAnalysis& getLiveAnalysis() noexcept      { return liveAnalysis; }
    ma::ReferenceTrack& getReference() noexcept       { return reference; }
    ma::ProfileLibrary& getProfiles() noexcept        { return profiles; }

    void loadReference (const juce::File& file);
    void resetAnalysis() noexcept { liveAnalysis.requestReset(); }

    // impostazioni (thread GUI, lette anche dall'audio thread)
    std::atomic<bool> listenReference { false };
    std::atomic<float> referenceGain { 1.0f };
    std::atomic<bool> autoResetOnPlay { true };
    std::atomic<bool> analyseOnlyWhilePlaying { true };
    std::atomic<bool> liveMode { false };

    /**
        Allineamento di livello per l'ascolto A/B: false = loudness integrata, true = sezione più forte
        (max short-term). Bilanciare le parti più forti è la prassi di mastering per confrontare brani con
        dinamiche diverse (S. Savage, Mixing and Mastering in the Box, cap. 10.1).
    */
    std::atomic<bool> abMatchLoudestSection { false };
    std::atomic<double> liveWindowSeconds { 20.0 };

    /** Fase di lavoro: sul mix bus durante il mixaggio oppure sul master (cambia target e consigli). */
    std::atomic<bool> mixPhase { false };
    ma::WorkPhase getWorkPhase() const noexcept { return mixPhase.load() ? ma::WorkPhase::mix : ma::WorkPhase::master; }

    /** Confronto tonale sulla sola sezione più forte (ritornello/drop) invece che sul brano intero. */
    std::atomic<bool> tonalLoudestSection { false };

    /** Brano intero (accumula dall'ultimo reset) oppure Live (finestra mobile). */
    void setLiveMode (bool shouldBeLive, double windowSeconds);

    static constexpr const char* referenceTargetId = "__reference__";
    juce::String getSelectedTargetId() const;
    void setSelectedTargetId (const juce::String& id);

    /** Diagnosi segnate dall'utente come scelte volute (Finding::id): fuori da voti e priorità, salvate nel progetto. */
    juce::StringArray getIgnoredFindings() const;
    void setFindingIgnored (const juce::String& findingId, bool ignored);
    void clearIgnoredFindings();

    //==============================================================================
    /**
        Brano analizzato come master (app standalone). L'analisi gira in background e registra anche
        la cronologia (forma d'onda e posizione dei problemi); il brano viene caricato nel lettore.
    */
    void analyseMasterFile (const juce::File& file);
    void clearMasterFile();
    /** Da chiamare periodicamente (timer dell'editor): raccoglie il risultato del job concluso. */
    void updateMasterFile();

    enum class MasterFileState { none, analysing, ready, failed };
    MasterFileState getMasterFileState() const;
    juce::File getMasterFile() const;
    juce::String getMasterFileError() const;
    bool getMasterFileSnapshot (ma::AnalysisSnapshot& dest) const;
    std::shared_ptr<const ma::TrackTimeline> getMasterFileTimeline() const;
    float getMasterFileProgress() const noexcept { return masterFileJob.getProgress(); }

    //==============================================================================
    /** Versioni del master salvate per confrontarle dopo le correzioni (al massimo maxVersions, salvate nel progetto). */
    struct SavedVersion
    {
        juce::String name;
        juce::Time time;
        juce::String source;          // "live" o nome del file analizzato
        ma::AnalysisSnapshot snapshot;
    };

    static constexpr int maxVersions = 8;

    /** Aggiunge una versione (la più vecchia esce se sono già maxVersions). Restituisce l'indice. */
    int addVersion (const ma::AnalysisSnapshot& snapshot, const juce::String& source);
    void renameVersion (int index, const juce::String& name);
    void removeVersion (int index);
    void clearVersions();

    int getNumVersions() const;
    juce::StringArray getVersionNames() const;
    bool getVersion (int index, SavedVersion& dest) const;

    /** Versione con cui confrontare il master (-1 = nessuna). */
    int getCompareVersion() const;
    void setCompareVersion (int index);

    /** Cresce a ogni modifica delle versioni: l'editor lo usa per aggiornare elenco e confronto. */
    int getVersionsRevision() const noexcept { return versionsRevision.load(); }

    //==============================================================================
    /**
        Coerenza album: 2-30 file analizzati in background. I risultati restano qui (anche a editor chiuso)
        finché non vengono svuotati; non sono salvati nel progetto.
    */
    void startAlbumCheck (const juce::Array<juce::File>& files);
    void clearAlbum();   // interrompe anche un'analisi in corso
    /** Da chiamare periodicamente (timer dell'editor): raccoglie il risultato del job concluso. */
    void updateAlbum();

    enum class AlbumState { none, analysing, ready };
    AlbumState getAlbumState() const;
    juce::String getAlbumStatusText() const { return albumJob.getStatusText(); }
    float getAlbumProgress() const noexcept   { return albumJob.getProgress(); }

    struct AlbumData
    {
        std::vector<ma::AnalysisSnapshot> snapshots;
        juce::Array<juce::File> files;
        juce::StringArray names, skipped;
        std::vector<std::shared_ptr<const ma::TrackTimeline>> timelines;
    };
    AlbumData getAlbum() const;

    /** Cresce quando cambiano stato o risultati dell'album. */
    int getAlbumRevision() const noexcept { return albumRevision.load(); }

    /** Mostra un brano dell'album nella dashboard come file master, senza rianalizzarlo. */
    void useAlbumTrackAsMaster (int index);

    int editorWidth = 1440, editorHeight = 900;

private:
    static BusesProperties makeBuses (bool standaloneApp);

    const bool standalone;
    ma::FilePlayer player;
    ma::LiveAnalysis liveAnalysis;
    ma::ReferenceTrack reference;
    ma::ProfileLibrary profiles;

    juce::CriticalSection settingsLock;
    juce::String selectedTargetId { "streaming_generic" };
    juce::StringArray ignoredFindings;

    ma::BatchAnalysisJob masterFileJob;
    juce::File masterFile;
    ma::AnalysisSnapshot masterFileSnapshot;
    std::shared_ptr<const ma::TrackTimeline> masterFileTimeline;
    MasterFileState masterFileState = MasterFileState::none;
    juce::String masterFileError;

    std::vector<SavedVersion> versions;
    int compareVersion = -1;
    std::atomic<int> versionsRevision { 0 };

    ma::BatchAnalysisJob albumJob;
    AlbumData album;
    AlbumState albumState = AlbumState::none;
    std::atomic<int> albumRevision { 0 };

    double currentSampleRate = 48000.0;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterAgentProcessor)
};
