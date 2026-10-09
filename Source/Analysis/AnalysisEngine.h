#pragma once

#include "AnalysisSnapshot.h"
#include "DynamicsAnalyzer.h"
#include "LoudnessMeter.h"
#include "SpectrumAnalyzer.h"
#include "TechnicalChecks.h"
#include "Timeline.h"
#include "TruePeakMeter.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <optional>

namespace ma
{

/** Insieme sincrono di tutti gli analizzatori. Usato sia in tempo reale sia offline (reference). */
class AnalysisEngine
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /**
        songTime: tempo del brano (s) del primo campione del blocco, se noto (transport dell'host).
        NaN = prosegue dal blocco precedente (0 dopo il reset): per i file è la posizione nel file.
    */
    void process (const float* left, const float* right, int numSamples, double songTime = std::numeric_limits<double>::quiet_NaN());
    void buildSnapshot (AnalysisSnapshot& s) const;

    /**
        Analisi offline di un file: registra anche la cronologia (forma d'onda e dove sono i problemi).
        Da chiamare dopo prepare(); finishTimeline() completa timeline e smette di registrare.
    */
    void startTimeline (TrackTimeline& timeline, long long expectedLength);
    void finishTimeline();

    double getSampleRate() const noexcept { return sampleRate; }

private:
    TrackTimeline* timeline = nullptr;
    TimelineRecorder timelineRecorder;

    double sampleRate = 48000.0;
    int numChannels = 2;
    double samplesProcessed = 0.0;

    double songTimeStart = 0.0;      // tempo del brano del primo campione dopo il reset
    double nextSongTime = 0.0;       // tempo atteso per il prossimo blocco
    bool songTimeContiguous = true;  // nessun salto (seek, loop) dall'ultimo reset

    LoudnessMeter loudness;
    TruePeakMeter truePeak;
    DynamicsAnalyzer dynamics;
    SpectrumAnalyzer spectrum;
    TechnicalChecks technical;

    size_t lastShortTermCount = 0;
    float minPsr = 0.0f;
    bool minPsrValid = false;
};

/**
    Analisi in tempo reale: l'audio thread scrive in un FIFO lock-free,
    un thread dedicato esegue l'analisi e pubblica snapshot per la GUI (10 Hz).

    Modalità:
    - Brano intero: le misure si accumulano dall'ultimo reset (verdetto finale sul brano).
    - Live: finestra mobile degli ultimi ~N secondi, per vedere subito l'effetto delle regolazioni.
*/
class LiveAnalysis : private juce::Thread
{
public:
    LiveAnalysis();
    ~LiveAnalysis() override;

    /** Da chiamare fuori dall'audio thread (prepareToPlay). */
    void prepare (double sampleRate, int numChannels);
    void release();

    /**
        Audio thread. In real-time non alloca e non blocca. Con canBlock = true (render offline)
        attende che l'analisi recuperi invece di perdere audio.
        hostTimeSeconds: posizione del transport (NaN se non disponibile), serve a dire in che punto
        del brano si trova un problema anche se la riproduzione non parte dall'inizio o va in loop.
    */
    void push (const float* left, const float* right, int numSamples, bool canBlock = false,
               double hostTimeSeconds = std::numeric_limits<double>::quiet_NaN()) noexcept;

    void requestReset() noexcept { resetRequested.store (true); }
    void setLiveMode (bool shouldBeLive, double windowSeconds) noexcept;

    /** Copia l'ultimo snapshot pubblicato (thread GUI). */
    void getSnapshot (AnalysisSnapshot& dest) const;

    bool hasOverflowed() const noexcept { return overflowed.load(); }

private:
    void run() override;
    void resetAll();
    void processChunk (const float* left, const float* right, int numSamples, double songTime);
    void publish();

    /** Applica i marker di tempo fino al campione readPosition (thread di analisi). */
    void consumeTimeMarkers();

    // tempo del brano: l'audio thread scrive un marker solo alle discontinuità del transport (avvio, seek, loop)
    struct TimeMarker { long long sampleIndex = 0; double songTime = 0.0; };
    static constexpr int kMaxMarkers = 64;
    juce::AbstractFifo markerFifo { kMaxMarkers };
    std::array<TimeMarker, kMaxMarkers> markers {};
    long long samplesPushed = 0;                       // audio thread
    double expectedHostTime = -1.0e9;                  // audio thread
    long long samplesRead = 0;                         // thread di analisi
    std::optional<TimeMarker> nextMarker;              // thread di analisi
    double currentSongTime = std::numeric_limits<double>::quiet_NaN();   // NaN = nessun transport

    // cronologia della short-term: tempo del brano del primo campione dopo il reset
    double historySongStart = 0.0, historyNextTime = 0.0;
    double historySamples = 0.0;
    bool historyContiguous = true;

    std::array<AnalysisEngine, 2> engines;   // [1] usato solo in modalità Live
    std::array<double, 2> engineSamples { 0.0, 0.0 };
    bool secondEngineRunning = false;
    LoudnessMeter historyMeter;              // continuo: momentary, short-term e cronologia

    double sampleRate = 48000.0;
    bool liveMode = false;
    double liveWindowSeconds = 20.0;
    std::atomic<bool> liveModeRequested { false };
    std::atomic<double> liveWindowRequested { 20.0 };

    juce::AbstractFifo fifo { 1 };
    std::vector<float> fifoL, fifoR;
    std::vector<float> workL, workR;
    int channels = 2;

    std::atomic<bool> resetRequested { false };
    std::atomic<bool> overflowed { false };

    mutable std::mutex snapshotMutex;
    AnalysisSnapshot published;
    AnalysisSnapshot scratch;
};

} // namespace ma
