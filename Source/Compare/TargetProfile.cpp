#include "TargetProfile.h"
#include "Comparator.h"

#include "MasterAgentProfiles.h"

#include <cmath>

namespace ma
{

namespace
{
    template <size_t N>
    std::optional<std::array<float, N>> readArray (const juce::var& root, const char* name)
    {
        if (const auto* arr = root.getProperty (name, {}).getArray(); arr != nullptr && arr->size() == (int) N)
        {
            std::array<float, N> values {};
            for (size_t i = 0; i < N; ++i)
                values[i] = (float) (*arr)[(int) i];
            return values;
        }
        return std::nullopt;
    }

    float round2 (float v) { return std::round (v * 100.0f) / 100.0f; }

    template <size_t N>
    void writeArray (juce::DynamicObject& root, const char* name, const std::optional<std::array<float, N>>& values)
    {
        if (! values)
            return;
        juce::Array<juce::var> arr;
        for (auto v : *values)
            arr.add (round2 (v));
        root.setProperty (name, arr);
    }

    /** Metriche ricavate da un brano analizzato: tolleranza "ok" (±) e margine di attenzione oltre i limiti. */
    struct MetricSpec
    {
        juce::String key;
        float okTol, warnTol;
        float (*get) (const AnalysisSnapshot&);
        bool (*valid) (const AnalysisSnapshot&);
    };

    const std::vector<MetricSpec>& metricSpecs()
    {
        static const std::vector<MetricSpec> specs {
            { metric::integratedLufs, 1.0f, 1.0f, [] (const AnalysisSnapshot& s) { return s.integratedLufs; },       [] (const AnalysisSnapshot&) { return true; } },
            { metric::truePeakMax,    0.5f, 0.5f, [] (const AnalysisSnapshot& s) { return s.truePeakMaxDb; },        [] (const AnalysisSnapshot&) { return true; } },
            { metric::loudnessRange,  1.5f, 1.5f, [] (const AnalysisSnapshot& s) { return s.loudnessRange; },        [] (const AnalysisSnapshot&) { return true; } },
            { metric::plr,            1.0f, 1.5f, [] (const AnalysisSnapshot& s) { return s.plr; },                  [] (const AnalysisSnapshot&) { return true; } },
            { metric::minPsr,         1.0f, 1.5f, [] (const AnalysisSnapshot& s) { return s.minPsr; },               [] (const AnalysisSnapshot&) { return true; } },
            { metric::dr,             1.0f, 1.0f, [] (const AnalysisSnapshot& s) { return s.drValue; },              [] (const AnalysisSnapshot& s) { return s.drValid; } },
            { metric::correlation,    0.1f, 0.1f, [] (const AnalysisSnapshot& s) { return s.correlation; },          [] (const AnalysisSnapshot&) { return true; } },
            { metric::widthPercent,   5.0f, 5.0f, [] (const AnalysisSnapshot& s) { return s.widthPercent; },         [] (const AnalysisSnapshot&) { return true; } },
            { metric::lowEndWidth,    4.0f, 4.0f, [] (const AnalysisSnapshot& s) { return s.lowEndWidthPercent; },   [] (const AnalysisSnapshot&) { return true; } },
            { metric::spectralTilt,   0.4f, 0.4f, [] (const AnalysisSnapshot& s) { return s.spectralTiltDbPerOct; }, [] (const AnalysisSnapshot&) { return true; } },
            { metric::crestLow,       1.5f, 1.5f, [] (const AnalysisSnapshot& s) { return s.bandCrestDb[0]; },       [] (const AnalysisSnapshot&) { return true; } },
            { metric::crestMid,       1.5f, 1.5f, [] (const AnalysisSnapshot& s) { return s.bandCrestDb[1]; },       [] (const AnalysisSnapshot&) { return true; } },
            { metric::crestHigh,      1.5f, 1.5f, [] (const AnalysisSnapshot& s) { return s.bandCrestDb[2]; },       [] (const AnalysisSnapshot&) { return true; } },
        };
        return specs;
    }

