#include "AnalysisEngine.h"

#include <algorithm>
#include <cmath>

namespace ma
{

//==============================================================================
void AnalysisEngine::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);

    loudness.prepare (sampleRate, numChannels);
    truePeak.prepare (sampleRate, numChannels);
    dynamics.prepare (sampleRate, numChannels);
    spectrum.prepare (sampleRate, numChannels);
    technical.prepare (sampleRate, numChannels);

    reset();
}

void AnalysisEngine::reset()
{
    loudness.reset();
    truePeak.reset();
    dynamics.reset();
    spectrum.reset();
    technical.reset();
    samplesProcessed = 0.0;
    songTimeStart = 0.0;
    nextSongTime = 0.0;
    songTimeContiguous = true;
    lastShortTermCount = 0;
    minPsr = 0.0f;
    minPsrValid = false;
}

void AnalysisEngine::process (const float* left, const float* right, int numSamples, double songTime)
{
    if (numChannels < 2)
        right = nullptr;

    if (! std::isnan (songTime))
    {
        if (samplesProcessed == 0.0)
            songTimeStart = songTime;
        else if (std::abs (songTime - nextSongTime) > 0.05)
            songTimeContiguous = false;
        nextSongTime = songTime;
    }
    const double chunkTime = nextSongTime;

    loudness.process (left, right, numSamples);
    truePeak.process (left, right, numSamples);
    dynamics.process (left, right, numSamples);
    spectrum.process (left, right, numSamples, loudness.getShortTerm(), chunkTime);
    technical.process (left, right, numSamples);
    samplesProcessed += numSamples;
    nextSongTime = chunkTime + numSamples / sampleRate;

    // PSR minimo: valutato a ogni nuovo valore short-term (ogni 100 ms)
    const auto stCount = loudness.getShortTermHistory().size();
    if (stCount != lastShortTermCount)
    {
        lastShortTermCount = stCount;
        const float st = loudness.getShortTerm();
        if (st > -40.0f)
        {
            const float psr = truePeak.getRecentTruePeakDb() - st;
            if (! minPsrValid || psr < minPsr)
            {
                minPsr = psr;
                minPsrValid = true;
            }
        }
    }
}

void AnalysisEngine::buildSnapshot (AnalysisSnapshot& s) const
{
    s.valid = samplesProcessed > 0.0;
    s.sampleRate = sampleRate;
    s.numChannels = numChannels;
    s.secondsAnalyzed = samplesProcessed / sampleRate;

    // loudness
    s.momentaryLufs = loudness.getMomentary();
    s.shortTermLufs = loudness.getShortTerm();
    s.integratedLufs = loudness.computeIntegrated();
    s.loudnessRange = loudness.computeLoudnessRange();
    s.maxMomentaryLufs = loudness.getMaxMomentary();
    s.maxShortTermLufs = loudness.getMaxShortTerm();
    s.rmsDb = dynamics.getRmsDb();

    const auto& history = loudness.getShortTermHistory();
    constexpr size_t maxHistory = 6000;   // 10 minuti
    const size_t first = history.size() > maxHistory ? history.size() - maxHistory : 0;
    s.shortTermHistory.assign (history.begin() + (long) first, history.end());
    s.historyStartSongSec = songTimeStart + (double) (first + 30) * 0.1;   // il primo short-term arriva dopo 3 s
    s.historyAligned = songTimeContiguous;
    loudness.fillHistogram (s.stHistogram);

    // peak
    for (int ch = 0; ch < 2; ++ch)
    {
        s.samplePeakDb[(size_t) ch] = truePeak.getSamplePeakDb (ch);
        s.truePeakDb[(size_t) ch] = truePeak.getTruePeakDb (ch);
    }
    s.truePeakMaxDb = truePeak.getTruePeakMaxDb();
    s.recentTruePeakDb = truePeak.getRecentTruePeakDb();
    s.overs1dB = truePeak.getOvers1dB();
    s.overs0dB = truePeak.getOvers0dB();

    // dinamica
    s.crestDb = s.rmsDb > kSilenceDb ? dynamics.getSamplePeakDb() - s.rmsDb : 0.0f;
    s.plr = s.integratedLufs > kSilenceDb ? s.truePeakMaxDb - s.integratedLufs : 0.0f;
    s.psr = s.shortTermLufs > -70.0f ? s.recentTruePeakDb - s.shortTermLufs : 0.0f;
    s.minPsr = minPsrValid ? minPsr : 0.0f;
    s.drValid = dynamics.computeDr (s.drValue);
    s.bandCrestDb = dynamics.computeBandCrest();

    // spettro, stereo, tecnici
    spectrum.fillSnapshot (s);
    technical.fillSnapshot (s);
}

//==============================================================================
LiveAnalysis::LiveAnalysis() : juce::Thread ("MasterAgent analysis") {}

LiveAnalysis::~LiveAnalysis()
{
    release();
}

