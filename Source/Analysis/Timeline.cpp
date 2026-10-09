#include "Timeline.h"
#include "../Common/Text.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ma
{

namespace
{
    // un buco è un taglio netto: nei 10 ms prima dello zero c'è segnale sopra -40 dBFS.
    // La coda di una dissolvenza è molto più bassa, quindi non viene segnalata.
    constexpr float kAudibleLevel = 0.01f;
    constexpr double kAudibleWindowSeconds = 0.01;
    constexpr double kMinDropoutSeconds = 0.01;      // buchi più brevi di 10 ms: non segnalati
    constexpr double kMinCorrelationMeanSquare = 1.0e-5;   // -50 dBFS: sotto, la correlazione non è significativa
    constexpr float kPsrMinShortTerm = -40.0f;       // stesso gate del PSR minimo dell'AnalysisEngine
}

//==============================================================================
void TimelineRecorder::start (TrackTimeline& timeline, double sampleRate, int numChannels, long long expectedLength)
{
    target = &timeline;
    timeline = {};
    timeline.sampleRate = sampleRate;
    timeline.numChannels = std::clamp (numChannels, 1, 2);

    // almeno 10 ms per punto; i brani lunghi si fermano a maxWavePoints punti
    const auto minPerPoint = std::max<long long> (1, (long long) std::lround (sampleRate * 0.01));
    const auto forLength = expectedLength > 0 ? (expectedLength + maxWavePoints - 1) / maxWavePoints : 1;
    timeline.samplesPerPoint = (int) std::max (minPerPoint, forLength);
    if (expectedLength > 0)
        timeline.wave.reserve ((size_t) (expectedLength / timeline.samplesPerPoint + 1));

    position = 0;
    point = {};
    pointSumSq = 0.0;
    pointCount = 0;

    blockLength = std::max (1, (int) std::lround (sampleRate * TrackTimeline::blockSeconds));
    blockCount = 0;
    blockSumSq = blockLL = blockRR = blockLR = 0.0;
    recentLL.fill (0.0);
    recentRR.fill (0.0);
    recentLR.fill (0.0);
    recentIndex = 0;

    zeroStart = { -1, -1 };
    lastAudible = { -1, -1 };
    zeroAfterAudible = { false, false };
    channelDropouts.clear();
}

void TimelineRecorder::process (const float* left, const float* right, int numSamples)
{
    if (target == nullptr)
        return;

    const bool stereo = target->numChannels > 1 && right != nullptr;
    const int channels = stereo ? 2 : 1;
    const auto audibleWindow = (long long) (target->sampleRate * kAudibleWindowSeconds);

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = stereo ? right[i] : l;

        // forma d'onda
        const float lo = std::min (l, r), hi = std::max (l, r);
        if (pointCount == 0)
        {
            point.min = lo;
            point.max = hi;
        }
        else
        {
            point.min = std::min (point.min, lo);
            point.max = std::max (point.max, hi);
        }
        const double meanSquare = 0.5 * ((double) l * l + (double) r * r);
        pointSumSq += meanSquare;
        if (++pointCount >= target->samplesPerPoint)
        {
            point.rms = (float) std::sqrt (pointSumSq / pointCount);
            target->wave.push_back (point);
            pointSumSq = 0.0;
            pointCount = 0;
        }

        // blocchi da 100 ms: livello e correlazione
        blockSumSq += meanSquare;
        blockLL += (double) l * l;
        blockRR += (double) r * r;
        blockLR += (double) l * r;
        if (++blockCount >= blockLength)
            finishBlock();

        // buchi di silenzio digitale, canale per canale
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = (size_t) ch;
            const float x = ch == 0 ? l : r;
            if (x == 0.0f)
            {
                if (zeroStart[c] < 0)
                {
                    zeroStart[c] = position;
                    zeroAfterAudible[c] = lastAudible[c] >= 0 && position - lastAudible[c] <= audibleWindow;
                }
            }
            else
            {
                if (zeroStart[c] >= 0)
                    closeZeroRun (ch);
                if (std::abs (x) > kAudibleLevel)
                    lastAudible[c] = position;
            }
        }

        ++position;
    }
}

