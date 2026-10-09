#pragma once

#include "AnalysisSnapshot.h"
#include "Timeline.h"

#include <array>
#include <vector>

namespace ma
{

/**
    Controlli di integrità tecnica:
    - DC offset per canale
    - clipping (campioni consecutivi identici ad alto livello, o a fondo scala). Nei file a 16 bit anche il picco
      lento di un sub (808, basso sintetico) si arrotonda su 3-4 campioni identici: un plateau conta solo se il segnale
      ci arriva ripido o se è più lungo di quanto un picco non tosato possa restare piatto.
      Ogni plateau è classificato: a fondo scala (sovraccarico) o sotto, al ceiling di un clipper/limiter; di questi
      si contano quelli lunghi, che tosano in modo udibile
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

    /** Analisi offline: registra anche dove avviene ogni evento di clipping (nullptr = spento). */
    void setClipLog (std::vector<SampleRange>* log) noexcept { clipLog = log; }

private:
    struct ClipCounts
    {
        int fullScale = 0;
        int longCeiling = 0;
        float ceilingLevel = 0.0f;   // ampiezza del plateau più alto sotto il fondo scala
    };

    void processClip (int ch, float x) noexcept;
    void countClip (float level, int length, ClipCounts& counts) const noexcept;
    int naturalPlateauLength() const noexcept;

    std::vector<SampleRange>* clipLog = nullptr;
    std::array<long, 2> openClip { -1, -1 };   // evento in corso nel registro, per canale
    long long samplePos = 0;

    double sampleRate = 48000.0;
    int numChannels = 2;

    std::array<double, 2> sum { 0.0, 0.0 };
    std::array<double, 2> sumSq { 0.0, 0.0 };
    double count = 0.0;

    std::array<float, 2> lastSample { 0.0f, 0.0f };
    std::array<float, 2> prevSample { 0.0f, 0.0f };    // il campione prima di lastSample
    std::array<float, 2> plateauEntry { 0.0f, 0.0f };  // ripidità con cui il segnale è arrivato sul valore attuale
    float quantStep = 1.0f / 32768.0f;                 // passo di quantizzazione del materiale (differenza minima tra campioni)
    double minCurvature = 1.0e-6;                      // curvatura del picco più lento che consideriamo (20 Hz a -6 dBFS)
    std::array<int, 2> flatRun { 0, 0 };
    std::array<int, 2> fullScaleRun { 0, 0 };
    std::array<bool, 2> inClip { false, false };
    std::array<float, 2> clipLevel { 0.0f, 0.0f };     // ampiezza del plateau in corso
    std::array<int, 2> clipLength { 0, 0 };            // campioni del plateau in corso
    int longClipRun = 8;
    int clipEvents = 0;
    ClipCounts clipCounts;

    int blockLength = 4800;
    int blockCounter = 0;
    double blockSumSq = 0.0;
    double minBlockRms = 0.0;
    bool minBlockValid = false;
    bool digitalSilence = false;
};

} // namespace ma
