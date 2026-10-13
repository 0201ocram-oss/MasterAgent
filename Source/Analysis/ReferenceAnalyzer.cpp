#include "ReferenceAnalyzer.h"

namespace ma
{

bool analyseBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate, AnalysisSnapshot& out,
                    std::function<void (float)> progress, std::function<bool()> shouldCancel, TrackTimeline* timeline)
{
    const int numChannels = std::min (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0 || sampleRate <= 0.0)
        return false;

    AnalysisEngine engine;
    engine.prepare (sampleRate, numChannels);
    if (timeline != nullptr)
        engine.startTimeline (*timeline, numSamples);

    constexpr int chunk = 4096;
    for (int pos = 0; pos < numSamples; pos += chunk)
    {
        if (shouldCancel && shouldCancel())
            return false;

        const int n = std::min (chunk, numSamples - pos);
        engine.process (buffer.getReadPointer (0, pos),
                        numChannels > 1 ? buffer.getReadPointer (1, pos) : nullptr, n);

        if (progress && (pos / chunk) % 64 == 0)
            progress ((float) pos / (float) numSamples);
    }

    engine.finishTimeline();
    engine.buildSnapshot (out);
    if (progress)
        progress (1.0f);
    return true;
}

namespace
{
    std::unique_ptr<juce::AudioFormatReader> openReader (const juce::File& file, juce::String& error, double maxSeconds)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
            error = "Formato non supportato o file illeggibile";
        else if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            error = "File vuoto";
        else if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
            error = "File troppo lungo (max " + juce::String (juce::roundToInt (maxSeconds / 60.0)) + " minuti)";
        else
            return reader;

        return nullptr;
    }
}

bool analyseFile (const juce::File& file, AnalysisSnapshot& out, juce::String& error,
                  std::function<void (float)> progress, std::function<bool()> shouldCancel, double maxSeconds,
                  TrackTimeline* timeline)
{
    auto reader = openReader (file, error, maxSeconds);
    if (reader == nullptr)
        return false;

    const int numChannels = (int) std::min (2u, reader->numChannels);
    const auto length = reader->lengthInSamples;

    AnalysisEngine engine;
    engine.prepare (reader->sampleRate, numChannels);
    if (timeline != nullptr)
        engine.startTimeline (*timeline, length);

    // blocchi grandi per la lettura, poi gli stessi blocchi da 4096 di analyseBuffer
    constexpr int readSize = 65536, chunk = 4096;
    juce::AudioBuffer<float> block (numChannels, readSize);
    int blockCount = 0;

    for (juce::int64 pos = 0; pos < length; pos += readSize)
    {
        if (shouldCancel && shouldCancel())
            return false;

        const int n = (int) std::min ((juce::int64) readSize, length - pos);
        if (! reader->read (&block, 0, n, pos, true, numChannels > 1))
        {
            error = "Errore di lettura";
            return false;
        }

        for (int offset = 0; offset < n; offset += chunk)
            engine.process (block.getReadPointer (0, offset), numChannels > 1 ? block.getReadPointer (1, offset) : nullptr,
                            std::min (chunk, n - offset));

        if (progress && (blockCount++ % 4) == 0)
            progress ((float) ((double) pos / (double) length));
    }

    engine.finishTimeline();
    engine.buildSnapshot (out);
    if (progress)
        progress (1.0f);
    return true;
}

bool readAudioFile (const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate, juce::String& error, double maxSeconds)
{
    auto reader = openReader (file, error, maxSeconds);
    if (reader == nullptr)
        return false;

    const int numChannels = (int) std::min (2u, reader->numChannels);
    const int length = (int) reader->lengthInSamples;
    audio.setSize (numChannels, length);
    if (! reader->read (&audio, 0, length, 0, true, numChannels > 1))
    {
        error = "Errore di lettura";
        return false;
    }

    sampleRate = reader->sampleRate;
    return true;
}

//==============================================================================
BatchAnalysisJob::BatchAnalysisJob() : juce::Thread ("MasterAgent batch analysis") {}

BatchAnalysisJob::~BatchAnalysisJob()
{
    stopThread (4000);
}

void BatchAnalysisJob::start (const juce::Array<juce::File>& newFiles, const juce::String& newLabel, bool withTimelines)
{
    stopThread (4000);
    {
        std::lock_guard<std::mutex> lock (mutex);
        files = newFiles;
        label = newLabel;
        timelinesWanted = withTimelines;
        results = {};
    }
    currentIndex.store (0);
    progress.store (0.0f);
    state.store (State::running);
    startThread (juce::Thread::Priority::background);
}

void BatchAnalysisJob::cancel()
{
    stopThread (4000);
    state.store (State::idle);
}

