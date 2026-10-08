#include "TestSignals.h"
#include "Analysis/ReferenceAnalyzer.h"
#include "Analysis/SnapshotIO.h"
#include "Compare/Comparator.h"
#include "Compare/TargetProfile.h"
#include "Compare/VersionCompare.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    template <size_t N>
    bool sameArray (const std::array<float, N>& a, const std::array<float, N>& b)
    {
        for (size_t i = 0; i < N; ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }

    juce::File writeWav (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("masteragent_test", ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                  .withNumChannels (buffer.getNumChannels())
                                                                                  .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_CASE ("SnapshotIO: round-trip identico e stesso confronto", "[snapshot]")
{
    const auto s = test::analyse (test::pinkNoise (30.0, 0.5f));
    const auto block = ma::saveSnapshot (s);
    CHECK (block.getSize() > 100);
    CHECK (block.getSize() < 200000);

    ma::AnalysisSnapshot loaded;
    REQUIRE (ma::loadSnapshot (block.getData(), block.getSize(), loaded));

    CHECK (loaded.valid == s.valid);
    CHECK (loaded.integratedLufs == s.integratedLufs);
    CHECK (loaded.truePeakMaxDb == s.truePeakMaxDb);
    CHECK (loaded.plr == s.plr);
    CHECK (loaded.drValid == s.drValid);
    CHECK (loaded.shortTermHistory == s.shortTermHistory);
    CHECK (loaded.resonances.size() == s.resonances.size());
    CHECK (sameArray (loaded.spectrumLongTermDb, s.spectrumLongTermDb));
    CHECK (sameArray (loaded.spectrumSideDb, s.spectrumSideDb));
    CHECK (sameArray (loaded.thirdOctaveDb, s.thirdOctaveDb));
    CHECK (sameArray (loaded.thirdOctaveLoudestDb, s.thirdOctaveLoudestDb));
    CHECK (sameArray (loaded.bandWidthPercent, s.bandWidthPercent));
    CHECK (loaded.bandDynamics[3].burstDb == s.bandDynamics[3].burstDb);

    // il confronto con un profilo dà lo stesso risultato
    ma::ProfileLibrary lib;
    const auto* profile = lib.findById ("pop");
    REQUIRE (profile != nullptr);
    const auto a = ma::compare (s, *profile);
    const auto b = ma::compare (loaded, *profile);
    CHECK (a.score == b.score);
    REQUIRE (a.findings.size() == b.findings.size());
    for (size_t i = 0; i < a.findings.size(); ++i)
    {
        CHECK (a.findings[i].id == b.findings[i].id);
        CHECK (a.findings[i].value == b.findings[i].value);
    }
}

TEST_CASE ("SnapshotIO: dati corrotti rifiutati", "[snapshot]")
{
    const auto s = test::analyse (test::pinkNoise (10.0, 0.5f));
    auto block = ma::saveSnapshot (s);

    ma::AnalysisSnapshot out;
    CHECK_FALSE (ma::loadSnapshot (block.getData(), 4, out));
    CHECK_FALSE (ma::loadSnapshot (block.getData(), block.getSize() / 3, out));

    static_cast<char*> (block.getData())[0] ^= 0x55;   // firma sbagliata
    CHECK_FALSE (ma::loadSnapshot (block.getData(), block.getSize(), out));
}

TEST_CASE ("analyseFile in streaming: stesso risultato di analyseBuffer", "[snapshot]")
{
    const auto buffer = test::pinkNoise (25.0, 0.4f, 7);
    const auto file = writeWav (buffer, test::kSampleRate);

    // il file è a 24 bit: confronto con il buffer riletto dal file
    juce::AudioBuffer<float> reread;
    double rate = 0.0;
    juce::String error;
    REQUIRE (ma::readAudioFile (file, reread, rate, error));

    ma::AnalysisSnapshot fromBuffer, fromFile;
    REQUIRE (ma::analyseBuffer (reread, rate, fromBuffer));
    float lastProgress = 0.0f;
    REQUIRE (ma::analyseFile (file, fromFile, error, [&] (float p) { lastProgress = p; }));
    file.deleteFile();

    CHECK (lastProgress == Approx (1.0f));
    CHECK (fromFile.integratedLufs == Approx (fromBuffer.integratedLufs).margin (0.001f));
    CHECK (fromFile.truePeakMaxDb == Approx (fromBuffer.truePeakMaxDb).margin (0.001f));
    CHECK (fromFile.loudnessRange == Approx (fromBuffer.loudnessRange).margin (0.001f));
    CHECK (sameArray (fromFile.thirdOctaveDb, fromBuffer.thirdOctaveDb));

    ma::AnalysisSnapshot none;
    CHECK_FALSE (ma::analyseFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("non_esiste.wav"), none, error));
    CHECK (error.isNotEmpty());
}

TEST_CASE ("Spettro Mid/Side: mono senza Side, controfase senza Mid", "[snapshot]")
{
    // rumore identico sui due canali: tutto Mid
    const auto mono = test::analyse (test::whiteNoise (10.0, 0.3f, true));
    // rumore in controfase (R = -L): tutto Side
    auto antiBuffer = test::whiteNoise (10.0, 0.3f, true);
    antiBuffer.applyGain (1, 0, antiBuffer.getNumSamples(), -1.0f);
    const auto anti = test::analyse (antiBuffer);

    for (int i : { 30, 60, 90 })   // ~113 Hz, ~640 Hz, ~3.6 kHz
    {
        INFO (i);
        CHECK (mono.spectrumMidDb[(size_t) i] - mono.spectrumSideDb[(size_t) i] > 60.0f);
        CHECK (anti.spectrumSideDb[(size_t) i] - anti.spectrumMidDb[(size_t) i] > 60.0f);
        // per un segnale mono il Mid coincide con lo spettro long-term
        CHECK (mono.spectrumMidDb[(size_t) i] == Approx (mono.spectrumLongTermDb[(size_t) i]).margin (0.1f));
    }
}

TEST_CASE ("Confronto versioni: miglioramenti e peggioramenti", "[versions]")
{
    const auto s = test::analyse (test::pinkNoise (40.0, 0.5f));
    const auto p = ma::TargetProfile::fromSnapshot (s, "Self");

    auto bad = s;
    bad.integratedLufs += 6.0f;     // fuori range
    auto good = s;                  // nel range

    // da "bad" a "good": la loudness è migliorata
    const auto improved = ma::compareVersions (ma::compare (bad, p), ma::compare (good, p));
    CHECK (improved.improved >= 1);
    CHECK (improved.worsened == 0);
    CHECK (improved.scoreNow > improved.scoreBefore);
    REQUIRE_FALSE (improved.changes.empty());
    CHECK (improved.changes.front().id.startsWith (ma::highlight::integrated));
    CHECK (improved.changes.front().verdict == ma::Verdict::better);

    // al contrario è un peggioramento, mostrato per primo
    const auto worsened = ma::compareVersions (ma::compare (good, p), ma::compare (bad, p));
    CHECK (worsened.worsened >= 1);
    REQUIRE_FALSE (worsened.changes.empty());
    CHECK (worsened.changes.front().verdict == ma::Verdict::worse);

    // stessa analisi: nulla cambia
    const auto same = ma::compareVersions (ma::compare (s, p), ma::compare (s, p));
    CHECK (same.improved == 0);
    CHECK (same.worsened == 0);
    CHECK (same.changes.empty());
}
