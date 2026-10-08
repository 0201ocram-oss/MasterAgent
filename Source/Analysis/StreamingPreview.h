#pragma once

#include <string>
#include <vector>

namespace ma
{

/** Simula la normalizzazione di loudness delle principali piattaforme. */
struct StreamingPlatform
{
    std::string name;
    float targetLufs;
    bool turnsUp;          // alza i brani più bassi del target
    float upPeakLimitDb;   // se turnsUp, il guadagno positivo è limitato perché il TP resti sotto questo valore
};

struct StreamingResult
{
    std::string name;
    float targetLufs = 0.0f;
    float gainDb = 0.0f;           // guadagno che applicherà la piattaforma
    float playbackLufs = 0.0f;     // loudness percepita in riproduzione
    float playbackTruePeak = 0.0f;
    bool tooLoud = false;          // verrà abbassato: loudness "sprecata"
    bool tooQuiet = false;         // suonerà più basso degli altri brani
};

const std::vector<StreamingPlatform>& getStreamingPlatforms();

std::vector<StreamingResult> computeStreamingPreview (float integratedLufs, float truePeakDb);

} // namespace ma