void TimelineRecorder::closeZeroRun (int ch)
{
    const auto c = (size_t) ch;
    const auto minLength = (long long) (target->sampleRate * kMinDropoutSeconds);
    if (zeroAfterAudible[c] && position - zeroStart[c] >= minLength && channelDropouts.size() < maxEvents)
        channelDropouts.push_back ({ zeroStart[c], position, ch });
    zeroStart[c] = -1;
}

void TimelineRecorder::finishBlock()
{
    if (blockCount == 0)
        return;

    target->levelDb.push_back (gainToDb (std::sqrt (blockSumSq / blockCount)));

    recentLL[(size_t) recentIndex] = blockLL;
    recentRR[(size_t) recentIndex] = blockRR;
    recentLR[(size_t) recentIndex] = blockLR;
    recentIndex = (recentIndex + 1) % (int) recentLL.size();

    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (size_t k = 0; k < recentLL.size(); ++k)
    {
        ll += recentLL[k];
        rr += recentRR[k];
        lr += recentLR[k];
    }
    const double minEnergy = kMinCorrelationMeanSquare * blockLength * (double) recentLL.size();
    const bool significant = target->numChannels > 1 && ll > minEnergy && rr > minEnergy;
    target->correlation.push_back (significant ? (float) (lr / std::sqrt (ll * rr)) : std::numeric_limits<float>::quiet_NaN());

    blockCount = 0;
    blockSumSq = blockLL = blockRR = blockLR = 0.0;
}

void TimelineRecorder::finish()
{
    if (target == nullptr)
        return;

    if (pointCount > 0)
    {
        point.rms = (float) std::sqrt (pointSumSq / pointCount);
        target->wave.push_back (point);
        pointCount = 0;
    }
    finishBlock();
    target->lengthSamples = position;

    // i silenzi che arrivano alla fine del file non sono buchi: restano aperti e non vengono registrati.
    // Un buco su entrambi i canali nello stesso punto diventa un solo evento (canale -1).
    auto runs = std::move (channelDropouts);
    std::sort (runs.begin(), runs.end(), [] (const SampleRange& a, const SampleRange& b) { return a.start < b.start; });

    std::vector<SampleRange> merged;
    std::vector<bool> used (runs.size(), false);
    for (size_t i = 0; i < runs.size(); ++i)
    {
        if (used[i])
            continue;
        auto range = runs[i];
        if (target->numChannels < 2)
            range.channel = -1;

        for (size_t j = i + 1; j < runs.size() && runs[j].start < range.end; ++j)
        {
            if (used[j] || runs[j].channel == runs[i].channel)
                continue;
            const auto overlap = std::min (range.end, runs[j].end) - std::max (range.start, runs[j].start);
            const auto shorter = std::min (range.end - range.start, runs[j].end - runs[j].start);
            if (overlap * 2 >= shorter)
            {
                range.start = std::min (range.start, runs[j].start);
                range.end = std::max (range.end, runs[j].end);
                range.channel = -1;
                used[j] = true;
                break;
            }
        }
        merged.push_back (range);
    }
    target->dropouts = std::move (merged);
    target = nullptr;
}

//==============================================================================
bool Marker::isCritical() const noexcept
{
    switch (type)
    {
        case MarkerType::truePeak:   return value > 0.0f;
        case MarkerType::compressed: return false;
        case MarkerType::clip:
        case MarkerType::dropout:
        case MarkerType::phase:      return true;
    }
    return true;
}

juce::String Marker::id() const
{
    return juce::String ((int) type) + "|" + juce::String (start, 2);
}

namespace
{
    /** Serie di indici consecutivi marcati, unendo le interruzioni lunghe al massimo maxGap. */
    template <typename Flag, typename Emit>
    void forEachRun (size_t count, int maxGap, Flag&& flagged, Emit&& emit)
    {
        long first = -1, last = -1;
        for (size_t i = 0; i < count; ++i)
        {
            if (! flagged (i))
                continue;
            if (first >= 0 && (long) i - last > maxGap + 1)
            {
                emit ((size_t) first, (size_t) last);
                first = -1;
            }
            if (first < 0)
                first = (long) i;
            last = (long) i;
        }
        if (first >= 0)
            emit ((size_t) first, (size_t) last);
    }
}

