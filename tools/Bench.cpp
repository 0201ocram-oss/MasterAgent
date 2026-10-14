#include "Bench.h"

#include "Analysis/ReferenceAnalyzer.h"
#include "Analysis/SnapshotIO.h"
#include "Analysis/Timeline.h"
#include "Compare/Comparator.h"
#include "Compare/TargetProfile.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdio>
#include <optional>

namespace
{
    using Buffer = juce::AudioBuffer<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<double>;

    /**
        Frequenza della risonanza di prova per il brano in corso: a metà tra due note (rispetto all'intonazione stimata)
        e il più lontano possibile dai picchi già presenti, altrimenti si fonde con una nota del brano e diventa una nota.
    */
    double resonanceFrequency = 452.9;

    double chooseResonanceFrequency (const ma::AnalysisSnapshot& original)
    {
        double best = 452.9, bestDistance = -1.0;
        for (int k = -12; k <= 12; ++k)
        {
            const double f = 440.0 * std::pow (2.0, (k + 0.5) / 12.0 + original.tuningCents / 1200.0);
            if (f < 250.0 || f > 1000.0)
                continue;
            double nearest = 1.0e9;
            for (const auto& r : original.resonances)
                nearest = std::min (nearest, std::abs (1200.0 * std::log2 (f / std::max (1.0f, r.frequency))));
            if (nearest > bestDistance)
            {
                bestDistance = nearest;
                best = f;
            }
        }
        return best;
    }

    /** Un difetto noto: nome, parametri (per il report) e come applicarlo all'audio. */
    struct Variant
    {
        juce::String name;
        std::vector<std::pair<juce::String, juce::var>> params;
        std::function<void (Buffer&, double)> apply;
        double minSeconds = 0.0;   // brani più corti: variante saltata
    };

    void filter (std::vector<double>& x, const Coefficients::Ptr& c)
    {
        juce::dsp::IIR::Filter<double> f (c);
        f.reset();
        for (auto& v : x)
            v = f.processSample (v);
    }

