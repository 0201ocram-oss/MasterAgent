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
    const auto s = test::analyse (clipped);
    CHECK (s.clipEvents > 0);
    CHECK (s.clipEventsFullScale == s.clipEvents);   // tosata a 0 dBFS: sovraccarico

    CHECK (test::analyse (test::sine (1000.0, -0.5, 2.0)).clipEvents == 0);
}

TEST_CASE ("Clipping: il picco di un 808 in un file a 16 bit non è una tosatura", "[technical]")
{
    // colpi di sub con decadimento, ampiezza e nota diverse: a 16 bit il picco si arrotonda su 3-4 campioni identici
    auto make808 = [] (float ceiling)
    {
        juce::AudioBuffer<float> b (2, (int) (test::kSampleRate * 16.0));
        b.clear();
        const double notes[] = { 36.7, 41.2, 43.65, 49.0, 55.0 };
        juce::Random rng (7);
        const int hitLength = (int) (test::kSampleRate * 0.8);
        for (int k = 0; k < 30; ++k)
        {
            const double f = notes[k % 5];
            const double a = 0.55 + 0.4 * rng.nextDouble();
            const int start = k * (int) (test::kSampleRate * 0.5);
            for (int i = 0; i < hitLength && start + i < b.getNumSamples(); ++i)
            {
                const double t = i / test::kSampleRate;
                const float v = (float) (a * std::exp (-t / 0.35) * std::sin (2.0 * juce::MathConstants<double>::pi * f * t));
                b.addSample (0, start + i, v);
            }
        }
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float clipped = juce::jlimit (-ceiling, ceiling, b.getSample (0, i));
            const float q = (float) std::round (clipped * 32767.0f) / 32768.0f;   // come un WAV a 16 bit letto da JUCE
            b.setSample (0, i, q);
            b.setSample (1, i, q);
        }
        return b;
    };

    CHECK (test::analyse (make808 (1.0f)).clipEvents == 0);
    const auto clipped = test::analyse (make808 (0.75f));
    CHECK (clipped.clipEvents > 10);    // circa metà dei colpi tosati (da pochi decimi a 2 dB)
    // tosati a -2.5 dBFS: è il ceiling di un clipper, non un sovraccarico
    CHECK (clipped.clipEventsFullScale == 0);
    CHECK (clipped.ceilingClipDb == Approx (-2.5f).margin (0.1f));
    CHECK (clipped.longCeilingClips > 0);   // i plateau sul sub sono lunghi
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
