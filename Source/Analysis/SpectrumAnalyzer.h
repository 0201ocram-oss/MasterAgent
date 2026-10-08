#pragma once

#include "AnalysisSnapshot.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace ma
{

/**
    Analisi spettrale e stereo basata su FFT (Hann, overlap 75%).
    - FFT lunga (32768 punti a 44.1/48 kHz, ~1.5 Hz per bin) per le misure a lungo termine: serve
      per misurare correttamente i terzi d'ottava sub (a 25 Hz un terzo d'ottava è largo meno di 6 Hz).
    - FFT corta (8192 punti) per lo spettro istantaneo visualizzato e per il comportamento nel tempo delle bande.
    Accumula a lungo termine, per ogni bin, le potenze L, R e lo spettro incrociato Re(L·R*).
    Da queste ricava: spettro medio, terzi d'ottava, energia per banda, tilt, centroide,
    risonanze/note, correlazione/larghezza/compatibilità mono per banda.
    In più:
    - terzi d'ottava per livello di short-term loudness, da cui lo spettro della sola sezione più forte;
    - per ogni banda, quanto l'energia arriva a raffiche (frame corti) e in quale punto del brano è più in eccesso.
*/
class SpectrumAnalyzer
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /**
        shortTermLufs: short-term loudness corrente (per lo spettro della sezione più forte).
        songTime: tempo del brano del primo campione del blocco (secondi), per localizzare i problemi.
    */
    void process (const float* left, const float* right, int numSamples, float shortTermLufs = kSilenceDb, double songTime = 0.0);

    /** Calcola i valori derivati e li scrive nello snapshot (thread di analisi). */
    void fillSnapshot (AnalysisSnapshot& s) const;

    /** Potenza (RMS², scala dBFS) tra due frequenze, sullo spettro a lungo termine L+R. */
    double bandPowerLongTerm (double lo, double hi) const;

    int getFftSize() const noexcept { return fftSize; }

    static constexpr int kMaxTimelineSeconds = 1800;   // 30 minuti

private:
    void performLongFrame();
    void performShortFrame (double frameEndTime);
    double bandPower (const std::vector<double>& bins, double lo, double hi, double binWidth) const;
    double longBinWidth() const noexcept  { return sampleRate / fftSize; }
    double shortBinWidth() const noexcept { return sampleRate / shortFftSize; }

    void fillBandDynamics (AnalysisSnapshot& s) const;
    void fillLoudestSection (AnalysisSnapshot& s) const;
    void fillResonances (AnalysisSnapshot& s, const std::vector<double>& sumP) const;

    double sampleRate = 48000.0;
    int numChannels = 2;

    int fftOrder = 15;
    int fftSize = 32768;
    int hopSize = 8192;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> fftL, fftR;

    int shortFftSize = 8192;
    int shortHopSize = 2048;
    std::unique_ptr<juce::dsp::FFT> shortFft;
    std::vector<float> shortWindow;
    std::vector<float> shortFftL, shortFftR;
    double shortPowerNorm = 1.0;
    int samplesSinceShortFrame = 0;

    std::vector<float> inputL, inputR;     // buffer circolare di fftSize campioni
    int inputPos = 0;
    int samplesSinceFrame = 0;
    int samplesBuffered = 0;

    std::vector<double> sumPL, sumPR, sumCross;   // long-term per bin
    std::vector<double> instantP;                 // media esponenziale (L+R)/2
    std::vector<double> shortRawP;                // ultimo frame corto (L+R)/2, senza media
    std::vector<double> framePower;               // ultimo frame lungo (L+R)/2
    long long framesAccumulated = 0;
    double powerNorm = 1.0;

    // spettro per livello di loudness: 0.5 LU per bin da -70 a 0 LUFS (l'ultimo raccoglie anche > 0).
    // Bin stretti perché nei master compressi strofe e ritornelli differiscono di pochi LU.
    static constexpr int kLoudnessBins = 141;
    std::array<std::array<double, kNumThirdOctaves>, kLoudnessBins> loudnessThirds {};
    std::array<double, kLoudnessBins> loudnessFrames {};
    float currentShortTerm = kSilenceDb;

    // comportamento nel tempo delle bande (frame corti): istogramma del livello relativo della banda
    static constexpr int kBurstBins = 160;        // da -80 a 0 dB rispetto al totale, 0.5 dB per bin
    struct BurstBin { double frames = 0.0, bandP = 0.0, totalP = 0.0; };
    std::array<std::array<BurstBin, kBurstBins>, kNumBands> burstHistogram {};

    // energia per secondo di brano, per trovare dove una banda è più in eccesso
    struct SecondBin { std::array<double, kNumBands> bandP {}; double totalP = 0.0; int frames = 0; };
    std::vector<SecondBin> timeline;

    // correlazione istantanea (dominio del tempo, costante ~300 ms)
    double emaLR = 0.0, emaLL = 0.0, emaRR = 0.0, emaCoeff = 0.0;

    // goniometro: campioni recenti decimati
    std::array<float, kGoniometerPoints * 2> gonio {};
    int gonioPos = 0;
    int gonioDecimation = 0;
};

} // namespace ma
