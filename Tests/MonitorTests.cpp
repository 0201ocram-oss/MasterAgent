#include "TestSignals.h"
#include "Analysis/ReferenceAnalyzer.h"
#include "Playback/Monitor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    /** Passa il buffer nel monitor a blocchi da 256, come un host. */
    void run (ma::Monitor& monitor, juce::AudioBuffer<float>& buffer)
    {
        for (int pos = 0; pos < buffer.getNumSamples(); pos += 256)
        {
            const int n = std::min (256, buffer.getNumSamples() - pos);
            monitor.process (buffer.getWritePointer (0, pos), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, pos) : nullptr, n);
        }
    }

    float rmsDb (const juce::AudioBuffer<float>& buffer, int channel, int start)
    {
        return juce::Decibels::gainToDecibels (buffer.getRMSLevel (channel, start, buffer.getNumSamples() - start), -200.0f);
    }

    /** Salto massimo tra due campioni consecutivi. */
    float maxStep (const juce::AudioBuffer<float>& buffer, int channel)
    {
        const float* x = buffer.getReadPointer (channel);
        float worst = 0.0f;
        for (int i = 1; i < buffer.getNumSamples(); ++i)
            worst = std::max (worst, std::abs (x[i] - x[i - 1]));
        return worst;
    }

    juce::File writeWav (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("masteragent_ref", ".wav");
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

TEST_CASE ("Monitor: spento non tocca l'uscita", "[monitor]")
{
    ma::Monitor monitor;
    monitor.prepare (test::kSampleRate);

    const auto input = test::whiteNoise (1.0, 0.5f, false);
    auto buffer = input;
    run (monitor, buffer);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (buffer.getSample (ch, i) == input.getSample (ch, i));
}

TEST_CASE ("Monitor: mono e side", "[monitor]")
{
    const auto input = test::whiteNoise (0.5, 0.5f, false);
    const int settled = (int) (2.0 * ma::Monitor::fadeSeconds * test::kSampleRate) + 256;

    SECTION ("mono = (L+R)/2 su entrambi i canali")
    {
        ma::Monitor monitor;
        monitor.prepare (test::kSampleRate);
        monitor.setChoice ({ ma::MonitorMode::mono, -1 });
        auto buffer = input;
        run (monitor, buffer);
        for (int i = settled; i < buffer.getNumSamples(); ++i)
        {
            const float expected = 0.5f * (input.getSample (0, i) + input.getSample (1, i));
            REQUIRE (buffer.getSample (0, i) == Approx (expected).margin (1e-6));
            REQUIRE (buffer.getSample (1, i) == Approx (expected).margin (1e-6));
        }
    }

    SECTION ("side = (L-R)/2 su entrambi i canali")
    {
        ma::Monitor monitor;
        monitor.prepare (test::kSampleRate);
        monitor.setChoice ({ ma::MonitorMode::side, -1 });
        auto buffer = input;
        run (monitor, buffer);
        for (int i = settled; i < buffer.getNumSamples(); ++i)
        {
            const float expected = 0.5f * (input.getSample (0, i) - input.getSample (1, i));
            REQUIRE (buffer.getSample (0, i) == Approx (expected).margin (1e-6));
            REQUIRE (buffer.getSample (1, i) == Approx (expected).margin (1e-6));
        }
    }

    SECTION ("spegnendolo si torna al segnale identico")
    {
        ma::Monitor monitor;
        monitor.prepare (test::kSampleRate);
        monitor.setChoice ({ ma::MonitorMode::side, -1 });
        auto first = input;
        run (monitor, first);
        monitor.setChoice ({});
        auto buffer = input;
        run (monitor, buffer);
        for (int i = settled; i < buffer.getNumSamples(); ++i)
            REQUIRE (buffer.getSample (0, i) == input.getSample (0, i));
    }
}

TEST_CASE ("Monitor: solo banda isola le frequenze della banda", "[monitor]")
{
    const auto tone = test::sine (1000.0, -6.0, 1.0);
    const int settled = (int) (0.2 * test::kSampleRate);
    const float inputDb = rmsDb (tone, 0, settled);

    auto levelIn = [&] (int band)
    {
        ma::Monitor monitor;
        monitor.prepare (test::kSampleRate);
        monitor.setChoice ({ ma::MonitorMode::band, band });
        auto buffer = tone;
        run (monitor, buffer);
        return rmsDb (buffer, 0, settled) - inputDb;
    };

    CHECK (levelIn (3) == Approx (0.0).margin (1.5));   // Mid 500-2k: il tono passa
    CHECK (levelIn (0) < -40.0f);                        // Sub: fuori banda
    CHECK (levelIn (7) < -40.0f);                        // Air: fuori banda
}

TEST_CASE ("Monitor: i cambi non fanno click", "[monitor]")
{
    // tono basso e continuo: un cambio brusco produrrebbe un salto molto più grande del passo naturale della sinusoide
    const auto input = test::sine (100.0, -6.0, 1.2);
    const float natural = maxStep (input, 0);

    ma::Monitor monitor;
    monitor.prepare (test::kSampleRate);
    auto buffer = input;
    const int quarter = buffer.getNumSamples() / 4;
    const ma::MonitorChoice steps[] { { ma::MonitorMode::side, -1 }, { ma::MonitorMode::band, 1 }, { ma::MonitorMode::mono, -1 }, {} };
    for (int k = 0; k < 4; ++k)
    {
        monitor.setChoice (steps[k]);
        for (int pos = k * quarter; pos < (k + 1) * quarter; pos += 256)
            monitor.process (buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos), std::min (256, (k + 1) * quarter - pos));
    }

    CHECK (maxStep (buffer, 0) < natural * 1.6f);
    CHECK (maxStep (buffer, 1) < natural * 1.6f);
}

