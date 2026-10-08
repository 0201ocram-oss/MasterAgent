#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ma
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kAbsoluteGateLufs = -70.0;
}

float LoudnessMeter::energyToLufs (double energy) noexcept
{
    return energy > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (energy)) : kSilenceDb;
}

double LoudnessMeter::lufsToEnergy (double lufs) noexcept
{
    return std::pow (10.0, (lufs + 0.691) / 10.0);
}

void LoudnessMeter::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);
    subBlockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
    designFilters();
    reset();
}

void LoudnessMeter::designFilters()
{
    // Coefficienti BS.1770 ricalcolati per la frequenza di campionamento corrente
    // (stessa derivazione usata da libebur128).
    double f0 = 1681.974450955533;
    double G = 3.999843853973347;
    double Q = 0.7071752369554196;

    double K = std::tan (kPi * f0 / sampleRate);
    const double Vh = std::pow (10.0, G / 20.0);
    const double Vb = std::pow (Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;

    Biquad pre;
    pre.b0 = (Vh + Vb * K / Q + K * K) / a0;
    pre.b1 = 2.0 * (K * K - Vh) / a0;
    pre.b2 = (Vh - Vb * K / Q + K * K) / a0;
    pre.a1 = 2.0 * (K * K - 1.0) / a0;
    pre.a2 = (1.0 - K / Q + K * K) / a0;

    f0 = 38.13547087602444;
    Q = 0.5003270373238773;
    K = std::tan (kPi * f0 / sampleRate);
    a0 = 1.0 + K / Q + K * K;

    Biquad rlb;
    rlb.b0 = 1.0;
    rlb.b1 = -2.0;
    rlb.b2 = 1.0;
    rlb.a1 = 2.0 * (K * K - 1.0) / a0;
    rlb.a2 = (1.0 - K / Q + K * K) / a0;

    preFilter = { pre, pre };
    rlbFilter = { rlb, rlb };
}

void LoudnessMeter::reset()
{
    for (auto& f : preFilter) f.reset();
    for (auto& f : rlbFilter) f.reset();

    subBlockCounter = 0;
    subBlockSum = 0.0;
    subBlockEnergies.fill (0.0);
    subBlocksWritten = 0;

    momentaryLufs = shortTermLufs = kSilenceDb;
    maxMomentaryLufs = maxShortTermLufs = kSilenceDb;

    momentaryBlocks.clear();
    shortTermBlocks.clear();
    shortTermHistoryLufs.clear();
    momentaryBlocks.reserve (36000);
    shortTermBlocks.reserve (36000);
    shortTermHistoryLufs.reserve (36000);
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    const bool stereo = numChannels == 2 && right != nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = rlbFilter[0].process (preFilter[0].process ((double) left[i]));
        double sum = l * l;

        if (stereo)
        {
            const double r = rlbFilter[1].process (preFilter[1].process ((double) right[i]));
            sum += r * r;
        }

        subBlockSum += sum;

        if (++subBlockCounter >= subBlockLength)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock()
{
    const double energy = subBlockSum / (double) subBlockLength;
    subBlockSum = 0.0;
    subBlockCounter = 0;

    subBlockEnergies[(size_t) (subBlocksWritten % 30)] = energy;
    ++subBlocksWritten;

    auto meanOfLast = [this] (int count)
    {
        double s = 0.0;
        for (int k = 0; k < count; ++k)
            s += subBlockEnergies[(size_t) ((subBlocksWritten - 1 - k) % 30)];
        return s / (double) count;
    };

    if (subBlocksWritten >= 4)
    {
        const double m = meanOfLast (4);
        momentaryBlocks.push_back (m);
        momentaryLufs = energyToLufs (m);
        maxMomentaryLufs = std::max (maxMomentaryLufs, momentaryLufs);
    }

    if (subBlocksWritten >= 30)
    {
        const double s = meanOfLast (30);
        shortTermBlocks.push_back (s);
        shortTermLufs = energyToLufs (s);
        maxShortTermLufs = std::max (maxShortTermLufs, shortTermLufs);
        shortTermHistoryLufs.push_back (shortTermLufs);
    }
}

float LoudnessMeter::computeIntegrated() const
{
    const double absGate = lufsToEnergy (kAbsoluteGateLufs);

    double sum = 0.0;
    size_t count = 0;
    for (auto e : momentaryBlocks)
        if (e > absGate) { sum += e; ++count; }

    if (count == 0)
        return kSilenceDb;

    const double relGate = (sum / (double) count) * std::pow (10.0, -10.0 / 10.0);

    sum = 0.0;
    count = 0;
    for (auto e : momentaryBlocks)
        if (e > absGate && e > relGate) { sum += e; ++count; }

    return count > 0 ? energyToLufs (sum / (double) count) : kSilenceDb;
}

float LoudnessMeter::computeLoudnessRange() const
{
    const double absGate = lufsToEnergy (kAbsoluteGateLufs);

    double sum = 0.0;
    size_t count = 0;
    for (auto e : shortTermBlocks)
        if (e > absGate) { sum += e; ++count; }

    if (count == 0)
        return 0.0f;

    const double relGate = (sum / (double) count) * std::pow (10.0, -20.0 / 10.0);

    std::vector<double> gated;
    gated.reserve (count);
    for (auto e : shortTermBlocks)
        if (e > absGate && e > relGate)
            gated.push_back (e);

    if (gated.size() < 2)
        return 0.0f;

    std::sort (gated.begin(), gated.end());

    auto percentile = [&gated] (double p)
    {
        const double pos = p * (double) (gated.size() - 1);
        const auto lo = (size_t) std::floor (pos);
        const auto hi = std::min (lo + 1, gated.size() - 1);
        const double frac = pos - (double) lo;
        return gated[lo] + (gated[hi] - gated[lo]) * frac;
    };

    return energyToLufs (percentile (0.95)) - energyToLufs (percentile (0.10));
}

void LoudnessMeter::fillHistogram (std::array<float, kHistogramBins>& histogram) const
{
    histogram.fill (0.0f);
    int total = 0;

    for (auto lufs : shortTermHistoryLufs)
    {
        if (lufs <= (float) kAbsoluteGateLufs)
            continue;

        const int bin = std::clamp ((int) std::floor (lufs - kHistogramMinLufs), 0, kHistogramBins - 1);
        histogram[(size_t) bin] += 1.0f;
        ++total;
    }

    if (total > 0)
        for (auto& h : histogram)
            h /= (float) total;
}

} // namespace ma
