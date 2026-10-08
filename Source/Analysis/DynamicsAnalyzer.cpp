#include "DynamicsAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace ma
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    const double kBandGateRms = std::pow (10.0, -60.0 / 20.0);   // blocchi più bassi di -60 dBFS esclusi
}

Biquad DynamicsAnalyzer::butterworth (double sr, double freq, bool highPass)
{
    // biquad RBJ con Q = 1/sqrt(2): due in cascata = Linkwitz-Riley 4° ordine
    const double w0 = 2.0 * kPi * freq / sr;
    const double cosw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
    const double a0 = 1.0 + alpha;

    Biquad b;
    if (highPass)
    {
        b.b0 = (1.0 + cosw) / 2.0 / a0;
        b.b1 = -(1.0 + cosw) / a0;
        b.b2 = b.b0;
    }
    else
    {
        b.b0 = (1.0 - cosw) / 2.0 / a0;
        b.b1 = (1.0 - cosw) / a0;
        b.b2 = b.b0;
    }
    b.a1 = -2.0 * cosw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

void DynamicsAnalyzer::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);
    blockLength = std::max (1, (int) std::lround (sampleRate * 3.0));

    const auto lp200 = butterworth (sampleRate, 200.0, false);
    const auto hp200 = butterworth (sampleRate, 200.0, true);
    const auto lp4k  = butterworth (sampleRate, 4000.0, false);
    const auto hp4k  = butterworth (sampleRate, 4000.0, true);

    lowLP  = { lp200, lp200 };
    midHP  = { hp200, hp200 };
    midLP  = { lp4k, lp4k };
    highHP = { hp4k, hp4k };

    reset();
}

void DynamicsAnalyzer::reset()
{
    sumSquares = { 0.0, 0.0 };
    peak = { 0.0, 0.0 };
    totalSamples = 0.0;

    blockCounter = 0;
    blockSumSq = { 0.0, 0.0 };
    blockPeak = { 0.0, 0.0 };
    for (auto& v : blockRms) v.clear();
    for (auto& v : blockPeaks) v.clear();

    lowLP.reset(); midHP.reset(); midLP.reset(); highHP.reset();
    bandSumSq.fill (0.0);
    bandPeak.fill (0.0);
    for (auto& v : bandCrestBlocks) v.clear();
}

void DynamicsAnalyzer::process (const float* left, const float* right, int numSamples)
{
    const bool stereo = numChannels == 2 && right != nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i];
        const double r = stereo ? (double) right[i] : l;

        sumSquares[0] += l * l;
        sumSquares[1] += r * r;
        peak[0] = std::max (peak[0], std::abs (l));
        peak[1] = std::max (peak[1], std::abs (r));

        blockSumSq[0] += l * l;
        blockSumSq[1] += r * r;
        blockPeak[0] = std::max (blockPeak[0], std::abs (l));
        blockPeak[1] = std::max (blockPeak[1], std::abs (r));

        const double mono = 0.5 * (l + r);
        const double lo = lowLP.process (mono);
        const double hi = highHP.process (mono);
        const double mid = midLP.process (midHP.process (mono));

        bandSumSq[0] += lo * lo;   bandPeak[0] = std::max (bandPeak[0], std::abs (lo));
        bandSumSq[1] += mid * mid; bandPeak[1] = std::max (bandPeak[1], std::abs (mid));
        bandSumSq[2] += hi * hi;   bandPeak[2] = std::max (bandPeak[2], std::abs (hi));

        if (++blockCounter >= blockLength)
            finishBlock();
    }

    totalSamples += numSamples;
}

void DynamicsAnalyzer::finishBlock()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        // RMS "DR-style": sqrt(2 * mean(x^2)) -> una sinusoide a fondo scala vale 0 dB
        blockRms[(size_t) ch].push_back (std::sqrt (2.0 * blockSumSq[(size_t) ch] / blockLength));
        blockPeaks[(size_t) ch].push_back (blockPeak[(size_t) ch]);
        blockSumSq[(size_t) ch] = 0.0;
        blockPeak[(size_t) ch] = 0.0;
    }

    for (int b = 0; b < kNumCrestBands; ++b)
    {
        const double rms = std::sqrt (bandSumSq[(size_t) b] / blockLength);
        if (rms > kBandGateRms)
            bandCrestBlocks[(size_t) b].push_back ((float) (20.0 * std::log10 (bandPeak[(size_t) b] / rms)));
        bandSumSq[(size_t) b] = 0.0;
        bandPeak[(size_t) b] = 0.0;
    }

    blockCounter = 0;
}

float DynamicsAnalyzer::getRmsDb() const noexcept
{
    if (totalSamples <= 0.0)
        return kSilenceDb;
    return powerToDb ((sumSquares[0] + sumSquares[1]) / (2.0 * totalSamples));
}

bool DynamicsAnalyzer::computeDr (float& dr) const
{
    if (blockRms[0].size() < 3)
        return false;

    double drSum = 0.0;
    int validChannels = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto rms = blockRms[(size_t) ch];
        auto peaks = blockPeaks[(size_t) ch];
        std::sort (rms.begin(), rms.end(), std::greater<>());
        std::sort (peaks.begin(), peaks.end(), std::greater<>());

        const size_t topCount = std::max<size_t> (1, (size_t) std::lround (rms.size() * 0.2));
        double sq = 0.0;
        for (size_t k = 0; k < topCount; ++k)
            sq += rms[k] * rms[k];
        const double topRms = std::sqrt (sq / (double) topCount);
        const double secondPeak = peaks.size() > 1 ? peaks[1] : peaks[0];

        if (topRms > 0.0 && secondPeak > 0.0)
        {
            drSum += 20.0 * std::log10 (secondPeak / topRms);
            ++validChannels;
        }
    }

    if (validChannels == 0)
        return false;

    dr = (float) (drSum / validChannels);
    return true;
}

std::array<float, kNumCrestBands> DynamicsAnalyzer::computeBandCrest() const
{
    std::array<float, kNumCrestBands> result {};
    for (int b = 0; b < kNumCrestBands; ++b)
    {
        auto v = bandCrestBlocks[(size_t) b];
        if (v.empty())
            continue;
        std::nth_element (v.begin(), v.begin() + (long) (v.size() / 2), v.end());
        result[(size_t) b] = v[v.size() / 2];
    }
    return result;
}

} // namespace ma
