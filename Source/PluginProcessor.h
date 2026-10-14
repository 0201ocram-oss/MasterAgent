#pragma once

#include "Analysis/AnalysisEngine.h"
#include "Analysis/ReferenceAnalyzer.h"
#include "Compare/AlbumCheck.h"
#include "Compare/Comparator.h"
#include "Compare/TargetProfile.h"
#include "Playback/FilePlayer.h"
#include "Playback/Monitor.h"

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
    /** Blocchi audio elaborati: se il lettore suona ma il numero non cresce, la scheda audio non è attiva. */
    std::atomic<juce::uint32> audioBlocks { 0 };

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

    /**
        Da che punto suona il reference quando si attiva l'A/B, quando parte la riproduzione e a ogni salto.
        Il reference segue il lettore (standalone) o il transport della DAW: a riproduzione ferma tace.
    */
    enum class AbPosition { samePoint = 0, loudestSection, resume };
    std::atomic<int> abPosition { (int) AbPosition::samePoint };
    static constexpr double abFadeSeconds = 0.03;

    /** Monitor d'ascolto (mono, side, una banda): solo sull'uscita, mai salvato nel progetto. */
    ma::Monitor& getMonitor() noexcept { return monitor; }

    /** Fase di lavoro: sul mix bus durante il mixaggio oppure sul master (cambia target e consigli). */
    std::atomic<bool> mixPhase { false };
    ma::WorkPhase getWorkPhase() const noexcept { return mixPhase.load() ? ma::WorkPhase::mix : ma::WorkPhase::master; }

    /** Confronto tonale sulla sola sezione più forte (ritornello/drop) invece che sul brano intero. */
    std::atomic<bool> tonalLoudestSection { false };

    /** Brano intero (accumula dall'ultimo reset) oppure Live (finestra mobile). */
    void setLiveMode (bool shouldBeLive, double windowSeconds);

    /** Target: genere (o reference, o nessuno) e consegna. */
    ma::TargetSelection getTargetSelection() const;
    void setTargetSelection (const ma::TargetSelection& selection);

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
        Coerenza album: fino a album::maxTracks brani, aggiunti anche uno alla volta. I file si analizzano in
        background uno dopo l'altro e ognuno entra nell'album appena è pronto. I risultati restano qui (anche
        a editor chiuso) finché non vengono svuotati; non sono salvati nel progetto.
    */
    /** Mette in coda i file (quelli già nell'album si saltano). Restituisce quanti ne ha aggiunti. */
    int addAlbumTracks (const juce::Array<juce::File>& files);
    void removeAlbumTrack (int index);
    void stopAlbumAnalysis();   // svuota la coda; i brani già analizzati restano
    void clearAlbum();          // toglie tutti i brani, interrompendo anche l'analisi
    /** Da chiamare periodicamente (timer dell'editor): raccoglie il brano analizzato e passa al successivo. */
    void updateAlbum();

    /** none = nessun brano e niente in coda; analysing = c'è almeno un file in coda. */
    enum class AlbumState { none, analysing, ready };
    AlbumState getAlbumState() const;
    float getAlbumProgress() const noexcept   { return albumJob.getProgress(); }
    /** Nomi dei file in attesa: il primo è quello in analisi. */
    juce::StringArray getAlbumPending() const;
    /** Brani analizzati più quelli in coda. */
    int getAlbumSize() const;

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
    ma::TargetSelection selectedTarget { ma::targetIds::defaultGenre, ma::targetIds::defaultDelivery };
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

    void startNextAlbumTrack();

    ma::BatchAnalysisJob albumJob;
    AlbumData album;
    juce::File albumCurrent;                  // file in analisi (vuoto = nessuno)
    juce::Array<juce::File> albumQueue;       // file in attesa, nell'ordine di aggiunta
    std::atomic<int> albumRevision { 0 };

    double currentSampleRate = 48000.0;
    bool wasPlaying = false;

    /**
        Audio thread: ascolto A/B con dissolvenza. running = il brano sta suonando (altrimenti il reference tace),
        resync = si è appena avviato o spostato, songTime = posizione del brano all'inizio del blocco (NaN se ignota).
    */
    void renderReference (juce::AudioBuffer<float>& buffer, int numChannels, bool running, bool resync, double songTime) noexcept;

    ma::Monitor monitor;
    juce::AudioBuffer<float> abScratch;       // blocco del reference, allocato in prepareToPlay
    float abMix = 0.0f;                       // 0 = master, 1 = reference
    float abStep = 0.001f;
    bool abWasOn = false;
    double expectedSongTime = -1.0e9;         // DAW: per riconoscere salti e loop del transport
    bool playerWasPlaying = false;

    /**
        Standalone: liveAnalysis segue il brano mentre si ascolta, per i valori istantanei della dashboard.
        Finestra breve: i meter dei picchi tengono gli ultimi secondi, come nella modalità Live della DAW.
    */
    static constexpr double playbackWindowSeconds = 6.0;
    int playbackSeeks = -1;                   // salti del lettore già visti dall'audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterAgentProcessor)
};