    void applyFilter (Buffer& b, const Coefficients::Ptr& c)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            juce::dsp::IIR::Filter<double> f (c);
            f.reset();
            auto* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                x[i] = (float) f.processSample ((double) x[i]);
        }
    }

    double rmsOf (const Buffer& b)
    {
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* x = b.getReadPointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                sum += (double) x[i] * x[i];
        }
        return std::sqrt (sum / std::max (1.0, (double) b.getNumSamples() * b.getNumChannels()));
    }

    double gainOf (double db) { return std::pow (10.0, db / 20.0); }

    /** Side (L-R)/2 moltiplicato per sideGain, Mid invariato. */
    void scaleSide (Buffer& b, double sideGain)
    {
        if (b.getNumChannels() < 2)
            return;
        auto* l = b.getWritePointer (0);
        auto* r = b.getWritePointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double m = 0.5 * (l[i] + r[i]), s = 0.5 * (l[i] - r[i]) * sideGain;
            l[i] = (float) (m + s);
            r[i] = (float) (m - s);
        }
    }

    void forEachSample (Buffer& b, const std::function<float (float, int)>& fn)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                x[i] = fn (x[i], ch);
        }
    }

    std::vector<Variant> makeVariants()
    {
        std::vector<Variant> v;
        v.push_back ({ "original", {}, [] (Buffer&, double) {} });

        // tonali: campana Q 1 al centro di ogni banda, +-3 dB (lieve) e +-6 dB (evidente)
        for (double f : { 40.0, 120.0, 350.0, 1000.0, 3000.0, 5000.0, 8500.0, 14000.0 })
            for (double g : { 3.0, -3.0, 6.0, -6.0 })
                v.push_back ({ "eq", { { "freq", f }, { "gain", g } },
                               [f, g] (Buffer& b, double sr) { applyFilter (b, Coefficients::makePeakFilter (sr, f, 1.0, gainOf (g))); } });

        // tilt: più brillante (+) o più scuro (-) attorno a 1 kHz
        for (double sign : { 1.0, -1.0 })
            v.push_back ({ sign > 0 ? "tilt+" : "tilt-", { { "db", 2.0 * sign } }, [sign] (Buffer& b, double sr)
            {
                applyFilter (b, Coefficients::makeLowShelf (sr, 400.0, 0.6, gainOf (-2.0 * sign)));
                applyFilter (b, Coefficients::makeHighShelf (sr, 2500.0, 0.6, gainOf (2.0 * sign)));
            } });

        // stereo
        v.push_back ({ "width+", { { "sideDb", 4.0 } }, [] (Buffer& b, double) { scaleSide (b, gainOf (4.0)); } });
        v.push_back ({ "width-", { { "sideDb", -6.0 } }, [] (Buffer& b, double) { scaleSide (b, gainOf (-6.0)); } });
        v.push_back ({ "lowSide", { { "db", -6.0 } }, [] (Buffer& b, double sr)
        {
            // bassi in controfase: metà del mono sotto 120 Hz (Butterworth del 4° ordine) aggiunta come side
            if (b.getNumChannels() < 2)
                return;
            std::vector<double> low ((size_t) b.getNumSamples());
            for (int i = 0; i < b.getNumSamples(); ++i)
                low[(size_t) i] = 0.5 * (b.getSample (0, i) + b.getSample (1, i));
            filter (low, Coefficients::makeLowPass (sr, 120.0, 0.5412));
            filter (low, Coefficients::makeLowPass (sr, 120.0, 1.3066));
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto d = (float) (0.5 * low[(size_t) i]);
                b.setSample (0, i, b.getSample (0, i) + d);
                b.setSample (1, i, b.getSample (1, i) - d);
            }
        } });
        v.push_back ({ "polarity", {}, [] (Buffer& b, double) { if (b.getNumChannels() > 1) b.applyGain (1, 0, b.getNumSamples(), -1.0f); } });
        for (double db : { 1.5, 3.0 })
            v.push_back ({ "balance", { { "db", db } }, [db] (Buffer& b, double)
            {
                if (b.getNumChannels() < 2)
                    return;
                b.applyGain (0, 0, b.getNumSamples(), (float) gainOf (0.5 * db));
                b.applyGain (1, 0, b.getNumSamples(), (float) gainOf (-0.5 * db));
            } });

        // dinamica e loudness
        v.push_back ({ "limit", { { "driveDb", 4.0 } }, [] (Buffer& b, double)
        {
            // 4 dB in più dentro un limiter morbido con ceiling a -1 dBFS: meno dinamica, niente clip
            const double drive = gainOf (4.0), ceiling = gainOf (-1.0);
            forEachSample (b, [=] (float x, int) { return (float) (ceiling * std::tanh (x * drive / ceiling)); });
        } });
        v.push_back ({ "clip", { { "driveDb", 3.0 } }, [] (Buffer& b, double)
        {
            const double drive = gainOf (3.0);
            forEachSample (b, [=] (float x, int) { return (float) juce::jlimit (-1.0, 1.0, x * drive); });
        } });
        v.push_back ({ "gain", { { "db", -6.0 } }, [] (Buffer& b, double) { b.applyGain ((float) gainOf (-6.0)); } });

        // tecnici
        v.push_back ({ "dc", { { "dbfs", -40.0 } }, [] (Buffer& b, double)
        {
            const auto dc = (float) gainOf (-40.0);
            forEachSample (b, [=] (float x, int) { return x + dc; });
        } });
        v.push_back ({ "mute", {}, [] (Buffer& b, double) { if (b.getNumChannels() > 1) b.clear (1, 0, b.getNumSamples()); } });
        v.push_back ({ "rumble", { { "freq", 15.0 } }, [] (Buffer& b, double sr)
        {
            // 15 Hz a circa -12 dB rispetto all'energia del brano: sopra la soglia di -15 dB
            const double amplitude = 0.35 * rmsOf (b), w = 2.0 * juce::MathConstants<double>::pi * 15.0 / sr;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, b.getSample (ch, i) + (float) (amplitude * std::sin (w * i)));
        } });
        v.push_back ({ "dropouts", { { "ms", 50.0 } }, [] (Buffer& b, double sr)
        {
            for (double fraction : { 0.25, 0.5, 0.75 })
            {
                const int start = (int) (fraction * b.getNumSamples());
                b.clear (start, std::min ((int) (0.05 * sr), b.getNumSamples() - start));
            }
        } });

        // localizzati: risonanza stretta fuori dalla griglia delle note, sibilanti a raffiche, eccesso in una sezione
        v.push_back ({ "resonance", { { "freq", 0.0 }, { "gain", 10.0 }, { "q", 20.0 } }, [] (Buffer& b, double sr)
        {
            // a metà tra due note, lontano dai picchi del brano (scelta dopo l'analisi dell'originale)
            applyFilter (b, Coefficients::makePeakFilter (sr, resonanceFrequency, 20.0, gainOf (10.0)));
        } });
        v.push_back ({ "sibilance", { { "lo", 6000.0 }, { "hi", 9000.0 } }, [] (Buffer& b, double sr)
        {
            // raffiche di rumore 6-9 kHz da 80 ms ogni 1.3 s, al livello RMS del brano
            const int n = b.getNumSamples();
            std::vector<double> noise ((size_t) n);
            juce::Random random (7);
            for (auto& x : noise)
                x = random.nextDouble() * 2.0 - 1.0;
            for (int k = 0; k < 2; ++k)
            {
                filter (noise, Coefficients::makeHighPass (sr, 6000.0, 0.7071));
                filter (noise, Coefficients::makeLowPass (sr, 9000.0, 0.7071));
            }
            double power = 0.0;
            for (auto x : noise)
                power += x * x;
            const double scale = rmsOf (b) / std::sqrt (std::max (1e-20, power / n));
            const int burst = (int) (0.08 * sr), fade = (int) (0.005 * sr), period = (int) (1.3 * sr);
            for (int start = (int) sr; start + burst < n; start += period)
                for (int i = 0; i < burst; ++i)
                {
                    const double env = std::min (1.0, std::min ((double) i, (double) (burst - 1 - i)) / fade);
                    const auto add = (float) (scale * env * noise[(size_t) (start + i)]);
                    for (int ch = 0; ch < b.getNumChannels(); ++ch)
                        b.setSample (ch, start + i, b.getSample (ch, start + i) + add);
                }
        } });
        v.push_back ({ "section", { { "freq", 300.0 }, { "gain", 8.0 }, { "start", 30.0 }, { "end", 60.0 } }, [] (Buffer& b, double sr)
        {
            // +8 dB a 300 Hz solo tra 30 e 60 s, con dissolvenze di mezzo secondo
            Buffer boosted;
            boosted.makeCopyOf (b);
            applyFilter (boosted, Coefficients::makePeakFilter (sr, 300.0, 1.0, gainOf (8.0)));
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double w = juce::jlimit (0.0, 1.0, std::min ((t - 29.75) / 0.5, (60.25 - t) / 0.5));
                if (w <= 0.0)
                    continue;
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    b.setSample (ch, i, (float) (b.getSample (ch, i) * (1.0 - w) + boosted.getSample (ch, i) * w));
            }
        }, 75.0 });

        return v;
    }

    juce::var toVar (float v)
    {
        return std::isfinite (v) ? juce::var ((double) v) : juce::var();
    }

    template <typename Container>
    juce::var toVarArray (const Container& values)
    {
        juce::Array<juce::var> a;
        for (auto v : values)
            a.add (toVar ((float) v));
        return a;
    }

    /** Diagnosi (da "info" in su), mosse EQ, scostamenti di banda e, se c'è la cronologia, i segni sulla forma d'onda. */
    juce::var resultToVar (const ma::ComparisonResult& result, const ma::TargetProfile& profile, const ma::TrackTimeline* timeline)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty ("score", result.score);

        juce::Array<juce::var> findings;
        for (const auto& f : result.findings)
        {
            if (f.severity < ma::Severity::info)
                continue;
            auto* o = new juce::DynamicObject();
            o->setProperty ("key", f.key);
            o->setProperty ("category", f.category);
            o->setProperty ("metric", f.metric);
            o->setProperty ("severity", (int) f.severity);
            o->setProperty ("delta", toVar (f.delta));
            if (f.hasTime())
            {
                o->setProperty ("timeStart", toVar (f.timeStart));
                o->setProperty ("timeEnd", toVar (f.timeEnd));
            }
            if (f.key.startsWith ("band:"))
                o->setProperty ("action", f.action);   // costante, a raffiche o in una sezione
            findings.add (juce::var (o));
        }
        r->setProperty ("findings", findings);

        juce::Array<juce::var> moves;
        for (const auto& m : result.eqMoves)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("type", m.typeName());
            o->setProperty ("frequency", toVar (m.frequency));
            o->setProperty ("gainDb", toVar (m.gainDb));
            o->setProperty ("q", toVar (m.q));
            o->setProperty ("bands", toVarArray (m.bands));
            moves.add (juce::var (o));
        }
        r->setProperty ("eqMoves", moves);
        r->setProperty ("bandTonalDelta", toVarArray (result.bandTonalDelta));
        r->setProperty ("bandTolerance", toVarArray (result.bandTolerance));

        if (timeline != nullptr)
        {
            ma::MarkerSettings settings;
            if (const auto* tp = profile.getMetric (ma::metric::truePeakMax))
                settings.ceilingDb = tp->max;
            if (const auto* psr = profile.getMetric (ma::metric::minPsr))
                settings.minPsr = psr->min;
            juce::Array<juce::var> markers;
            for (const auto& mk : ma::findMarkers (*timeline, settings))
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("type", (int) mk.type);
                o->setProperty ("start", mk.start);
                o->setProperty ("end", mk.end);
                markers.add (juce::var (o));
            }
            r->setProperty ("markers", markers);
        }
        return juce::var (r);
    }

    /** Le misure che servono a leggere i risultati del banco. */
    juce::var metricsToVar (const ma::AnalysisSnapshot& s)
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("integratedLufs", toVar (s.integratedLufs));
        m->setProperty ("truePeakMaxDb", toVar (s.truePeakMaxDb));
        m->setProperty ("plr", toVar (s.plr));
        m->setProperty ("minPsr", toVar (s.minPsr));
        m->setProperty ("drValue", s.drValid ? toVar (s.drValue) : juce::var());
        m->setProperty ("widthPercent", toVar (s.widthPercent));
        m->setProperty ("lowEndWidthPercent", toVar (s.lowEndWidthPercent));
        m->setProperty ("lowEndCorrelation", toVar (s.lowEndCorrelation));
        m->setProperty ("correlation", toVar (s.correlation));
        m->setProperty ("balanceDb", toVar (s.balanceDb));
        m->setProperty ("spectralTilt", toVar (s.spectralTiltDbPerOct));
        m->setProperty ("subRumbleDb", toVar (s.subRumbleDb));
        m->setProperty ("clipEventsFullScale", s.clipEventsFullScale);
        m->setProperty ("dcOffsetDb", toVarArray (s.dcOffsetDb));
        juce::Array<juce::var> burst, resonances;
        for (const auto& d : s.bandDynamics)
            burst.add (toVar (d.burstDb));
        for (const auto& r : s.resonances)
            resonances.add (juce::Array<juce::var> { toVar (r.frequency), toVar (r.excessDb), r.musical });
        m->setProperty ("bandBurstDb", burst);
        m->setProperty ("resonances", resonances);
        return juce::var (m);
    }

    std::optional<ma::AnalysisSnapshot> loadSnapshotFile (const juce::File& file)
    {
        juce::MemoryBlock data;
        ma::AnalysisSnapshot s;
        if (file.loadFileAsData (data) && ma::loadSnapshot (data.getData(), data.getSize(), s))
            return s;
        return std::nullopt;
    }
}

