#include "TestSignals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE ("Rumore rosa: tilt ~0 dB/oct, bande a terzi piatte", "[spectrum]")
{
    const auto s = test::analyse (test::pinkNoise (30.0, 0.5f));
    CHECK (s.spectralTiltDbPerOct == Approx (0.0f).margin (0.3f));
    for (int i = 5; i <= 28; ++i)
        CHECK (s.thirdOctaveDb[(size_t) i] == Approx (0.0f).margin (1.0f));
}

TEST_CASE ("Rumore bianco: tilt ~+3 dB/oct", "[spectrum]")
{
    const auto s = test::analyse (test::whiteNoise (20.0, 0.5f, true));
    CHECK (s.spectralTiltDbPerOct == Approx (3.01f).margin (0.3f));
}

TEST_CASE ("Sinusoide 1 kHz: energia concentrata nella banda Mid, centroide ~1 kHz", "[spectrum]")
{
    const auto s = test::analyse (test::sine (1000.0, -6.0, 10.0));
    CHECK (s.bandEnergyDb[3] == Approx (0.0f).margin (0.1f));
    CHECK (s.spectralCentroidHz == Approx (1000.0f).margin (20.0f));
    // potenza della banda in dBFS RMS: sinusoide a -6 dBFS picco = -9.01 dB RMS
    float peak = -200.0f;
    for (auto v : s.spectrumLongTermDb) peak = std::max (peak, v);
    CHECK (peak == Approx (-9.01f).margin (0.5f));
}

TEST_CASE ("Stereo: L = R", "[stereo]")
{
    const auto s = test::analyse (test::whiteNoise (10.0, 0.3f, true));
    CHECK (s.correlation == Approx (1.0f).margin (0.01f));
    CHECK (s.widthPercent == Approx (0.0f).margin (0.5f));
    CHECK (s.monoLossDb == Approx (0.0f).margin (0.05f));
    CHECK (s.balanceDb == Approx (0.0f).margin (0.05f));
}

TEST_CASE ("Stereo: L = -R (fase invertita)", "[stereo]")
{
    auto b = test::whiteNoise (10.0, 0.3f, true);
    b.applyGain (1, 0, b.getNumSamples(), -1.0f);
    const auto s = test::analyse (b);
    CHECK (s.correlation == Approx (-1.0f).margin (0.01f));
    CHECK (s.widthPercent == Approx (100.0f).margin (0.5f));
    CHECK (s.monoLossDb < -40.0f);
}

TEST_CASE ("Stereo: L e R indipendenti", "[stereo]")
{
    const auto s = test::analyse (test::whiteNoise (20.0, 0.3f, false));
    CHECK (s.correlation == Approx (0.0f).margin (0.05f));
    CHECK (s.widthPercent == Approx (50.0f).margin (2.0f));
    CHECK (s.monoLossDb == Approx (-3.01f).margin (0.2f));
}

TEST_CASE ("Stereo: sbilanciamento di 3 dB", "[stereo]")
{
    auto b = test::whiteNoise (10.0, 0.3f, true);
    b.applyGain (1, 0, b.getNumSamples(), juce::Decibels::decibelsToGain (-3.0f));
    CHECK (test::analyse (b).balanceDb == Approx (3.0f).margin (0.1f));
}

TEST_CASE ("Precisione alle basse frequenze: sinusoidi sub alla stessa potenza di 1 kHz", "[spectrum][sub]")
{
    // terzo d'ottava i -> centro 1000 * 2^((i-17)/3): 1 = 24.8 Hz, 2 = 31.3 Hz, 3 = 39.4 Hz, 4 = 49.6 Hz
    for (int band : { 1, 2, 3, 4, 5, 7 })
    {
        const double f = ma::thirdOctaveCentre (band);
        auto b = test::sine (f, -12.0, 20.0);
        b.addFrom (0, 0, test::sine (1000.0 * std::pow (2.0, 0.0), -12.0, 20.0), 0, 0, b.getNumSamples());
        b.addFrom (1, 0, test::sine (1000.0 * std::pow (2.0, 0.0), -12.0, 20.0), 1, 0, b.getNumSamples());
        const auto s = test::analyse (b);

        INFO ("banda " << band << " (" << f << " Hz): " << s.thirdOctaveDb[(size_t) band] << " dB, 1 kHz: " << s.thirdOctaveDb[17] << " dB");
        CHECK (s.thirdOctaveDb[(size_t) band] - s.thirdOctaveDb[17] == Approx (0.0f).margin (0.5f));
    }
}

TEST_CASE ("Rumore rosa piatto anche nelle bande sub", "[spectrum][sub]")
{
    const auto s = test::analyse (test::pinkNoise (60.0, 0.5f));
    for (int i = 1; i <= 29; ++i)
    {
        INFO ("banda " << i << " = " << s.thirdOctaveDb[(size_t) i] << " dB");
        CHECK (s.thirdOctaveDb[(size_t) i] == Approx (0.0f).margin (1.0f));
    }
}
