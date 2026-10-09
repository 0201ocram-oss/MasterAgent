#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace ma
{

/**
    Lettore del brano analizzato (solo app standalone): play/pausa e posizionamento dalla forma d'onda.
    Lettura anticipata su un thread proprio; il ricampionamento alla frequenza della scheda audio
    lo fa AudioTransportSource. load/play/pause/setPosition dal thread GUI, render dall'audio thread.
*/
class FilePlayer
{
public:
    FilePlayer();
    ~FilePlayer();

    bool load (const juce::File& file);
    void unload();
    juce::File getFile() const { return file; }

    void prepare (double sampleRate, int blockSize);
    void release();

    /** Audio thread: scrive il brano nei primi due canali di buffer (silenzio se fermo o senza file). */
    void render (juce::AudioBuffer<float>& buffer);

    void play();
    void pause();
    bool isPlaying() const;

    void setPosition (double seconds);
    double getPosition() const;
    double getLength() const;

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAheadThread { "MasterAgent player" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::File file;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE (FilePlayer)
};

} // namespace ma