TEST_CASE ("Monitor: cosa ascoltare per una diagnosi", "[monitor]")
{
    ma::ComparisonResult result;
    ma::EqMove move;
    move.bands = { 5, 6 };
    result.eqMoves.push_back (move);
    result.bandTonalDelta[5] = 1.0f;
    result.bandTonalDelta[6] = 3.0f;

    auto choiceFor = [&] (const juce::String& key)
    {
        ma::Finding f;
        f.key = key;
        return ma::monitorForFinding (f, result);
    };

    CHECK (choiceFor (ma::highlight::band (2)) == ma::MonitorChoice { ma::MonitorMode::band, 2 });
    CHECK (choiceFor (ma::highlight::eqMove (0)) == ma::MonitorChoice { ma::MonitorMode::band, 6 });   // la banda che sposta di più
    CHECK (choiceFor (ma::highlight::resonance (315.0f)) == ma::MonitorChoice { ma::MonitorMode::band, 2 });
    CHECK (choiceFor (ma::highlight::width).mode == ma::MonitorMode::side);
    CHECK (choiceFor (ma::highlight::monoLoss).mode == ma::MonitorMode::mono);
    CHECK (choiceFor (ma::highlight::integrated).mode == ma::MonitorMode::off);
}

TEST_CASE ("Reference: salto di posizione e inizio del ritornello", "[reference]")
{
    // 4 s a -30 dB, poi 6 s a -10 dB (il "ritornello"), poi 4 s a -30 dB
    juce::AudioBuffer<float> audio (2, (int) (14.0 * test::kSampleRate));
    {
        juce::Random random (3);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const double t = i / test::kSampleRate;
            const float gain = (t >= 4.0 && t < 10.0) ? 0.3f : 0.03f;
            const float v = gain * (random.nextFloat() * 2.0f - 1.0f);
            audio.setSample (0, i, v);
            audio.setSample (1, i, v);
        }
    }

    const auto snapshot = test::analyse (audio);
    // parte poco prima del ritornello: si sente anche l'ingresso
    const double loudest = ma::ReferenceTrack::findLoudestStart (snapshot);
    CHECK (loudest > 3.0);
    CHECK (loudest < 4.2);

    // il reference caricato: dopo un salto rende proprio i campioni di quel punto
    const auto file = writeWav (audio, test::kSampleRate);
    ma::ReferenceTrack reference;
    reference.load (file, test::kSampleRate);
    for (int i = 0; i < 300 && reference.getState() == ma::ReferenceTrack::State::loading; ++i)
        juce::Thread::sleep (20);
    REQUIRE (reference.getState() == ma::ReferenceTrack::State::ready);
    CHECK (reference.getLoudestStartSeconds() == Approx (loudest).margin (0.05));

    std::vector<float> left (64), right (64);
    reference.seekPlayback (5.0);
    REQUIRE (reference.renderPlayback (left.data(), right.data(), 64, 1.0f));
    const int start = (int) (5.0 * test::kSampleRate);
    for (int i = 0; i < 64; ++i)
        CHECK (left[(size_t) i] == Approx (audio.getSample (0, start + i)).margin (1.0e-6));   // WAV a 24 bit

    // oltre la fine riparte dall'inizio, come il loop
    reference.seekPlayback (14.0 + 1.0);
    REQUIRE (reference.renderPlayback (left.data(), right.data(), 64, 1.0f));
    CHECK (left[0] == Approx (audio.getSample (0, (int) (1.0 * test::kSampleRate))).margin (1.0e-6));

    reference.clear();
    file.deleteFile();
}

