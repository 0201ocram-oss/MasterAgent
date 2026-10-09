#pragma once

#include "AnalysisSnapshot.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace ma
{

/** Un punto della forma d'onda: picchi minimo e massimo (tra i due canali) e RMS del tratto. */
struct WavePoint
{
    float min = 0.0f, max = 0.0f, rms = 0.0f;
};

/** Tratto del file in campioni [start, end), con il canale (-1 = entrambi). */
struct SampleRange
{
    long long start = 0, end = 0;
    int channel = -1;
};

/**
    Cronologia di un file analizzato offline: forma d'onda e serie da 100 ms con cui si trova
    dove sono i problemi (vedi findMarkers). Serve solo all'app standalone, che mostra il brano intero.
*/
struct TrackTimeline
{
    double sampleRate = 0.0;
    long long lengthSamples = 0;
    int numChannels = 2;

    int samplesPerPoint = 1;
    std::vector<WavePoint> wave;

    static constexpr double blockSeconds = 0.1;
    std::vector<float> truePeakDb;        // true peak massimo di ogni blocco (dBTP)
    std::vector<float> levelDb;           // RMS di ogni blocco, media dei canali (dBFS)
    std::vector<float> correlation;       // correlazione L/R sugli ultimi 400 ms, NaN se il segnale è troppo basso
    std::vector<float> shortTermLufs;     // short-term (3 s): il valore k chiude la finestra al tempo (k + 30) * 0.1 s

    std::vector<SampleRange> clips;       // eventi di clipping (stessi del contatore del report)
    std::vector<SampleRange> dropouts;    // buchi di silenzio digitale a metà brano

    double durationSeconds() const noexcept { return sampleRate > 0.0 ? (double) lengthSamples / sampleRate : 0.0; }
    bool isValid() const noexcept { return sampleRate > 0.0 && lengthSamples > 0 && ! wave.empty(); }
};

/**
    Registra forma d'onda, livello, correlazione e buchi di silenzio mentre il file viene analizzato.
    True peak, clipping e short-term arrivano dagli analizzatori dell'AnalysisEngine.
*/
class TimelineRecorder
{
public:
    void start (TrackTimeline& timeline, double sampleRate, int numChannels, long long expectedLength);
    void process (const float* left, const float* right, int numSamples);
    void finish();

    static constexpr int maxWavePoints = 20000;
    static constexpr size_t maxEvents = 4000;

private:
    void finishBlock();
    void closeZeroRun (int ch);

    TrackTimeline* target = nullptr;
    long long position = 0;

    WavePoint point;
    double pointSumSq = 0.0;
    int pointCount = 0;

    int blockLength = 4800, blockCount = 0;
    double blockSumSq = 0.0, blockLL = 0.0, blockRR = 0.0, blockLR = 0.0;
    std::array<double, 4> recentLL {}, recentRR {}, recentLR {};
    int recentIndex = 0;

    // buchi di silenzio: campioni esattamente a zero per canale, preceduti da segnale udibile
    std::array<long long, 2> zeroStart { -1, -1 };
    std::array<long long, 2> lastAudible { -1, -1 };
    std::array<bool, 2> zeroAfterAudible { false, false };
    std::vector<SampleRange> channelDropouts;
};

//==============================================================================
/** Tipi di problema segnati sulla forma d'onda. */
enum class MarkerType { clip, truePeak, dropout, compressed, phase };
constexpr int kNumMarkerTypes = 5;

struct Marker
{
    MarkerType type = MarkerType::clip;
    double start = 0.0, end = 0.0;   // secondi
    float value = 0.0f;              // clip: eventi, true peak: dBTP massimo, compressed: PSR minimo, phase: correlazione minima
    int channel = -1;                // -1 = entrambi o non applicabile

    bool isZone() const noexcept { return type == MarkerType::compressed || type == MarkerType::phase; }
    bool isCritical() const noexcept;

    /** Identità per "ignora questo": tipo + inizio al centesimo di secondo. */
    juce::String id() const;
};

/** Soglie dei segni: dipendono dal profilo attivo e dalla fase (mix o master). */
struct MarkerSettings
{
    float ceilingDb = -1.0f;    // true peak oltre questo valore
    float minPsr = 0.0f;        // PSR sotto questo valore = sezione troppo compressa (0 = controllo spento)
};

/** I problemi del brano con il punto in cui si trovano, in ordine di tempo. */
std::vector<Marker> findMarkers (const TrackTimeline& timeline, const MarkerSettings& settings);

juce::String markerTypeName (MarkerType type);
/** Descrizione di un segno per il tooltip: cosa è, dove, quanto. */
juce::String describeMarker (const Marker& marker);

/** "1:23.4" */
juce::String formatTimelineTime (double seconds, bool tenths = true);

} // namespace ma