std::vector<Marker> findMarkers (const TrackTimeline& t, const MarkerSettings& settings)
{
    std::vector<Marker> markers;
    if (t.sampleRate <= 0.0)
        return markers;

    const double sr = t.sampleRate;
    const double duration = t.durationSeconds();
    const double block = TrackTimeline::blockSeconds;
    auto clampTime = [duration] (double s) { return std::clamp (s, 0.0, duration > 0.0 ? duration : s); };

    // clipping: gli eventi a meno di 50 ms l'uno dall'altro (anche su canali diversi) sono un solo segno
    {
        auto clips = t.clips;
        std::sort (clips.begin(), clips.end(), [] (const SampleRange& a, const SampleRange& b) { return a.start < b.start; });
        const auto gap = (long long) (sr * 0.05);
        for (size_t i = 0; i < clips.size();)
        {
            Marker m;
            m.type = MarkerType::clip;
            auto end = std::max (clips[i].end, clips[i].start + 1);
            int channel = clips[i].channel, count = 0;
            const auto start = clips[i].start;
            for (; i < clips.size() && clips[i].start <= end + gap; ++i)
            {
                end = std::max (end, std::max (clips[i].end, clips[i].start + 1));
                if (clips[i].channel != channel)
                    channel = -1;
                ++count;
            }
            m.start = (double) start / sr;
            m.end = (double) end / sr;
            m.value = (float) count;
            m.channel = channel;
            markers.push_back (m);
        }
    }

    // true peak oltre il ceiling (blocchi da 100 ms, interruzioni fino a 200 ms unite)
    forEachRun (t.truePeakDb.size(), 2, [&] (size_t i) { return t.truePeakDb[i] > settings.ceilingDb; },
                [&] (size_t first, size_t last)
                {
                    Marker m;
                    m.type = MarkerType::truePeak;
                    m.start = clampTime ((double) first * block);
                    m.end = clampTime ((double) (last + 1) * block);
                    m.value = *std::max_element (t.truePeakDb.begin() + (long) first, t.truePeakDb.begin() + (long) last + 1);
                    markers.push_back (m);
                });

    // buchi di silenzio digitale
    for (const auto& d : t.dropouts)
    {
        Marker m;
        m.type = MarkerType::dropout;
        m.start = (double) d.start / sr;
        m.end = (double) d.end / sr;
        m.value = (float) ((m.end - m.start) * 1000.0);
        m.channel = d.channel;
        markers.push_back (m);
    }

    // sezioni troppo compresse: PSR (true peak degli ultimi 3 s - short-term) sotto il minimo del profilo.
    // Stesso calcolo del PSR minimo del report, quindi i segni compaiono solo se il report lo segnala.
    if (settings.minPsr > 0.0f && ! t.shortTermLufs.empty() && ! t.truePeakDb.empty())
    {
        constexpr size_t window = 30;
        std::vector<float> psr (t.shortTermLufs.size(), std::numeric_limits<float>::quiet_NaN());
        for (size_t k = 0; k < psr.size(); ++k)
        {
            const float st = t.shortTermLufs[k];
            if (st <= kPsrMinShortTerm || k >= t.truePeakDb.size())
                continue;
            const size_t last = std::min (k + window, t.truePeakDb.size());
            const float peak = *std::max_element (t.truePeakDb.begin() + (long) k, t.truePeakDb.begin() + (long) last);
            psr[k] = peak - st;
        }

        forEachRun (psr.size(), 10, [&] (size_t k) { return ! std::isnan (psr[k]) && psr[k] < settings.minPsr; },
                    [&] (size_t first, size_t last)
                    {
                        Marker m;
                        m.type = MarkerType::compressed;
                        m.start = clampTime ((double) first * block);                 // inizio della finestra di 3 s
                        m.end = clampTime ((double) (last + window) * block);
                        float lowest = std::numeric_limits<float>::max();
                        for (size_t k = first; k <= last; ++k)
                            if (! std::isnan (psr[k]))
                                lowest = std::min (lowest, psr[k]);
                        m.value = lowest;
                        markers.push_back (m);
                    });
    }

    // fase invertita: correlazione negativa per almeno un secondo
    forEachRun (t.correlation.size(), 5, [&] (size_t i) { return ! std::isnan (t.correlation[i]) && t.correlation[i] < 0.0f; },
                [&] (size_t first, size_t last)
                {
                    if (last - first + 1 < 10)
                        return;
                    Marker m;
                    m.type = MarkerType::phase;
                    m.start = clampTime ((double) std::max<long> (0, (long) first - 3) * block);   // la correlazione copre gli ultimi 400 ms
                    m.end = clampTime ((double) (last + 1) * block);
                    float lowest = 1.0f;
                    for (size_t i = first; i <= last; ++i)
                        if (! std::isnan (t.correlation[i]))
                            lowest = std::min (lowest, t.correlation[i]);
                    m.value = lowest;
                    markers.push_back (m);
                });

    std::stable_sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.start < b.start; });
    return markers;
}