int listBenchVariants()
{
    for (const auto& v : makeVariants())
    {
        juce::String text (v.name);
        for (const auto& [name, value] : v.params)
            text << "  " << name << "=" << value.toString();
        std::printf ("%s\n", text.toRawUTF8());
    }
    return 0;
}

int runBench (int argc, char* argv[])
{
    if (argc < 4)
    {
        std::printf ("Uso: MasterAgentAnalyze --bench <jobs.json> <cartella risultati>\n");
        return 1;
    }

    const juce::File jobsFile (juce::String::fromUTF8 (argv[2])), outDir (juce::String::fromUTF8 (argv[3]));
    outDir.createDirectory();
    const auto jobs = juce::JSON::parse (jobsFile);
    if (! jobs.isArray())
    {
        std::printf ("File dei job non valido: %s\n", jobsFile.getFullPathName().toRawUTF8());
        return 1;
    }

    ma::ProfileLibrary lib;
    const auto variants = makeVariants();

    for (const auto& job : *jobs.getArray())
    {
        const auto id = job["id"].toString();
        const auto out = outDir.getChildFile (id + ".json");
        if (out.existsAsFile())
            continue;   // già fatto: il banco riprende da dove si era fermato

        const juce::File file (job["file"].toString());

        // profilo del genere: quello calcolato senza l'album del brano (validazione incrociata) o quello incluso
        std::optional<ma::TargetProfile> genre;
        if (const auto path = job["genreProfile"].toString(); path.isNotEmpty())
            genre = ma::TargetProfile::fromJson (juce::File (path).loadFileAsString());
        if (! genre)
            if (const auto* p = lib.findById (job["genre"].toString()))
                genre = *p;
        // master da CD: il genere con la consegna "Master forte", come nell'app
        if (const auto* loud = lib.find (ma::targetIds::loud, ma::ProfileKind::delivery); genre && loud != nullptr)
            genre = ma::composeTarget (&*genre, *loud);

        // profilo dagli altri brani dello stesso album
        std::vector<ma::AnalysisSnapshot> albumSnapshots;
        if (const auto* paths = job["albumSnapshots"].getArray())
            for (const auto& p : *paths)
                if (auto s = loadSnapshotFile (juce::File (p.toString())))
                    albumSnapshots.push_back (std::move (*s));
        std::optional<ma::TargetProfile> album;
        if (albumSnapshots.size() >= 2)
            album = ma::TargetProfile::fromSnapshots (albumSnapshots, "Album");

        Buffer audio;
        double rate = 0.0;
        juce::String error;
        if (! ma::readAudioFile (file, audio, rate, error))
        {
            std::printf ("Saltato %s: %s\n", file.getFileName().toRawUTF8(), error.toRawUTF8());
            continue;
        }
        const double seconds = audio.getNumSamples() / rate;
        const bool allVariants = job["variants"].toString() != "original";

        ma::AnalysisSnapshot original;
        juce::Array<juce::var> entries;
        for (const auto& v : variants)
        {
            if ((! allVariants && v.name != "original") || seconds < v.minSeconds)
                continue;

            Buffer degraded;
            degraded.makeCopyOf (audio);
            v.apply (degraded, rate);

            ma::AnalysisSnapshot s;
            ma::TrackTimeline timeline;
            if (! ma::analyseBuffer (degraded, rate, s, {}, {}, &timeline))
                continue;
            if (v.name == "original")
            {
                original = s;
                resonanceFrequency = chooseResonanceFrequency (original);
            }

            auto* params = new juce::DynamicObject();
            for (const auto& [name, value] : v.params)
                params->setProperty (name, value);
            if (v.name == "resonance")
                params->setProperty ("freq", resonanceFrequency);

            auto* results = new juce::DynamicObject();
            if (genre)
                results->setProperty ("genre", resultToVar (ma::compare (s, *genre), *genre, &timeline));
            if (album)
                results->setProperty ("album", resultToVar (ma::compare (s, *album), *album, nullptr));
            if (v.name != "original" && original.valid)
            {
                // l'originale come reference: quanto il confronto vede il difetto quando il target è perfetto
                ma::CompareOptions options;
                options.reference = &original;
                const auto reference = ma::TargetProfile::fromSnapshot (original, "Originale");
                results->setProperty ("reference", resultToVar (ma::compare (s, reference, options), reference, nullptr));
            }

            auto* entry = new juce::DynamicObject();
            entry->setProperty ("variant", v.name);
            entry->setProperty ("params", juce::var (params));
            entry->setProperty ("metrics", metricsToVar (s));
            entry->setProperty ("results", juce::var (results));
            entries.add (juce::var (entry));
        }

        auto* root = new juce::DynamicObject();
        root->setProperty ("id", id);
        root->setProperty ("file", file.getFullPathName());
        root->setProperty ("genre", job["genre"]);
        root->setProperty ("album", job["album"]);
        root->setProperty ("seconds", seconds);
        root->setProperty ("sampleRate", rate);
        root->setProperty ("variants", entries);
        out.replaceWithText (juce::JSON::toString (juce::var (root), true));
        std::printf ("%s  %s  %d varianti\n", id.toRawUTF8(), file.getFileName().toRawUTF8(), entries.size());
        std::fflush (stdout);
    }
    return 0;
}