void LiveAnalysis::prepare (double newSampleRate, int numChannels)
{
    release();

    sampleRate = newSampleRate;
    channels = std::clamp (numChannels, 1, 2);
    const int capacity = juce::nextPowerOfTwo ((int) (sampleRate * 2.0));   // 2 s di margine
    fifo.setTotalSize (capacity);
    fifo.reset();
    fifoL.assign ((size_t) capacity, 0.0f);
    fifoR.assign ((size_t) capacity, 0.0f);
    workL.assign (4096, 0.0f);
    workR.assign (4096, 0.0f);

    for (auto& e : engines)
        e.prepare (sampleRate, channels);
    historyMeter.prepare (sampleRate, channels);
    resetAll();

    markerFifo.reset();
    samplesPushed = 0;
    expectedHostTime = -1.0e9;
    samplesRead = 0;
    nextMarker.reset();
    currentSongTime = std::numeric_limits<double>::quiet_NaN();

    resetRequested.store (false);
    overflowed.store (false);

    {
        std::lock_guard<std::mutex> lock (snapshotMutex);
        published = {};
    }

    startThread (juce::Thread::Priority::normal);
}

void LiveAnalysis::release()
{
    if (isThreadRunning())
        stopThread (2000);
}

void LiveAnalysis::setLiveMode (bool shouldBeLive, double windowSeconds) noexcept
{
    liveModeRequested.store (shouldBeLive);
    liveWindowRequested.store (std::clamp (windowSeconds, 6.0, 120.0));
}

void LiveAnalysis::push (const float* left, const float* right, int numSamples, bool canBlock, double hostTimeSeconds) noexcept
{
    if (fifoL.empty())
        return;

    if (fifo.getFreeSpace() < numSamples)
    {
        // Render offline (es. Export Mixdown): nessun vincolo real-time, quindi si attende che
        // l'analisi recuperi invece di perdere audio. Timeout di sicurezza: 5 s.
        if (canBlock && isThreadRunning())
        {
            for (int waited = 0; fifo.getFreeSpace() < numSamples && waited < 5000; ++waited)
            {
                notify();
                juce::Thread::sleep (1);
            }
        }

        if (fifo.getFreeSpace() < numSamples)
        {
            overflowed.store (true);
            return;
        }
    }

    // marker di tempo solo alle discontinuità del transport (avvio, seek, loop, stop); se la coda è piena
    // si riprova al blocco successivo. Tolleranza 10 ms: alcuni host riportano la posizione in modo approssimato.
    {
        constexpr double unknown = -1.0e9;
        const bool known = ! std::isnan (hostTimeSeconds);
        const bool discontinuity = known ? std::abs (hostTimeSeconds - expectedHostTime) > 0.01 : expectedHostTime != unknown;

        bool recorded = true;
        if (discontinuity)
        {
            if (markerFifo.getFreeSpace() > 0)
            {
                const auto markerScope = markerFifo.write (1);
                markers[(size_t) (markerScope.blockSize1 > 0 ? markerScope.startIndex1 : markerScope.startIndex2)] = { samplesPushed, hostTimeSeconds };
            }
            else
            {
                recorded = false;
            }
        }
        if (recorded)
            expectedHostTime = known ? hostTimeSeconds + numSamples / sampleRate : unknown;
    }
    samplesPushed += numSamples;

    const auto scope = fifo.write (numSamples);

    auto copy = [&] (int start, int size, int offset)
    {
        if (size <= 0) return;
        std::copy (left + offset, left + offset + size, fifoL.begin() + start);
        if (right != nullptr)
            std::copy (right + offset, right + offset + size, fifoR.begin() + start);
        else
            std::copy (left + offset, left + offset + size, fifoR.begin() + start);
    };

    copy (scope.startIndex1, scope.blockSize1, 0);
    copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
}

void LiveAnalysis::resetAll()
{
    for (auto& e : engines)
        e.reset();
    engineSamples = { 0.0, 0.0 };
    secondEngineRunning = false;
    historyMeter.reset();
    historySongStart = 0.0;
    historyNextTime = 0.0;
    historySamples = 0.0;
    historyContiguous = true;
}