//==============================================================================
juce::String formatTimelineTime (double seconds, bool tenths)
{
    seconds = std::max (0.0, seconds);
    if (! tenths)
    {
        const int s = (int) std::floor (seconds);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
    const int t = (int) std::floor (seconds * 10.0);
    return juce::String (t / 600) + ":" + juce::String ((t / 10) % 60).paddedLeft ('0', 2) + "." + juce::String (t % 10);
}

juce::String markerTypeName (MarkerType type)
{
    switch (type)
    {
        case MarkerType::clip:       return "Clipping"_t;
        case MarkerType::truePeak:   return "True peak oltre il ceiling"_t;
        case MarkerType::dropout:    return "Buco di silenzio"_t;
        case MarkerType::compressed: return "Sezione troppo compressa"_t;
        case MarkerType::phase:      return "Fase invertita"_t;
    }
    return {};
}

juce::String describeMarker (const Marker& m)
{
    const juce::String channel = m.channel == 0 ? " (L)" : (m.channel == 1 ? " (R)" : juce::String());
    juce::String text = markerTypeName (m.type) + channel + "\n";

    if (m.isZone() || m.end - m.start >= 0.2)
        text << formatTimelineTime (m.start) << " - " << formatTimelineTime (m.end);
    else
        text << formatTimelineTime (m.start);

    switch (m.type)
    {
        case MarkerType::clip:
            text << "   " << (m.value > 1.0f ? "%d eventi ravvicinati"_t.replace ("%d", juce::String ((int) m.value)) : "1 evento"_t)
                 << "\n" << "Plateau di campioni identici (onda squadrata). Se non è un clipper voluto, riduci il guadagno a monte."_t;
            break;
        case MarkerType::truePeak:
            text << "   " << juce::String (m.value, 2) << " dBTP"
                 << "\n" << (m.value > 0.0f ? "Oltre 0 dBTP: distorsione in conversione D/A e nella codifica lossy."_t
                                            : "Sopra il ceiling del target: abbassa il ceiling del limiter (modalità true peak)."_t);
            break;
        case MarkerType::dropout:
            text << "   " << juce::String (juce::roundToInt (m.value)) << " ms"
                 << "\n" << "Campioni a zero subito dopo audio udibile: probabile taglio di editing, buffer perso o regione mancante."_t;
            break;
        case MarkerType::compressed:
            text << "   " << "PSR minimo"_t << " " << juce::String (m.value, 1) << " dB"
                 << "\n" << "I picchi sporgono poco dal livello medio: limiter o clipper stanno schiacciando i transienti in questo tratto."_t;
            break;
        case MarkerType::phase:
            text << "   " << "correlazione"_t << " " << juce::String (m.value, 2)
                 << "\n" << "In mono questo tratto perde gran parte del segnale: controlla polarità, widener e riverberi."_t;
            break;
    }
    return text;
}

} // namespace ma
