#include "TestSignals.h"
#include "Compare/Comparator.h"
#include "Compare/ReportExporter.h"
#include "Compare/TargetProfile.h"
#include "Common/Text.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE ("Tutti i profili inclusi vengono caricati", "[profiles]")
{
    ma::ProfileLibrary lib;
    int builtIn = 0, deliveries = 0, standards = 0;
    for (const auto& p : lib.getProfiles())
    {
        if (p.id.startsWith ("user_"))
            continue;
        ++builtIn;
        INFO (p.id.toStdString());
        // il suono lo decide il genere: le consegne (piattaforme, broadcast, podcast) non hanno una curva tonale
        const bool delivery = p.kind == ma::ProfileKind::delivery;
        CHECK (p.tonalCurve.has_value() == ! delivery);
        // tutti fissano loudness e true peak, tranne "Master forte", che li prende dal genere
        CHECK ((p.getMetric (ma::metric::integratedLufs) != nullptr) == ! p.dynamicsFromGenre);
        CHECK ((p.getMetric (ma::metric::truePeakMax) != nullptr) == ! p.dynamicsFromGenre);
        // l'LRA dipende dall'arrangiamento: è del genere
        CHECK ((p.getMetric (ma::metric::loudnessRange) != nullptr) == ! delivery);
        deliveries += delivery ? 1 : 0;
        standards += p.isDeliveryStandard() ? 1 : 0;
    }
    CHECK (builtIn == 13);
    CHECK (deliveries == 8);
    CHECK (standards == 3);
    REQUIRE (lib.find (ma::targetIds::loud, ma::ProfileKind::delivery) != nullptr);
    CHECK (lib.find (ma::targetIds::loud, ma::ProfileKind::delivery)->dynamicsFromGenre);
    CHECK (lib.find ("rock", ma::ProfileKind::delivery) == nullptr);
    CHECK (lib.find ("spotify", ma::ProfileKind::genre) == nullptr);
    CHECK (lib.findById ("spotify") != nullptr);
    CHECK (lib.findById ("spotify")->context == ma::ProfileContext::streaming);
    CHECK (lib.findById ("rock")->context == ma::ProfileContext::music);
    CHECK (lib.findById ("ebu_r128")->context == ma::ProfileContext::broadcast);
    CHECK (lib.findById ("atsc_a85")->context == ma::ProfileContext::broadcast);
    CHECK (lib.findById ("podcast")->context == ma::ProfileContext::podcast);
}

TEST_CASE ("Contesto del profilo: lettura, default e round-trip JSON", "[profiles]")
{
    const auto parse = [] (const char* json) { return ma::TargetProfile::fromJson (juce::String::fromUTF8 (json)); };

    // profili utente salvati prima del campo: streaming se normalizzano, altrimenti musica
    CHECK (parse (R"({"id":"a","name":"A","normalizationLufs":-14})")->context == ma::ProfileContext::streaming);
    CHECK (parse (R"({"id":"b","name":"B"})")->context == ma::ProfileContext::music);

    const auto podcast = parse (R"({"id":"c","name":"C","context":"podcast"})");
    REQUIRE (podcast.has_value());
    CHECK (podcast->isDeliveryStandard());
    CHECK (ma::TargetProfile::fromJson (podcast->toJson())->context == ma::ProfileContext::podcast);

    // tipo: senza campo, chi ha un contesto di consegna (streaming, broadcast, podcast) è una consegna
    CHECK (parse (R"({"id":"a","name":"A","normalizationLufs":-14})")->kind == ma::ProfileKind::delivery);
    CHECK (parse (R"({"id":"b","name":"B"})")->kind == ma::ProfileKind::genre);
    CHECK (podcast->kind == ma::ProfileKind::delivery);
    CHECK (parse (R"({"id":"d","name":"D","kind":"genre","normalizationLufs":-14})")->kind == ma::ProfileKind::genre);
    // "dynamicsFromGenre" vale solo per le consegne
    CHECK_FALSE (parse (R"({"id":"e","name":"E","dynamicsFromGenre":true})")->dynamicsFromGenre);

    const auto loud = parse (R"({"id":"f","name":"F lungo","shortName":"F","kind":"delivery","dynamicsFromGenre":true})");
    REQUIRE (loud.has_value());
    const auto again = ma::TargetProfile::fromJson (loud->toJson());
    REQUIRE (again.has_value());
    CHECK (again->kind == ma::ProfileKind::delivery);
    CHECK (again->dynamicsFromGenre);
    CHECK (again->getShortName() == "F");
    CHECK (ma::TargetProfile::fromJson (parse (R"({"id":"g","name":"G"})")->toJson())->getShortName() == "G");
}

