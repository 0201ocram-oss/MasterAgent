#include "TechnicalChecks.h"

#include <algorithm>
#include <cmath>

namespace ma
{

namespace
{
    constexpr float kFlatTopLevel = 0.5f;        // -6 dBFS: plateau di campioni identici sopra questo livello = clip
    constexpr float kFullScale = 0.99995f;
    constexpr float kNearFullScale = 0.99426f;   // -0.05 dBFS: un plateau qui è un sovraccarico, più in basso è il ceiling di un clipper
    constexpr double kLongClipSeconds = 0.00018; // ~8 campioni a 44.1 kHz: un plateau così lungo tosa in modo udibile
    constexpr int kFlatTopRun = 3;
    constexpr float kNaturalEntrySteps = 6.0f;   // un picco arrotondato arriva sul plateau con passi di 1-4 quanti
}

void TechnicalChecks::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);
    blockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
    longClipRun = std::max (4, (int) std::lround (sampleRate * kLongClipSeconds));

    // x = A cos(w n) vicino al picco scende di A w^2 n^2 / 2: con A = 0.5 e 20 Hz è la discesa più lenta che consideriamo
    const double w = 2.0 * 3.14159265358979323846 * 20.0 / sampleRate;
    minCurvature = 0.5 * w * w / 2.0;
    reset();
}

void TechnicalChecks::reset()
{
    sum = { 0.0, 0.0 };
    sumSq = { 0.0, 0.0 };
    count = 0.0;
    lastSample = { 0.0f, 0.0f };
    prevSample = { 0.0f, 0.0f };
    plateauEntry = { 0.0f, 0.0f };
    quantStep = 1.0f / 32768.0f;   // finché il materiale non mostra passi più fini lo trattiamo come 16 bit
    flatRun = { 0, 0 };
    fullScaleRun = { 0, 0 };
    inClip = { false, false };
    openClip = { -1, -1 };
    clipLevel = { 0.0f, 0.0f };
    clipLength = { 0, 0 };
    samplePos = 0;
    clipEvents = 0;
    clipCounts = {};
    blockCounter = 0;
    blockSumSq = 0.0;
    minBlockRms = 0.0;
    minBlockValid = false;
    digitalSilence = false;
}

int TechnicalChecks::naturalPlateauLength() const noexcept
{
    // un picco non tosato resta su un solo valore finché la sua discesa c (L/2)^2 sta dentro un passo di quantizzazione
    return 2 + (int) std::ceil (2.0 * std::sqrt (quantStep / minCurvature));
}

void TechnicalChecks::processClip (int ch, float x) noexcept
{
    const auto c = (size_t) ch;
    const float ax = std::abs (x);
    const float step = std::abs (x - lastSample[c]);
    if (step > 0.0f && step < quantStep)
        quantStep = step;

    // plateau: campioni identici consecutivi ad alto livello (clip a qualunque ceiling)
    if (ax >= kFlatTopLevel && x == lastSample[c])
        ++flatRun[c];
    else
    {
        flatRun[c] = ax >= kFlatTopLevel ? 1 : 0;
        plateauEntry[c] = std::max (step, std::abs (lastSample[c] - prevSample[c]));
    }

    // campioni consecutivi a fondo scala
    fullScaleRun[c] = ax >= kFullScale ? fullScaleRun[c] + 1 : 0;

    // un picco lento arrotondato arriva sul plateau rallentando (passi di pochi quanti) e ci resta poco;
    // una tosatura taglia il segnale mentre sale: ingresso ripido, o un plateau troppo lungo per essere un picco
    const bool flatClip = flatRun[c] >= kFlatTopRun
                          && (plateauEntry[c] > kNaturalEntrySteps * quantStep || flatRun[c] > naturalPlateauLength());
    const bool fullScaleClip = fullScaleRun[c] >= kFlatTopRun;
    const bool clipping = flatClip || fullScaleClip;

    const bool wasInClip = inClip[c];
    if (clipping && ! wasInClip)
    {
        ++clipEvents;
        clipLevel[c] = ax;
        clipLength[c] = flatClip ? flatRun[c] : fullScaleRun[c];
        openClip[c] = -1;
        if (clipLog != nullptr && clipLog->size() < TimelineRecorder::maxEvents)
        {
            const int runLength = flatClip ? flatRun[c] : fullScaleRun[c];   // il plateau è iniziato prima
            openClip[c] = (long) clipLog->size();
            clipLog->push_back ({ std::max (0LL, samplePos - (runLength - 1)), samplePos + 1, ch });
        }
    }

    inClip[c] = clipping || (inClip[c] && (flatRun[c] > 1 || fullScaleRun[c] > 0));
    if (wasInClip && inClip[c])
        ++clipLength[c];
    else if (wasInClip)
        countClip (clipLevel[c], clipLength[c], clipCounts);
    prevSample[c] = lastSample[c];
    lastSample[c] = x;

    if (! inClip[c])
        openClip[c] = -1;
    else if (openClip[c] >= 0)
        (*clipLog)[(size_t) openClip[c]].end = samplePos + 1;
}

