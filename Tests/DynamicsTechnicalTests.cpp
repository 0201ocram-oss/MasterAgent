#include "TestSignals.h"
#include "Analysis/StreamingPreview.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE ("Sinusoide: crest factor 3.01 dB, DR 0", "[dynamics]")
{
    const auto s = test::analyse (test::sine (1000.0, -6.0, 30.0));
    CHECK (s.crestDb == Approx (3.01f).margin (0.05f));
    REQUIRE (s.drValid);
    CHECK (s.drValue == Approx (0.0f).margin (0.1f));
}

TEST_CASE ("PLR = true peak - integrated", "[dynamics]")
{
    const auto s = test::analyse (test::sine (1000.0, -10.0, 20.0));
    CHECK (s.plr == Approx (s.truePeakMaxDb - s.integratedLufs).margin (0.01f));
    CHECK (s.plr == Approx (0.0f).margin (0.2f));   // sinusoide 1 kHz stereo: LUFS ~= dBFS di picco
}

TEST_CASE ("DC offset rilevato", "[technical]")
{
    auto b = test::sine (1000.0, -12.0, 5.0);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.addSample (ch, i, 0.01f);
    const auto s = test::analyse (b);
    CHECK (s.dcOffsetDb[0] == Approx (-40.0f).margin (0.5f));
}

TEST_CASE ("Clipping: sinusoide tosata rilevata, sinusoide pulita no", "[technical]")
{
    auto clipped = test::sine (100.0, 6.0, 2.0);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::clip (clipped.getWritePointer (ch), clipped.getReadPointer (ch), -1.0f, 1.0f, clipped.getNumSamples());
    CHECK (test::analyse (clipped).clipEvents > 0);

    CHECK (test::analyse (test::sine (1000.0, -0.5, 2.0)).clipEvents == 0);
}

TEST_CASE ("Canale muto rilevato", "[technical]")
{
    auto b = test::sine (1000.0, -12.0, 5.0);
    b.clear (1, 0, b.getNumSamples());
    const auto s = test::analyse (b);
    CHECK (s.channelSilent[1]);
    CHECK_FALSE (s.channelSilent[0]);
}

TEST_CASE ("Anteprima streaming", "[streaming]")
{
    const auto loud = ma::computeStreamingPreview (-8.0f, -0.5f);
    REQUIRE (! loud.empty());
    CHECK (loud[0].name == "Spotify");
    CHECK (loud[0].gainDb == Approx (-6.0f));
    CHECK (loud[1].gainDb == Approx (-8.0f));   // Apple -16
    CHECK (loud[0].tooLoud);

    const auto quiet = ma::computeStreamingPreview (-20.0f, -3.0f);
    CHECK (quiet[0].gainDb == Approx (2.0f));    // Spotify alza ma il TP resta <= -1 dBTP
    CHECK (quiet[2].gainDb == Approx (0.0f));    // YouTube non alza
    CHECK (quiet[0].tooQuiet);
}
