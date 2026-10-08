#pragma once

#include "AnalysisSnapshot.h"

#include <juce_core/juce_core.h>

namespace ma
{

/**
    Snapshot in binario compresso (versioni del master salvate nel progetto).
    Non salva i dati istantanei (goniometro, spettro istantaneo, valori momentary/short-term correnti),
    che hanno senso solo durante la riproduzione.
*/
juce::MemoryBlock saveSnapshot (const AnalysisSnapshot& s);

/** false se i dati sono illeggibili o di una versione futura del formato. */
bool loadSnapshot (const void* data, size_t size, AnalysisSnapshot& out);

} // namespace ma