    /** Media e deviazione standard campionaria. */
    std::pair<double, double> meanAndDeviation (const std::vector<double>& values)
    {
        if (values.empty())
            return { 0.0, 0.0 };
        double mean = 0.0;
        for (auto v : values) mean += v;
        mean /= (double) values.size();
        if (values.size() < 2)
            return { mean, 0.0 };
        double sq = 0.0;
        for (auto v : values) sq += (v - mean) * (v - mean);
        return { mean, std::sqrt (sq / (double) (values.size() - 1)) };
    }
}

const MetricRange* TargetProfile::getMetric (const juce::String& key) const
{
    const auto it = metrics.find (key);
    return it != metrics.end() ? &it->second : nullptr;
}

std::optional<TargetProfile> TargetProfile::fromJson (const juce::String& json)
{
    const auto root = juce::JSON::parse (json);
    if (! root.isObject())
        return std::nullopt;

    TargetProfile p;
    p.id = root.getProperty ("id", {}).toString();
    p.name = root.getProperty ("name", {}).toString();
    p.description = root.getProperty ("description", {}).toString();
    p.isReference = (bool) root.getProperty ("isReference", false);

    if (p.id.isEmpty() || p.name.isEmpty())
        return std::nullopt;

    if (auto* obj = root.getProperty ("metrics", {}).getDynamicObject())
    {
        for (const auto& prop : obj->getProperties())
        {
            const auto& v = prop.value;
            if (! v.isObject())
                continue;

            MetricRange r;
            r.min = (float) v.getProperty ("min", 0.0);
            r.max = (float) v.getProperty ("max", 0.0);
            r.target = (float) v.getProperty ("target", (r.min + r.max) * 0.5);
            r.warn = (float) v.getProperty ("warn", 1.0);
            if (r.max < r.min)
                std::swap (r.min, r.max);
            p.metrics[prop.name.toString()] = r;
        }
    }

    if (const auto* curve = root.getProperty ("tonalCurve", {}).getArray(); curve != nullptr && curve->size() == kNumThirdOctaves)
    {
        std::array<float, kNumThirdOctaves> c {};
        for (int i = 0; i < kNumThirdOctaves; ++i)
            c[(size_t) i] = (float) (*curve)[i];
        p.tonalCurve = c;
    }
    p.tonalTolerance = (float) root.getProperty ("tonalTolerance", 2.0);

    if (const auto* widths = root.getProperty ("bandWidthPercent", {}).getArray(); widths != nullptr && widths->size() == kNumBands)
    {
        std::array<float, kNumBands> w {};
        for (int i = 0; i < kNumBands; ++i)
            w[(size_t) i] = (float) (*widths)[i];
        p.bandWidthPercent = w;
    }
    p.bandWidthTolerance = (float) root.getProperty ("bandWidthTolerance", 10.0);

    p.tonalCurveLoudest = readArray<kNumThirdOctaves> (root, "tonalCurveLoudest");
    p.bandTolerance = readArray<kNumBands> (root, "bandTolerance");
    p.bandWidthTolerances = readArray<kNumBands> (root, "bandWidthTolerances");
    p.bandBurstDb = readArray<kNumBands> (root, "bandBurstDb");
    p.sourceCount = (int) root.getProperty ("sourceCount", 0);
    if (const auto* names = root.getProperty ("sources", {}).getArray())
        for (const auto& n : *names)
            p.sources.add (n.toString());

    return p;
}

juce::String TargetProfile::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("id", id);
    root->setProperty ("name", name);
    root->setProperty ("description", description);
    root->setProperty ("isReference", isReference);

