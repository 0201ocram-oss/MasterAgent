#include "TestSignals.h"
#include "Compare/Comparator.h"
#include "Compare/ReportExporter.h"
#include "Compare/TargetProfile.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE ("Tutti i profili inclusi vengono caricati", "[profiles]")
{
    ma::ProfileLibrary lib;
    int builtIn = 0;
    for (const auto& p : lib.getProfiles())
    {
        if (p.id.startsWith ("user_"))
            continue;
        ++builtIn;
        CHECK (p.tonalCurve.has_value());
        CHECK (p.getMetric (ma::metric::integratedLufs) != nullptr);
    }
    CHECK (builtIn == 9);
    CHECK (lib.findById ("spotify") != nullptr);
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

    CHECK (ma::buildTextReport (s, result, spotify->name).contains ("Integrated"));
    CHECK (juce::JSON::parse (ma::buildJsonReport (s, result, spotify->name)).isObject());
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
