#pragma once

#include "AnalysisSnapshot.h"

#include <array>

namespace ma
{

/**
    Controlli di integrità tecnica:
    - DC offset per canale
    - clipping (campioni consecutivi identici ad alto livello, o a fondo scala)
    - noise floor (blocco da 100 ms non nullo più basso)
    - silenzio digitale e canale muto
*/
class TechnicalChecks
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void process (const float* left, const float* right, int numSamples);
    void fillSnapshot (AnalysisSnapshot& s) const;

    int getClipEvents() const noexcept { return clipEvents; }

private:
    void processClip (int ch, float x) noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2;

    std::array<double, 2> sum { 0.0, 0.0 };
    std::array<double, 2> sumSq { 0.0, 0.0 };
    double count = 0.0;

    std::array<float, 2> lastSample { 0.0f, 0.0f };
    std::array<int, 2> flatRun { 0, 0 };
    std::array<int, 2> fullScaleRun { 0, 0 };
    std::array<bool, 2> inClip { false, false };
    int clipEvents = 0;

    int blockLength = 4800;
    int blockCounter = 0;
    double blockSumSq = 0.0;
    double minBlockRms = 0.0;
    bool minBlockValid = false;
    bool digitalSilence = false;
};

} // namespace ma