    auto* m = new juce::DynamicObject();
    for (const auto& [key, r] : metrics)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("min", round2 (r.min));
        o->setProperty ("max", round2 (r.max));
        o->setProperty ("target", round2 (r.target));
        o->setProperty ("warn", round2 (r.warn));
        m->setProperty (key, juce::var (o));
    }
    root->setProperty ("metrics", juce::var (m));

    if (tonalCurve)
    {
        juce::Array<juce::var> arr;
        for (auto v : *tonalCurve) arr.add (round2 (v));
        root->setProperty ("tonalCurve", arr);
    }
    root->setProperty ("tonalTolerance", tonalTolerance);

    if (bandWidthPercent)
    {
        juce::Array<juce::var> arr;
        for (auto v : *bandWidthPercent) arr.add (round2 (v));
        root->setProperty ("bandWidthPercent", arr);
    }
    root->setProperty ("bandWidthTolerance", bandWidthTolerance);

    writeArray (*root, "tonalCurveLoudest", tonalCurveLoudest);
    writeArray (*root, "bandTolerance", bandTolerance);
    writeArray (*root, "bandWidthTolerances", bandWidthTolerances);
    writeArray (*root, "bandBurstDb", bandBurstDb);
    if (sourceCount > 0)
    {
        root->setProperty ("sourceCount", sourceCount);
        juce::Array<juce::var> names;
        for (const auto& n : sources)
            names.add (n);
        root->setProperty ("sources", names);
    }

    return juce::JSON::toString (juce::var (root));
}

TargetProfile TargetProfile::fromSnapshot (const AnalysisSnapshot& s, const juce::String& name)
{
    TargetProfile p;
    p.id = "ref_" + juce::File::createLegalFileName (name).replaceCharacter (' ', '_').toLowerCase();
    p.name = name;
    p.description = "Generato dall'analisi del brano di riferimento";
    p.isReference = true;

    for (const auto& spec : metricSpecs())
        if (spec.valid (s))
        {
            const float value = spec.get (s);
            p.metrics[spec.key] = { value - spec.okTol, value + spec.okTol, value, spec.warnTol };
        }

    p.tonalCurve = s.thirdOctaveDb;
    p.tonalTolerance = 1.5f;
    p.bandWidthPercent = s.bandWidthPercent;
    p.bandWidthTolerance = 8.0f;

    if (s.loudestSectionSeconds > 0.0f)
        p.tonalCurveLoudest = s.thirdOctaveLoudestDb;

    std::array<float, kNumBands> burst {};
    for (int b = 0; b < kNumBands; ++b)
        burst[(size_t) b] = s.bandDynamics[(size_t) b].burstDb;
    p.bandBurstDb = burst;
    p.sourceCount = 1;
    return p;
}

