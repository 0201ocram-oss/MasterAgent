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

struct TargetProfile
{
    juce::String id;
    juce::String name;
    juce::String description;
    bool isReference = false;   // generato dall'analisi di un brano

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

/** Profili inclusi nel plugin + profili utente (%APPDATA%/MasterAgent/profiles). */
class ProfileLibrary
{
public:
    ProfileLibrary();

    void reload();
    const std::vector<TargetProfile>& getProfiles() const noexcept { return profiles; }
    const TargetProfile* findById (const juce::String& id) const;

    static juce::File getUserProfileDirectory();
    bool saveUserProfile (const TargetProfile& profile, juce::String& error);

private:
    std::vector<TargetProfile> profiles;
};

} // namespace ma
