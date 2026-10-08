#include "TestSignals.h"
#include "Analysis/AnalysisEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    /** Spinge un buffer nel LiveAnalysis a blocchi, come farebbe l'audio thread di una DAW. */
    void pushAll (ma::LiveAnalysis& live, const juce::AudioBuffer<float>& b, int block, bool canBlock)
    {
        for (int pos = 0; pos < b.getNumSamples(); pos += block)
        {
            const int n = std::min (block, b.getNumSamples() - pos);
            live.push (b.getReadPointer (0, pos), b.getReadPointer (1, pos), n, canBlock);
        }
    }

    /** Attende che il thread di analisi abbia pubblicato uno snapshot con almeno "seconds" analizzati. */
    ma::AnalysisSnapshot waitFor (ma::LiveAnalysis& live, double seconds)
    {
        ma::AnalysisSnapshot s;
        for (int i = 0; i < 1000; ++i)
        {
            live.getSnapshot (s);
            if (s.valid && s.secondsAnalyzed >= seconds - 0.2)
                break;
            juce::Thread::sleep (10);
        }
        juce::Thread::sleep (250);   // ultimo publish
        live.getSnapshot (s);
        return s;
    }
}

TEST_CASE ("LiveAnalysis (brano intero) coincide con l'analisi offline", "[live]")
{
    const auto signal = test::sineSegments (1000.0, { { -26.0, 20.0 }, { -20.0, 20.1 }, { -26.0, 20.0 } });

    ma::LiveAnalysis live;
    live.prepare (test::kSampleRate, 2);
    pushAll (live, signal, 512, true);   // canBlock: nessuna perdita anche spingendo più veloce del tempo reale
    const auto s = waitFor (live, 60.0);

    CHECK_FALSE (live.hasOverflowed());
    CHECK (s.integratedLufs == Approx (-23.0f).margin (0.1f));
    CHECK (s.secondsAnalyzed == Approx (60.1).margin (0.2));
    live.release();
}

TEST_CASE ("LiveAnalysis senza attesa va in overflow se l'audio arriva troppo in fretta", "[live]")
{
    ma::LiveAnalysis live;
    live.prepare (test::kSampleRate, 2);
    const auto signal = test::whiteNoise (60.0, 0.3f, false);
    pushAll (live, signal, 4096, false);   // tutto in un colpo: il FIFO da 2 s non basta
    CHECK (live.hasOverflowed());
    live.release();
}

TEST_CASE ("Modalità Live: la finestra mobile segue il materiale recente", "[live]")
{
    ma::LiveAnalysis live;
    live.prepare (test::kSampleRate, 2);
    live.setLiveMode (true, 10.0);
    juce::Thread::sleep (50);   // il thread applica il cambio di modalità

    // 30 s a -30 dBFS poi 12 s a -14 dBFS: in Live l'integrated deve riflettere solo la parte finale
    const auto signal = test::sineSegments (1000.0, { { -30.0, 30.0 }, { -14.0, 12.0 } });
    pushAll (live, signal, 512, true);

    ma::AnalysisSnapshot s;
    for (int i = 0; i < 500; ++i)
    {
        juce::Thread::sleep (10);
        live.getSnapshot (s);
        if (s.valid && s.integratedLufs > -15.0f)
            break;
    }
    juce::Thread::sleep (250);
    live.getSnapshot (s);

    CHECK (s.liveMode);
    CHECK (s.secondsAnalyzed <= 10.5);
    CHECK (s.secondsAnalyzed >= 4.5);
    CHECK (s.integratedLufs == Approx (-14.0f).margin (0.3f));
    live.release();
}
