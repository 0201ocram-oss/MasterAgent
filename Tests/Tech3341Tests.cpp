#include "TestSignals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

// EBU Tech 3341 (2023), tabella 1 "Minimum requirements test signals": i casi generabili in modo esatto.
// I casi 1-5 sono in LoudnessTests.cpp; 6 è multicanale; 7 e 8 sono programmi reali (tools/ebu_conformance.py).

namespace
{
    constexpr double sr = test::kSampleRate;

    /** Silenzio, tono a 1 kHz a level dBFS per toneSeconds, silenzio. */
    juce::AudioBuffer<float> gapToneGap (double before, double level, double toneSeconds, double after)
    {
        const int a = (int) std::lround (before * sr), t = (int) std::lround (toneSeconds * sr), b = (int) std::lround (after * sr);
        juce::AudioBuffer<float> out (2, a + t + b);
        out.clear();
        const auto tone = test::sine (1000.0, level, toneSeconds);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, a, tone, ch, 0, std::min (t, tone.getNumSamples()));
        return out;
    }

    /** Dissolvenze di 10 ms all'inizio e alla fine, come chiede la norma per i casi di true peak. */
    void taper (juce::AudioBuffer<float>& b, double rate = sr)
    {
        const int fade = (int) (0.01 * rate);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            b.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
            b.applyGainRamp (ch, b.getNumSamples() - fade, fade, 1.0f, 0.0f);
        }
    }

    /** Sinusoide stereo con ampiezza in FFS (1.0 = fondo scala) e fase in gradi. */
    juce::AudioBuffer<float> toneFfs (double freq, double amplitude, double phaseDegrees, double seconds = 2.0)
    {
        auto b = test::sine (freq, juce::Decibels::gainToDecibels (amplitude), seconds, 2, sr, juce::degreesToRadians (phaseDegrees));
        taper (b);
        return b;
    }

    /**
        Casi 20-23: sinusoide fs/6 (ampiezza 0.5) con un solo periodo di fs/4 (ampiezza 1.0) inserito in continuità di
        fase, sintetizzata a 4 fs, filtrata passa-basso e sottocampionata a fs partendo dal campione offset.
    */
    juce::AudioBuffer<float> singlePeriodBurst (int offset)
    {
        const double fs4 = 4.0 * sr;
        const int n4 = (int) (2.0 * fs4);
        const int start = 24 * (n4 / 48);   // passaggio per lo zero in salita di fs/6 (24 campioni a 4 fs)
        std::vector<double> x ((size_t) n4);
        for (int n = 0; n < n4; ++n)
        {
            if (n < start)
                x[(size_t) n] = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * (sr / 6.0) * n / fs4);
            else if (n < start + 16)
                x[(size_t) n] = 1.0 * std::sin (2.0 * juce::MathConstants<double>::pi * (sr / 4.0) * (n - start) / fs4);
            else
                x[(size_t) n] = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * (sr / 6.0) * (n - 16) / fs4);
        }

        // passa-basso anti-alias a 0.45 fs (sinc con finestra di Kaiser, 511 tap) applicato a 4 fs
        constexpr int taps = 511;
        const double cutoff = 0.45 * sr / fs4;   // in cicli per campione a 4 fs
        std::vector<double> h ((size_t) taps);
        double sum = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const double m = k - (taps - 1) / 2.0;
            const double sinc = m == 0.0 ? 2.0 * cutoff : std::sin (2.0 * juce::MathConstants<double>::pi * cutoff * m) / (juce::MathConstants<double>::pi * m);
            const double r = m / ((taps - 1) / 2.0);
            auto i0 = [] (double v) { double s = 1.0, t = 1.0; for (int j = 1; j < 50; ++j) { t *= (v / (2.0 * j)) * (v / (2.0 * j)); s += t; } return s; };
            h[(size_t) k] = sinc * i0 (9.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (9.0);
            sum += h[(size_t) k];
        }
        for (auto& v : h)
            v /= sum;

        const int out = (n4 - offset) / 4;
        juce::AudioBuffer<float> b (2, out);
        for (int i = 0; i < out; ++i)
        {
            const int centre = i * 4 + offset;
            double acc = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const int idx = centre + k - (taps - 1) / 2;
                if (idx >= 0 && idx < n4)
                    acc += h[(size_t) k] * x[(size_t) idx];
            }
            b.setSample (0, i, (float) acc);
            b.setSample (1, i, (float) acc);
        }
        taper (b);
        return b;
    }
}

