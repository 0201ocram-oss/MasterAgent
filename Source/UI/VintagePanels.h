#pragma once

#include "Theme.h"

namespace ma::ui
{

/**
    Stile vintage: ponte dei meter. Tre strumenti ad ago (loudness VU con lo 0 sul target, picchi L/R a doppio ago,
    correlazione di fase) e sotto ciascuno i display a sette segmenti dei valori base.
*/
class MeterBridgePanel : public Panel
{
public:
    MeterBridgePanel() : Panel ("Meter", "panel:meters") {}

    /** Aggiorna anche gli aghi, con l'inerzia degli strumenti veri (va chiamata a ogni refresh). */
    void setData (const DashboardData& d) override;
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;

private:
    float vuNeedle = 0.0f, phaseNeedle = 0.5f;            // posizione sulla scala (0..1)
    std::array<float, 2> peakNeedleDb { -40.0f, -40.0f };
    double lastUpdate = 0.0;
};

/**
    Stile vintage: bilanciamento tonale come un equalizzatore grafico a 31 bande. Ogni cursore è lo scostamento
    del terzo d'ottava dal target; sotto, le 8 bande con il loro scostamento e la spia. Clic su una banda: la si ascolta da sola.
*/
class TonalEqPanel : public Panel
{
public:
    TonalEqPanel() : Panel ("Bilanciamento tonale", "panel:tonalEq") {}

    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    std::function<void (int band)> onSoloBand;

private:
    int bandAt (juce::Point<float> p) const;

    std::array<juce::Rectangle<float>, kNumBands> bandCells {};   // geometria dell'ultimo paint, per il mouse
    int hoveredBand = -1;
};

/** Stile vintage: analizzatore a LED in terzi d'ottava (spettro di ciò che si ascolta, media del brano e picchi). */
class AnalyzerPanel : public Panel
{
public:
    AnalyzerPanel() : Panel ("Analizzatore", "panel:analyzer") {}

    void setData (const DashboardData& d) override;
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;

private:
    std::array<float, kNumThirdOctaves> peakHold {};
    double lastUpdate = 0.0;
};

/** Stile vintage: oscilloscopio a fosfori verdi (goniometro) e tasti d'ascolto Stereo / Mono / Side. */
class ScopePanel : public Panel
{
public:
    ScopePanel() : Panel ("Fase e ascolto", "panel:scope") {}

    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    /** Tasti d'ascolto (Stereo = monitor spento). */
    std::function<void (MonitorChoice)> onMonitorChanged;

private:
    void drawScope (juce::Graphics& g, juce::Rectangle<float> r);

    std::array<juce::Rectangle<float>, 3> monitorButtons {};
};

} // namespace ma::ui
