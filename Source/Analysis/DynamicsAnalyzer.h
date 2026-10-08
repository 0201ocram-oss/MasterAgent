#pragma once

#include "AnalysisSnapshot.h"
#include "LoudnessMeter.h"

#include <array>
#include <vector>

namespace ma
{

/**
    Analisi della dinamica:
    - RMS full-band (per il crest factor)
    - DR secondo l'algoritmo TT/Pleasurize (blocchi da 3 s, top 20% RMS, secondo picco più alto)
    - crest factor multibanda (crossover Linkwitz-Riley 4° ordine a 200 Hz e 4 kHz), mediana sui blocchi da 3 s
*/
class DynamicsAnalyzer
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void process (const float* left, const float* right, int numSamples);

    float getRmsDb() const noexcept;
    float getSamplePeakDb() const noexcept { return gainToDb (std::max (peak[0], peak[1])); }

    /** Restituisce false se non ci sono ancora abbastanza blocchi. */
    bool computeDr (float& dr) const;
    std::array<float, kNumCrestBands> computeBandCrest() const;

private:
    struct LR4
    {
        Biquad s1, s2;
        double process (double x) noexcept { return s2.process (s1.process (x)); }
        void reset() noexcept { s1.reset(); s2.reset(); }
    };

    static Biquad butterworth (double sampleRate, double freq, bool highPass);
    void finishBlock();

    double sampleRate = 48000.0;
    int numChannels = 2;

    std::array<double, 2> sumSquares { 0.0, 0.0 };
    std::array<double, 2> peak { 0.0, 0.0 };
    double totalSamples = 0.0;

    // DR
    int blockLength = 144000;
    int blockCounter = 0;
    std::array<double, 2> blockSumSq { 0.0, 0.0 };
    std::array<double, 2> blockPeak { 0.0, 0.0 };
    std::array<std::vector<double>, 2> blockRms, blockPeaks;

    // crest multibanda
    LR4 lowLP, midHP, midLP, highHP;
    std::array<double, kNumCrestBands> bandSumSq {};
    std::array<double, kNumCrestBands> bandPeak {};
    std::array<std::vector<float>, kNumCrestBands> bandCrestBlocks;
};

} // namespace ma
