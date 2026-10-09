#include "TestSignals.h"
#include "Analysis/ReferenceAnalyzer.h"
#include "Analysis/Timeline.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    struct Analysed
    {
        ma::AnalysisSnapshot snapshot;
        ma::TrackTimeline timeline;
    };

    Analysed analyseWithTimeline (const juce::AudioBuffer<float>& buffer)
    {
        Analysed a;
        REQUIRE (ma::analyseBuffer (buffer, test::kSampleRate, a.snapshot, {}, {}, &a.timeline));
        return a;
    }

    std::vector<ma::Marker> markersOfType (const std::vector<ma::Marker>& markers, ma::MarkerType type)
    {
        std::vector<ma::Marker> out;
        for (const auto& m : markers)
            if (m.type == type)
                out.push_back (m);
        return out;
    }

    int sampleAt (double seconds) { return (int) (seconds * test::kSampleRate); }
}

TEST_CASE ("Timeline: durata, forma d'onda e serie da 100 ms", "[timeline]")
{
    const auto a = analyseWithTimeline (test::sine (997.0, -6.0, 10.0));
    const auto& t = a.timeline;

    REQUIRE (t.isValid());
    CHECK (t.lengthSamples == sampleAt (10.0));
    CHECK (t.durationSeconds() == Approx (10.0));
    CHECK (t.wave.size() == (size_t) (t.lengthSamples / t.samplesPerPoint));
    CHECK (t.truePeakDb.size() == 100);
    CHECK (t.levelDb.size() == 100);
    CHECK (t.shortTermLufs.size() == a.snapshot.shortTermHistory.size());

    float peak = 0.0f, rms = 0.0f;
    for (const auto& p : t.wave)
    {
        peak = std::max (peak, p.max);
        rms = std::max (rms, p.rms);
    }
    CHECK (peak == Approx (0.501f).margin (0.005f));
    CHECK (rms == Approx (0.354f).margin (0.01f));

    // segnale pulito: nessun segno
    CHECK (ma::findMarkers (t, {}).empty());
}

TEST_CASE ("Timeline: la cronologia non cambia le misure del report", "[timeline]")
{
    auto clipped = test::pinkNoise (12.0, 0.3f);
    for (int i = sampleAt (5.0); i < sampleAt (5.0) + 30; ++i)
        for (int ch = 0; ch < 2; ++ch)
            clipped.setSample (ch, i, 0.97f);

    const auto plain = test::analyse (clipped);
    const auto a = analyseWithTimeline (clipped);
    CHECK (a.snapshot.integratedLufs == plain.integratedLufs);
    CHECK (a.snapshot.truePeakMaxDb == plain.truePeakMaxDb);
    CHECK (a.snapshot.clipEvents == plain.clipEvents);
    CHECK ((int) a.timeline.clips.size() == plain.clipEvents);   // stessi eventi del contatore del report
}

TEST_CASE ("Timeline: clipping segnato nel punto giusto, con il canale", "[timeline]")
{
    auto b = test::pinkNoise (10.0, 0.2f);
    for (int i = sampleAt (4.0); i < sampleAt (4.0) + 40; ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, i, 0.95f);
    for (int i = sampleAt (7.0); i < sampleAt (7.0) + 40; ++i)
        b.setSample (0, i, -0.95f);

    const auto a = analyseWithTimeline (b);
    const auto clips = markersOfType (ma::findMarkers (a.timeline, {}), ma::MarkerType::clip);

    REQUIRE (clips.size() == 2);
    CHECK (clips[0].start == Approx (4.0).margin (0.002));
    CHECK (clips[0].channel == -1);          // entrambi i canali: un solo segno
    CHECK (clips[0].value == 2.0f);          // un evento per canale
    CHECK (clips[1].start == Approx (7.0).margin (0.002));
    CHECK (clips[1].channel == 0);
}