TEST_CASE ("Profilo da snapshot: round-trip JSON", "[profiles]")
{
    const auto s = test::analyse (test::pinkNoise (10.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Test Ref");
    const auto parsed = ma::TargetProfile::fromJson (p.toJson());

    REQUIRE (parsed.has_value());
    CHECK (parsed->name == "Test Ref");
    CHECK (parsed->isReference);
    REQUIRE (parsed->tonalCurve.has_value());
    CHECK ((*parsed->tonalCurve)[17] == Approx ((*p.tonalCurve)[17]).margin (0.01f));
    CHECK (parsed->getMetric (ma::metric::integratedLufs)->target == Approx (s.integratedLufs).margin (0.01f));
}

TEST_CASE ("Un brano confrontato con se stesso non ha problemi di profilo", "[compare]")
{
    const auto s = test::analyse (test::pinkNoise (40.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Self");
    const auto result = ma::compare (s, p);

    for (const auto& f : result.findings)
    {
        // il rumore rosa ha davvero molta energia sotto 30 Hz: è un controllo tecnico, non di profilo
        if (f.metric.startsWith ("Energia sotto"))
            continue;
        if (f.category == "Loudness" || f.category == "Tonale" || f.category == "Dinamica")
        {
            INFO (f.metric.toStdString());
            CHECK (f.severity <= ma::Severity::info);
        }
    }
}

TEST_CASE ("Master troppo forte per Spotify: verrà abbassato, da verificare ma non critico", "[compare]")
{
    ma::ProfileLibrary lib;
    const auto* spotify = lib.findById ("spotify");
    REQUIRE (spotify != nullptr);

    const auto s = test::analyse (test::pinkNoise (20.0, 2.5f));   // molto forte
    const auto result = ma::compare (s, *spotify);

    bool found = false;
    for (const auto& f : result.findings)
        if (f.metric == "Integrated loudness")
        {
            found = true;
            CHECK (f.severity == ma::Severity::warning);
            CHECK (f.delta > 0.0f);
            CHECK (f.message.contains ("abbasser"));
        }
    CHECK (found);
    for (size_t i = 1; i < result.findings.size(); ++i)   // ordinati per severità
        CHECK (result.findings[i - 1].severity >= result.findings[i].severity);

    // export: solo i dati del master, senza punteggio né consigli
    const auto text = ma::buildTextReport (s, result, spotify->name);
    CHECK (text.contains ("Integrated"));
    CHECK_FALSE (text.contains ("DIAGNOSI"));
    CHECK_FALSE (text.contains ("Punteggio"));
    for (const auto& f : result.findings)
        if (f.action.isNotEmpty())
            CHECK_FALSE (text.contains (f.action));

    const auto json = juce::JSON::parse (ma::buildJsonReport (s, result, spotify->name));
    REQUIRE (json.isObject());
    CHECK (json.hasProperty ("metrics"));
    CHECK_FALSE (json.hasProperty ("findings"));
    CHECK_FALSE (json.hasProperty ("eqMoves"));
    CHECK_FALSE (json.hasProperty ("score"));
}

TEST_CASE ("Fase invertita segnalata", "[compare]")
{
    auto b = test::whiteNoise (10.0, 0.3f, true);
    b.applyGain (1, 0, b.getNumSamples(), -1.0f);
    const auto s = test::analyse (b);

    ma::ProfileLibrary lib;
    const auto result = ma::compare (s, *lib.findById ("pop"));
    bool found = false;
    for (const auto& f : result.findings)
        if (f.metric == "Fase invertita")
            found = f.severity == ma::Severity::critical;
    CHECK (found);
}

namespace
{
    ma::AnalysisSnapshot fakeMaster (float integrated, float truePeak)
    {
        ma::AnalysisSnapshot s;
        s.valid = true;
        s.numChannels = 2;
        s.secondsAnalyzed = 180.0;
        s.integratedLufs = integrated;
        s.truePeakMaxDb = truePeak;
        s.samplePeakDb = { truePeak - 0.3f, truePeak - 0.3f };
        s.plr = truePeak - integrated;
        s.minPsr = s.plr - 1.0f;
        s.loudnessRange = 5.0f;
        s.correlation = 0.6f;
        s.widthPercent = 20.0f;
        s.bandCorrelation.fill (0.9f);
        s.bandWidthPercent = { 1.0f, 2.0f, 10.0f, 18.0f, 25.0f, 28.0f, 30.0f, 32.0f };
        s.lowEndWidthPercent = 1.0f;
        s.spectralTiltDbPerOct = -3.3f;
        s.subRumbleDb = -30.0f;
        s.dcOffsetDb = { -120.0f, -120.0f };
        return s;
    }

    const ma::Finding* findKey (const ma::ComparisonResult& r, const juce::String& key)
    {
        for (const auto& f : r.findings)
            if (f.key == key)
                return &f;
        return nullptr;
    }
}

TEST_CASE ("Suggerimenti coerenti: master basso ma già schiacciato non suggerisce di alzare il limiter", "[compare]")
{
    ma::ProfileLibrary lib;
    const auto* pop = lib.findById ("pop");
    REQUIRE (pop != nullptr);

    // -11 LUFS (sotto il target pop) con PLR 5 dB (molto limitato)
    const auto result = ma::compare (fakeMaster (-11.0f, -6.0f), *pop);
    const auto* integrated = findKey (result, ma::highlight::integrated);
    REQUIRE (integrated != nullptr);
    CHECK (integrated->severity >= ma::Severity::warning);
    CHECK (integrated->message.contains ("già molto limitato"));
    CHECK_FALSE (integrated->message.contains ("aumenta il guadagno"));
}

TEST_CASE ("Master forte con ceiling vicino a 0 dBTP: avviso per la codifica lossy", "[compare]")
{
    ma::ProfileLibrary lib;
    const auto result = ma::compare (fakeMaster (-7.5f, -0.4f), *lib.findById ("pop"));
    bool lossyNote = false;
    for (const auto& f : result.findings)
        lossyNote = lossyNote || f.metric.contains ("lossy");
    CHECK (lossyNote);
}

TEST_CASE ("Curve tonali generiche non producono mai diagnosi critiche", "[compare]")
{
    ma::ProfileLibrary lib;
    auto s = fakeMaster (-8.0f, -1.0f);
    for (auto& v : s.thirdOctaveDb) v = 0.0f;   // spettro piatto: molto diverso dalla curva pop
    const auto result = ma::compare (s, *lib.findById ("pop"));

    for (int b = 0; b < ma::kNumBands; ++b)
        if (const auto* f = findKey (result, ma::highlight::band (b)))
            CHECK (f->severity <= ma::Severity::warning);
}

TEST_CASE ("Ogni diagnosi ha una chiave per l'evidenziazione e le priorità non hanno doppioni", "[compare]")
{
    ma::ProfileLibrary lib;
    auto s = fakeMaster (-5.0f, 0.5f);
    s.overs0dB = 3;
    s.clipEvents = 30;
    s.balanceDb = 2.0f;
    const auto result = ma::compare (s, *lib.findById ("spotify"));

    for (const auto& f : result.findings)
    {
        INFO (f.metric.toStdString());
        CHECK (f.key.isNotEmpty());
    }

    REQUIRE_FALSE (result.priorities.empty());
    CHECK (result.priorities.size() <= 3);
    for (size_t i = 0; i < result.priorities.size(); ++i)
        for (size_t j = i + 1; j < result.priorities.size(); ++j)
            CHECK (result.priorities[i].key != result.priorities[j].key);
}

//==============================================================================
// Calibrazione su master commerciali (studio su 170 brani da CD, ottobre 2026)

TEST_CASE ("Clipping al ceiling di un clipper: informazione o attenzione, mai un limite al voto", "[compare][clip]")
{
    ma::ProfileLibrary lib;
    const auto& pop = *lib.findById ("pop");
    auto s = fakeMaster (-7.5f, -1.0f);
    s.clipEvents = 300;            // plateau di pochi campioni a -0.3 dBFS
    s.clipEventsFullScale = 0;
    s.longCeilingClips = 3;
    s.ceilingClipDb = -0.3f;

    auto r = ma::compare (s, pop);
    const auto* clip = findKey (r, ma::highlight::clip);
    REQUIRE (clip != nullptr);
    INFO (clip->message.toStdString());
    CHECK (clip->severity == ma::Severity::info);
    CHECK (clip->message.contains ("-0.3 dBFS"));
    CHECK (r.areas[(size_t) ma::Area::technical].critical == 0);

    s.longCeilingClips = 200;      // 3 minuti: circa 67 plateau lunghi al minuto, distorsione udibile
    r = ma::compare (s, pop);
    clip = findKey (r, ma::highlight::clip);
    REQUIRE (clip != nullptr);
    CHECK (clip->severity == ma::Severity::warning);
    CHECK (r.areas[(size_t) ma::Area::technical].critical == 0);

    s.clipEventsFullScale = 50;    // a fondo scala: sovraccarico
    r = ma::compare (s, pop);
    bool fullScaleCritical = false;
    for (const auto& f : r.findings)
        fullScaleCritical = fullScaleCritical || (f.key == ma::highlight::clip && f.severity == ma::Severity::critical);
    CHECK (fullScaleCritical);
    CHECK (r.score <= 60);
}

TEST_CASE ("PLR gonfiato dai picchi inter-campione: nessun consiglio di aumentare la densità", "[compare]")
{
    ma::ProfileLibrary lib;
    auto s = fakeMaster (-8.5f, 3.5f);     // true peak +3.5 dBTP...
    s.samplePeakDb = { -0.1f, -0.1f };     // ...con i campioni a -0.1 dBFS: clipping, non dinamica
    s.plr = 12.0f;

    const auto r = ma::compare (s, *lib.findById ("pop"));
    const auto* plr = findKey (r, ma::highlight::plr);
    REQUIRE (plr != nullptr);
    INFO (plr->message.toStdString());
    CHECK (plr->severity == ma::Severity::info);
    CHECK (plr->message.contains ("apparenza"));
    for (const auto& p : r.priorities)
        CHECK (p.group != "limiting:more");
}

TEST_CASE ("Spotify: sopra -14 LUFS il true peak consigliato è -2 dBTP", "[compare]")
{
    ma::ProfileLibrary lib;
    const auto& spotify = *lib.findById ("spotify");

    const auto loud = ma::compare (fakeMaster (-9.0f, -1.5f), spotify);
    const auto* tp = findKey (loud, ma::highlight::truePeak);
    REQUIRE (tp != nullptr);
    INFO (tp->message.toStdString());
    CHECK (tp->severity == ma::Severity::warning);
    CHECK (tp->message.contains ("-2.0 dBTP"));

    const auto quiet = ma::compare (fakeMaster (-14.5f, -1.5f), spotify);
    REQUIRE (findKey (quiet, ma::highlight::truePeak) != nullptr);
    CHECK (findKey (quiet, ma::highlight::truePeak)->severity < ma::Severity::warning);
}

TEST_CASE ("LRA ampio: da verificare, mai critico (dipende dall'arrangiamento)", "[compare]")
{
    ma::ProfileLibrary lib;
    auto s = fakeMaster (-8.5f, -1.0f);
    s.loudnessRange = 20.0f;
    const auto r = ma::compare (s, *lib.findById ("rock"));
    const auto* lra = findKey (r, ma::highlight::lra);
    REQUIRE (lra != nullptr);
    CHECK (lra->severity == ma::Severity::warning);
}

TEST_CASE ("Bilanciamento L/R: qualche decimo è normale, oltre 1 dB da verificare, oltre 2 dB critico", "[compare]")
{
    ma::ProfileLibrary lib;
    const auto& rock = *lib.findById ("rock");
    auto severityAt = [&] (float balance)
    {
        auto s = fakeMaster (-8.5f, -1.0f);
        s.balanceDb = balance;
        const auto* f = findKey (ma::compare (s, rock), ma::highlight::balance);
        return f != nullptr ? f->severity : ma::Severity::ok;
    };
    CHECK (severityAt (0.8f) == ma::Severity::ok);
    CHECK (severityAt (-1.5f) == ma::Severity::warning);
    CHECK (severityAt (2.5f) == ma::Severity::critical);
}

TEST_CASE ("Mosse EQ: una mossa non è mai attribuita a una banda che sposterebbe ancora più lontano", "[compare][eq]")
{
    ma::ProfileLibrary lib;
    const auto& rock = *lib.findById ("rock");
    auto s = fakeMaster (-8.5f, -1.0f);
    s.thirdOctaveDb = *rock.tonalCurve;
    for (int i = 0; i <= 5; ++i) s.thirdOctaveDb[(size_t) i] -= 25.0f;   // sub quasi assente (fino a 63 Hz)
    for (int i = 7; i <= 10; ++i) s.thirdOctaveDb[(size_t) i] += 3.0f;   // bassi sopra il target

    for (auto p : { ma::WorkPhase::mix, ma::WorkPhase::master })
    {
        ma::CompareOptions options;
        options.phase = p;
        const auto r = ma::compare (s, rock, options);
        for (const auto& m : r.eqMoves)
        {
            REQUIRE_FALSE (m.bands.empty());
            for (int b : m.bands)
            {
                INFO (m.describe (m.gainDb).toStdString() << " banda " << b << " scostamento " << r.bandTonalDelta[(size_t) b]);
                CHECK (r.bandTonalDelta[(size_t) b] * m.gainDb < 0.0f);
            }
        }
    }
}

TEST_CASE ("EBU R128: -23 LUFS passa, -21 è critico, true peak -0.5 è critico", "[compare][broadcast]")
{
    ma::ProfileLibrary lib;
    const auto& ebu = *lib.findById ("ebu_r128");

    // in regola: nessun problema di loudness, nessun confronto tonale, nessun avviso per la codifica lossy
    const auto ok = ma::compare (fakeMaster (-23.0f, -3.0f), ebu);
    REQUIRE (findKey (ok, ma::highlight::integrated) != nullptr);
    CHECK (findKey (ok, ma::highlight::integrated)->severity == ma::Severity::ok);
    CHECK (findKey (ok, ma::highlight::truePeak)->severity == ma::Severity::ok);
    CHECK_FALSE (ok.hasTonalTarget);
    CHECK (ok.eqMoves.empty());
    CHECK (ok.areas[(size_t) ma::Area::loudness].score == 100);

    // troppo forte: si abbassa con un guadagno, il QC lo rifiuterebbe
    const auto loud = ma::compare (fakeMaster (-21.0f, -3.0f), ebu);
    const auto* integrated = findKey (loud, ma::highlight::integrated);
    REQUIRE (integrated != nullptr);
    CHECK (integrated->severity == ma::Severity::critical);
    CHECK (integrated->message.contains ("QC"));
    CHECK (integrated->action.contains ("Abbassa il guadagno di circa 2.0 dB"));

    // true peak oltre -1 dBTP: critico, con il target scritto come limite
    const auto peaky = ma::compare (fakeMaster (-23.0f, -0.5f), ebu);
    const auto* tp = findKey (peaky, ma::highlight::truePeak);
    REQUIRE (tp != nullptr);
    CHECK (tp->severity == ma::Severity::critical);
    CHECK (tp->target == "<= -1.0 dBTP");
    CHECK (tp->action.contains ("ceiling a -1.0 dBTP"));

    // molto sotto il massimo: in regola, non "headroom inutilizzato"
    const auto quietPeaks = ma::compare (fakeMaster (-23.0f, -12.0f), ebu);
    CHECK (findKey (quietPeaks, ma::highlight::truePeak)->severity == ma::Severity::ok);
}

TEST_CASE ("Broadcast troppo basso: guadagno, e limiter true peak solo se i picchi superano il massimo", "[compare][broadcast]")
{
    ma::ProfileLibrary lib;
    const auto& ebu = *lib.findById ("ebu_r128");

    // +3 dB portano il true peak da -6 a -3 dBTP: basta il guadagno
    const auto room = ma::compare (fakeMaster (-26.0f, -6.0f), ebu);
    const auto* a = findKey (room, ma::highlight::integrated);
    REQUIRE (a != nullptr);
    CHECK (a->severity == ma::Severity::critical);
    CHECK (a->action.contains ("Alza il guadagno di circa 3.0 dB"));
    CHECK (a->action.contains ("entro il limite"));

    // +3 dB porterebbero il true peak a +1 dBTP: serve un limiter che lavori circa 2 dB
    const auto noRoom = ma::compare (fakeMaster (-26.0f, -2.0f), ebu);
    const auto* b = findKey (noRoom, ma::highlight::integrated);
    REQUIRE (b != nullptr);
    CHECK (b->action.contains ("limiter true peak"));
    CHECK (b->action.contains ("circa 2.0 dB sui picchi"));
}

TEST_CASE ("ATSC A/85 e podcast: tolleranze dei rispettivi standard", "[compare][broadcast]")
{
    ma::ProfileLibrary lib;
    const auto& atsc = *lib.findById ("atsc_a85");
    const auto& podcast = *lib.findById ("podcast");

    auto integratedSeverity = [] (const ma::TargetProfile& p, float lufs, float tp)
    {
        return findKey (ma::compare (fakeMaster (lufs, tp), p), ma::highlight::integrated)->severity;
    };

    CHECK (integratedSeverity (atsc, -24.0f, -6.0f) == ma::Severity::ok);
    CHECK (integratedSeverity (atsc, -22.5f, -6.0f) == ma::Severity::ok);        // ±2 dB
    CHECK (integratedSeverity (atsc, -21.0f, -6.0f) == ma::Severity::critical);
    CHECK (findKey (ma::compare (fakeMaster (-24.0f, -1.5f), atsc), ma::highlight::truePeak)->severity
           == ma::Severity::critical);                                          // massimo -2 dBTP

    CHECK (integratedSeverity (podcast, -16.0f, -2.0f) == ma::Severity::ok);
    CHECK (integratedSeverity (podcast, -18.5f, -4.0f) == ma::Severity::critical);
    const auto loud = ma::compare (fakeMaster (-14.5f, -2.0f), podcast);
    CHECK (findKey (loud, ma::highlight::integrated)->severity == ma::Severity::warning);
    CHECK (findKey (loud, ma::highlight::integrated)->message.contains ("podcast"));

    // in fase di mix loudness e picchi del master non si giudicano, neanche per il broadcast
    ma::CompareOptions mix;
    mix.phase = ma::WorkPhase::mix;
    const auto r = ma::compare (fakeMaster (-30.0f, -8.0f), atsc, mix);
    CHECK (findKey (r, ma::highlight::integrated)->severity == ma::Severity::info);
}

TEST_CASE ("Broadcast troppo forte con picchi oltre il massimo: un solo guadagno risolve tutto, niente limiter", "[compare][broadcast]")
{
    ma::ProfileLibrary lib;
    const auto& ebu = *lib.findById ("ebu_r128");

    // master musicale consegnato così com'è: -16.4 LUFS, true peak +0.4 dBTP con picchi inter-campione
    auto s = fakeMaster (-16.4f, 0.4f);
    s.overs0dB = 21;
    const auto r = ma::compare (s, ebu);

    const auto* tp = findKey (r, ma::highlight::truePeak);
    const auto* overs = findKey (r, ma::highlight::overs);
    REQUIRE (tp != nullptr);
    REQUIRE (overs != nullptr);
    CHECK (tp->severity == ma::Severity::critical);
    CHECK (tp->action.contains ("stesso guadagno"));
    CHECK (tp->action.contains ("-6.2 dBTP"));       // 0.4 - 6.6
    CHECK (overs->action.contains ("stesso guadagno"));
    CHECK_FALSE (overs->message.contains ("limiter"));

    // tra le priorità una sola voce: abbassare il guadagno
    REQUIRE (r.priorities.size() == 1);
    CHECK (r.priorities.front().key == ma::highlight::integrated);
    CHECK (r.priorities.front().action.contains ("Abbassa il guadagno di circa 6.6 dB"));

    // se abbassando i picchi restano oltre il massimo, il limiter resta necessario
    const auto hot = ma::compare (fakeMaster (-22.0f, 1.0f), ebu);
    CHECK (findKey (hot, ma::highlight::truePeak)->action.contains ("Limiter true peak"));
}

//==============================================================================
// Target in due parti: genere (il suono) × consegna (loudness, picchi e dinamica)

namespace
{
    bool sameRange (const ma::MetricRange* a, const ma::MetricRange* b)
    {
        return a != nullptr && b != nullptr && a->min == b->min && a->max == b->max && a->target == b->target && a->warn == b->warn;
    }

    /** Stesse diagnosi (metrica, severità, messaggio, azione) e stesso voto. */
    void checkSameComparison (const ma::ComparisonResult& a, const ma::ComparisonResult& b)
    {
        CHECK (a.score == b.score);
        REQUIRE (a.findings.size() == b.findings.size());
        for (size_t i = 0; i < a.findings.size(); ++i)
        {
            INFO (a.findings[i].metric.toStdString());
            CHECK (a.findings[i].metric == b.findings[i].metric);
            CHECK (a.findings[i].severity == b.findings[i].severity);
            CHECK (a.findings[i].message == b.findings[i].message);
            CHECK (a.findings[i].action == b.findings[i].action);
        }
    }
}

TEST_CASE ("Genere × consegna: il suono dal genere, loudness e dinamica dalla consegna", "[target]")
{
    ma::ProfileLibrary lib;
    const auto& rock = *lib.findById ("rock");
    const auto& spotify = *lib.findById ("spotify");
    const auto t = ma::composeTarget (&rock, spotify);

    CHECK (t.name == "Rock · Spotify"_u);
    CHECK (t.getShortName() == "Rock · Spotify"_u);
    CHECK (t.id == "rock+spotify");

    REQUIRE (t.tonalCurve.has_value());
    CHECK (*t.tonalCurve == *rock.tonalCurve);
    CHECK (t.bandTolerance == rock.bandTolerance);
    CHECK_FALSE (t.isReference);
    for (const auto& key : { ma::metric::loudnessRange, ma::metric::correlation, ma::metric::widthPercent,
                             ma::metric::lowEndWidth, ma::metric::spectralTilt })
    {
        INFO (key.toStdString());
        CHECK (sameRange (t.getMetric (key), rock.getMetric (key)));
    }
    for (const auto& key : { ma::metric::integratedLufs, ma::metric::truePeakMax, ma::metric::plr, ma::metric::minPsr })
    {
        INFO (key.toStdString());
        CHECK (sameRange (t.getMetric (key), spotify.getMetric (key)));
    }
    // Spotify non chiede un DR: un master dinamico non è un errore (prima il DR del rock da CD lo segnalava)
    CHECK (t.getMetric (ma::metric::dr) == nullptr);
    CHECK (t.normalizationLufs == spotify.normalizationLufs);
    CHECK (t.loudTruePeakMax == spotify.loudTruePeakMax);
    CHECK (t.context == ma::ProfileContext::streaming);
}

TEST_CASE ("Master forte: tutto dal genere, come i vecchi profili di genere", "[target]")
{
    ma::ProfileLibrary lib;
    const auto& rock = *lib.findById ("rock");
    const auto& loud = *lib.findById (ma::targetIds::loud);
    const auto t = ma::composeTarget (&rock, loud);

    CHECK (t.id == "rock");
    CHECK (t.getShortName() == "Rock · Master forte"_u);
    CHECK (t.context == ma::ProfileContext::music);
    CHECK (t.metrics.size() == rock.metrics.size());
    for (const auto& [key, range] : rock.metrics)
    {
        INFO (key.toStdString());
        CHECK (sameRange (t.getMetric (key), &range));
    }

    auto s = fakeMaster (-12.0f, -1.5f);
    checkSameComparison (ma::compare (s, t), ma::compare (s, rock));
    s = fakeMaster (-7.0f, 0.3f);
    checkSameComparison (ma::compare (s, t), ma::compare (s, rock));
}

TEST_CASE ("Nessun genere con EBU R128: lo stesso confronto del vecchio profilo broadcast", "[target]")
{
    ma::ProfileLibrary lib;
    const auto& ebu = *lib.findById ("ebu_r128");
    const auto t = ma::composeTarget (nullptr, ebu);

    CHECK (t.id == "ebu_r128");
    CHECK (t.name == ebu.name);
    CHECK (t.getShortName() == "EBU R128");
    CHECK_FALSE (t.tonalCurve.has_value());
    CHECK (t.isDeliveryStandard());
    CHECK (t.metrics.size() == ebu.metrics.size());

    for (const auto& s : { fakeMaster (-16.0f, -0.5f), fakeMaster (-23.0f, -3.0f), fakeMaster (-26.0f, -8.0f) })
        checkSameComparison (ma::compare (s, t), ma::compare (s, ebu));

    // con un genere: il tonale del rock e le specifiche EBU, e i consigli restano quelli di consegna
    const auto rockEbu = ma::composeTarget (lib.findById ("rock"), ebu);
    CHECK (rockEbu.tonalCurve.has_value());
    CHECK (rockEbu.isDeliveryStandard());
    CHECK (sameRange (rockEbu.getMetric (ma::metric::integratedLufs), ebu.getMetric (ma::metric::integratedLufs)));
    CHECK (rockEbu.getMetric (ma::metric::plr) == nullptr);
    const auto r = ma::compare (fakeMaster (-16.0f, -0.5f), rockEbu);
    REQUIRE (findKey (r, ma::highlight::integrated) != nullptr);
    CHECK (findKey (r, ma::highlight::integrated)->action.contains ("Abbassa il guadagno"));
    // il consiglio nomina lo standard, non il target composto ("Rock · EBU R128")
    CHECK (findKey (r, ma::highlight::integrated)->message.contains ("standard " + ebu.name));
    CHECK_FALSE (findKey (r, ma::highlight::integrated)->message.contains ("Rock"));
}

TEST_CASE ("Rock a -12 LUFS per Spotify: nessun critico di loudness, tonale del rock", "[target]")
{
    ma::ProfileLibrary lib;
    const auto& rock = *lib.findById ("rock");
    auto s = fakeMaster (-12.0f, -1.5f);
    s.thirdOctaveDb = *rock.tonalCurve;
    s.spectralTiltDbPerOct = rock.getMetric (ma::metric::spectralTilt)->target;

    // con il solo profilo Rock (master da CD) il loudness è critico
    const auto asCd = ma::compare (s, ma::composeTarget (&rock, *lib.findById (ma::targetIds::loud)));
    REQUIRE (findKey (asCd, ma::highlight::integrated) != nullptr);
    CHECK (findKey (asCd, ma::highlight::integrated)->severity == ma::Severity::critical);

    const auto r = ma::compare (s, ma::composeTarget (&rock, *lib.findById ("spotify")));
    for (const auto& f : r.findings)
    {
        INFO (f.metric.toStdString() + ": " + f.message.toStdString());
        CHECK (f.severity < ma::Severity::critical);
        CHECK_FALSE ((f.key.startsWith ("band:") && f.severity >= ma::Severity::warning));
    }
    // più forte di -14 LUFS: la piattaforma lo abbassa, da verificare
    REQUIRE (findKey (r, ma::highlight::integrated) != nullptr);
    CHECK (findKey (r, ma::highlight::integrated)->severity == ma::Severity::warning);
    CHECK (r.hasTonalTarget);
    CHECK (r.tonalTarget == *rock.tonalCurve);
}

TEST_CASE ("Reference come genere: con Master forte vale per intero, con una piattaforma solo il suono", "[target]")
{
    ma::ProfileLibrary lib;
    const auto s = test::analyse (test::pinkNoise (10.0, 0.5f));
    auto ref = ma::TargetProfile::fromSnapshot (s, "Reference: Brano");
    ref.shortName = "Reference";

    const auto& loud = *lib.findById (ma::targetIds::loud);
    const auto whole = ma::composeTarget (&ref, loud);
    CHECK (whole.name == "Reference: Brano");
    CHECK (whole.getShortName() == "Reference");
    CHECK (whole.id == ref.id);
    CHECK (whole.isReference);
    CHECK (whole.metrics.size() == ref.metrics.size());

    const auto& spotify = *lib.findById ("spotify");
    const auto t = ma::composeTarget (&ref, spotify);
    CHECK (t.getShortName() == "Reference · Spotify"_u);
    CHECK (t.id.startsWith ("ref_"));   // lo spettro non ridisegna la curva del reference come target
    CHECK (t.isReference);
    CHECK (*t.tonalCurve == *ref.tonalCurve);
    CHECK (sameRange (t.getMetric (ma::metric::integratedLufs), spotify.getMetric (ma::metric::integratedLufs)));
    CHECK (sameRange (t.getMetric (ma::metric::loudnessRange), ref.getMetric (ma::metric::loudnessRange)));
    // il crest per banda cala con il limiting: con un reference da CD e Spotify segnalerebbe un master giusto
    CHECK (t.getMetric (ma::metric::crestLow) == nullptr);
    CHECK (t.getMetric (ma::metric::dr) == nullptr);
}

TEST_CASE ("Migrazione del vecchio target unico", "[target]")
{
    ma::ProfileLibrary lib;
    const auto check = [&] (const char* oldId, const juce::String& genre, const juce::String& delivery)
    {
        const auto t = ma::migrateTargetId (lib, oldId);
        INFO (oldId);
        CHECK (t.genre == genre);
        CHECK (t.delivery == delivery);
    };
    check ("spotify", "pop", "spotify");                     // le piattaforme usavano la curva Pop
    check ("streaming_generic", "pop", "streaming_generic");
    check ("youtube", "pop", "youtube");
    check ("ebu_r128", "none", "ebu_r128");
    check ("podcast", "none", "podcast");
    check ("rock", "rock", "loud");
    check ("__reference__", "__reference__", "loud");
    check ("user_mio_profilo", "user_mio_profilo", "loud");   // anche se non esiste più: l'editor ripiega sul predefinito
    check ("", "pop", "streaming_generic");

    // il vecchio "Spotify" diventa Pop · Spotify: stessa curva tonale di prima
    const auto popSpotify = ma::composeTarget (lib.findById ("pop"), *lib.findById ("spotify"));
    CHECK (*popSpotify.tonalCurve == *lib.findById ("pop")->tonalCurve);
}
