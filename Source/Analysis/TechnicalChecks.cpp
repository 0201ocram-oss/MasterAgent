#include "TechnicalChecks.h"

#include <algorithm>
#include <cmath>

namespace ma
{

namespace
{
    constexpr float kFlatTopLevel = 0.5f;        // -6 dBFS: plateau di campioni identici sopra questo livello = clip
    constexpr float kFullScale = 0.99995f;
    constexpr int kFlatTopRun = 3;
}

void TechnicalChecks::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);
    blockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
    reset();
}

void TechnicalChecks::reset()
{
    sum = { 0.0, 0.0 };
    sumSq = { 0.0, 0.0 };
    count = 0.0;
    lastSample = { 0.0f, 0.0f };
    flatRun = { 0, 0 };
    fullScaleRun = { 0, 0 };
    inClip = { false, false };
    openClip = { -1, -1 };
    samplePos = 0;
    clipEvents = 0;
    blockCounter = 0;
    blockSumSq = 0.0;
    minBlockRms = 0.0;
    minBlockValid = false;
    digitalSilence = false;
}

void TechnicalChecks::processClip (int ch, float x) noexcept
{
    const auto c = (size_t) ch;
    const float ax = std::abs (x);

    // plateau: campioni identici consecutivi ad alto livello (clip a qualunque ceiling)
    if (ax >= kFlatTopLevel && x == lastSample[c])
        ++flatRun[c];
    else
        flatRun[c] = ax >= kFlatTopLevel ? 1 : 0;

    // campioni consecutivi a fondo scala
    fullScaleRun[c] = ax >= kFullScale ? fullScaleRun[c] + 1 : 0;

    const bool clipping = flatRun[c] >= kFlatTopRun || fullScaleRun[c] >= kFlatTopRun;

    if (clipping && ! inClip[c])
    {
        ++clipEvents;
        openClip[c] = -1;
        if (clipLog != nullptr && clipLog->size() < TimelineRecorder::maxEvents)
        {
            openClip[c] = (long) clipLog->size();
            clipLog->push_back ({ std::max (0LL, samplePos - (kFlatTopRun - 1)), samplePos + 1, ch });   // il plateau è iniziato prima
        }
    }

    inClip[c] = clipping || (inClip[c] && (flatRun[c] > 1 || fullScaleRun[c] > 0));
    lastSample[c] = x;

    if (! inClip[c])
        openClip[c] = -1;
    else if (openClip[c] >= 0)
        (*clipLog)[(size_t) openClip[c]].end = samplePos + 1;
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
}

} // namespace ma