TEST_CASE ("Tech 3341 #1 completo: M, S e I = -23 LUFS", "[tech3341]")
{
    const auto s = test::analyse (test::sine (1000.0, -23.0, 20.0));
    CHECK (s.momentaryLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.shortTermLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.integratedLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.maxMomentaryLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.maxShortTermLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #9: short-term costante a -23 LUFS dopo 3 s", "[tech3341]")
{
    std::vector<std::pair<double, double>> segments;
    for (int k = 0; k < 5; ++k)
    {
        segments.push_back ({ -20.0, 1.34 });
        segments.push_back ({ -30.0, 1.66 });
    }
    const auto s = test::analyse (test::sineSegments (1000.0, segments));
    REQUIRE_FALSE (s.shortTermHistory.empty());
    for (auto v : s.shortTermHistory)
        CHECK (v == Approx (-23.0f).margin (0.1f));
    CHECK (s.maxShortTermLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #10: massimo short-term = -23 LUFS con qualsiasi ritardo", "[tech3341]")
{
    for (int i = 0; i < 20; ++i)
    {
        INFO ("segmento " << i);
        const auto s = test::analyse (gapToneGap (i * 0.15, -23.0, 3.0, 1.0));
        CHECK (s.maxShortTermLufs == Approx (-23.0f).margin (0.1f));
    }
}

TEST_CASE ("Tech 3341 #12: momentary costante a -23 LUFS dopo 1 s", "[tech3341]")
{
    std::vector<std::pair<double, double>> segments;
    for (int k = 0; k < 25; ++k)
    {
        segments.push_back ({ -20.0, 0.18 });
        segments.push_back ({ -30.0, 0.22 });
    }
    const auto s = test::analyse (test::sineSegments (1000.0, segments));
    CHECK (s.momentaryLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.maxMomentaryLufs == Approx (-23.0f).margin (0.1f));
}

TEST_CASE ("Tech 3341 #13: massimo momentary = -23 LUFS con qualsiasi ritardo", "[tech3341]")
{
    // senza la griglia fine da 10 ms, con ritardi di 40-60 ms il massimo si leggeva fino a 0.5 dB sotto
    for (int i = 0; i < 20; ++i)
    {
        INFO ("segmento " << i);
        const auto s = test::analyse (gapToneGap (i * 0.02, -23.0, 0.4, 1.0));
        CHECK (s.maxMomentaryLufs == Approx (-23.0f).margin (0.1f));
    }
}

TEST_CASE ("Tech 3341 #15-19: true peak delle sinusoidi di prova (+0.2 / -0.4 dB)", "[tech3341][truepeak]")
{
    struct Case { int number; double freq, amplitude, phase, expected; };
    const Case cases[] {
        { 15, sr / 4.0, 0.50, 0.0, -6.0 },
        { 16, sr / 4.0, 0.50, 45.0, -6.0 },
        { 17, sr / 6.0, 0.50, 60.0, -6.0 },
        { 18, sr / 8.0, 0.50, 67.5, -6.0 },
        { 19, sr / 4.0, 1.41, 45.0, 3.0 },
    };
    for (const auto& c : cases)
    {
        INFO ("caso " << c.number);
        const float tp = test::analyse (toneFfs (c.freq, c.amplitude, c.phase)).truePeakMaxDb;
        CHECK (tp <= (float) c.expected + 0.2f);
        CHECK (tp >= (float) c.expected - 0.4f);
    }
}

TEST_CASE ("Tech 3341 #20-23: picco di un singolo periodo con quattro allineamenti (+0.2 / -0.4 dB)", "[tech3341][truepeak]")
{
    for (int offset = 0; offset < 4; ++offset)
    {
        INFO ("caso " << 20 + offset << " (offset " << offset << ")");
        const float tp = test::analyse (singlePeriodBurst (offset)).truePeakMaxDb;
        CHECK (tp <= 0.2f);
        CHECK (tp >= -0.4f);
    }
}
