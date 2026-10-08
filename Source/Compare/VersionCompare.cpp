#include "VersionCompare.h"

#include <algorithm>
#include <map>

namespace ma
{

namespace
{
    bool counts (const Finding& f)
    {
        return f.id.isNotEmpty() && f.area != Area::none && ! f.key.startsWith ("eq:") && ! f.ignored;
    }

    /** 0 = nel range o informativa, poi da verificare e critica. */
    int rank (const Finding* f)
    {
        return f != nullptr && f->severity >= Severity::warning ? (int) f->severity : 0;
    }

    Verdict judge (const Finding* before, const Finding* now)
    {
        const int rb = rank (before), rn = rank (now);
        if (rn < rb) return Verdict::better;
        if (rn > rb) return Verdict::worse;
        if (rn == 0)  return Verdict::same;

        // stessa severità: conta di quanto si è avvicinato (o allontanato) dal range
        const float db = std::abs (before->delta), dn = std::abs (now->delta);
        if (dn < db * 0.85f - 0.05f) return Verdict::better;
        if (dn > db * 1.15f + 0.05f) return Verdict::worse;
        return Verdict::same;
    }
}

VersionComparison compareVersions (const ComparisonResult& before, const ComparisonResult& now)
{
    VersionComparison result;
    result.scoreBefore = before.score;
    result.scoreNow = now.score;

    std::map<juce::String, const Finding*> b, n;
    for (const auto& f : before.findings)
        if (counts (f) && b.find (f.id) == b.end())
            b[f.id] = &f;
    for (const auto& f : now.findings)
        if (counts (f) && n.find (f.id) == n.end())
            n[f.id] = &f;

    std::vector<juce::String> ids;
    for (const auto& [id, f] : b) ids.push_back (id);
    for (const auto& [id, f] : n)
        if (b.find (id) == b.end())
            ids.push_back (id);

    for (const auto& id : ids)
    {
        const auto* fb = b.count (id) ? b[id] : nullptr;
        const auto* fn = n.count (id) ? n[id] : nullptr;
        const auto verdict = judge (fb, fn);

        if (verdict == Verdict::same)
        {
            ++result.unchanged;
            continue;
        }

        (verdict == Verdict::better ? result.improved : result.worsened) += 1;

        FindingChange c;
        c.id = id;
        c.metric = fn != nullptr ? fn->metric : fb->metric;
        c.valueBefore = fb != nullptr ? fb->value : juce::String ("-");
        c.valueNow = fn != nullptr ? fn->value : juce::String ("-");
        c.severityBefore = fb != nullptr ? fb->severity : Severity::ok;
        c.severityNow = fn != nullptr ? fn->severity : Severity::ok;
        c.verdict = verdict;
        result.changes.push_back (std::move (c));
    }

    // prima i peggioramenti (da guardare subito), poi i miglioramenti; dentro, i più gravi
    std::stable_sort (result.changes.begin(), result.changes.end(), [] (const FindingChange& x, const FindingChange& y)
    {
        if (x.verdict != y.verdict)
            return x.verdict == Verdict::worse;
        return std::max (x.severityBefore, x.severityNow) > std::max (y.severityBefore, y.severityNow);
    });

    return result;
}

} // namespace ma
