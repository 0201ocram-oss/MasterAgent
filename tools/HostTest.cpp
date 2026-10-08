// Verifica del plugin VST3 reale dentro un host minimale.
// Uso: MasterAgentHostTest <percorso MasterAgent.vst3>
//
// 1. Bit-transparency: l'uscita deve essere identica all'ingresso (sample rate, block size, layout diversi,
//    block size variabili come fanno molte DAW).
// 2. Costo CPU di processBlock in real-time.
// 3. Render offline (setNonRealtime): velocità e assenza di blocchi.
// 4. Salvataggio/ripristino dello stato.

#include <juce_audio_processors/juce_audio_processors.h>

#include <chrono>
#include <cstdio>

namespace
{
    int failures = 0;

    /** Transport in riproduzione, come quello di una DAW durante play ed export. */
    struct PlayingTransport : public juce::AudioPlayHead
    {
        double sampleRate = 48000.0;
        int64_t samplePosition = 0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setTimeInSamples (samplePosition);
            info.setTimeInSeconds ((double) samplePosition / sampleRate);
            info.setBpm (120.0);
            return info;
        }
    };

    PlayingTransport transport;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    /** Segnale "musicale": sinusoidi + rumore, con livelli vicini al fondo scala e valori ripetuti. */
    void fillSignal (juce::AudioBuffer<float>& b, int64_t startSample, double sr, juce::Random& rng)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (double) (startSample + i) / sr;
            const float base = (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t)
                                      + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 1234.5 * t));
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.setSample (ch, i, juce::jlimit (-1.0f, 1.0f, base + (rng.nextFloat() - 0.5f) * 0.6f + (ch == 1 ? 0.01f : 0.0f)));
        }
    }

    std::unique_ptr<juce::AudioPluginInstance> load (juce::VST3PluginFormat& format, const juce::PluginDescription& desc,
                                                     double sr, int block)
    {
        juce::String error;
        auto instance = format.createInstanceFromDescription (desc, sr, block, error);
        if (instance == nullptr)
            std::printf ("Impossibile caricare il plugin: %s\n", error.toRawUTF8());
        return instance;
    }

    /** Processa "seconds" di audio e confronta uscita e ingresso campione per campione. */
    bool nullTest (juce::AudioPluginInstance& plugin, double sr, int maxBlock, bool variableBlocks, int channels, double seconds)
    {
        juce::AudioChannelSet set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        if (! plugin.setBusesLayout (layout))
        {
            std::printf ("  layout a %d canali non accettato\n", channels);
            return false;
        }

        plugin.setRateAndBufferSizeDetails (sr, maxBlock);
        plugin.prepareToPlay (sr, maxBlock);
        transport.sampleRate = sr;
        transport.samplePosition = 0;
        plugin.setPlayHead (&transport);

        juce::Random rng (42);
        juce::AudioBuffer<float> buffer (channels, maxBlock), original (channels, maxBlock);
        juce::MidiBuffer midi;

        const auto total = (int64_t) (seconds * sr);
        int64_t pos = 0;
        bool identical = true;

        while (pos < total)
        {
            const int n = (int) std::min<int64_t> (total - pos, variableBlocks ? 1 + rng.nextInt (maxBlock) : maxBlock);
            buffer.setSize (channels, n, false, false, true);
            original.setSize (channels, n, false, false, true);
            fillSignal (buffer, pos, sr, rng);
            original.makeCopyOf (buffer, true);

            plugin.processBlock (buffer, midi);
            transport.samplePosition += n;

            for (int ch = 0; ch < channels && identical; ++ch)
                identical = std::memcmp (buffer.getReadPointer (ch), original.getReadPointer (ch), sizeof (float) * (size_t) n) == 0;

            pos += n;
        }

        plugin.releaseResources();
        return identical;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::printf ("Uso: MasterAgentHostTest <MasterAgent.vst3>\n");
        return 2;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, juce::String (argv[1]));

    if (descriptions.isEmpty())
    {
        std::printf ("Nessun plugin trovato in %s\n", argv[1]);
        return 2;
    }

    const auto& desc = *descriptions[0];
    std::printf ("Plugin: %s %s (%s)\n\n", desc.manufacturerName.toRawUTF8(), desc.name.toRawUTF8(), desc.version.toRawUTF8());

    // ---------------------------------------------------------------------------
    std::printf ("1) Bit-transparency (uscita == ingresso, confronto bit a bit)\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int block : { 1, 32, 256, 511, 1024, 4096 })
        {
            auto plugin = load (format, desc, sr, block);
            if (plugin == nullptr) return 1;
            check (nullTest (*plugin, sr, block, false, 2, block == 1 ? 1.0 : 6.0),
                   "stereo " + juce::String (sr / 1000.0, 1) + " kHz, blocco " + juce::String (block));
        }

        auto plugin = load (format, desc, sr, 2048);
        check (nullTest (*plugin, sr, 2048, true, 2, 10.0), "stereo " + juce::String (sr / 1000.0, 1) + " kHz, blocchi variabili 1..2048");
        check (nullTest (*plugin, sr, 512, false, 1, 5.0), "mono " + juce::String (sr / 1000.0, 1) + " kHz, blocco 512");
    }

    // ---------------------------------------------------------------------------
    std::printf ("\n2) Costo CPU dell'audio thread (48 kHz, blocco 256)\n");
    {
        auto plugin = load (format, desc, 48000.0, 256);
        plugin->prepareToPlay (48000.0, 256);
        transport.sampleRate = 48000.0;
        transport.samplePosition = 0;
        plugin->setPlayHead (&transport);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        juce::Random rng (1);

        double worst = 0.0, sum = 0.0;
        const int blocks = (int) (30.0 * 48000.0 / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            fillSignal (buffer, (int64_t) b * 256, 48000.0, rng);
            const auto t0 = std::chrono::high_resolution_clock::now();
            plugin->processBlock (buffer, midi);
            const auto t1 = std::chrono::high_resolution_clock::now();
            transport.samplePosition += 256;
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count();
            worst = std::max (worst, us);
            sum += us;

            // ritmo real-time approssimato, così il thread di analisi lavora come in una DAW
            if (b % 16 == 0)
                juce::Thread::sleep (80);
        }
        const double blockUs = 256.0 / 48000.0 * 1e6;
        std::printf ("  medio %.2f us, peggiore %.1f us su %.0f us di budget (%.2f%% medio)\n", sum / blocks, worst, blockUs, 100.0 * sum / blocks / blockUs);
        check (sum / blocks < blockUs * 0.02, "processBlock medio sotto il 2% del budget del blocco");
        plugin->releaseResources();
    }

    // ---------------------------------------------------------------------------
    std::printf ("\n3) Render offline (Export Mixdown): 5 minuti a 48 kHz, blocco 1024\n");
    {
        auto plugin = load (format, desc, 48000.0, 1024);
        plugin->setNonRealtime (true);
        plugin->prepareToPlay (48000.0, 1024);
        transport.sampleRate = 48000.0;
        transport.samplePosition = 0;
        plugin->setPlayHead (&transport);
        juce::AudioBuffer<float> buffer (2, 1024), original (2, 1024);
        juce::MidiBuffer midi;
        juce::Random rng (7);

        const int blocks = (int) (300.0 * 48000.0 / 1024.0);
        bool identical = true;
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            fillSignal (buffer, (int64_t) b * 1024, 48000.0, rng);
            original.makeCopyOf (buffer, true);
            plugin->processBlock (buffer, midi);
            transport.samplePosition += 1024;
            identical = identical && std::memcmp (buffer.getReadPointer (0), original.getReadPointer (0), sizeof (float) * 1024) == 0;
        }
        const double secs = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("  5:00 di audio processati in %.1f s (%.0fx tempo reale)\n", secs, 300.0 / secs);
        check (identical, "uscita identica all'ingresso anche in render offline");
        check (secs > 1.0, "l'analisi ha davvero elaborato tutto l'audio (il render ha atteso il thread di analisi)");
        check (secs < 300.0, "render offline piu' veloce del tempo reale anche attendendo l'analisi");
        plugin->releaseResources();
    }

    // ---------------------------------------------------------------------------
    std::printf ("\n4) Stato del plugin\n");
    {
        auto plugin = load (format, desc, 48000.0, 512);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto restored = load (format, desc, 48000.0, 512);
        restored->setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock state2;
        restored->getStateInformation (state2);
        check (state.getSize() > 0 && state == state2, "stato salvato e ripristinato identico (" + juce::String ((int) state.getSize()) + " byte)");
        check (nullTest (*restored, 48000.0, 512, false, 2, 3.0), "trasparente dopo il ripristino dello stato");
        check (plugin->getLatencySamples() == 0, "latenza dichiarata: 0 campioni");
        check (plugin->getTailLengthSeconds() == 0.0, "coda (tail): 0 s");
    }

    std::printf ("\n%s (%d errori)\n", failures == 0 ? "TUTTI I CONTROLLI SUPERATI" : "CI SONO ERRORI", failures);
    return failures == 0 ? 0 : 1;
}
