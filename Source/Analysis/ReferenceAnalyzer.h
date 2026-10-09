#pragma once

#include "AnalysisEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <mutex>

namespace ma
{

/** Analisi offline di un file audio (usata per il brano di riferimento e nei test). */
bool analyseBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate, AnalysisSnapshot& out,
                    std::function<void (float)> progress = {}, std::function<bool()> shouldCancel = {},
                    TrackTimeline* timeline = nullptr);

/** Legge un file audio (al massimo 2 canali e maxSeconds di durata). In caso di errore compila error. */
bool readAudioFile (const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate, juce::String& error,
                    double maxSeconds = 20.0 * 60.0);

/**
    Analizza un file leggendolo a blocchi, senza caricarlo tutto in memoria (stesso risultato di
    readAudioFile + analyseBuffer). In caso di errore compila error.
    Con timeline registra anche forma d'onda e posizione dei problemi.
*/
bool analyseFile (const juce::File& file, AnalysisSnapshot& out, juce::String& error,
                  std::function<void (float)> progress = {}, std::function<bool()> shouldCancel = {},
                  double maxSeconds = 20.0 * 60.0, TrackTimeline* timeline = nullptr);

/**
    Analizza uno o più file in background, uno alla volta (profilo da più reference, file come master,
    coerenza album). I file illeggibili o muti vengono saltati e segnalati.
*/
class BatchAnalysisJob : private juce::Thread
{
public:
    enum class State { idle, running, done };

    BatchAnalysisJob();
    ~BatchAnalysisJob() override;

    /** label precede lo stato (es. "Profilo da 5 brani", "Album"). withTimelines: registra anche la cronologia di ogni file. */
    void start (const juce::Array<juce::File>& files, const juce::String& label, bool withTimelines = false);
    void cancel();

    State getState() const noexcept { return state.load(); }
    float getProgress() const noexcept { return progress.load(); }

    /** Es. "Album: 2/5  brano.wav 40%", o "Analisi brano.wav 40%" con un solo file. */
    juce::String getStatusText() const;

    struct Results
    {
        std::vector<AnalysisSnapshot> snapshots;
        juce::Array<juce::File> files;         // stesso ordine degli snapshot
        juce::StringArray names;               // nomi dei file analizzati
        juce::StringArray skipped;             // "nome (motivo)"
        std::vector<std::shared_ptr<const TrackTimeline>> timelines;   // stesso ordine (nullptr senza withTimelines)
    };

    /** Risultati di un job concluso; il job torna inattivo. */
    Results takeResults();

private:
    void run() override;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> currentIndex { 0 };

    mutable std::mutex mutex;
    juce::Array<juce::File> files;
    juce::String label;
    bool timelinesWanted = false;
    Results results;
};

/**
    Brano di riferimento: caricamento e analisi in background, più un buffer ricampionato
    alla frequenza dell'host per l'ascolto A/B con livello allineato.
*/
class ReferenceTrack : private juce::Thread
{
public:
    enum class State { empty, loading, ready, failed };

    ReferenceTrack();
    ~ReferenceTrack() override;

    void load (const juce::File& file, double hostSampleRate);
    void clear();

    /** Se il sample rate dell'host cambia, ricampiona il buffer di playback. */
    void setHostSampleRate (double newRate);

    State getState() const noexcept { return state.load(); }
    float getProgress() const noexcept { return progress.load(); }
    juce::String getError() const;
    juce::File getFile() const;

    bool getSnapshot (AnalysisSnapshot& dest) const;

    /** Audio thread: scrive il reference (in loop) nei buffer con il guadagno indicato. Restituisce false se non disponibile. */
    bool renderPlayback (float* left, float* right, int numSamples, float gain) noexcept;

    static constexpr double maxDurationSeconds = 20.0 * 60.0;

private:
    void run() override;
    void buildPlaybackBuffer (double hostRate);

    std::atomic<State> state { State::empty };
    std::atomic<float> progress { 0.0f };

    mutable std::mutex dataMutex;
    juce::File file;
    juce::String error;
    AnalysisSnapshot snapshot;

    juce::AudioBuffer<float> sourceAudio;   // audio originale (per ricampionare)
    double sourceRate = 0.0;
    double pendingHostRate = 48000.0;

    juce::SpinLock playbackLock;
    juce::AudioBuffer<float> playback;      // al sample rate dell'host
    int playbackPos = 0;
};

} // namespace ma