TEST_CASE ("Timeline: buco di silenzio a metà brano, non i silenzi all'inizio, alla fine o dopo una dissolvenza", "[timeline]")
{
    auto b = test::pinkNoise (12.0, 0.2f);
    const auto zero = [&b] (double from, double to, int channel)
    {
        for (int ch = 0; ch < 2; ++ch)
            if (channel < 0 || channel == ch)
                for (int i = sampleAt (from); i < sampleAt (to); ++i)
                    b.setSample (ch, i, 0.0f);
    };

    zero (0.0, 1.0, -1);      // silenzio iniziale
    zero (4.0, 4.05, -1);     // buco di 50 ms
    zero (5.5, 5.52, 1);      // buco di 20 ms sul solo canale destro
    zero (11.0, 12.0, -1);    // silenzio finale

    // dissolvenza di mezzo secondo fino a zero, poi silenzio e ripartenza: non è un buco
    for (int i = sampleAt (7.5); i < sampleAt (8.0); ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, i, b.getSample (ch, i) * (float) (sampleAt (8.0) - i) / (float) (sampleAt (8.0) - sampleAt (7.5)));
    zero (8.0, 8.5, -1);

    const auto a = analyseWithTimeline (b);
    const auto drops = markersOfType (ma::findMarkers (a.timeline, {}), ma::MarkerType::dropout);

    REQUIRE (drops.size() == 2);
    CHECK (drops[0].start == Approx (4.0).margin (0.001));
    CHECK (drops[0].value == Approx (50.0f).margin (1.0f));
    CHECK (drops[0].channel == -1);
    CHECK (drops[1].start == Approx (5.5).margin (0.001));
    CHECK (drops[1].channel == 1);
}

TEST_CASE ("Timeline: true peak oltre il ceiling del target", "[timeline]")
{
    auto b = test::sine (997.0, -3.0, 10.0);
    const float louder = juce::Decibels::decibelsToGain (2.5f);   // -0.5 dBFS tra 5 e 5.5 s
    for (int i = sampleAt (5.0); i < sampleAt (5.5); ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, i, b.getSample (ch, i) * louder);

    const auto a = analyseWithTimeline (b);

    ma::MarkerSettings settings;
    settings.ceilingDb = -1.0f;
    const auto peaks = markersOfType (ma::findMarkers (a.timeline, settings), ma::MarkerType::truePeak);
    REQUIRE (peaks.size() == 1);
    CHECK (peaks[0].start == Approx (5.0).margin (0.11));
    CHECK (peaks[0].end == Approx (5.5).margin (0.11));
    CHECK (peaks[0].value == Approx (-0.5f).margin (0.1f));
    CHECK_FALSE (peaks[0].isCritical());

    // con un ceiling più alto non c'è niente da segnalare
    settings.ceilingDb = 0.0f;
    CHECK (markersOfType (ma::findMarkers (a.timeline, settings), ma::MarkerType::truePeak).empty());
}

TEST_CASE ("Timeline: fase invertita in un tratto", "[timeline]")
{
    auto b = test::whiteNoise (10.0, 0.3f, true);
    for (int i = sampleAt (4.0); i < sampleAt (6.0); ++i)
        b.setSample (1, i, -b.getSample (1, i));

    const auto a = analyseWithTimeline (b);
    const auto phase = markersOfType (ma::findMarkers (a.timeline, {}), ma::MarkerType::phase);
    REQUIRE (phase.size() == 1);
    CHECK (phase[0].start == Approx (4.0).margin (0.35));
    CHECK (phase[0].end == Approx (6.0).margin (0.15));
    CHECK (phase[0].value < -0.9f);
    CHECK (phase[0].isZone());
}

