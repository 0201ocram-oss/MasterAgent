#pragma once

#include "../Analysis/AnalysisSnapshot.h"
#include "Comparator.h"

#include <juce_core/juce_core.h>

namespace ma
{

juce::String buildTextReport (const AnalysisSnapshot& s, const ComparisonResult& c, const juce::String& targetName);
juce::String buildJsonReport (const AnalysisSnapshot& s, const ComparisonResult& c, const juce::String& targetName);

} // namespace ma
