#include "TestSignals.h"
#include "Compare/Comparator.h"
#include "Compare/TargetProfile.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    const ma::Finding* findKey (const ma::ComparisonResult& r, const juce::String& key)
    {
        for (const auto& f : r.findings)
            if (f.key == key)
                return &f;
        return nullptr;
    }

    const ma::AreaScore& area (const ma::ComparisonResult& r, ma::Area a) { return r.areas[(size_t) a]; }
}

TEST_CASE ("Pagella: un brano confrontato con se stesso ha voti alti in tutte le aree", "[score]")
{
    const auto s = test::analyse (test::pinkNoise (40.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Self");
    const auto r = ma::compare (s, p);

    juce::String scored;
    for (const auto& f : r.findings)
        if (f.isScored() && f.severity >= ma::Severity::warning)
            scored << f.id << " (" << ma::severityName (f.severity) << ")  ";

    for (int a = 0; a < ma::kNumAreas; ++a)
    {
        INFO (ma::areaName ((ma::Area) a).toStdString() << ": " << scored.toStdString());
        CHECK (r.areas[(size_t) a].evaluated);

        // il rumore rosa sintetico ha davvero un po' di DC: i controlli tecnici lo segnalano
        if ((ma::Area) a != ma::Area::technical)
            CHECK (r.areas[(size_t) a].score >= 80);
    }
    CHECK (r.score >= 75);
}

TEST_CASE ("Pagella: un problema di loudness abbassa solo la sua area", "[score]")
{
    const auto s = test::analyse (test::pinkNoise (40.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Self");
    const auto base = ma::compare (s, p);

    auto louder = s;
    louder.integratedLufs += 6.0f;   // fuori di molto dal range del profilo
    const auto r = ma::compare (louder, p);

    REQUIRE (findKey (r, ma::highlight::integrated) != nullptr);
    CHECK (findKey (r, ma::highlight::integrated)->severity == ma::Severity::critical);
    CHECK (area (r, ma::Area::loudness).score < area (base, ma::Area::loudness).score);
    CHECK (area (r, ma::Area::loudness).critical >= 1);
    CHECK (area (r, ma::Area::tonal).score == area (base, ma::Area::tonal).score);
    CHECK (area (r, ma::Area::stereo).score == area (base, ma::Area::stereo).score);
    CHECK (r.score < base.score);
    CHECK (r.score > 0);   // un solo problema non azzera il punteggio
}

TEST_CASE ("Pagella: un problema tecnico critico limita il punteggio a 60", "[score]")
{
    const auto s = test::analyse (test::pinkNoise (40.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Self");

    auto clipped = s;
    clipped.clipEvents = 50;
    clipped.clipEventsFullScale = 50;
    const auto r = ma::compare (clipped, p);

    REQUIRE (findKey (r, ma::highlight::clip) != nullptr);
    CHECK (findKey (r, ma::highlight::clip)->area == ma::Area::technical);
    CHECK (area (r, ma::Area::technical).critical == 1);
    CHECK (r.score <= 60);
}

TEST_CASE ("Diagnosi ignorate: fuori da voti, priorità, contatori e colori", "[score]")
{
    const auto s = test::analyse (test::pinkNoise (40.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Self");
    const auto base = ma::compare (s, p);

    auto louder = s;
    louder.integratedLufs += 6.0f;
    const auto withProblem = ma::compare (louder, p);
    const auto* integrated = findKey (withProblem, ma::highlight::integrated);
    REQUIRE (integrated != nullptr);
    REQUIRE (integrated->id.isNotEmpty());

    ma::CompareOptions options;
    options.ignoredIds.add (integrated->id);
    const auto r = ma::compare (louder, p, options);

    const auto* ignored = findKey (r, ma::highlight::integrated);
    REQUIRE (ignored != nullptr);
    CHECK (ignored->ignored);
    CHECK (r.countIgnored() == 1);
    CHECK (r.severityOfKey (ma::highlight::integrated) == ma::Severity::ok);
    CHECK (r.count (ma::Severity::critical) == withProblem.count (ma::Severity::critical) - 1);
    CHECK (area (r, ma::Area::loudness).critical == 0);
    for (const auto& f : r.priorities)
        CHECK (f.id != integrated->id);

    // l'identità è stabile tra un'analisi e l'altra
    CHECK (findKey (withProblem, ma::highlight::integrated)->id == findKey (base, ma::highlight::integrated)->id);
}