TEST_CASE ("Reference a 44.1 kHz con la scheda audio a 48 kHz: arriva a pronto in pochi secondi", "[reference]")
{
    // un brano da CD (44.1 kHz) con l'uscita a 48 kHz: dopo l'analisi il buffer d'ascolto va ricampionato
    const double fileRate = 44100.0, hostRate = 48000.0, seconds = 240.0;
    juce::AudioBuffer<float> audio (2, (int) (seconds * fileRate));
    juce::Random random (5);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample (ch, i, 0.2f * (random.nextFloat() * 2.0f - 1.0f));
    const auto file = writeWav (audio, fileRate);

    ma::ReferenceTrack reference;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    reference.load (file, hostRate);
    float progressWhenSlow = -1.0f;   // prima della correzione restava al 90% per minuti (ricampionamento lentissimo)
    for (int i = 0; i < 3000 && reference.getState() == ma::ReferenceTrack::State::loading; ++i)
        juce::Thread::sleep (20);
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    if (reference.getState() == ma::ReferenceTrack::State::loading)
        progressWhenSlow = reference.getProgress();
    INFO ("secondi: " << elapsed << ", avanzamento se ancora in corso: " << progressWhenSlow);
    CHECK (reference.getState() == ma::ReferenceTrack::State::ready);
    CHECK (elapsed < 20.0);

    reference.clear();
    file.deleteFile();
}

TEST_CASE ("Ricampionamento per l'ascolto: fedele fino a 20 kHz, senza aliasing udibile", "[reference]")
{
    const auto sine = [] (double frequency, double rate, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate)));
        return b;
    };

    // scarto massimo dalla sinusoide ideale alla nuova frequenza, lontano dai bordi, in dB rispetto all'ampiezza
    const auto errorDb = [] (const juce::AudioBuffer<float>& out, double frequency, double rate)
    {
        double worst = 0.0;
        for (int i = (int) (0.1 * rate); i < out.getNumSamples() - (int) (0.1 * rate); ++i)
        {
            const double ideal = 0.5 * std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate);
            worst = std::max (worst, std::abs (out.getSample (0, i) - ideal));
            worst = std::max (worst, std::abs (out.getSample (1, i) - ideal));   // mono duplicato
        }
        return juce::Decibels::gainToDecibels (worst / 0.5, -200.0);
    };

    for (const double frequency : { 100.0, 1000.0, 10000.0, 18000.0 })
    {
        INFO ("44.1 -> 48 kHz, " << frequency << " Hz");
        const auto out = ma::resampleForPlayback (sine (frequency, 44100.0, 1.0), 44100.0, 48000.0);
        REQUIRE (out.getNumChannels() == 2);
        CHECK (out.getNumSamples() == 48000);
        CHECK (errorDb (out, frequency, 48000.0) < -60.0);
    }
    for (const double frequency : { 1000.0, 15000.0 })
    {
        INFO ("96 -> 48 kHz, " << frequency << " Hz");
        CHECK (errorDb (ma::resampleForPlayback (sine (frequency, 96000.0, 1.0), 96000.0, 48000.0), frequency, 48000.0) < -60.0);
    }

    // 96 -> 48 kHz: un tono a 30 kHz ricadrebbe a 18 kHz; il filtro lo toglie
    const auto alias = ma::resampleForPlayback (sine (30000.0, 96000.0, 1.0), 96000.0, 48000.0);
    float peak = 0.0f;
    for (int i = 4800; i < alias.getNumSamples() - 4800; ++i)
        peak = std::max (peak, std::abs (alias.getSample (0, i)));
    CHECK (juce::Decibels::gainToDecibels (peak / 0.5f) < -80.0f);

    // stessa frequenza: copia esatta; interruzione: buffer vuoto
    const auto same = ma::resampleForPlayback (sine (440.0, 48000.0, 0.5), 48000.0, 48000.0);
    CHECK (same.getNumSamples() == 24000);
    CHECK (ma::resampleForPlayback (sine (440.0, 44100.0, 2.0), 44100.0, 48000.0, [] { return true; }).getNumSamples() == 0);
}
