#pragma once

#include "../Analysis/AnalysisSnapshot.h"
#include "EqSuggestion.h"
#include "TargetProfile.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace ma
{

enum class Severity { ok = 0, info = 1, warning = 2, critical = 3 };

juce::String severityName (Severity s);

/**
    Fase di lavoro. Sul mix bus durante il mixaggio loudness, PLR e true peak non vanno confrontati con
    target di mastering (li deciderà il mastering) e i problemi si risolvono sulle singole tracce;
    sul master si interviene con correzioni piccole sul bus stereo.
*/
enum class WorkPhase { mix, master };

juce::String phaseName (WorkPhase p);

/** Su quale porzione del brano confrontare il bilanciamento tonale. */
enum class TonalBasis
{
    automatic,        // brano intero; in modalità Live contro la sezione più forte del target (se disponibile)
    wholeSong,
    loudestSection    // sezione più forte del master contro sezione più forte del target
};

/** Chiavi che collegano una diagnosi all'elemento della dashboard da evidenziare. */
namespace highlight
{
    inline const juce::String integrated  = "integrated";
    inline const juce::String lra         = "lra";
    inline const juce::String truePeak    = "truePeak";
    inline const juce::String overs       = "overs";
    inline const juce::String clip        = "clip";
    inline const juce::String dc          = "dc";
    inline const juce::String plr         = "plr";
    inline const juce::String psr         = "psr";
    inline const juce::String dr          = "dr";
    inline const juce::String crestLow    = "crest:0";
    inline const juce::String crestMid    = "crest:1";
    inline const juce::String crestHigh   = "crest:2";
    inline const juce::String tilt        = "tilt";
    inline const juce::String rumble      = "rumble";
    inline const juce::String correlation = "correlation";
    inline const juce::String width       = "width";
    inline const juce::String lowEndWidth = "lowEndWidth";
    inline const juce::String monoLoss    = "monoLoss";
    inline const juce::String balance     = "balance";
    inline const juce::String time        = "time";

    inline juce::String band (int b)            { return "band:" + juce::String (b); }
    inline juce::String bandWidth (int b)       { return "bandWidth:" + juce::String (b); }
    inline juce::String bandCorrelation (int b) { return "bandCorr:" + juce::String (b); }
    inline juce::String crest (int b)           { return "crest:" + juce::String (b); }
    inline juce::String resonance (float hz)    { return "resonance:" + juce::String (juce::roundToInt (hz)); }
    inline juce::String eqMove (int i)          { return "eq:" + juce::String (i); }
}

/** Ordine di lavoro: prima ciò che condiziona tutto il resto (fase, low-end), per ultimi loudness e ceiling. */
namespace stage
{
    constexpr int technical = 0;
    constexpr int lowEnd    = 1;
    constexpr int tonal     = 2;
    constexpr int dynamics  = 3;
    constexpr int loudness  = 4;
    constexpr int analysis  = 5;
}

/** Aree della pagella del report. */
enum class Area { loudness, dynamics, tonal, stereo, technical, none };
constexpr int kNumAreas = 5;

/** "Loudness e picchi", "Dinamica", ... (tradotto) */
juce::String areaName (Area a);

/** Area di una categoria di diagnosi (testo sorgente italiano: Loudness, Peak, Dinamica, Tonale, Stereo, Tecnico). */
Area areaForCategory (const juce::String& category);

struct Finding
{
    juce::String category;     // Loudness, Peak, Dinamica, Tonale, Stereo, Tecnico
    juce::String metric;
    juce::String value;        // valore misurato formattato
    juce::String target;       // target / range formattato
    float delta = 0.0f;        // differenza dal limite più vicino (0 se dentro il range)
    Severity severity = Severity::ok;
    juce::String message;      // diagnosi
    juce::String key;          // elemento della dashboard collegato (vedi namespace highlight)

    juce::String action;       // intervento concreto (strumento, parametri, dove)
    juce::String group;        // diagnosi risolte dalla stessa azione (es. la stessa mossa EQ)
    int stage = stage::tonal;
    float timeStart = -1.0f;   // dove il problema è più evidente (secondi del brano), -1 = n/d
    float timeEnd = -1.0f;

    juce::String id;           // identità stabile (chiave + metrica nella lingua sorgente): diagnosi ignorate, confronti
    Area area = Area::none;
    bool ignored = false;      // l'utente l'ha segnata come scelta voluta: fuori da voti, priorità e contatori

    bool hasTime() const noexcept { return timeStart >= 0.0f && timeEnd > timeStart; }
    bool isScored() const noexcept { return ! ignored && area != Area::none && severity != Severity::info && ! key.startsWith ("eq:"); }
};

struct CompareOptions
{
    WorkPhase phase = WorkPhase::master;
    TonalBasis tonalBasis = TonalBasis::automatic;
    bool fifoOverflow = false;
    const AnalysisSnapshot* reference = nullptr;   // reference caricato: un picco presente anche lì non è un difetto
    juce::StringArray ignoredIds;                  // Finding::id segnate dall'utente come scelte volute
};

/** Voto di un'area: 100 senza problemi; ogni misura valutata pesa allo stesso modo (critico 1, da verificare 0.4). */
struct AreaScore
{
    int score = 100;
    int critical = 0, warning = 0, ok = 0;
    bool evaluated = false;
};

struct ComparisonResult
{
    std::vector<Finding> findings;           // ordinati per severità decrescente
    std::vector<Finding> priorities;         // al massimo 3 interventi, nell'ordine in cui farli
    std::array<float, kNumThirdOctaves> tonalDelta {};   // master - target (dB), se disponibile
    std::array<float, kNumBands> bandTonalDelta {};
    std::array<float, kNumBands> bandTolerance {};       // tolleranza effettiva per banda (dB)
    std::array<float, kNumThirdOctaves> tonalTarget {};  // curva target effettivamente usata
    bool hasTonalTarget = false;
    bool tonalUsesLoudest = false;           // confronto sulla sezione più forte
    std::vector<EqMove> eqMoves;             // mosse EQ suggerite (in ordine di importanza)
    WorkPhase phase = WorkPhase::master;
    int score = 100;                         // 0-100, indicativo: media pesata dei voti delle aree
    std::array<AreaScore, kNumAreas> areas {};

    /** Diagnosi con questa severità, escluse quelle ignorate. */
    int count (Severity s) const;
    int countIgnored() const;
    Severity severityOfKey (const juce::String& key) const;
};

/**
    Tolleranza tonale di una banda. Sub e Air variano molto di più tra brani commerciali
    (Elowsson & Friberg, AES 2017: deviazione standard massima agli estremi dello spettro).
    I profili creati da più reference hanno la loro tolleranza per banda, misurata.
*/
float tonalToleranceForBand (const TargetProfile& profile, int band);

/**
    Scostamento tonale di una banda, pesato sull'energia: confronta la potenza totale dei terzi d'ottava
    della banda invece di mediare i dB, così un terzo quasi vuoto (es. 20 Hz) non domina il risultato.
*/
float bandTonalDelta (const std::array<float, kNumThirdOctaves>& master,
                      const std::array<float, kNumThirdOctaves>& target, int band);

/** "1:32" */
juce::String formatSongTime (float seconds);

/** "La (A2)" */
juce::String noteName (int midiNote);

/**
    Confronta uno snapshot con un profilo di mercato o con il profilo generato da un reference.
    Aggiunge sempre i controlli tecnici indipendenti dal profilo (clip, fase, DC, ecc.)
    e armonizza i suggerimenti quando più metriche puntano a interventi opposti.
*/
ComparisonResult compare (const AnalysisSnapshot& master, const TargetProfile& profile, const CompareOptions& options = {});

} // namespace ma
