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

TEST_CASE ("Master troppo forte per Spotify: integrated segnalato come critico", "[compare]")
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
            CHECK (f.severity == ma::Severity::critical);
            CHECK (f.delta > 0.0f);
        }
    CHECK (found);
    CHECK (result.findings.front().severity == ma::Severity::critical);   // ordinati per severità

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
