#include "FilePlayer.h"

namespace ma
{

FilePlayer::FilePlayer()
{
    formats.registerBasicFormats();
    readAheadThread.startThread (juce::Thread::Priority::normal);
}

FilePlayer::~FilePlayer()
{
    transport.setSource (nullptr);
    readAheadThread.stopThread (2000);
}

bool FilePlayer::load (const juce::File& newFile)
{
    unload();

    auto* reader = formats.createReaderFor (newFile);
    if (reader == nullptr)
        return false;

    const double fileRate = reader->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (source.get(), 1 << 16, &readAheadThread, fileRate, 2);
    readerSource = std::move (source);
    file = newFile;
    return true;
}

void FilePlayer::unload()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    file = juce::File();
}

void FilePlayer::prepare (double sampleRate, int blockSize)
{
    transport.prepareToPlay (blockSize, sampleRate);
    prepared = true;
}

void FilePlayer::release()
{
    transport.releaseResources();
    prepared = false;
}

void FilePlayer::render (juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
    if (! prepared)
        return;

    juce::AudioSourceChannelInfo info (&buffer, 0, buffer.getNumSamples());
    transport.getNextAudioBlock (info);
}

void FilePlayer::play()
{
    if (readerSource == nullptr)
        return;
    // dalla fine del brano riparte dall'inizio
    if (getLength() > 0.0 && getPosition() >= getLength() - 0.05)
        transport.setPosition (0.0);
    transport.start();
}

void FilePlayer::pause()
{
    transport.stop();
}

bool FilePlayer::isPlaying() const
{
    return transport.isPlaying();
}

void FilePlayer::setPosition (double seconds)
{
    transport.setPosition (juce::jlimit (0.0, std::max (0.0, getLength()), seconds));
}

double FilePlayer::getPosition() const
{
    return transport.getCurrentPosition();
}

double FilePlayer::getLength() const
{
    return readerSource != nullptr ? transport.getLengthInSeconds() : 0.0;
}

} // namespace ma