void LiveAnalysis::consumeTimeMarkers()
{
    for (;;)
    {
        if (! nextMarker.has_value())
        {
            if (markerFifo.getNumReady() == 0)
                return;
            const auto scope = markerFifo.read (1);
            nextMarker = markers[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        }

        if (nextMarker->sampleIndex > samplesRead)
            return;

        currentSongTime = nextMarker->songTime + (double) (samplesRead - nextMarker->sampleIndex) / sampleRate;
        nextMarker.reset();
    }
}

void LiveAnalysis::processChunk (const float* left, const float* right, int numSamples, double songTime)
{
    // senza transport il tempo è quello trascorso dall'ultimo reset, uguale per tutti i motori
    if (std::isnan (songTime))
        songTime = historyNextTime;

    if (historySamples == 0.0)
        historySongStart = songTime;
    else if (std::abs (songTime - historyNextTime) > 0.05)
        historyContiguous = false;
    historyNextTime = songTime + numSamples / sampleRate;
    historySamples += numSamples;

    historyMeter.process (left, right, numSamples);
    engines[0].process (left, right, numSamples, songTime);
    engineSamples[0] += numSamples;

    if (! liveMode)
        return;

    // Due motori sfalsati di mezza finestra, ciascuno azzerato ogni "finestra" secondi:
    // quello pubblicato contiene sempre tra W/2 e W secondi di audio recente.
    const double window = liveWindowSeconds * sampleRate;

    if (secondEngineRunning)
    {
        engines[1].process (left, right, numSamples, songTime);
        engineSamples[1] += numSamples;
    }
    else if (engineSamples[0] >= window * 0.5)
    {
        secondEngineRunning = true;
    }

    for (size_t i = 0; i < 2; ++i)
    {
        if (engineSamples[i] >= window)
        {
            engines[i].reset();
            engineSamples[i] = 0.0;
        }
    }
}

void LiveAnalysis::publish()
{
    size_t source = 0;
    if (liveMode && secondEngineRunning && engineSamples[1] > engineSamples[0])
        source = 1;

    engines[source].buildSnapshot (scratch);

    // momentary, short-term e cronologia vengono sempre dal meter continuo
    scratch.momentaryLufs = historyMeter.getMomentary();
    scratch.shortTermLufs = historyMeter.getShortTerm();
    const auto& history = historyMeter.getShortTermHistory();
    constexpr size_t maxHistory = 6000;   // 10 minuti
    const size_t first = history.size() > maxHistory ? history.size() - maxHistory : 0;
    scratch.shortTermHistory.assign (history.begin() + (long) first, history.end());
    scratch.historyStartSongSec = historySongStart + (double) (first + 30) * 0.1;
    scratch.historyAligned = historyContiguous;
    scratch.psr = scratch.shortTermLufs > -70.0f ? scratch.recentTruePeakDb - scratch.shortTermLufs : 0.0f;

    scratch.liveMode = liveMode;
    scratch.liveWindowSeconds = liveWindowSeconds;

    std::lock_guard<std::mutex> lock (snapshotMutex);
    std::swap (published, scratch);
}

void LiveAnalysis::run()
{
    auto lastPublish = juce::Time::getMillisecondCounterHiRes();
    auto lastData = lastPublish;
    bool unpublishedWork = false;   // dati analizzati non ancora visibili alla GUI

    while (! threadShouldExit())
    {
        const bool wantLive = liveModeRequested.load();
        const double wantWindow = liveWindowRequested.load();
        const bool modeChanged = wantLive != liveMode || (wantLive && wantWindow != liveWindowSeconds);

        if (resetRequested.exchange (false) || modeChanged)
        {
            if (! modeChanged)
            {
                const int discarded = fifo.getNumReady();
                fifo.read (discarded);   // reset utente: scarta l'audio in coda
                samplesRead += discarded;
                consumeTimeMarkers();
            }

            liveMode = wantLive;
            liveWindowSeconds = wantWindow;
            resetAll();
            overflowed.store (false);

            std::lock_guard<std::mutex> lock (snapshotMutex);
            published = {};
        }

        while (fifo.getNumReady() > 0 && ! threadShouldExit())
        {
            consumeTimeMarkers();
            int toRead = std::min (fifo.getNumReady(), (int) workL.size());
            if (nextMarker.has_value() && nextMarker->sampleIndex > samplesRead)
                toRead = (int) std::min<long long> (toRead, nextMarker->sampleIndex - samplesRead);
            {
                const auto scope = fifo.read (toRead);
                int pos = 0;
                for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 },
                                            std::pair { scope.startIndex2, scope.blockSize2 } })
                {
                    if (size <= 0) continue;
                    std::copy (fifoL.begin() + start, fifoL.begin() + start + size, workL.begin() + pos);
                    std::copy (fifoR.begin() + start, fifoR.begin() + start + size, workR.begin() + pos);
                    pos += size;
                }
            }

            processChunk (workL.data(), channels > 1 ? workR.data() : nullptr, toRead, currentSongTime);
            samplesRead += toRead;
            if (! std::isnan (currentSongTime))
                currentSongTime += toRead / sampleRate;
            unpublishedWork = true;
            lastData = juce::Time::getMillisecondCounterHiRes();

            // durante un render offline lungo il FIFO non si svuota mai: pubblica comunque a 10 Hz
            if (juce::Time::getMillisecondCounterHiRes() - lastPublish >= 100.0)
                break;
        }

        // pubblica a 10 Hz, e l'ultimo stato quando l'audio smette di arrivare (stop del transport)
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const bool idle = fifo.getNumReady() == 0 && now - lastData >= 50.0;
        if (unpublishedWork && (now - lastPublish >= 100.0 || idle))
        {
            lastPublish = now;
            unpublishedWork = false;
            publish();
        }

        if (fifo.getNumReady() == 0)
            wait (10);
    }
}

void LiveAnalysis::getSnapshot (AnalysisSnapshot& dest) const
{
    std::lock_guard<std::mutex> lock (snapshotMutex);
    dest = published;
}

} // namespace ma
