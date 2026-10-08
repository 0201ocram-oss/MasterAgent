#pragma once

#include "AnalysisSnapshot.h"

#include <array>
#include <vector>

namespace ma
{

/**
    True Peak (dBTP) secondo ITU-R BS.1770-4 Annex 2.
    Oversampling polifase (4x sotto 96 kHz, 2x a 88.2/96 kHz, 1x sopra) con filtro sinc
    a finestra di Kaiser da 48 tap per fase: più accurato del filtro minimo a 12 tap della norma.
*/
class TruePeakMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void process (const float* left, const float* right, int numSamples);

    int getOversamplingFactor() const noexcept { return factor; }

    float getTruePeakDb (int ch) const noexcept    { return gainToDb (truePeak[(size_t) ch]); }
    float getSamplePeakDb (int ch) const noexcept  { return gainToDb (samplePeak[(size_t) ch]); }
    float getTruePeakMaxDb() const noexcept        { return gainToDb (std::max (truePeak[0], truePeak[1])); }
    float getRecentTruePeakDb() const noexcept;
    int getOvers1dB() const noexcept { return overs1Events; }
    int getOvers0dB() const noexcept { return overs0Events; }

private:
    struct ChannelState
    {
        std::vector<double> history;   // doppia lunghezza per evitare il wrap nel prodotto scalare
        int writePos = 0;
        bool above1 = false, above0 = false;
    };

    float processSample (ChannelState& state, double x) noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2;
    int factor = 4;
    int tapsPerPhase = 48;
    std::vector<std::vector<double>> phases;

    std::array<ChannelState, 2> channels;
    std::array<double, 2> truePeak { 0.0, 0.0 };
    std::array<double, 2> samplePeak { 0.0, 0.0 };

    // massimi su blocchi da 100 ms per il true peak "recente" (3 s)
    std::array<double, 30> recentBlockMax {};
    int recentBlockIndex = 0;
    int recentBlockLength = 4800;
    int recentBlockCounter = 0;
    double currentBlockMax = 0.0;

    int overs1Events = 0, overs0Events = 0;
};

} // namespace ma
