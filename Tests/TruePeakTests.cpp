#include "TestSignals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE ("Sinusoide a fs/4 con fase 45 gradi: true peak 3 dB sopra il sample peak", "[truepeak]")
{
    // i campioni cadono a ±0.707 * A, il picco reale è A
    const auto b = test::sine (test::kSampleRate / 4.0, -6.0, 5.0, 2, test::kSampleRate, juce::MathConstants<double>::pi / 4.0);
    const auto s = test::analyse (b);

    CHECK (s.samplePeakDb[0] == Approx (-9.01f).margin (0.05f));
    CHECK (s.truePeakMaxDb == Approx (-6.0f).margin (0.1f));
}

TEST_CASE ("Sinusoide 997 Hz: true peak = ampiezza", "[truepeak]")
{
    const auto s = test::analyse (test::sine (997.0, -3.0, 5.0));
    CHECK (s.truePeakMaxDb == Approx (-3.0f).margin (0.05f));
    CHECK (s.samplePeakDb[0] == Approx (-3.0f).margin (0.05f));
}

TEST_CASE ("Tech 3341 #15-like: inter-sample overs contati", "[truepeak]")
{
    // sinusoide a fs/4, fase 45°, picco reale +1.5 dBTP con sample peak -1.5 dBFS
    const auto b = test::sine (test::kSampleRate / 4.0, 1.5, 2.0, 2, test::kSampleRate, juce::MathConstants<double>::pi / 4.0);
    const auto s = test::analyse (b);
    CHECK (s.truePeakMaxDb == Approx (1.5f).margin (0.15f));
    CHECK (s.overs0dB >= 1);
    CHECK (s.overs1dB >= 1);
}

TEST_CASE ("True peak a 44.1 e 96 kHz", "[truepeak]")
{
    for (double sr : { 44100.0, 96000.0 })
    {
        const auto b = test::sine (sr / 4.0, -6.0, 3.0, 2, sr, juce::MathConstants<double>::pi / 4.0);
        CHECK (test::analyse (b, sr).truePeakMaxDb == Approx (-6.0f).margin (0.15f));
    }
}