TEST_CASE ("Timeline: sezione troppo compressa (PSR sotto il minimo)", "[timeline]")
{
    // 8 s dinamici (colpi forti su un fondo basso), poi 8 s schiacciati (rumore saturato quasi a onda quadra)
    const int n = sampleAt (16.0);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (3);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / test::kSampleRate;
        const float noise = rng.nextFloat() * 2.0f - 1.0f;
        float x;
        if (t < 8.0)
        {
            const double beat = std::fmod (t, 0.5);
            x = 0.03f * noise + (float) (0.9 * std::exp (-beat * 40.0) * std::sin (juce::MathConstants<double>::twoPi * 80.0 * beat));
        }
        else
        {
            x = 0.5f * std::tanh (20.0f * noise);
        }
        b.setSample (0, i, x);
        b.setSample (1, i, x);
    }

    const auto a = analyseWithTimeline (b);

    ma::MarkerSettings settings;
    settings.minPsr = 7.0f;
    const auto zones = markersOfType (ma::findMarkers (a.timeline, settings), ma::MarkerType::compressed);
    REQUIRE (zones.size() == 1);
    CHECK (zones[0].start >= 5.0);
    CHECK (zones[0].start <= 8.5);
    CHECK (zones[0].end >= 15.5);
    CHECK (zones[0].value < 7.0f);
    CHECK (zones[0].value >= a.snapshot.minPsr - 0.5f);   // stesso calcolo del PSR minimo del report

    // sul mix bus (minPsr = 0) il controllo è spento
    CHECK (markersOfType (ma::findMarkers (a.timeline, {}), ma::MarkerType::compressed).empty());
}

TEST_CASE ("Timeline: identità dei segni e formato del tempo", "[timeline]")
{
    ma::Marker m;
    m.type = ma::MarkerType::dropout;
    m.start = 83.456;
    CHECK (m.id() == "2|83.46");
    CHECK (ma::formatTimelineTime (83.456) == "1:23.4");
    CHECK (ma::formatTimelineTime (83.456, false) == "1:23");
}

TEST_CASE ("Timeline: analisi in background di un file su disco, come nell'app standalone", "[timeline]")
{
    auto b = test::pinkNoise (8.0, 0.2f);
    for (int i = sampleAt (2.0); i < sampleAt (2.0) + 40; ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, i, 0.95f);
    b.clear (sampleAt (5.0), sampleAt (0.03));

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("masteragent_timeline", ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (test::kSampleRate)
                                                                                  .withNumChannels (2)
                                                                                  .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    ma::BatchAnalysisJob job;
    job.start ({ file }, "Analisi", true);
    for (int waited = 0; job.getState() == ma::BatchAnalysisJob::State::running && waited < 600; ++waited)
        juce::Thread::sleep (50);
    REQUIRE (job.getState() == ma::BatchAnalysisJob::State::done);

    const auto results = job.takeResults();
    file.deleteFile();
    REQUIRE (results.timelines.size() == 1);
    REQUIRE (results.timelines[0] != nullptr);
    CHECK (results.timelines[0]->durationSeconds() == Approx (8.0));

    const auto markers = ma::findMarkers (*results.timelines[0], {});
    const auto clips = markersOfType (markers, ma::MarkerType::clip);
    const auto drops = markersOfType (markers, ma::MarkerType::dropout);
    REQUIRE (clips.size() == 1);
    CHECK (clips[0].start == Approx (2.0).margin (0.002));
    REQUIRE (drops.size() == 1);
    CHECK (drops[0].start == Approx (5.0).margin (0.001));

    // senza cronologia (profilo da più brani) i risultati restano leggeri
    ma::BatchAnalysisJob plain;
    auto again = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("masteragent_timeline", ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (again);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (test::kSampleRate)
                                                                                  .withNumChannels (2)
                                                                                  .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    plain.start ({ again }, "Profilo");
    for (int waited = 0; plain.getState() == ma::BatchAnalysisJob::State::running && waited < 600; ++waited)
        juce::Thread::sleep (50);
    const auto plainResults = plain.takeResults();
    again.deleteFile();
    REQUIRE (plainResults.timelines.size() == 1);
    CHECK (plainResults.timelines[0] == nullptr);
}
