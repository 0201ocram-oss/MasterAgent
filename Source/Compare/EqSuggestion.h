#pragma once

#include "../Analysis/AnalysisSnapshot.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace ma
{

/** Una mossa di EQ concreta: tipo, frequenza, guadagno e Q. */
struct EqMove
{
    enum class Type { bell, lowShelf, highShelf };

    Type type = Type::bell;
    float frequency = 1000.0f;   // centro (bell) o frequenza di metà guadagno (shelf)
    float gainDb = 0.0f;         // guadagno stimato per allinearsi al target
    float q = 0.7f;
    std::vector<int> bands;      // bande fuori tolleranza che la mossa corregge

    // impostati dal confronto in base a fase di lavoro e affidabilità del target
    float shownGainDb = 0.0f;    // guadagno consigliato (più prudente con curve generiche o in mastering)
    bool fixInMix = false;       // scostamento troppo grande per il mastering

    juce::String typeName() const;

    /** Es. "Bell 315 Hz -2.0 dB, Q 1.0" con il guadagno indicato. */
    juce::String describe (float gain) const;
};

/** Risposta in dB della mossa alla frequenza f (prototipo analogico RBJ: non dipende dal sample rate). */
float eqMoveResponseDb (const EqMove& move, float frequency);

/** Risposta in dB di una mossa su un terzo d'ottava (media di potenza sulla banda, come la misura). */
float eqMoveThirdOctaveDb (const EqMove& move, int thirdOctave);

struct EqFitOptions
{
    int maxMoves = 3;
    float minGainDb = 0.5f;      // mosse più piccole non valgono la pena
    float maxGainDb = 18.0f;     // limite del fit; il guadagno consigliato può essere più prudente
};

/**
    Trova le mosse EQ (bell e shelf) che spiegano meglio lo scostamento tonale master - target.

    Matching pursuit pesato sui terzi d'ottava 25 Hz - 16 kHz: a ogni passo prova bell (1/6 d'ottava, Q 0.5-2)
    e shelf a frequenze tipiche, sceglie quella che riduce di più l'errore (pesato con 1/tolleranza², così gli
    estremi dello spettro, più variabili tra brani, contano meno), sottrae la sua risposta e ripete.
    Si ferma quando tutte le bande rientrano nella tolleranza o la mossa successiva sarebbe trascurabile.
    Così più bande fuori target spiegate da una sola causa (es. Brilliance + Air troppo brillanti)
    diventano un'unica mossa (un high shelf) invece di tre consigli separati.
*/
std::vector<EqMove> suggestEqMoves (const std::array<float, kNumThirdOctaves>& tonalDelta,
                                    const std::array<float, kNumBands>& bandTolerance,
                                    const EqFitOptions& options = {});

} // namespace ma