juce::String BatchAnalysisJob::getStatusText() const
{
    std::lock_guard<std::mutex> lock (mutex);
    const int index = currentIndex.load();
    if (! juce::isPositiveAndBelow (index, files.size()))
        return {};

    const auto percent = juce::String (juce::roundToInt (progress.load() * 100.0f)) + "%";
    if (files.size() == 1)
        return label + " " + files[index].getFileName() + " " + percent;

    return label + ": " + juce::String (index + 1) + "/" + juce::String (files.size())
         + "  " + files[index].getFileName() + " " + percent;
}

BatchAnalysisJob::Results BatchAnalysisJob::takeResults()
{
    std::lock_guard<std::mutex> lock (mutex);
    state.store (State::idle);
    return std::exchange (results, {});
}

void BatchAnalysisJob::run()
{
    juce::Array<juce::File> toAnalyse;
    bool withTimelines = false;
    {
        std::lock_guard<std::mutex> lock (mutex);
        toAnalyse = files;
        withTimelines = timelinesWanted;
    }

    for (int i = 0; i < toAnalyse.size() && ! threadShouldExit(); ++i)
    {
        currentIndex.store (i);
        progress.store (0.0f);

        juce::String error;
        AnalysisSnapshot snapshot;
        auto timeline = withTimelines ? std::make_shared<TrackTimeline>() : nullptr;

        const bool ok = analyseFile (toAnalyse[i], snapshot, error, [this] (float p) { progress.store (p); },
                                     [this] { return threadShouldExit(); }, 20.0 * 60.0, timeline.get());

        std::lock_guard<std::mutex> lock (mutex);
        if (ok && snapshot.integratedLufs > -70.0f)
        {
            results.snapshots.push_back (std::move (snapshot));
            results.files.add (toAnalyse[i]);
            results.names.add (toAnalyse[i].getFileName());
            results.timelines.push_back (std::move (timeline));
        }
        else if (! threadShouldExit())
        {
            results.skipped.add (toAnalyse[i].getFileName() + (error.isNotEmpty() ? " (" + error + ")" : juce::String (" (silenzio)")));
        }
    }

    if (! threadShouldExit())
        state.store (State::done);
}

//==============================================================================
ReferenceTrack::ReferenceTrack() : juce::Thread ("MasterAgent reference") {}

ReferenceTrack::~ReferenceTrack()
{
    stopThread (4000);
}

void ReferenceTrack::load (const juce::File& newFile, double hostSampleRate)
{
    const std::lock_guard<std::mutex> control (controlMutex);
    stopThread (4000);

    {
        std::lock_guard<std::mutex> lock (dataMutex);
        file = newFile;
        error.clear();
        pendingHostRate = hostSampleRate;
        resampleOnly = false;
    }

    progress.store (0.0f);
    state.store (State::loading);
    startThread (juce::Thread::Priority::background);
}

void ReferenceTrack::clear()
{
    const std::lock_guard<std::mutex> control (controlMutex);
    stopThread (4000);

    {
        std::lock_guard<std::mutex> lock (dataMutex);
        file = juce::File();
        error.clear();
        snapshot = {};
        resampleOnly = false;
    }
    {
        const juce::SpinLock::ScopedLockType lock (playbackLock);
        playback.setSize (0, 0);
        playbackPos = 0;
        playbackRate = 0.0;
    }
    loudestStart.store (0.0);
    state.store (State::empty);
}

juce::String ReferenceTrack::getError() const
{
    std::lock_guard<std::mutex> lock (dataMutex);
    return error;
}

juce::File ReferenceTrack::getFile() const
{
    std::lock_guard<std::mutex> lock (dataMutex);
    return file;
}

bool ReferenceTrack::getSnapshot (AnalysisSnapshot& dest) const
{
    if (state.load() != State::ready)
        return false;

    std::lock_guard<std::mutex> lock (dataMutex);
    dest = snapshot;
    return true;
}

void ReferenceTrack::setHostSampleRate (double newRate)
{
    const std::lock_guard<std::mutex> control (controlMutex);
    {
        std::lock_guard<std::mutex> lock (dataMutex);
        pendingHostRate = newRate;
    }

    // un caricamento in corso legge il nuovo valore da solo; a reference pronto si rilegge il file in background
    double currentRate;
    {
        const juce::SpinLock::ScopedLockType lock (playbackLock);
        currentRate = playbackRate;
    }
    if (state.load() == State::ready && std::abs (currentRate - newRate) > 1e-6 && ! isThreadRunning())
    {
        {
            std::lock_guard<std::mutex> lock (dataMutex);
            resampleOnly = true;
        }
        startThread (juce::Thread::Priority::background);
    }
}