TargetProfile TargetProfile::fromSnapshots (const std::vector<AnalysisSnapshot>& snapshots, const juce::String& name,
                                            const juce::StringArray& sourceNames)
{
    if (snapshots.size() == 1)
    {
        auto single = fromSnapshot (snapshots.front(), name);
        single.sources = sourceNames;
        return single;
    }

    TargetProfile p;
    p.id = "ref_" + juce::File::createLegalFileName (name).replaceCharacter (' ', '_').toLowerCase();
    p.name = name;
    p.description = "Generato dall'analisi di " + juce::String ((int) snapshots.size()) + " brani di riferimento";
    p.isReference = true;
    p.sourceCount = (int) snapshots.size();
    p.sources = sourceNames;

    if (snapshots.empty())
        return p;

    // metriche: media, con tolleranza pari alla dispersione tra i brani (mai sotto quella di un singolo reference)
    for (const auto& spec : metricSpecs())
    {
        std::vector<double> values;
        for (const auto& s : snapshots)
            if (spec.valid (s))
                values.push_back (spec.get (s));
        if (values.empty())
            continue;

        const auto [mean, deviation] = meanAndDeviation (values);
        const auto spread = (float) std::max (deviation, (double) spec.okTol);
        p.metrics[spec.key] = { (float) mean - spread, (float) mean + spread, (float) mean, std::max ((float) deviation, spec.warnTol) };
    }

    // curva tonale media e tolleranza per banda dalla dispersione dei brani attorno alla media
    std::array<float, kNumThirdOctaves> curve {}, curveLoudest {};
    int loudestCount = 0;
    for (const auto& s : snapshots)
    {
        for (int i = 0; i < kNumThirdOctaves; ++i)
            curve[(size_t) i] += s.thirdOctaveDb[(size_t) i] / (float) snapshots.size();
        if (s.loudestSectionSeconds > 0.0f)
        {
            for (int i = 0; i < kNumThirdOctaves; ++i)
                curveLoudest[(size_t) i] += s.thirdOctaveLoudestDb[(size_t) i];
            ++loudestCount;
        }
    }
    p.tonalCurve = curve;
    p.tonalTolerance = 1.5f;
    if (loudestCount > 0)
    {
        for (auto& v : curveLoudest) v /= (float) loudestCount;
        p.tonalCurveLoudest = curveLoudest;
    }

    TargetProfile unitTolerance;   // scala per banda (estremi più variabili), con tolleranza 1 dB
    unitTolerance.tonalTolerance = 1.0f;

    std::array<float, kNumBands> tolerance {}, width {}, widthTolerance {}, burst {};
    for (int b = 0; b < kNumBands; ++b)
    {
        std::vector<double> deltas, widths, bursts;
        for (const auto& s : snapshots)
        {
            deltas.push_back (bandTonalDelta (s.thirdOctaveDb, curve, b));
            widths.push_back (s.bandWidthPercent[(size_t) b]);
            bursts.push_back (s.bandDynamics[(size_t) b].burstDb);
        }
        const float scale = tonalToleranceForBand (unitTolerance, b);
        tolerance[(size_t) b] = (float) std::clamp (meanAndDeviation (deltas).second, (double) scale, 4.0 * scale);

        const auto [widthMean, widthDeviation] = meanAndDeviation (widths);
        width[(size_t) b] = (float) widthMean;
        widthTolerance[(size_t) b] = (float) std::clamp (widthDeviation, 6.0, 20.0);
        burst[(size_t) b] = (float) meanAndDeviation (bursts).first;
    }
    p.bandTolerance = tolerance;
    p.bandWidthPercent = width;
    p.bandWidthTolerance = 8.0f;
    p.bandWidthTolerances = widthTolerance;
    p.bandBurstDb = burst;
    return p;
}

//==============================================================================
ProfileLibrary::ProfileLibrary()
{
    reload();
}

juce::File ProfileLibrary::getUserProfileDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("MasterAgent")
               .getChildFile ("profiles");
}

void ProfileLibrary::reload()
{
    profiles.clear();

    for (int i = 0; i < ProfileData::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = ProfileData::getNamedResource (ProfileData::namedResourceList[i], size))
            if (auto p = TargetProfile::fromJson (juce::String::fromUTF8 (data, size)))
                profiles.push_back (std::move (*p));
    }

    const auto dir = getUserProfileDirectory();
    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            if (auto p = TargetProfile::fromJson (f.loadFileAsString()))
            {
                p->id = "user_" + p->id;
                p->name = p->name + " (utente)";
                profiles.push_back (std::move (*p));
            }
        }
    }
}

const TargetProfile* ProfileLibrary::findById (const juce::String& id) const
{
    for (const auto& p : profiles)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool ProfileLibrary::saveUserProfile (const TargetProfile& profile, juce::String& error)
{
    const auto dir = getUserProfileDirectory();
    if (! dir.createDirectory())
    {
        error = "Impossibile creare la cartella " + dir.getFullPathName();
        return false;
    }

    const auto file = dir.getChildFile (juce::File::createLegalFileName (profile.id) + ".json");
    if (! file.replaceWithText (profile.toJson()))
    {
        error = "Impossibile scrivere " + file.getFullPathName();
        return false;
    }

    reload();
    return true;
}

} // namespace ma
