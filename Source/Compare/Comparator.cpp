#include "Comparator.h"
#include "../Common/Text.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ma
{

juce::String severityName (Severity s)
{
    switch (s)
    {
        case Severity::ok:       return "OK";
        case Severity::info:     return "Info";
        case Severity::warning:  return "Attenzione";
        case Severity::critical: return "Critico";
    }
    return {};
}

juce::String phaseName (WorkPhase p)
{
    return p == WorkPhase::mix ? "Mix" : "Master";
}

int ComparisonResult::count (Severity s) const
{
    return (int) std::count_if (findings.begin(), findings.end(), [s] (const Finding& f) { return f.severity == s && ! f.ignored; });
}

int ComparisonResult::countIgnored() const
{
    return (int) std::count_if (findings.begin(), findings.end(), [] (const Finding& f) { return f.ignored; });
}

juce::String areaName (Area a)
{
    switch (a)
    {
        case Area::loudness:  return "Loudness e picchi"_t;
        case Area::dynamics:  return "Dinamica"_t;
        case Area::tonal:     return "Tonale"_t;
        case Area::stereo:    return "Stereo"_t;
        case Area::technical: return "Tecnici"_t;
        case Area::none:      break;
    }
    return {};
}

Area areaForCategory (const juce::String& category)
{
    if (category == "Loudness" || category == "Peak") return Area::loudness;
    if (category == "Dinamica")                       return Area::dynamics;
    if (category == "Tonale")                         return Area::tonal;
    if (category == "Stereo")                         return Area::stereo;
    if (category == "Tecnico")                        return Area::technical;
    return Area::none;
}

float tonalToleranceForBand (const TargetProfile& profile, int band)
{
    if (profile.bandTolerance)
        return (*profile.bandTolerance)[(size_t) band];

    static constexpr std::array<float, kNumBands> scale { 1.6f, 1.15f, 1.0f, 1.0f, 1.0f, 1.1f, 1.2f, 1.4f };
    return profile.tonalTolerance * scale[(size_t) band];
}

float bandTonalDelta (const std::array<float, kNumThirdOctaves>& master,
                      const std::array<float, kNumThirdOctaves>& target, int band)
{
    double sumMaster = 0.0, sumTarget = 0.0;
    for (int i = 0; i < kNumThirdOctaves; ++i)
    {
        if (bandOfThirdOctave (i) == band)
        {
            sumMaster += std::pow (10.0, master[(size_t) i] / 10.0);
            sumTarget += std::pow (10.0, target[(size_t) i] / 10.0);
        }
    }
    if (sumMaster <= 0.0 || sumTarget <= 0.0)
        return 0.0f;
    return (float) (10.0 * std::log10 (sumMaster / sumTarget));
}

Severity ComparisonResult::severityOfKey (const juce::String& key) const
{
    auto worst = Severity::ok;
    for (const auto& f : findings)
        if (f.key == key && ! f.ignored)
            worst = std::max (worst, f.severity);
    return worst;
}

juce::String formatSongTime (float seconds)
{
    const int s = std::max (0, (int) std::lround (seconds));
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

juce::String noteName (int midiNote)
{
    if (midiNote < 0)
        return {};
    const auto pc = (size_t) (midiNote % 12);
    return juce::String (kNoteNamesIt[pc]) + " (" + kNoteNamesEn[pc] + juce::String (midiNote / 12 - 1) + ")";
}

namespace
{
    juce::String fmt (float v, int decimals = 1, const juce::String& unit = {})
    {
        const auto number = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return number + (unit.isEmpty() ? juce::String() : " " + unit);
    }

    juce::String signedFmt (float v, int decimals, const juce::String& unit)
    {
        return (v > 0.0f ? "+" : "") + fmt (v, decimals, unit);
    }

    juce::String rangeText (const MetricRange& r, int decimals, const juce::String& unit)
    {
        if (std::abs (r.max - r.min) < 1e-4f)
            return fmt (r.target, decimals, unit);
        return fmt (r.min, decimals) + " .. " + fmt (r.max, decimals, unit) + "  (target " + fmt (r.target, decimals) + ")";
    }

    juce::String bandRangeText (int band)
    {
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : juce::String ((int) f) + " Hz"; };
        return hz (kBandEdges[(size_t) band]) + " - " + hz (kBandEdges[(size_t) band + 1]);
    }

    juce::String capitalised (const juce::String& text)
    {
        return text.substring (0, 1).toUpperCase() + text.substring (1);
    }

    /**
        Effetto percepito di eccesso/carenza in ogni banda (B. Owsinski, "Magic Frequencies"), correzione sul bus
        e, per la fase di mix, le tracce che di solito ne sono responsabili.
    */
    struct BandCharacter
    {
        const char* whenHigh;
        const char* whenLow;
        const char* fix;        // correzione di mastering sul bus
        const char* mixHigh;    // in mix: chi causa l'eccesso e cosa fare sulle tracce
        const char* mixLow;     // in mix: cosa manca e dove recuperarlo
    };

    const std::array<BandCharacter, kNumBands>& bandCharacters()
    {
        static const std::array<BandCharacter, kNumBands> c {{
            { "troppo sub-bass rende il suono impastato e consuma headroom del limiter",
              "manca la sensazione di potenza, il pezzo risulta leggero sui sistemi con subwoofer",
              "shelf basso o EQ dinamico sotto i 60 Hz; spesso è il kick o l'808 da sistemare nel mix",
              "di solito kick e basso/808 si sovrappongono nel sub. Decidi chi dei due lo occupa (l'altro con high-pass o EQ), "
              "metti un high-pass su tutto ciò che non deve avere sub (pad, chitarre, voci, FX) e valuta un sidechain del basso dal kick",
              "il sub è affidato a kick e basso: rinforza la loro fondamentale (layer sub, armonica più bassa) e controlla che "
              "nessun high-pass li tagli troppo in alto" },
            { "bassi rimbombanti (boomy), il basso copre il kick",
              "suono sottile (thin), manca il corpo della sezione ritmica",
              "campana larga tra 60 e 250 Hz",
              "basso, kick, tom e la mano sinistra di piano e synth si accumulano qui. Spesso togliere un po' di basse al basso "
              "lo rende più definito (S. Savage, Mixing and Mastering in the Box, 3.1); controlla gli high-pass di chitarre (80-120 Hz) e voci",
              "alza il corpo di basso e kick (80-200 Hz) e verifica che gli high-pass sulle altre tracce non siano troppo aggressivi" },
            { "eccesso di pienezza: il classico fango (mud) sui 250-500 Hz",
              "manca calore e corpo",
              "campana larga attorno ai 300 Hz",
              "il fango nasce da chitarre ritmiche, piano, pad, voci registrate vicine (effetto prossimità), microfoni ambiente "
              "e code dei riverberi: taglia 200-400 Hz su queste tracce e metti un high-pass ai ritorni dei riverberi",
              "manca il calore di chitarre, piano e voci: riduci i tagli in questa zona o alza un po' gli strumenti armonici" },
            { "suono nasale (500 Hz-1 kHz) o metallico (1-2 kHz), a lungo andare affatica",
              "medi scavati: voce e strumenti perdono sostanza",
              "campana larga tra 500 Hz e 2 kHz; in mastering spesso si crea spazio alla voce scavando appena sotto la sua zona",
              "chitarre distorte (800 Hz-1 kHz), rullante scatolato, synth e voci nasali: trova la traccia che suona nasale o "
              "metallica (sweep con una campana stretta) e attenuala lì",
              "medi scavati, spesso per troppi EQ \"a sorriso\" sulle singole tracce: riduci i tagli sui medi di voce, chitarre e rullante" },
            { "zona 2-4 kHz aspra: è la causa più comune di fatica d'ascolto",
              "manca attacco e intelligibilità",
              "campana larga attorno ai 3 kHz (bastano pochi decimi di dB: agisce su quasi tutti gli strumenti)",
              "voce, chitarre distorte, synth e attacco del rullante si contendono i 2-4 kHz: trova la traccia aspra e attenuala lì; "
              "se la voce deve restare davanti, scava appena questa zona negli strumenti invece di alzarla nella voce",
              "manca intelligibilità: alza la presenza della voce (2-4 kHz) o l'attacco di rullante e chitarre" },
            { "suono troppo vicino e aggressivo",
              "suono distante e poco definito",
              "campana larga attorno ai 5 kHz",
              "voce, piatti e battente del kick troppo in avanti: attenua 4-6 kHz sulla traccia più aggressiva",
              "suono distante: aggiungi presenza a voce e batteria, o riduci i riverberi che allontanano" },
            { "brillantezza eccessiva: sibilanti e piatti in evidenza",
              "suono opaco, poca chiarezza",
              "shelf alto morbido o de-esser se il problema sono le sibilanti",
              "sibilanti della voce, hi-hat, piatti e shaker: de-esser sulla voce, shelf o campana sulle tracce di piatti",
              "manca brillantezza: shelf alto su voce e overhead, oppure piatti più presenti" },
            { "aria eccessiva: fruscio o asprezza nell'estremo alto",
              "manca aria e apertura",
              "shelf alto sopra i 10-12 kHz",
              "riverberi molto brillanti, piatti ed exciter: attenua l'estremo alto sui ritorni FX e sugli overhead",
              "manca aria: shelf morbido sopra i 10-12 kHz su voce e bus degli effetti" },
        }};
        return c;
    }

    /**
        Consiglio di mastering quando non c'è una mossa EQ calcolata. In mastering si interviene raramente
        più di 1-2 dB con EQ larghi (S. Savage, Mixing and Mastering in the Box, cap. 11.2).
    */
    juce::String masterEqAdvice (int band, float delta, bool genericCurve)
    {
        const auto fix = juce::String::fromUTF8 (bandCharacters()[(size_t) band].fix);
        const float mag = std::abs (delta);

        if (mag > 4.0f)
            return "Scostamento ampio: in mastering raramente si corregge più di 1-2 dB, conviene intervenire nel mix ("_u + fix + ").";

        // con una curva generica si suggerisce metà dello scostamento: è una media di mercato, non un obiettivo
        const float suggested = juce::jlimit (0.5f, 2.0f, std::round ((genericCurve ? mag * 0.5f : mag) * 2.0f) / 2.0f);
        return juce::String (delta > 0 ? "Prova un'attenuazione di circa " : "Prova un'enfasi di circa ")
             + juce::String (suggested, 1) + " dB, Q larga (0.7-1): " + fix
             + ". Confronta a volume allineato: un boost sembra migliore solo perché è più forte."_u;
    }

    /** Eccesso che arriva a raffiche: uno strumento dinamico agisce solo quando serve (S. Savage, 4.4 "Dynamic EQs"). */
    juce::String burstAction (int band, WorkPhase phase)
    {
        const bool mix = phase == WorkPhase::mix;
        if (band <= 1)
            return mix ? "probabilmente alcune note del basso o dell'808 rimbombano: EQ dinamico o compressione sulla traccia del basso, "
                         "oppure correggi le singole note."_u
                       : "probabilmente alcune note dei bassi rimbombano: EQ dinamico o multibanda sulla zona invece di un taglio statico."_u;
        if (band <= 3)
            return mix ? "l'accumulo c'è solo in alcuni passaggi: automazione o EQ dinamico sulla traccia responsabile in quei punti."_u
                       : "l'accumulo c'è solo in alcuni passaggi: EQ dinamico sulla zona invece di un taglio statico."_u;
        if (band <= 5)
            return mix ? "alcune frasi della voce o delle chitarre sono aspre: EQ dinamico sulla traccia, che intervenga solo su quelle."_u
                       : "asprezza a tratti: EQ dinamico nella zona 2-6 kHz invece di un taglio statico."_u;
        return mix ? "sono sibilanti o colpi di piatti: de-esser sulla voce (o automazione delle \"s\" più forti, Savage 5.7)."_u
                   : "sibilanti o piatti a tratti: de-esser a banda larga sul mix invece di uno shelf statico."_u;
    }

    struct MetricText
    {
        juce::String key;          // elemento della dashboard da evidenziare
        juce::String category, label, unit;
        int decimals;
        juce::String whenHigh, whenLow;
        int stage;
    };

    const std::map<juce::String, MetricText>& metricTexts()
    {
        static const std::map<juce::String, MetricText> texts {
            { metric::integratedLufs, { highlight::integrated, "Loudness", "Integrated loudness", "LUFS", 1,
                "Master più forte del target: riduci il guadagno in ingresso al limiter/maximizer. Le piattaforme lo abbasseranno comunque, perdendo solo dinamica."_u,
                "Master più basso del target: aumenta il guadagno nel limiter con moderazione, controllando PLR e distorsione."_u, stage::loudness } },
            { metric::truePeakMax, { highlight::truePeak, "Peak", "True peak max", "dBTP", 1,
                "Abbassa il ceiling del limiter (es. -1.0 dBTP) e abilita la modalità true-peak/oversampling: la codifica lossy (AAC/Ogg/MP3) può generare clipping."_u,
                "Headroom di picco inutilizzato: puoi alzare il ceiling del limiter verso il target.", stage::loudness } },
            { metric::loudnessRange, { highlight::lra, "Loudness", "Loudness Range (LRA)", "LU", 1,
                "Differenza ampia tra sezioni piano e forti: valuta automazioni di volume o una compressione lenta (glue/bus) per uniformare.",
                "Sezioni molto uniformi: riduci il limiting/compressione o crea contrasto con automazioni tra strofe e ritornelli.", stage::dynamics } },
            { metric::plr, { highlight::plr, "Dinamica", "PLR (Peak to Loudness)", "dB", 1,
                "PLR alto: c'è margine per più densità (compressione/limiting) se il genere lo richiede."_u,
                "PLR basso: master molto limitato. Riduci il guadagno nel limiter o distribuisci il lavoro (clipper morbido + limiter, compressione di bus). "
                "Come riferimento, un limiting moderato riduce i picchi di circa 3-6 dB nei passaggi più forti."_u, stage::dynamics } },
            { metric::minPsr, { highlight::psr, "Dinamica", "PSR minimo (sezione più forte)"_u, "dB", 1,
                "La sezione più forte ha più transiente del target: puoi aumentare la densità nel ritornello/drop."_u,
                "La sezione più forte è schiacciata: i transienti del ritornello/drop vengono sacrificati. Riduci il limiting o usa un transient shaper prima del limiter."_u, stage::dynamics } },
            { metric::dr, { highlight::dr, "Dinamica", "DR (Dynamic Range)", "dB", 1,
                "Dinamica più ampia del target: valuta una leggera compressione di bus."_u,
                "Dinamica ridotta rispetto al target: riduci compressione/limiting complessivi.", stage::dynamics } },
            { metric::correlation, { highlight::correlation, "Stereo", "Correlazione di fase", "", 2,
                "Immagine molto mono rispetto al target: valuta di allargare medi/alti (M/S EQ sul side, stereo imager sopra 1-2 kHz).",
                "Correlazione bassa: rischio di cancellazioni in mono. Controlla stereo widener, chorus, riverberi e rendi mono il low-end.", stage::lowEnd } },
            { metric::widthPercent, { highlight::width, "Stereo", "Larghezza stereo (S/(M+S))", "%", 0,
                "Immagine più larga del target: riduci il canale side (M/S EQ o imager) e verifica la compatibilità mono."_u,
                "Immagine più stretta del target: allarga medi/alti sul side senza toccare i bassi."_u, stage::tonal } },
            { metric::lowEndWidth, { highlight::lowEndWidth, "Stereo", "Side sotto 100 Hz", "%", 0,
                "Basse frequenze troppo larghe: rendi mono sotto 100-150 Hz (bass mono / taglio del side con EQ M/S).",
                "Low-end molto mono: è corretto per la maggior parte dei generi."_u, stage::lowEnd } },
            { metric::spectralTilt, { highlight::tilt, "Tonale", "Tilt spettrale (100 Hz-10 kHz)", "dB/oct", 2,
                "Bilanciamento complessivo più brillante del target: un tilt EQ verso il basso o un'attenuazione larga delle alte."_u,
                "Bilanciamento complessivo più scuro del target: un tilt EQ verso l'alto o uno shelf sulle alte."_u, stage::tonal } },
            { metric::crestLow, { highlight::crestLow, "Dinamica", "Crest factor bassi (<200 Hz)", "dB", 1,
                "Bassi con molto transiente rispetto al target: valuta compressione/saturazione sul low-end per più costanza."_u,
                "Bassi molto compressi: riduci compressione multibanda/clipping sulle basse, il kick perde impatto.", stage::dynamics } },
            { metric::crestMid, { highlight::crestMid, "Dinamica", "Crest factor medi", "dB", 1,
                "Medi con molto transiente rispetto al target: valuta più densità sui medi."_u,
                "Medi molto compressi: voci e rullante possono risultare piatti, riduci la compressione sui medi.", stage::dynamics } },
            { metric::crestHigh, { highlight::crestHigh, "Dinamica", "Crest factor alti (>4 kHz)", "dB", 1,
                "Alti con transienti marcati: controlla sibilanti e piatti (de-esser, compressione dinamica sulle alte).",
                "Alti molto compressi: rischio di suono affaticante/piatto, riduci limiting o clipping sulle alte.", stage::dynamics } },
        };
        return texts;
    }

    /** Metriche che descrivono il master finito: sul mix bus vengono decise dal mastering. */
    bool isMasteringOnly (const juce::String& key)
    {
        static const std::set<juce::String> keys { metric::integratedLufs, metric::truePeakMax, metric::plr, metric::minPsr,
                                                   metric::dr, metric::crestLow, metric::crestMid, metric::crestHigh };
        return keys.count (key) > 0;
    }

    Severity severityFor (float value, const MetricRange& r, float& delta)
    {
        if (value >= r.min && value <= r.max)
        {
            delta = 0.0f;
            return Severity::ok;
        }
        delta = value > r.max ? value - r.max : value - r.min;
        return std::abs (delta) <= r.warn ? Severity::warning : Severity::critical;
    }

    struct FindingList
    {
        std::vector<Finding>& out;

        Finding& add (const juce::String& key, const juce::String& category, const juce::String& metricName,
                      const juce::String& value, const juce::String& target, float delta, Severity sev, const juce::String& message,
                      int findingStage)
        {
            Finding f { category, metricName, value, target, delta, sev, message, key };
            f.stage = findingStage;
            f.id = key + "|" + metricName;
            f.area = key == highlight::clip ? Area::technical : areaForCategory (category);   // il clipping è un difetto tecnico
            out.push_back (std::move (f));
            return out.back();
        }

        Finding* find (const juce::String& key)
        {
            for (auto& f : out)
                if (f.key == key)
                    return &f;
            return nullptr;
        }
    };

    bool isProblem (const Finding* f) { return f != nullptr && f->severity >= Severity::warning; }

    /** Sul mix bus: margine per il mastering e niente brickwall nel file da consegnare. */
    void addMixDeliveryChecks (const AnalysisSnapshot& s, FindingList& list)
    {
        list.add (highlight::integrated, "Loudness", "Integrated loudness", fmt (s.integratedLufs, 1, "LUFS"), "deciso dal mastering", 0.0f, Severity::info,
                  "In fase di mix la loudness finale non è un obiettivo: la imposta il mastering. Lavora a un volume d'ascolto costante "
                  "e lascia margine sui picchi."_u, stage::loudness);

        const float tp = s.truePeakMaxDb;
        auto sev = Severity::ok;
        float delta = 0.0f;
        juce::String message ("Margine adeguato per il mastering.");
        juce::String action;
        if (tp > -1.0f || s.overs0dB > 0)
        {
            sev = Severity::critical;
            delta = tp + 3.0f;
            message = "Il mix arriva quasi a fondo scala: al mastering non resta margine e qualunque elaborazione rischia di clippare."_u;
            action = "Abbassa il master fader (o tutti i gruppi/VCA in proporzione) di circa " + fmt (std::ceil (tp + 4.5f), 0, "dB")
                   + ", finché i picchi stanno tra -6 e -3 dBTP: il suono non cambia, cambia solo il margine."_u;
        }
        else if (tp > -3.0f)
        {
            sev = Severity::warning;
            delta = tp + 3.0f;
            message = "Picchi sopra -3 dBTP: margine scarso per il mastering.";
            action = "Abbassa il master fader (o i gruppi) di circa " + fmt (std::ceil (tp + 4.5f), 0, "dB") + " per lasciare 3-6 dB di margine.";
        }
        else if (tp < -12.0f)
        {
            sev = Severity::info;
            message = "Molto margine: in virgola mobile non è un problema, ma i plugin con soglie fisse (compressori, saturatori, "
                      "emulazioni analogiche) potrebbero lavorare a un livello diverso da quello previsto."_u;
        }
        auto& headroom = list.add (highlight::truePeak, "Peak", "Headroom per il mastering", fmt (tp, 1, "dBTP"), "-6 .. -3 dBTP",
                                   delta, sev, message, stage::loudness);
        headroom.action = action;

        if (s.integratedLufs > -70.0f && s.plr < 9.0f)
        {
            auto& f = list.add (highlight::plr, "Dinamica", "Limiting sul mix bus", fmt (s.plr, 1, "dB") + " PLR", "> 9 dB", s.plr - 9.0f,
                                Severity::warning,
                                "Il mix sembra già limitato o molto compresso: il mastering avrà meno margine e il limiting si sommerà."_u,
                                stage::dynamics);
            f.action = "Puoi tenere un limiter mentre mixi per sentire come suonerà il master, ma consegna la versione senza "
                       "(S. Savage, Mixing and Mastering in the Box, 8.1): il brickwall va applicato una sola volta, alla fine."_u;
        }
    }

    /** Master spostato in dB perché la mediana degli scostamenti dal target (63 Hz - 12.5 kHz) sia zero. */
    std::array<float, kNumThirdOctaves> alignToTarget (const std::array<float, kNumThirdOctaves>& master,
                                                       const std::array<float, kNumThirdOctaves>& target)
    {
        std::vector<float> deltas;
        for (int i = 5; i <= 28; ++i)
            deltas.push_back (master[(size_t) i] - target[(size_t) i]);
        std::nth_element (deltas.begin(), deltas.begin() + (long) (deltas.size() / 2), deltas.end());
        // 24 valori: la mediana è la media del 12° e del 13° più piccolo
        const float offset = 0.5f * (deltas[deltas.size() / 2] + *std::max_element (deltas.begin(), deltas.begin() + (long) (deltas.size() / 2)));

        auto aligned = master;
        for (auto& v : aligned)
            v -= offset;
        return aligned;
    }

    /** Elenco leggibile delle bande: "Low-mid e Mid". */
    juce::String bandList (const std::vector<int>& bands)
    {
        juce::StringArray names;
        for (int b : bands)
            names.add (kBandNames[(size_t) b]);
        if (names.size() <= 1)
            return names.joinIntoString ("");
        return names.joinIntoString (", ", 0, names.size() - 1) + " e " + names[names.size() - 1];
    }
}

ComparisonResult compare (const AnalysisSnapshot& s, const TargetProfile& profile, const CompareOptions& options)
{
    ComparisonResult result;
    result.phase = options.phase;
    FindingList list { result.findings };
    const bool mix = options.phase == WorkPhase::mix;

    if (! s.valid || s.integratedLufs <= kSilenceDb + 1.0f)
    {
        list.add ({}, "Analisi", "Segnale", "-", "-", 0.0f, Severity::info,
                  "In attesa di segnale: avvia la riproduzione del brano dall'inizio per un'analisi completa.", stage::analysis);
        return result;
    }

    // In modalità Live la finestra è di pochi secondi: LRA e DR descrivono il brano intero, non hanno senso.
    const bool live = s.liveMode;

    // --- metriche del profilo ---------------------------------------------------
    const std::map<juce::String, float> values {
        { metric::integratedLufs, s.integratedLufs },
        { metric::truePeakMax,    s.truePeakMaxDb },
        { metric::loudnessRange,  live ? NAN : s.loudnessRange },
        { metric::plr,            s.plr },
        { metric::minPsr,         s.minPsr },
        { metric::dr,             (s.drValid && ! live) ? s.drValue : NAN },
        { metric::correlation,    s.correlation },
        { metric::widthPercent,   s.widthPercent },
        { metric::lowEndWidth,    s.lowEndWidthPercent },
        { metric::spectralTilt,   s.spectralTiltDbPerOct },
        { metric::crestLow,       s.bandCrestDb[0] },
        { metric::crestMid,       s.bandCrestDb[1] },
        { metric::crestHigh,      s.bandCrestDb[2] },
    };

    for (const auto& [key, text] : metricTexts())
    {
        // sul mix bus loudness, picchi e densità del master finito li decide il mastering
        if (mix && isMasteringOnly (key))
            continue;

        const auto* range = profile.getMetric (key);
        const auto it = values.find (key);
        if (range == nullptr || it == values.end() || std::isnan (it->second))
            continue;

        float delta = 0.0f;
        auto sev = severityFor (it->second, *range, delta);

        // un true peak più basso del target non è un problema, solo un'informazione
        if (key == metric::truePeakMax && delta < 0.0f)
            sev = Severity::info;
        // low-end più mono del target: va bene
        if (key == metric::lowEndWidth && delta < 0.0f)
            sev = Severity::ok;
        // nel mix l'LRA cambierà ancora con il mastering: al massimo da verificare
        if (mix && key == metric::loudnessRange)
            sev = std::min (sev, Severity::warning);

        juce::String message ("Nel range.");
        if (sev != Severity::ok)
            message = (delta > 0.0f ? text.whenHigh : text.whenLow)
                    + "  (" + signedFmt (delta, std::max (1, text.decimals), text.unit)
                    + (delta > 0.0f ? " sopra il massimo)" : " sotto il minimo)");

        list.add (text.key, text.category, text.label, fmt (it->second, text.decimals, text.unit),
                  rangeText (*range, text.decimals, text.unit), delta, sev, message, text.stage);
    }

    if (mix)
        addMixDeliveryChecks (s, list);

    // --- bilanciamento tonale -------------------------------------------------------
    if (profile.tonalCurve)
    {
        // su quale porzione del brano confrontare
        const auto* masterCurve = &s.thirdOctaveDb;
        const auto* targetCurve = &*profile.tonalCurve;
        const bool targetHasLoudest = profile.tonalCurveLoudest.has_value();

        if (options.tonalBasis == TonalBasis::automatic && live && targetHasLoudest)
        {
            // la finestra Live è una sezione del brano (spesso il ritornello in loop): si confronta con la sezione più forte del target
            targetCurve = &*profile.tonalCurveLoudest;
            result.tonalUsesLoudest = true;
        }
        else if (options.tonalBasis == TonalBasis::loudestSection)
        {
            if (targetHasLoudest)
                targetCurve = &*profile.tonalCurveLoudest;
            if (! live && s.loudestSectionSeconds > 0.0f)
                masterCurve = &s.thirdOctaveLoudestDb;
            result.tonalUsesLoudest = targetHasLoudest || masterCurve != &s.thirdOctaveDb;
        }

        result.hasTonalTarget = true;
        result.tonalTarget = *targetCurve;

        // Le curve sono normalizzate sulla media 63 Hz - 12.5 kHz: un eccesso forte in una zona (es. sibilanti)
        // alza quella media e farebbe sembrare carenti tutte le altre bande. Si riallinea il master sulla
        // mediana degli scostamenti, che ignora le zone anomale.
        const auto aligned = alignToTarget (*masterCurve, *targetCurve);
        masterCurve = &aligned;
        for (int i = 0; i < kNumThirdOctaves; ++i)
            result.tonalDelta[(size_t) i] = (*masterCurve)[(size_t) i] - (*targetCurve)[(size_t) i];

        // Le curve dei profili di mercato sono modelli generici: al massimo "da verificare".
        // Con un reference vero lo scostamento può essere critico.
        const bool genericCurve = ! profile.isReference;

        std::array<Severity, kNumBands> bandSeverity {};
        bool anyBandProblem = false;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float tol = tonalToleranceForBand (profile, b);
            const float d = bandTonalDelta (*masterCurve, *targetCurve, b);
            result.bandTonalDelta[(size_t) b] = d;
            result.bandTolerance[(size_t) b] = tol;

            auto sev = std::abs (d) <= tol ? Severity::ok : (std::abs (d) <= 2.0f * tol ? Severity::warning : Severity::critical);
            if (genericCurve)
                sev = std::min (sev, Severity::warning);
            bandSeverity[(size_t) b] = sev;
            anyBandProblem = anyBandProblem || sev >= Severity::warning;
        }

        // mosse EQ concrete che spiegano gli scostamenti
        std::array<int, kNumBands> moveOfBand {};
        moveOfBand.fill (-1);
        if (anyBandProblem)
        {
            EqFitOptions fit;
            fit.maxMoves = mix ? 4 : 3;
            fit.minGainDb = mix ? 1.0f : 0.5f;
            auto moves = suggestEqMoves (result.tonalDelta, result.bandTolerance, fit);

            // solo le mosse che correggono almeno una banda segnalata
            moves.erase (std::remove_if (moves.begin(), moves.end(), [&] (const EqMove& m)
            {
                return std::none_of (m.bands.begin(), m.bands.end(), [&] (int b) { return bandSeverity[(size_t) b] >= Severity::warning; });
            }), moves.end());

            for (auto& m : moves)
            {
                // con una curva generica metà dello scostamento; in mastering al massimo 2 dB, in mix 6 dB
                const float magnitude = std::abs (m.gainDb) * (genericCurve ? 0.5f : 1.0f);
                float shown = std::max (0.5f, std::round (magnitude * 2.0f) / 2.0f);
                if (mix)
                {
                    shown = std::min (shown, 6.0f);
                }
                else
                {
                    m.fixInMix = magnitude > 4.0f;
                    shown = std::min (shown, 2.0f);
                }
                m.shownGainDb = std::copysign (shown, m.gainDb);
                m.q = std::round (m.q * 10.0f) / 10.0f;
            }
            result.eqMoves = std::move (moves);

            for (int i = 0; i < (int) result.eqMoves.size(); ++i)
                for (int b : result.eqMoves[(size_t) i].bands)
                    if (moveOfBand[(size_t) b] < 0)
                        moveOfBand[(size_t) b] = i;
        }

        // una diagnosi per mossa: a parità di severità precede le bande che spiega
        for (int i = 0; i < (int) result.eqMoves.size(); ++i)
        {
            const auto& m = result.eqMoves[(size_t) i];
            auto sev = Severity::ok;
            int mainBand = m.bands.front();
            for (int b : m.bands)
            {
                sev = std::max (sev, bandSeverity[(size_t) b]);
                if (std::abs (result.bandTonalDelta[(size_t) b]) > std::abs (result.bandTonalDelta[(size_t) mainBand]))
                    mainBand = b;
            }

            juce::String message;
            juce::String action;
            if (mix)
            {
                message << "Sul bus questa mossa riporterebbe " << bandList (m.bands) << " verso il target (scostamento stimato "
                        << signedFmt (m.gainDb, 1, "dB") << "). In mix conviene ottenerla sulle tracce responsabili."_u;
                const auto* culprits = m.gainDb < 0.0f ? bandCharacters()[(size_t) mainBand].mixHigh : bandCharacters()[(size_t) mainBand].mixLow;
                action = capitalised (juce::String::fromUTF8 (culprits)) + ".";
            }
            else
            {
                message << "Una sola mossa per riportare " << bandList (m.bands) << " verso il target.";
                if (m.fixInMix)
                    message << " Lo scostamento stimato è di "_u << fmt (std::abs (m.gainDb), 1, "dB")
                            << ": in mastering si corregge al massimo 1-2 dB (S. Savage, 11.2), il resto va sistemato nel mix."_u;
                action << "EQ sul bus: " << m.describe (m.shownGainDb) << ".";
            }
            if (genericCurve)
                message << " Guadagno dimezzato: la curva del profilo è una media di genere, conferma con un brano di riferimento."_u;

            auto& f = list.add (highlight::eqMove (i), "Tonale", "Mossa EQ " + juce::String (i + 1),
                                m.describe (m.shownGainDb), "corregge " + bandList (m.bands), m.gainDb, sev, message, stage::tonal);
            f.action = action;
            f.group = "eq:" + juce::String (i);
        }

        for (int b = 0; b < kNumBands; ++b)
        {
            const float d = result.bandTonalDelta[(size_t) b];
            const float tol = result.bandTolerance[(size_t) b];
            const auto sev = bandSeverity[(size_t) b];
            const auto& character = bandCharacters()[(size_t) b];

            juce::String message ("Bilanciato rispetto al target.");
            juce::String action;
            float timeStart = -1.0f, timeEnd = -1.0f;

            if (sev != Severity::ok)
            {
                message = capitalised (juce::String::fromUTF8 (d > 0 ? character.whenHigh : character.whenLow)) + ".";
                if (genericCurve)
                    message << " La curva del profilo è una media di genere: conferma con un brano di riferimento."_u;

                // eccesso costante (EQ statico), a raffiche (strumento dinamico) o concentrato in una sezione (automazione)?
                const auto& dyn = s.bandDynamics[(size_t) b];
                const float baseline = profile.bandBurstDb ? (*profile.bandBurstDb)[(size_t) b] : 1.5f;
                // una sezione chiaramente più carica è l'indicazione più utile; altrimenti raffiche brevi (sibilanti, note)
                const bool oneSection = d > 0.0f && dyn.worstStartSec >= 0.0f && dyn.worstExcessDb >= std::max (3.0f, 0.5f * d)
                                        && dyn.worstEndSec - dyn.worstStartSec <= 0.5f * (float) s.secondsAnalyzed;
                const bool bursts = ! oneSection && d > 0.0f && dyn.burstDb - baseline >= std::max (1.0f, 0.5f * d);
                const bool localised = d > 0.0f && dyn.worstStartSec >= 0.0f && dyn.worstExcessDb >= (bursts ? 2.0f : 3.0f);
                const bool intermittent = bursts || oneSection;

                if (bursts)
                    action << "L'eccesso arriva a raffiche (i momenti più carichi alzano la banda di "_u << fmt (dyn.burstDb, 1, "dB")
                           << "): " << burstAction (b, options.phase) << " ";
                else if (oneSection)
                    action << "L'eccesso è concentrato tra "_u << formatSongTime (dyn.worstStartSec) << " e " << formatSongTime (dyn.worstEndSec)
                           << " (" << signedFmt (dyn.worstExcessDb, 1, "dB") << " rispetto al resto del brano): "
                           << (mix ? "intervieni solo lì, con automazione o EQ sulla traccia che entra in quella sezione. "_u
                                   : "intervieni solo lì, con EQ dinamico o automazione dell'EQ, invece di un taglio su tutto il brano. "_u);

                const int move = moveOfBand[(size_t) b];
                const auto* culprits = d > 0 ? character.mixHigh : character.mixLow;
                if (move >= 0)
                {
                    const auto& m = result.eqMoves[(size_t) move];
                    if (mix)
                        action << "Sul bus equivale a " << m.describe (m.shownGainDb) << " (mossa EQ " << (move + 1)
                               << "), ma è meglio agire sulle tracce: "_u << juce::String::fromUTF8 (culprits) << ".";
                    else
                        action << (intermittent ? "In alternativa, mossa EQ " : "Mossa EQ ") << (move + 1) << ": " << m.describe (m.shownGainDb) << "."
                               << (m.fixInMix ? " Il resto va risolto nel mix ("_u + juce::String::fromUTF8 (character.fix) + ")." : juce::String());
                }
                else
                {
                    action << (mix ? capitalised (juce::String::fromUTF8 (culprits)) + "." : masterEqAdvice (b, d, genericCurve));
                }

                // dove: solo se l'eccesso è localizzato (una zona chiaramente più carica del resto del brano)
                if (localised)
                {
                    timeStart = dyn.worstStartSec;
                    timeEnd = dyn.worstEndSec;
                    if (! oneSection)
                        action << " Più evidente tra "_u << formatSongTime (timeStart) << " e " << formatSongTime (timeEnd)
                               << " (" << signedFmt (dyn.worstExcessDb, 1, "dB") << " rispetto al resto del brano).";
                }
            }

            auto& f = list.add (highlight::band (b), "Tonale", juce::String ("Banda ") + kBandNames[(size_t) b] + " (" + bandRangeText (b) + ")",
                                signedFmt (d, 1, "dB") + " vs target",
                                juce::String::charToString (0x00B1) + juce::String (tol, 1) + " dB" + (result.tonalUsesLoudest ? " (sezione più forte)"_u : juce::String()),
                                d, sev, message, stage::tonal);
            f.action = action;
            f.timeStart = timeStart;
            f.timeEnd = timeEnd;
            if (moveOfBand[(size_t) b] >= 0)
                f.group = "eq:" + juce::String (moveOfBand[(size_t) b]);
        }
    }

    // il tilt è un sintomo: se una mossa EQ lo corregge, la priorità e l'azione sono quelle della mossa
    if (auto* tilt = list.find (highlight::tilt); tilt != nullptr && tilt->severity >= Severity::warning)
    {
        for (int i = 0; i < (int) result.eqMoves.size(); ++i)
        {
            const auto& m = result.eqMoves[(size_t) i];
            const bool highs = std::any_of (m.bands.begin(), m.bands.end(), [] (int b) { return b >= 4; });
            const bool lows = std::any_of (m.bands.begin(), m.bands.end(), [] (int b) { return b <= 2; });
            // troppo brillante: meno alte o più basse; troppo scuro: il contrario
            const bool fixes = tilt->delta > 0.0f ? ((highs && m.gainDb < 0.0f) || (lows && m.gainDb > 0.0f))
                                                  : ((highs && m.gainDb > 0.0f) || (lows && m.gainDb < 0.0f));
            if (fixes)
            {
                tilt->group = "eq:" + juce::String (i);
                tilt->action = "Si corregge con la mossa EQ " + juce::String (i + 1) + ": " + m.describe (m.shownGainDb) + ".";
                break;
            }
        }
    }

    if (profile.bandWidthPercent)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            const float target = (*profile.bandWidthPercent)[(size_t) b];
            const float d = s.bandWidthPercent[(size_t) b] - target;
            const float tol = profile.bandWidthTolerances ? (*profile.bandWidthTolerances)[(size_t) b] : profile.bandWidthTolerance;
            auto sev = std::abs (d) <= tol ? Severity::ok : (std::abs (d) <= 2.0f * tol ? Severity::warning : Severity::critical);
            if (b <= 1 && d < 0.0f)
                sev = Severity::ok;   // bassi più mono del target: va bene

            if (sev == Severity::ok)
                continue;   // per non affollare il report si mostrano solo le bande fuori tolleranza

            juce::String message;
            if (mix)
                message = d > 0 ? "Banda più larga del target: riduci l'allargamento delle tracce che occupano questa zona "
                                  "(widener, chorus, riverberi stereo, doppie molto aperte)."_u
                                : "Banda più stretta del target: apri il pan delle tracce in questa zona (doppie, chitarre, synth) "
                                  "o usa riverberi e delay stereo."_u;
            else
                message = d > 0 ? "Banda più larga del target: riduci il side in questa zona (EQ M/S)."_u
                                : "Banda più stretta del target: valuta di allargare questa zona (EQ M/S sul side, imager)."_u;

            list.add (highlight::bandWidth (b), "Stereo", juce::String ("Larghezza ") + kBandNames[(size_t) b],
                      fmt (s.bandWidthPercent[(size_t) b], 0, "%"), fmt (target, 0, "%"), d, sev, message, stage::tonal);
        }
    }

    // --- controlli incrociati: suggerimenti coerenti tra loro -----------------------------
    if (! mix)
    {
        auto* integrated = list.find (highlight::integrated);
        const bool alreadySquashed = isProblem (list.find (highlight::plr)) && list.find (highlight::plr)->delta < 0.0f;
        const bool sectionSquashed = isProblem (list.find (highlight::psr)) && list.find (highlight::psr)->delta < 0.0f;

        // "alza il limiter" e "riduci il limiter" non possono comparire insieme
        if (isProblem (integrated) && integrated->delta < 0.0f && (alreadySquashed || sectionSquashed))
            integrated->message = "Il master è sotto il target ma è già molto limitato: alzare ancora il limiter aumenterebbe solo la distorsione. "
                                  "Il limite è nel mix: di solito bassi/sub o transienti che consumano headroom. Controlla il bilanciamento tonale, "
                                  "poi usa clipper/saturazione prima del limiter per guadagnare densità."_u
                                + "  (" + signedFmt (integrated->delta, 1, "LUFS") + " sotto il minimo)";

        // master forti con ceiling vicino a 0: la codifica lossy genera overs anche se il WAV è pulito
        if (s.integratedLufs > -10.0f && s.truePeakMaxDb > -1.0f && s.overs0dB == 0)
        {
            auto* tp = list.find (highlight::truePeak);
            if (tp == nullptr || tp->severity < Severity::warning)
                list.add (highlight::truePeak, "Peak", "Margine per la codifica lossy", fmt (s.truePeakMaxDb, 2, "dBTP"), "<= -1.0 dBTP", s.truePeakMaxDb + 1.0f,
                          Severity::info,
                          "Con un master così forte, la conversione in AAC/Ogg/MP3 delle piattaforme può superare 0 dBTP. "
                          "Spotify consiglia -1 dBTP (anche -2 dBTP sopra -14 LUFS): valuta di abbassare il ceiling."_u, stage::loudness);
        }

        // il sub che consuma headroom spiega spesso un loudness basso
        if (isProblem (integrated) && integrated->delta < 0.0f && result.hasTonalTarget
            && (result.bandTonalDelta[0] > result.bandTolerance[0] || s.subRumbleDb > -15.0f))
            integrated->message << " Nota: c'è più energia del target nel Sub, che probabilmente sta limitando il loudness."_u;
    }

    // --- controlli tecnici indipendenti dal profilo --------------------------------------
    if (s.overs0dB > 0)
        list.add (highlight::overs, "Peak", "Inter-sample peak > 0 dBTP", juce::String (s.overs0dB) + " eventi", "0", (float) s.overs0dB, Severity::critical,
                  mix ? "Picchi inter-campione sopra 0 dBTP: sul mix bus significa che manca margine. Abbassa il master fader."
                      : "Picchi inter-campione sopra 0 dBTP: distorsione garantita in conversione D/A e codifica lossy. Usa un limiter true-peak.",
                  stage::loudness);

    if (s.clipEvents > 0)
        list.add (highlight::clip, "Peak", "Clipping digitale", juce::String (s.clipEvents) + " eventi", "0", (float) s.clipEvents,
                  s.clipEvents > 20 ? Severity::critical : Severity::warning,
                  mix ? "Rilevati plateau di campioni identici (onda squadrata): clipping. Controlla il gain staging (fader, gruppi, plugin che "
                        "saturano) e non lasciare clipper o limiter sul mix da consegnare."_u
                      : "Rilevati plateau di campioni identici (onda squadrata): clipping. Se non è voluto (clipper), riduci il guadagno a monte."_u,
                  stage::technical);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float dc = s.dcOffsetDb[(size_t) ch];
        if (dc > -60.0f)
            list.add (highlight::dc, "Tecnico", juce::String ("DC offset ") + (ch == 0 ? "L" : "R"), fmt (dc, 1, "dBFS"), "< -60 dBFS", dc + 60.0f,
                      dc > -40.0f ? Severity::critical : Severity::warning,
                      "Componente continua presente: riduce l'headroom e crea click nei tagli. Applica un filtro DC/high-pass a 5-10 Hz.",
                      stage::technical);

        if (s.channelSilent[(size_t) ch])
            list.add (highlight::balance, "Tecnico", juce::String ("Canale ") + (ch == 0 ? "L" : "R") + " muto", "silenzio", "-", 0.0f, Severity::critical,
                      "Un canale è muto mentre l'altro ha segnale: controlla routing e pan."_u, stage::technical);
    }

    if (s.numChannels > 1)
    {
        if (s.correlation < 0.0f)
            list.add (highlight::correlation, "Stereo", "Fase invertita", fmt (s.correlation, 2), "> 0", s.correlation, Severity::critical,
                      "Correlazione negativa: probabile canale con polarità invertita o forte problema di fase. In mono il brano perderà gran parte del segnale."_u,
                      stage::technical);

        if (s.lowEndCorrelation < 0.8f)
            list.add (highlight::lowEndWidth, "Stereo", "Correlazione sotto 100 Hz", fmt (s.lowEndCorrelation, 2), "> 0.80", s.lowEndCorrelation - 0.8f,
                      s.lowEndCorrelation < 0.4f ? Severity::critical : Severity::warning,
                      mix ? "Basse frequenze poco correlate: in mono e nei club il low-end perde energia. Cerca le tracce con bassi stereo "
                            "(synth bass con unison/chorus, riverberi, sample stereo) e rendile mono sotto i 100 Hz."_u
                          : "Basse frequenze poco correlate: in mono e nei club il low-end perde energia. "
                            "La prassi è non avere side sotto i 90-100 Hz: usa un bass mono / EQ M/S che tagli il side in quella zona."_u,
                      stage::lowEnd);

        if (s.bandCorrelation[1] < 0.5f)
            list.add (highlight::bandCorrelation (1), "Stereo", "Correlazione Bass (60-250 Hz)", fmt (s.bandCorrelation[1], 2), "> 0.50", s.bandCorrelation[1] - 0.5f,
                      Severity::warning,
                      "Bassi molto decorrelati: controlla chorus, widener o riverberi stereo su basso e synth bassi.", stage::lowEnd);

        if (s.monoLossDb < -3.0f)
            list.add (highlight::monoLoss, "Stereo", "Compatibilità mono"_u, fmt (s.monoLossDb, 1, "dB"), "> -3 dB", s.monoLossDb + 3.0f,
                      s.monoLossDb < -6.0f ? Severity::critical : Severity::warning,
                      "Il livello cala molto sommando in mono (smartphone, altoparlanti Bluetooth, club): riduci l'ampiezza stereo.", stage::lowEnd);

        if (std::abs (s.balanceDb) > 0.5f)
            list.add (highlight::balance, "Stereo", "Bilanciamento L/R", fmt (s.balanceDb, 1, "dB"), juce::String::charToString (0x00B1) + "0.5 dB", s.balanceDb,
                      std::abs (s.balanceDb) > 1.5f ? Severity::critical : Severity::warning,
                      juce::String ("Il canale ") + (s.balanceDb > 0 ? "sinistro" : "destro") + " è più forte: verifica panning e bilanciamento del mix."_u,
                      stage::lowEnd);
    }

    if (s.subRumbleDb > -15.0f)
        list.add (highlight::rumble, "Tonale", "Energia sotto 30 Hz", fmt (s.subRumbleDb, 1, "dB"), "< -15 dB", s.subRumbleDb + 15.0f, Severity::warning,
                  mix ? "Molta energia sub-sonica: consuma headroom senza essere udibile sulla maggior parte dei sistemi. Metti un high-pass "
                        "a 20-30 Hz su kick e basso e più in alto sulle tracce che non devono avere sub."_u
                      : "Molta energia sub-sonica: consuma headroom senza essere udibile sulla maggior parte dei sistemi. Valuta un high-pass a 20-30 Hz.",
                  stage::lowEnd);

    // --- picchi stretti: note del brano o risonanze -------------------------------------------
    for (const auto& r : s.resonances)
    {
        if (r.excessDb <= 6.0f)
            continue;

        const auto hz = juce::String (juce::roundToInt (r.frequency)) + " Hz";
        if (r.musical)
        {
            auto& f = list.add (highlight::resonance (r.frequency), "Tonale", "Nota prominente a " + hz,
                                "+" + juce::String (r.excessDb, 1) + " dB", noteName (r.midiNote), r.excessDb - 6.0f, Severity::info,
                                "Picco stretto e persistente che coincide con la nota " + noteName (r.midiNote)
                                    + ": probabilmente una nota portante del brano (tonica, basso o accordo principale), fa parte della musica."_u,
                                stage::tonal);
            f.action = mix ? "Non correggerla con un EQ statico. Se in alcuni punti rimbomba, EQ dinamico stretto su quella nota sulla traccia del basso o del synth."_u
                           : "Non correggerla con un EQ statico. Se in alcuni punti rimbomba, EQ dinamico stretto che intervenga solo su quella nota."_u;
            continue;
        }

        bool inReference = false;
        if (options.reference != nullptr)
            for (const auto& rr : options.reference->resonances)
                inReference = inReference || (rr.excessDb > 4.0f && std::abs (std::log2 (rr.frequency / r.frequency)) < 1.0 / 12.0);

        juce::String message ("Picco stretto e persistente ");
        if (std::abs (r.cents) > 25.0f)
            message << "che non coincide con una nota (" << signedFmt (r.cents, 0, "cent") << " da " << noteName (r.midiNote) << ")";
        else
            message << "vicino a " << noteName (r.midiNote) << " ma senza altre ottave nello spettro e fuori dalle note principali del brano";
        message << ": probabile risonanza (stanza, fusto di un tamburo, EQ o synth).";
        if (inReference)
            message << " È presente anche nel reference: potrebbe essere un tratto del genere."_u;

        auto& f = list.add (highlight::resonance (r.frequency), "Tonale", "Risonanza a " + hz,
                            "+" + juce::String (r.excessDb, 1) + " dB", "< +6 dB", r.excessDb - 6.0f,
                            inReference ? Severity::info : Severity::warning, message, stage::tonal);
        f.action = mix ? "Trova la traccia che la contiene (sweep con un EQ stretto) e attenua di 2-4 dB con Q 4-8 a " + hz + "."
                       : "EQ dinamico stretto (Q 4-8) a " + hz + ", 1-3 dB, che intervenga solo quando la risonanza suona.";
    }

    if (live)
        list.add (highlight::time, "Analisi", "Modalità Live"_u, "finestra " + fmt ((float) s.liveWindowSeconds, 0, "s"), "-", 0.0f, Severity::info,
                  "I valori seguono gli ultimi secondi di audio: ideale mentre regoli i plugin. LRA e DR sono esclusi. "
                  "Per il verdetto finale passa a \"Brano intero\" e riproduci tutto il brano."_u
                      + (result.tonalUsesLoudest ? " Il bilanciamento tonale è confrontato con la sezione più forte del target."_u : juce::String()),
                  stage::analysis);
    else if (s.secondsAnalyzed < 30.0)
        list.add (highlight::time, "Analisi", "Durata analizzata", fmt ((float) s.secondsAnalyzed, 0, "s"), "brano intero", 0.0f, Severity::info,
                  "Analisi parziale: integrated, LRA e DR sono affidabili solo analizzando il brano intero dall'inizio alla fine.", stage::analysis);

    if (options.fifoOverflow)
        list.add (highlight::time, "Analisi", "Buffer di analisi", "overflow", "-", 0.0f, Severity::warning,
                  "Parte dell'audio non è stata analizzata (CPU sovraccarica). Premi Reset e riproduci di nuovo."_u, stage::analysis);

    // diagnosi diverse che si risolvono con la stessa azione: una sola voce tra le priorità
    {
        const auto* plr = list.find (highlight::plr);
        const bool squashed = plr != nullptr && plr->severity >= Severity::warning && plr->delta < 0.0f;
        for (auto& f : result.findings)
        {
            if (f.severity < Severity::warning || f.group.isNotEmpty())
                continue;

            if (mix)
            {
                if (f.key == highlight::truePeak || f.key == highlight::overs)
                    f.group = "headroom";   // abbassare il master fader
                continue;
            }

            const bool dynamicsKey = f.key == highlight::plr || f.key == highlight::psr || f.key == highlight::dr || f.key.startsWith ("crest:");
            if ((f.key == highlight::integrated && (f.delta > 0.0f || squashed)) || (dynamicsKey && f.delta < 0.0f))
                f.group = "limiting:less";    // meno guadagno nel limiter / meno compressione
            else if ((f.key == highlight::integrated && f.delta < 0.0f) || (dynamicsKey && f.delta > 0.0f))
                f.group = "limiting:more";
            else if ((f.key == highlight::truePeak && f.delta > 0.0f) || f.key == highlight::overs)
                f.group = "ceiling";          // ceiling del limiter in modalità true peak
        }
    }

    // scelte volute dall'utente: restano nell'elenco ma non contano
    for (auto& f : result.findings)
        f.ignored = f.id.isNotEmpty() && options.ignoredIds.contains (f.id);

    // per severità, e a parità di severità nell'ordine di lavoro (fase e low-end prima del loudness)
    auto& out = result.findings;
    std::stable_sort (out.begin(), out.end(), [] (const Finding& a, const Finding& b)
    {
        if (a.severity != b.severity)
            return (int) a.severity > (int) b.severity;
        return a.stage < b.stage;
    });

    // priorità: i problemi più gravi, una sola volta per azione/elemento, poi nell'ordine in cui affrontarli
    for (const auto& f : out)
    {
        if (f.severity < Severity::warning || f.ignored || result.priorities.size() >= 3)
            continue;
        const auto id = f.group.isNotEmpty() ? f.group : f.key;
        const bool duplicate = std::any_of (result.priorities.begin(), result.priorities.end(), [&id] (const Finding& p)
        {
            return (p.group.isNotEmpty() ? p.group : p.key) == id;
        });
        if (! duplicate)
            result.priorities.push_back (f);
    }
    std::stable_sort (result.priorities.begin(), result.priorities.end(), [] (const Finding& a, const Finding& b) { return a.stage < b.stage; });

    // pagella: ogni misura valutata pesa allo stesso modo nella sua area (le mosse EQ riassumono bande già contate)
    std::array<double, kNumAreas> penalty {};
    std::array<int, kNumAreas> evaluated {};
    bool technicalCritical = false;
    for (const auto& f : out)
    {
        if (! f.isScored())
            continue;
        const auto a = (size_t) f.area;
        auto& area = result.areas[a];
        ++evaluated[a];
        if (f.severity == Severity::critical)     { ++area.critical; penalty[a] += 1.0; }
        else if (f.severity == Severity::warning) { ++area.warning;  penalty[a] += 0.4; }
        else                                      { ++area.ok; }
        technicalCritical = technicalCritical || (f.area == Area::technical && f.severity == Severity::critical);
    }

    // i controlli tecnici producono una diagnosi solo quando trovano un problema
    if (s.valid && s.integratedLufs > -70.0f && evaluated[(size_t) Area::technical] == 0)
        evaluated[(size_t) Area::technical] = 1;

    static constexpr std::array<double, kNumAreas> weights { 1.0, 1.0, 1.2, 0.8, 1.0 };
    double weighted = 0.0, totalWeight = 0.0;
    for (size_t a = 0; a < (size_t) kNumAreas; ++a)
    {
        auto& area = result.areas[a];
        area.evaluated = evaluated[a] > 0;
        if (! area.evaluated)
            continue;
        area.score = std::clamp ((int) std::lround (100.0 * (1.0 - penalty[a] / evaluated[a])), 0, 100);
        weighted += weights[a] * area.score;
        totalWeight += weights[a];
    }

    int score = totalWeight > 0.0 ? (int) std::lround (weighted / totalWeight) : 100;
    if (technicalCritical)
        score = std::min (score, 60);   // clip, fase invertita, canale muto: il master non è consegnabile
    result.score = std::clamp (score, 0, 100);

    return result;
}

} // namespace ma