juce::AudioBuffer<float> ReferenceTrack::makePlaybackBuffer (const juce::AudioBuffer<float>& source, double sourceRate, double hostRate)
{
    juce::AudioBuffer<float> resampled;
    const double ratio = sourceRate / hostRate;

    if (std::abs (ratio - 1.0) < 1e-9)
    {
        resampled.makeCopyOf (source);
    }
    else
    {
        const int outSamples = (int) std::floor (source.getNumSamples() / ratio);
        resampled.setSize (2, outSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::WindowedSincInterpolator interpolator;
            interpolator.process (ratio, source.getReadPointer (std::min (ch, source.getNumChannels() - 1)),
                                  resampled.getWritePointer (ch), outSamples);
        }
    }

    if (resampled.getNumChannels() == 1)
    {
        resampled.setSize (2, resampled.getNumSamples(), true);
        resampled.copyFrom (1, 0, resampled, 0, 0, resampled.getNumSamples());
    }
    return resampled;
}

double ReferenceTrack::findLoudestStart (const AnalysisSnapshot& s)
{
    const auto& history = s.shortTermHistory;
    if (history.empty())
        return 0.0;

    // la prima volta che lo short-term arriva vicino al massimo: l'inizio del primo ritornello/drop pieno
    const float peak = *std::max_element (history.begin(), history.end());
    for (size_t k = 0; k < history.size(); ++k)
        if (history[k] >= peak - 1.0f)
            return std::max (0.0, s.historyStartSongSec + 0.1 * (double) k - 3.0);   // la finestra short-term dura 3 s
    return 0.0;
}

void ReferenceTrack::run()
{
    juce::File toLoad;
    bool onlyResample;
    {
        std::lock_guard<std::mutex> lock (dataMutex);
        toLoad = file;
        onlyResample = resampleOnly;
        resampleOnly = false;
    }

    auto fail = [this] (const juce::String& message)
    {
        std::lock_guard<std::mutex> lock (dataMutex);
        error = message;
        state.store (State::failed);
    };

    juce::AudioBuffer<float> audio;
    double fileRate = 0.0;
    juce::String readError;
    if (! readAudioFile (toLoad, audio, fileRate, readError, maxDurationSeconds))
    {
        if (! onlyResample)
            fail (readError);
        return;   // nuovo sample rate ma file sparito: resta il buffer di prima
    }

    AnalysisSnapshot result;
    if (! onlyResample)
    {
        progress.store (0.05f);
        const bool ok = analyseBuffer (audio, fileRate, result,
                                       [this] (float p) { progress.store (0.05f + 0.85f * p); },
                                       [this] { return threadShouldExit(); });
        if (! ok)
        {
            if (! threadShouldExit())
                fail ("Analisi non riuscita");
            return;
        }
    }

    // ricampiona al sample rate dell'host; se nel frattempo è cambiato, si rifà con quello nuovo
    for (;;)
    {
        double hostRate;
        {
            std::lock_guard<std::mutex> lock (dataMutex);
            hostRate = pendingHostRate;
        }
        auto resampled = makePlaybackBuffer (audio, fileRate, hostRate);
        if (threadShouldExit())
            return;

        {
            const juce::SpinLock::ScopedLockType lock (playbackLock);
            std::swap (playback, resampled);
            playbackRate = hostRate;
            playbackPos = onlyResample ? std::min (playbackPos, std::max (0, playback.getNumSamples() - 1)) : 0;
        }

        std::lock_guard<std::mutex> lock (dataMutex);
        if (std::abs (pendingHostRate - hostRate) < 1e-6)
            break;
    }

    if (! onlyResample)
    {
        loudestStart.store (findLoudestStart (result));
        std::lock_guard<std::mutex> lock (dataMutex);
        snapshot = std::move (result);
    }
    progress.store (1.0f);
    state.store (State::ready);
}

bool ReferenceTrack::renderPlayback (float* left, float* right, int numSamples, float gain) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (playbackLock);
    if (! lock.isLocked() || playback.getNumSamples() == 0)
        return false;

    const int length = playback.getNumSamples();
    const float* srcL = playback.getReadPointer (0);
    const float* srcR = playback.getReadPointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        if (right != nullptr)
        {
            left[i] = srcL[playbackPos] * gain;
            right[i] = srcR[playbackPos] * gain;
        }
        else
        {
            left[i] = 0.5f * (srcL[playbackPos] + srcR[playbackPos]) * gain;
        }
        if (++playbackPos >= length)
            playbackPos = 0;
    }
    return true;
}

void ReferenceTrack::seekPlayback (double seconds) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (playbackLock);
    if (! lock.isLocked() || playback.getNumSamples() == 0 || playbackRate <= 0.0)
        return;

    const auto length = (juce::int64) playback.getNumSamples();
    const auto target = (juce::int64) std::llround (std::max (0.0, seconds) * playbackRate);
    playbackPos = (int) (target % length);
}

} // namespace ma
