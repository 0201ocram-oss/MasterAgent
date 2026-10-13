#pragma once

#include "../Compare/Comparator.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace ma
{

/** Cosa si ascolta in uscita: il segnale normale oppure una sua parte, per sentire ciò che dice il report. */
enum class MonitorMode { off = 0, mono, side, band };

struct MonitorChoice
{
    MonitorMode mode = MonitorMode::off;
    int band = -1;   // solo per MonitorMode::band (indice di kBandNames)

    bool operator== (const MonitorChoice&) const = default;
};

/**
    Monitor d'ascolto: somma mono, solo Side (L-R)/2 o una sola banda (Linkwitz-Riley 24 dB/oct sui bordi
    di kBandEdges). Agisce solo sull'uscita, dopo l'analisi. Le accensioni e i cambi passano per una breve
    dissolvenza (via il segnale normale), così non ci sono click. Spento e a riposo l'uscita resta identica.
*/
class Monitor
{
public:
    void prepare (double sampleRate);

    /** Thread GUI. */
    void setChoice (MonitorChoice choice) noexcept;
    MonitorChoice getChoice() const noexcept;

    /** Audio thread. right può essere nullptr (uscita mono). */
    void process (float* left, float* right, int numSamples) noexcept;

    static constexpr double fadeSeconds = 0.015;

private:
    void applyChoice (int packed) noexcept;
    static int pack (MonitorChoice c) noexcept   { return (int) c.mode * 16 + std::max (0, c.band); }
    static MonitorChoice unpack (int v) noexcept  { return { (MonitorMode) (v / 16), v / 16 == (int) MonitorMode::band ? v % 16 : -1 }; }

    std::atomic<int> requested { 0 };

    // audio thread
    double sampleRate = 48000.0;
    int current = 0;            // scelta in uscita (impacchettata)
    float mix = 0.0f;           // 0 = segnale normale, 1 = monitor
    float step = 0.001f;
    bool useLowPass = false;
    juce::dsp::LinkwitzRileyFilter<float> highPass, lowPass;
};

/** Cosa ascoltare per capire una diagnosi (mode off = niente di utile). */
MonitorChoice monitorForFinding (const Finding& finding, const ComparisonResult& result);

/** "Mono", "Side", "solo Low-mid (250-500 Hz)" */
juce::String describeMonitor (MonitorChoice choice);

} // namespace ma
