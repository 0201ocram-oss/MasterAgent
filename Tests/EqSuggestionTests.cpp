#include "TestSignals.h"
#include "Compare/Comparator.h"
#include "Compare/EqSuggestion.h"

#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    ma::EqMove makeMove (ma::EqMove::Type type, float frequency, float gainDb, float q)
    {
        ma::EqMove m;
        m.type = type;
        m.frequency = frequency;
        m.gainDb = gainDb;
        m.q = q;
        return m;
    }

    /** Scostamento master - target prodotto da un EQ applicato al master. */
    std::array<float, ma::kNumThirdOctaves> deltaOf (std::initializer_list<ma::EqMove> moves)
    {
        std::array<float, ma::kNumThirdOctaves> d {};
        for (const auto& m : moves)
            for (int i = 0; i < ma::kNumThirdOctaves; ++i)
                d[(size_t) i] += ma::eqMoveThirdOctaveDb (m, i);
        return d;
    }

    std::array<float, ma::kNumBands> tolerances (float v)
    {
        std::array<float, ma::kNumBands> t {};
        t.fill (v);
        return t;
    }

    double octavesBetween (double a, double b) { return std::abs (std::log2 (a / b)); }

    bool hasBand (const ma::EqMove& m, int band)
    {
        return std::find (m.bands.begin(), m.bands.end(), band) != m.bands.end();
    }
}

TEST_CASE ("Fit EQ: una campana a 300 Hz diventa una sola mossa opposta", "[eq]")
{
    const auto moves = ma::suggestEqMoves (deltaOf ({ makeMove (ma::EqMove::Type::bell, 300.0f, 3.0f, 1.0f) }), tolerances (1.0f));

    REQUIRE (moves.size() == 1);
    CHECK (moves[0].type == ma::EqMove::Type::bell);
    CHECK (octavesBetween (moves[0].frequency, 300.0) <= 1.0 / 6.0 + 0.01);
    CHECK (moves[0].gainDb == Approx (-3.0f).margin (0.5f));
    CHECK (moves[0].q >= 0.5f);
    CHECK (moves[0].q <= 1.4f);
    CHECK (hasBand (moves[0], 2));   // Low-mid
}

TEST_CASE ("Fit EQ: troppa brillantezza diventa un high shelf, non tre campane", "[eq]")
{
    const auto moves = ma::suggestEqMoves (deltaOf ({ makeMove (ma::EqMove::Type::highShelf, 8000.0f, 3.0f, 0.707f) }), tolerances (1.0f));

    REQUIRE (moves.size() == 1);
    CHECK (moves[0].type == ma::EqMove::Type::highShelf);
    CHECK (octavesBetween (moves[0].frequency, 8000.0) <= 0.75);
    CHECK (moves[0].gainDb == Approx (-3.0f).margin (0.75f));
    CHECK (hasBand (moves[0], 6));   // Brilliance
    CHECK (hasBand (moves[0], 7));   // Air
}

TEST_CASE ("Fit EQ: due problemi indipendenti, due mosse", "[eq]")
{
    const auto moves = ma::suggestEqMoves (deltaOf ({ makeMove (ma::EqMove::Type::bell, 250.0f, 4.0f, 1.4f),
                                                      makeMove (ma::EqMove::Type::bell, 3000.0f, -3.0f, 1.0f) }),
                                           tolerances (1.0f));
    REQUIRE (moves.size() == 2);

    const auto& low = moves[0].frequency < moves[1].frequency ? moves[0] : moves[1];
    const auto& high = moves[0].frequency < moves[1].frequency ? moves[1] : moves[0];
    CHECK (octavesBetween (low.frequency, 250.0) <= 1.0 / 3.0);
    CHECK (low.gainDb < -2.5f);
    CHECK (octavesBetween (high.frequency, 3000.0) <= 1.0 / 3.0);
    CHECK (high.gainDb > 2.0f);
}

TEST_CASE ("Fit EQ: nessuna mossa se tutte le bande sono in tolleranza", "[eq]")
{
    std::array<float, ma::kNumThirdOctaves> d {};
    for (int i = 0; i < ma::kNumThirdOctaves; ++i)
        d[(size_t) i] = 0.4f * std::sin ((float) i * 1.7f);   // piccole irregolarità

    CHECK (ma::suggestEqMoves (d, tolerances (1.0f)).empty());
    CHECK (ma::suggestEqMoves ({}, tolerances (1.0f)).empty());
}

TEST_CASE ("Mosse EQ nel confronto: in mastering al massimo 2 dB, in mix fino a 6 dB", "[eq][compare]")
{
    const auto base = test::analyse (test::pinkNoise (30.0, 0.5f));
    const auto profile = ma::TargetProfile::fromSnapshot (base, "Ref");

    auto master = base;   // stesso brano con +5 dB a 400 Hz
    const auto bump = deltaOf ({ makeMove (ma::EqMove::Type::bell, 400.0f, 5.0f, 1.0f) });
    for (int i = 0; i < ma::kNumThirdOctaves; ++i)
        master.thirdOctaveDb[(size_t) i] += bump[(size_t) i];

    ma::CompareOptions options;
    options.phase = ma::WorkPhase::master;
    const auto mastering = ma::compare (master, profile, options);
    REQUIRE_FALSE (mastering.eqMoves.empty());
    CHECK (mastering.eqMoves[0].shownGainDb == Approx (-2.0f));
    CHECK (mastering.eqMoves[0].fixInMix);
    CHECK (mastering.eqMoves[0].gainDb == Approx (-5.0f).margin (0.75f));

    options.phase = ma::WorkPhase::mix;
    const auto mixing = ma::compare (master, profile, options);
    REQUIRE_FALSE (mixing.eqMoves.empty());
    CHECK (mixing.eqMoves[0].shownGainDb == Approx (-5.0f).margin (0.5f));

    // la mossa compare tra le priorità una sola volta, anche se spiega più bande
    int eqPriorities = 0;
    for (const auto& p : mixing.priorities)
        eqPriorities += p.group == "eq:0" ? 1 : 0;
    CHECK (eqPriorities == 1);
}

TEST_CASE ("Rumore rosa filtrato: la mossa suggerita annulla il filtro", "[eq][spectrum]")
{
    const auto pink = test::pinkNoise (30.0, 0.5f);
    const auto profile = ma::TargetProfile::fromSnapshot (test::analyse (pink), "Pink");

    auto filtered = pink;
    for (int ch = 0; ch < filtered.getNumChannels(); ++ch)
    {
        juce::dsp::IIR::Filter<float> peak (juce::dsp::IIR::Coefficients<float>::makePeakFilter (test::kSampleRate, 2000.0f, 1.0f,
                                                                                                    juce::Decibels::decibelsToGain (4.0f)));
        auto* data = filtered.getWritePointer (ch);
        for (int i = 0; i < filtered.getNumSamples(); ++i)
            data[i] = peak.processSample (data[i]);
    }

    const auto result = ma::compare (test::analyse (filtered), profile);
    REQUIRE_FALSE (result.eqMoves.empty());
    const auto& m = result.eqMoves[0];
    INFO (m.describe (m.gainDb).toStdString());
    CHECK (m.type == ma::EqMove::Type::bell);
    CHECK (octavesBetween (m.frequency, 2000.0) <= 1.0 / 3.0);
    CHECK (m.gainDb == Approx (-4.0f).margin (1.0f));
}
