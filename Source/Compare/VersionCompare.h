#pragma once

#include "Comparator.h"

namespace ma
{

enum class Verdict { better, worse, same };

/** Come è cambiata una diagnosi tra la versione salvata e il master attuale. */
struct FindingChange
{
    juce::String id;
    juce::String metric;
    juce::String valueBefore, valueNow;
    Severity severityBefore = Severity::ok, severityNow = Severity::ok;
    Verdict verdict = Verdict::same;
};

struct VersionComparison
{
    int improved = 0, worsened = 0, unchanged = 0;
    int scoreBefore = 0, scoreNow = 0;
    std::vector<FindingChange> changes;   // migliorate e peggiorate, le più importanti per prime
};

/**
    Confronta due risultati ottenuti con lo stesso profilo e le stesse opzioni (versione salvata e master attuale).
    Una diagnosi migliora se scende di severità, o se a pari severità si avvicina al range in modo apprezzabile;
    un problema che compare è un peggioramento, uno che scompare un miglioramento. Le mosse EQ e i messaggi
    informativi non contano (riassumono o accompagnano altre diagnosi).
*/
VersionComparison compareVersions (const ComparisonResult& before, const ComparisonResult& now);

} // namespace ma