void TechnicalChecks::countClip (float level, int length, ClipCounts& counts) const noexcept
{
    if (level >= kNearFullScale)
    {
        ++counts.fullScale;
        return;
    }
    if (length >= longClipRun)
        ++counts.longCeiling;
    counts.ceilingLevel = std::max (counts.ceilingLevel, level);
}

void TechnicalChecks::process (const float* left, const float* right, int numSamples)
{
    const bool stereo = numChannels == 2 && right != nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = stereo ? right[i] : l;

        sum[0] += l;  sumSq[0] += (double) l * l;
        sum[1] += r;  sumSq[1] += (double) r * r;

        samplePos = (long long) count + i;
        processClip (0, l);
        if (stereo)
            processClip (1, r);

        blockSumSq += 0.5 * ((double) l * l + (double) r * r);

        if (++blockCounter >= blockLength)
        {
            const double rms = std::sqrt (blockSumSq / blockLength);
            if (rms == 0.0)
                digitalSilence = true;
            else if (! minBlockValid || rms < minBlockRms)
            {
                minBlockRms = rms;
                minBlockValid = true;
            }
            blockCounter = 0;
            blockSumSq = 0.0;
        }
    }

    count += numSamples;
}

void TechnicalChecks::fillSnapshot (AnalysisSnapshot& s) const
{
    if (count <= 0.0)
        return;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        s.dcOffsetDb[ch] = gainToDb (std::abs (sum[ch] / count));
        s.channelSilent[ch] = false;
    }

    const double rmsL = std::sqrt (sumSq[0] / count);
    const double rmsR = std::sqrt (sumSq[1] / count);
    const double loud = std::pow (10.0, -60.0 / 20.0), quiet = std::pow (10.0, -90.0 / 20.0);
    s.channelSilent[0] = rmsL < quiet && rmsR > loud;
    s.channelSilent[1] = rmsR < quiet && rmsL > loud;

    s.noiseFloorDb = minBlockValid ? gainToDb (minBlockRms) : kSilenceDb;
    s.hasDigitalSilence = digitalSilence;
    s.clipEvents = clipEvents;

    // gli eventi ancora aperti si classificano con la lunghezza raggiunta finora
    auto counts = clipCounts;
    for (int ch = 0; ch < 2; ++ch)
        if (inClip[(size_t) ch])
            countClip (clipLevel[(size_t) ch], clipLength[(size_t) ch], counts);
    s.clipEventsFullScale = counts.fullScale;
    s.longCeilingClips = counts.longCeiling;
    s.ceilingClipDb = counts.ceilingLevel > 0.0f ? gainToDb (counts.ceilingLevel) : kSilenceDb;
}

} // namespace ma
