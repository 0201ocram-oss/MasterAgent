#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ma
{

constexpr float kSilenceDb = -150.0f;

inline float gainToDb (double g, float floorDb = kSilenceDb)
{
    return g > 0.0 ? std::max (floorDb, (float) (20.0 * std::log10 (g))) : floorDb;
}

inline float powerToDb (double p, float floorDb = kSilenceDb)
{
    return p > 0.0 ? std::max (floorDb, (float) (10.0 * std::log10 (p))) : floorDb;
}

/**
    Bande "musicali" usate per energia, bilanciamento tonale e stereo.
    Suddivisione classica delle "magic frequencies" (B. Owsinski, The Mixing Engineer's Handbook):
    sub-bass 16-60, bass 60-250, low mids 250-2k, high mids 2-4k, presence 4-6k, brilliance 6-16k;
    qui i low mids sono divisi in 250-500 ("fango") e 500-2k ("nasale/metallico") e la brilliance in 6-12k / aria 12-20k.
*/
constexpr int kNumBands = 8;
constexpr std::array<float, kNumBands + 1> kBandEdges { 20.0f, 60.0f, 250.0f, 500.0f, 2000.0f, 4000.0f, 6000.0f, 12000.0f, 20000.0f };
constexpr std::array<const char*, kNumBands> kBandNames { "Sub", "Bass", "Low-mid", "Mid", "High-mid", "Presence", "Brilliance", "Air" };

/** Bande a terzi d'ottava ISO (20 Hz - 20 kHz), usate per il bilanciamento tonale. */
constexpr int kNumThirdOctaves = 31;
inline float thirdOctaveCentre (int i) { return 1000.0f * std::pow (2.0f, (float) (i - 17) / 3.0f); }
/** Banda a cui appartiene un terzo d'ottava (stessa regola di bandTonalDelta), -1 se fuori da 20 Hz - 20 kHz. */
inline int bandOfThirdOctave (int i)
{
    const float fc = thirdOctaveCentre (i);
    for (int b = 0; b < kNumBands; ++b)
        if (fc >= kBandEdges[(size_t) b] * 0.99f && fc < kBandEdges[(size_t) b + 1] * 0.99f)
            return b;
    return -1;
}

constexpr std::array<const char*, kNumThirdOctaves> kThirdOctaveLabels {
    "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500",
    "630", "800", "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k"
};

/** Griglia di visualizzazione dello spettro: 1/12 d'ottava da 20 Hz. */
constexpr int kNumDisplayPoints = 120;
inline float displayFrequency (int i) { return 20.0f * std::pow (2.0f, (float) i / 12.0f); }

/** Bande per il crest factor multibanda. */
constexpr int kNumCrestBands = 3;
constexpr std::array<const char*, kNumCrestBands> kCrestBandNames { "Low (<200)", "Mid", "High (>4k)" };

constexpr int kHistogramBins = 40;      // short-term loudness da -40 a 0 LUFS, 1 dB per bin
constexpr float kHistogramMinLufs = -40.0f;

constexpr int kGoniometerPoints = 1024;

/** Nomi delle note (classe 0 = Do). */
constexpr std::array<const char*, 12> kNoteNamesIt { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
constexpr std::array<const char*, 12> kNoteNamesEn { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

struct Resonance
{
    float frequency = 0.0f;
    float excessDb = 0.0f;

    // Un picco stretto e persistente può essere una nota del brano (tonica, basso ostinato) o una risonanza
    // (stanza, fusto di un tamburo, EQ). Le note cadono sulla griglia temperata e hanno di solito altre ottave nello spettro.
    bool musical = false;
    int midiNote = -1;          // nota temperata più vicina (dopo la correzione di intonazione)
    float cents = 0.0f;         // scarto dalla nota
};

/** Comportamento nel tempo dell'energia di una banda rispetto al totale. */
struct BandDynamics
{
    /**
        Quanto pesano i momenti in cui la banda è più prominente: livello relativo della banda su tutto il brano
        meno lo stesso livello calcolato escludendo il 10% dei frame in cui la banda è più forte.
        ~0.5-1.5 dB per materiale costante, molto di più se l'energia arriva a raffiche (sibilanti, note che rimbombano).
    */
    float burstDb = 0.0f;
    float worstStartSec = -1.0f;    // finestra in cui la banda è più sopra la sua mediana (secondi del brano), -1 = n/d
    float worstEndSec = -1.0f;
    float worstExcessDb = 0.0f;     // eccesso medio nella finestra rispetto alla mediana del brano
};

/**
    Risultato completo di un'analisi. Se aggiungi un campo, aggiornalo anche in SnapshotIO.cpp
    (versioni salvate nel progetto): il test di round-trip in SnapshotTests.cpp lo controlla.
*/
struct AnalysisSnapshot
{
    bool valid = false;
    double sampleRate = 0.0;
    int numChannels = 2;
    double secondsAnalyzed = 0.0;
    bool liveMode = false;                      // finestra mobile invece del brano intero
    double liveWindowSeconds = 0.0;

    // --- Loudness (ITU-R BS.1770-4 / EBU R128) -----------------------------
    float momentaryLufs = kSilenceDb;
    float shortTermLufs = kSilenceDb;
    float integratedLufs = kSilenceDb;
    float loudnessRange = 0.0f;
    float maxMomentaryLufs = kSilenceDb;
    float maxShortTermLufs = kSilenceDb;
    float rmsDb = kSilenceDb;                   // RMS full-band, media dei canali
    std::vector<float> shortTermHistory;        // un valore ogni 100 ms (ultimi N minuti)

    // --- Tempo del brano (posizione del transport dell'host, o tempo dall'inizio del file) ---------
    double historyStartSongSec = 0.0;           // tempo del brano del primo valore di shortTermHistory
    bool historyAligned = true;                 // nessun salto (seek/loop) dall'ultimo reset: la cronologia è allineata al brano

    // --- Peak ---------------------------------------------------------------
    std::array<float, 2> samplePeakDb { kSilenceDb, kSilenceDb };
    std::array<float, 2> truePeakDb { kSilenceDb, kSilenceDb };
    float truePeakMaxDb = kSilenceDb;
    float recentTruePeakDb = kSilenceDb;        // max sugli ultimi 3 s
    int overs1dB = 0;                           // eventi sopra -1 dBTP
    int overs0dB = 0;                           // eventi sopra 0 dBTP
    int clipEvents = 0;                         // >= 3 campioni consecutivi a fondo scala

    // --- Dinamica -------------------------------------------------------------
    float crestDb = 0.0f;                       // sample peak - RMS
    float plr = 0.0f;                           // true peak max - integrated
    float psr = 0.0f;                           // true peak recente - short-term
    float minPsr = 0.0f;                        // PSR minimo registrato (sezione più compressa)
    float drValue = 0.0f;                       // algoritmo DR (TT / Pleasurize)
    bool drValid = false;
    std::array<float, kNumCrestBands> bandCrestDb {};
    std::array<float, kHistogramBins> stHistogram {};   // frazione di tempo per bin (0..1)

    // --- Spettro / tonale -----------------------------------------------------
    std::array<float, kNumDisplayPoints> spectrumLongTermDb {};
    std::array<float, kNumDisplayPoints> spectrumInstantDb {};
    std::array<float, kNumDisplayPoints> spectrumMidDb {};      // long-term del Mid (L+R)/2, stessa scala di spectrumLongTermDb
    std::array<float, kNumDisplayPoints> spectrumSideDb {};     // long-term del Side (L-R)/2
    std::array<float, kNumThirdOctaves> thirdOctaveDb {};       // long-term, normalizzato (media 63 Hz-12.5 kHz = 0 dB)
    std::array<float, kNumBands> bandEnergyDb {};               // energia di banda relativa al totale (dB)
    float spectralTiltDbPerOct = 0.0f;
    float spectralCentroidHz = 0.0f;
    float subRumbleDb = kSilenceDb;                             // energia < 30 Hz relativa al totale
    float ultraHighDb = kSilenceDb;                             // energia > 16 kHz relativa al totale
    std::vector<Resonance> resonances;
    int keyPitchClass = -1;                                     // classe di nota più presente (tonica probabile), -1 = n/d
    float tuningCents = 0.0f;                                   // intonazione stimata rispetto a La = 440 Hz

    /**
        Terzi d'ottava della sola sezione più forte (frame con short-term loudness nel 25% superiore, almeno 8 s),
        normalizzati come thirdOctaveDb. Confrontare ritornello con ritornello è più affidabile quando
        intro, strofe e breakdown dei due brani sono arrangiati in modo diverso.
    */
    std::array<float, kNumThirdOctaves> thirdOctaveLoudestDb {};
    float loudestSectionSeconds = 0.0f;                         // 0 = non disponibile

    std::array<BandDynamics, kNumBands> bandDynamics {};

    // --- Stereo ---------------------------------------------------------------
    float correlation = 1.0f;                  // long-term
    float correlationInstant = 1.0f;
    std::array<float, kNumBands> bandCorrelation {};
    float widthPercent = 0.0f;                 // S / (M + S) energia, 0 = mono, 50 = decorrelato, 100 = solo side
    std::array<float, kNumBands> bandWidthPercent {};
    float balanceDb = 0.0f;                    // L - R
    float monoLossDb = 0.0f;                   // variazione di livello sommando in mono
    std::array<float, kNumBands> bandMonoLossDb {};
    float lowEndWidthPercent = 0.0f;           // larghezza (side) sotto 100 Hz: dovrebbe essere ~mono
    float lowEndCorrelation = 1.0f;            // correlazione sotto 100 Hz
    std::array<float, kGoniometerPoints * 2> goniometer {};    // coppie (L,R) recenti
    int goniometerWritePos = 0;

    // --- Tecnici ----------------------------------------------------------------
    std::array<float, 2> dcOffsetDb { kSilenceDb, kSilenceDb };
    float noiseFloorDb = kSilenceDb;
    bool hasDigitalSilence = false;
    std::array<bool, 2> channelSilent { false, false };
};

} // namespace ma
