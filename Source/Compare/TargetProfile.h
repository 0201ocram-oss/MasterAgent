#pragma once

#include "../Analysis/AnalysisSnapshot.h"

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

namespace ma
{

/** Intervallo accettabile per una metrica: [min, max] = ok, entro "warn" oltre i limiti = attenzione, oltre = critico. */
struct MetricRange
{
    float min = 0.0f;
    float max = 0.0f;
    float target = 0.0f;
    float warn = 1.0f;
};

/** Chiavi delle metriche supportate nei profili. */
namespace metric
{
    inline const juce::String integratedLufs   = "integratedLufs";
    inline const juce::String truePeakMax      = "truePeakMax";
    inline const juce::String loudnessRange    = "loudnessRange";
    inline const juce::String plr              = "plr";
    inline const juce::String minPsr           = "minPsr";
    inline const juce::String dr               = "dr";
    inline const juce::String correlation      = "correlation";
    inline const juce::String widthPercent     = "widthPercent";
    inline const juce::String lowEndWidth      = "lowEndWidthPercent";
    inline const juce::String spectralTilt     = "spectralTilt";
    inline const juce::String crestLow         = "crestLow";
    inline const juce::String crestMid         = "crestMid";
    inline const juce::String crestHigh        = "crestHigh";
}

/**
    Dove andrà il master. Per musica e streaming loudness e picchi sono scelte di mastering; per broadcast e
    podcast sono specifiche di consegna (EBU R128, ATSC A/85, AES TD1004): fuori tolleranza il file non passa
    il controllo tecnico e si corregge con un guadagno, non con il limiter.
*/
enum class ProfileContext { music, streaming, broadcast, podcast };

/**
    Il target si sceglie in due parti. Il genere dice come deve suonare il master (curva tonale, stereo, LRA);
    la consegna dice dove andrà (loudness, true peak, PLR, PSR, DR, crest): un rock per Spotify ha il tonale
    del rock e la loudness di Spotify.
*/
enum class ProfileKind { genre, delivery };

/** Id speciali della scelta del target. */
namespace targetIds
{
    inline const juce::String reference = "__reference__";   // genere: il brano di riferimento caricato
    inline const juce::String noGenre   = "none";            // genere: nessuno, solo le specifiche della consegna
    inline const juce::String loud      = "loud";            // consegna: loudness e dinamica dal genere
    inline const juce::String defaultGenre    = "pop";
    inline const juce::String defaultDelivery = "streaming_generic";
}

struct TargetProfile
{
    juce::String id;
    juce::String name;
    juce::String shortName;     // per il pulsante del target ("Pop" invece di "Pop commerciale"); vuoto = name
    juce::String description;
    bool isReference = false;   // generato dall'analisi di un brano
    ProfileContext context = ProfileContext::music;   // JSON "context"; senza campo: streaming se c'è normalizationLufs
    ProfileKind kind = ProfileKind::genre;            // JSON "kind"; senza campo: consegna se il contesto non è musica
    bool dynamicsFromGenre = false;                   // consegna "Master forte": loudness e dinamica le dà il genere

    /** Broadcast o podcast: loudness e true peak sono requisiti di consegna. */
    bool isDeliveryStandard() const noexcept { return context == ProfileContext::broadcast || context == ProfileContext::podcast; }

    juce::String getShortName() const { return shortName.isNotEmpty() ? shortName : name; }

    /** Target composto: la consegna da cui vengono loudness e picchi ("EBU R128 (TV e radio)"), per i consigli. */
    juce::String deliveryName;
    juce::String getDeliveryName() const { return deliveryName.isNotEmpty() ? deliveryName : name; }

    std::map<juce::String, MetricRange> metrics;

    std::optional<std::array<float, kNumThirdOctaves>> tonalCurve;   // normalizzata come AnalysisSnapshot::thirdOctaveDb
    float tonalTolerance = 2.0f;

    std::optional<std::array<float, kNumBands>> bandWidthPercent;
    float bandWidthTolerance = 10.0f;

