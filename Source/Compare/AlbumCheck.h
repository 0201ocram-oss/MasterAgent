#pragma once

#include "Comparator.h"

namespace ma
{

/**
    Tolleranze della coerenza album. Fisse e non dipendenti da un profilo: i brani di un album si confrontano
    tra loro, non con un target di mercato.
*/
namespace album
{
    constexpr float loudestWarnLu = 1.5f;       // sezione più forte rispetto alla mediana dell'album
    constexpr float loudestCriticalLu = 3.0f;
    constexpr float tonalMinToleranceDb = 1.5f; // per banda; Sub e Air tollerano di più (tonalToleranceForBand)
    constexpr float truePeakInfoDb = 1.0f;      // ceiling diverso dalla mediana
    constexpr float widthInfoPercent = 8.0f;    // larghezza stereo diversa dalla mediana (punti percentuali)
    constexpr float maxEqDb = 4.0f;             // correzione EQ suggerita al massimo: oltre, il resto va fatto nel mix
    constexpr int maxTracks = 30;
}

/** Un brano confrontato con il resto dell'album. */
struct AlbumTrack
{
    juce::String name;

    float integratedLufs = kSilenceDb;
    float loudestLufs = kSilenceDb;            // max short-term: la sezione più forte (ritornello/drop)
    float loudestDeltaLu = 0.0f;               // rispetto alla mediana dell'album
    float gainDb = 0.0f;                       // guadagno che lo allinea alla mediana (= -loudestDeltaLu)
    Severity loudness = Severity::ok;

    std::array<float, kNumBands> bandDelta {}; // bilanciamento tonale rispetto all'album (dB)
    std::array<float, kNumBands> bandTolerance {};
    std::array<Severity, kNumBands> bandSeverity {};
    Severity tonal = Severity::ok;             // la banda peggiore

    float truePeakDb = kSilenceDb;
    float truePeakDeltaDb = 0.0f;
    float widthPercent = 0.0f;
    float widthDelta = 0.0f;
    float plr = 0.0f;
    float lra = 0.0f;

    std::vector<Finding> findings;             // diagnosi del brano, le più gravi per prime
    Severity worst = Severity::ok;             // severità peggiore tra le diagnosi (le informative contano come info)
};

struct AlbumResult
{
    std::vector<AlbumTrack> tracks;            // nell'ordine dei file

    float medianLoudestLufs = kSilenceDb;
    float medianIntegratedLufs = kSilenceDb;
    float medianTruePeakDb = kSilenceDb;
    float medianWidthPercent = 0.0f;
    float loudestSpreadLu = 0.0f;              // sezione più forte: brano più forte - più piano
    std::array<float, kNumThirdOctaves> albumCurve {};   // curva tonale dell'album (mediana per terzo d'ottava)
    bool usesLoudestSection = false;           // curva e confronto sulla sezione più forte di ogni brano

    int critical = 0, warning = 0;             // brani con almeno una diagnosi critica / da verificare (peggiore)

    bool isValid() const noexcept { return tracks.size() >= 2; }
};

/**
    Coerenza di un album (o EP, o playlist di un artista): ogni brano è confrontato con la mediana dell'album
    (un riferimento comune: allineando i brani segnalati, tutti convergono sullo stesso livello).
    - Sezione più forte (max short-term) rispetto alla mediana: oltre ±1.5 LU da verificare, oltre ±3 LU critico,
      con il guadagno che la allinea. L'integrated resta informativa: le ballate possono essere volutamente più basse.
    - Bilanciamento tonale per banda rispetto alla curva mediana dell'album (sezione più forte, se c'è in tutti i brani).
    - Ceiling true peak e larghezza stereo diversi dagli altri brani: informativi.
    La mediana, al contrario della media, non si sposta per un solo brano fuori posto, che così risulta segnalato da solo.
    Servono almeno 2 brani; names è nello stesso ordine degli snapshot.
*/
AlbumResult checkAlbum (const std::vector<AnalysisSnapshot>& snapshots, const juce::StringArray& names);

/** Report testuale dell'album (tabella e diagnosi per brano). */
juce::String buildAlbumTextReport (const AlbumResult& album);

/** Tabella CSV (separatore ";", decimali con il punto): un brano per riga. */
juce::String buildAlbumCsv (const AlbumResult& album);

} // namespace ma
