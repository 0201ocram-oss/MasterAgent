#pragma once

#include "AnalysisSnapshot.h"

#include <array>
#include <vector>

namespace ma
{

/** Biquad in forma diretta II trasposta, in doppia precisione. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }
};

/**
    Misuratore di loudness conforme a ITU-R BS.1770-4 ed EBU R128 / Tech 3341 / Tech 3342.
    - Momentary (400 ms), Short-term (3 s), Integrated (gating -70 LUFS assoluto, -10 LU relativo)
    - Loudness Range (gating -70 assoluto, -20 LU relativo, percentili 10-95)
*/
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** right può essere nullptr per segnali mono. */
    void process (const float* left, const float* right, int numSamples);

    float getMomentary() const noexcept   { return momentaryLufs; }
    float getShortTerm() const noexcept   { return shortTermLufs; }
    float getMaxMomentary() const noexcept { return maxMomentaryLufs; }
    float getMaxShortTerm() const noexcept { return maxShortTermLufs; }

    /** Calcolo con gating su tutta la storia: O(n), chiamare a bassa frequenza. */
    float computeIntegrated() const;
    float computeLoudnessRange() const;

    const std::vector<float>& getShortTermHistory() const noexcept { return shortTermHistoryLufs; }
    void fillHistogram (std::array<float, kHistogramBins>& histogram) const;

    static float energyToLufs (double energy) noexcept;
    static double lufsToEnergy (double lufs) noexcept;

private:
    void designFilters();
    void finishSubBlock();

    double sampleRate = 48000.0;
    int numChannels = 2;

    std::array<Biquad, 2> preFilter, rlbFilter;

    int subBlockLength = 4800;          // 100 ms
    int subBlockCounter = 0;
    double subBlockSum = 0.0;

    std::array<double, 30> subBlockEnergies {};
    int subBlocksWritten = 0;

    float momentaryLufs = kSilenceDb, shortTermLufs = kSilenceDb;
    float maxMomentaryLufs = kSilenceDb, maxShortTermLufs = kSilenceDb;

    std::vector<double> momentaryBlocks;     // energie dei blocchi da 400 ms (overlap 75%)
    std::vector<double> shortTermBlocks;     // energie short-term ogni 100 ms (per LRA)
    std::vector<float> shortTermHistoryLufs;
};

} // namespace ma
