#include "TestSignals.h"
#include "Analysis/LoudnessMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

// Casi di test tratti da EBU Tech 3341 (loudness) e Tech 3342 (LRA)

TEST_CASE ("Tech 3341 #1: sinusoide stereo 1 kHz a -23 dBFS = -23 LUFS", "[loudness]")
{
    const auto s = test::analyse (test::sine (1000.0, -23.0, 20.0));
    CHECK (s.integratedLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.momentaryLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.shortTermLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #2: sinusoide stereo 1 kHz a -33 dBFS = -33 LUFS", "[loudness]")
{
    const auto s = test::analyse (test::sine (1000.0, -33.0, 20.0));
    CHECK (s.integratedLufs == Approx (-33.0f).margin (0.1f));
}

TEST_CASE ("Canale singolo 997 Hz a 0 dBFS = -3.01 LUFS (BS.1770)", "[loudness]")
{
    const auto s = test::analyse (test::sine (997.0, 0.0, 10.0, 1));
    CHECK (s.integratedLufs == Approx (-3.01f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #3: gating relativo (-36/-23/-36)", "[loudness]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 } }));
    CHECK (s.integratedLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #4: gating assoluto (-72/-36/-23/-36/-72)", "[loudness]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -72.0, 10.0 }, { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 }, { -72.0, 10.0 } }));
    CHECK (s.integratedLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #5: -26/-20/-26 = -23 LUFS", "[loudness]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -26.0, 20.0 }, { -20.0, 20.1 }, { -26.0, 20.0 } }));
    CHECK (s.integratedLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Sample rate 44.1 e 96 kHz", "[loudness]")
{
    CHECK (test::analyse (test::sine (1000.0, -23.0, 20.0, 2, 44100.0), 44100.0).integratedLufs == Approx (-23.0f).margin (0.1f));
    CHECK (test::analyse (test::sine (1000.0, -23.0, 20.0, 2, 96000.0), 96000.0).integratedLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3342 #1: LRA 10 LU (-20/-30)", "[lra]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -20.0, 20.0 }, { -30.0, 20.0 } }));
    CHECK (s.loudnessRange == Approx (10.0f).margin (1.0f));
}

TEST_CASE ("Tech 3342 #2: LRA 5 LU (-20/-15)", "[lra]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -20.0, 20.0 }, { -15.0, 20.0 } }));
    CHECK (s.loudnessRange == Approx (5.0f).margin (1.0f));
}

TEST_CASE ("Tech 3342 #3: LRA 20 LU (-40/-20)", "[lra]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -40.0, 20.0 }, { -20.0, 20.0 } }));
    CHECK (s.loudnessRange == Approx (20.0f).margin (1.0f));
}

TEST_CASE ("Tech 3342 #4: LRA 15 LU (-50/-35/-20/-35/-50)", "[lra]")
{
    const auto s = test::analyse (test::sineSegments (1000.0, { { -50.0, 20.0 }, { -35.0, 20.0 }, { -20.0, 20.0 }, { -35.0, 20.0 }, { -50.0, 20.0 } }));
    CHECK (s.loudnessRange == Approx (15.0f).margin (1.0f));
}