    // --- piattaforme con normalizzazione del volume ------------------------------------------------
    std::optional<float> normalizationLufs;    // livello a cui la piattaforma riporta i brani (es. -14 LUFS)
    std::optional<float> loudTruePeakMax;      // true peak consigliato per i master più forti di normalizationLufs

    // --- campi opzionali dei profili creati da brani di riferimento ------------------------------
    std::optional<std::array<float, kNumThirdOctaves>> tonalCurveLoudest;   // sola sezione più forte (ritornello/drop)
    std::optional<std::array<float, kNumBands>> bandTolerance;              // tolleranza tonale per banda (da più brani: dispersione reale)
    std::optional<std::array<float, kNumBands>> bandWidthTolerances;        // tolleranza di larghezza per banda
    std::optional<std::array<float, kNumBands>> bandBurstDb;                // quanto ogni banda arriva "a raffiche" nei brani di riferimento
    int sourceCount = 0;                                                    // brani analizzati (0 = profilo di mercato)
    juce::StringArray sources;

    const MetricRange* getMetric (const juce::String& key) const;

    static std::optional<TargetProfile> fromJson (const juce::String& json);
    juce::String toJson() const;

    /** Profilo con i valori di un brano analizzato come target e tolleranze di default. */
    static TargetProfile fromSnapshot (const AnalysisSnapshot& s, const juce::String& name);

    /**
        Profilo da più brani di riferimento (consigliati 3-5 della stessa estetica): target = media,
        tolleranze = dispersione reale tra i brani (deviazione standard), mai sotto quelle di un singolo reference.
        Così un genere con bassi molto variabili tollera di più sui bassi, e uno coerente è più severo.
    */
    static TargetProfile fromSnapshots (const std::vector<AnalysisSnapshot>& snapshots, const juce::String& name,
                                        const juce::StringArray& sourceNames = {});
};

/** Metriche che decide la consegna (loudness, picchi, densità); le altre le decide il genere. */
bool isDeliveryMetric (const juce::String& key);

/**
    Target composto da genere e consegna:
    - dal genere: curve tonali e tolleranze, larghezza stereo, raffiche, LRA, tilt, correlazione;
    - dalla consegna: integrated, true peak, PLR, PSR, DR e crest per banda, più normalizzazione e contesto.
      Le metriche che la consegna non ha non si giudicano (Spotify non ha un DR: un master dinamico va bene).
    Con la consegna "Master forte" tutto viene dal genere (con un reference: "Come il reference").
    Senza genere (nullptr) restano solo le specifiche della consegna, come i vecchi profili broadcast e podcast.
*/
TargetProfile composeTarget (const TargetProfile* genre, const TargetProfile& delivery);

/** "Rock · Spotify", "EBU R128", "Reference": nome breve del target composto. */
juce::String composedShortName (const TargetProfile* genre, const TargetProfile& delivery);

/** Scelta del target: id del genere (o targetIds::reference / noGenre) e della consegna. */
struct TargetSelection
{
    juce::String genre, delivery;
    bool operator== (const TargetSelection& o) const { return genre == o.genre && delivery == o.delivery; }
};

class ProfileLibrary;

/**
    Vecchio target unico (stato dei progetti, MA_PROFILE) -> genere e consegna:
    piattaforma streaming -> (Pop, piattaforma), perché quei profili usavano la curva Pop;
    broadcast e podcast -> (nessun genere, consegna); genere, reference e profili utente -> (quello, Master forte).
*/
TargetSelection migrateTargetId (const ProfileLibrary& library, const juce::String& oldId);

/** Profili inclusi nel plugin + profili utente (%APPDATA%/MasterAgent/profiles). */
class ProfileLibrary
{
public:
    ProfileLibrary();

    void reload();
    const std::vector<TargetProfile>& getProfiles() const noexcept { return profiles; }
    const TargetProfile* findById (const juce::String& id) const;
    /** Profilo di quel tipo con quell'id, oppure nullptr. */
    const TargetProfile* find (const juce::String& id, ProfileKind kind) const;

    static juce::File getUserProfileDirectory();
    bool saveUserProfile (const TargetProfile& profile, juce::String& error);

private:
    std::vector<TargetProfile> profiles;
};

} // namespace ma
