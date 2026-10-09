// Analizza file audio con lo stesso motore del plugin e stampa bilanciamento tonale, mosse EQ e diagnosi.
// Uso:
//   MasterAgentAnalyze <file audio> [<file audio> ...]
//       variabili d'ambiente: MA_PROFILE (id del profilo, default pop), MA_PHASE (mix | master, default master)
//   MasterAgentAnalyze --make-profile "<nome>" <out.json> <file audio> <file audio> [...]
//       crea un profilo da più brani di riferimento (target = media, tolleranze = dispersione tra i brani)
//   MasterAgentAnalyze --album <file audio> <file audio> [...]
//       coerenza album: ogni brano confrontato con la mediana dell'album (stampa il report testuale)
//   MasterAgentAnalyze --verify <cartella> <file audio> [...]
//       per tools/verify_analysis.py: salva l'audio decodificato (WAV float) e misure, cronologia, segni
//       e diagnosi (tutti i profili, fase mix e master) in JSON, da confrontare con un calcolo indipendente
//       (MA_VERIFY_NO_WAV=1 salva solo il JSON)
// Utile per calibrare i profili su master commerciali e per provare i consigli su un mix.

#include "Analysis/ReferenceAnalyzer.h"
#include "Analysis/Timeline.h"
#include "Compare/AlbumCheck.h"
#include "Compare/Comparator.h"

#include <cstdio>

namespace
{
    bool analyseFile (const juce::File& file, ma::AnalysisSnapshot& s)
    {
        juce::AudioBuffer<float> audio;
        double rate = 0.0;
        juce::String error;
        if (! ma::readAudioFile (file, audio, rate, error))
        {
            std::printf ("Impossibile leggere %s: %s\n", file.getFullPathName().toRawUTF8(), error.toRawUTF8());
            return false;
        }
        return ma::analyseBuffer (audio, rate, s);
    }

    int makeProfile (int argc, char* argv[])
    {
        if (argc < 6)
        {
            std::printf ("Uso: MasterAgentAnalyze --make-profile \"<nome>\" <out.json> <file> <file> [...]\n");
            return 1;
        }

        const auto name = juce::String::fromUTF8 (argv[2]);
        const juce::File out (juce::String::fromUTF8 (argv[3]));
        std::vector<ma::AnalysisSnapshot> snapshots;
        juce::StringArray names;

        for (int a = 4; a < argc; ++a)
        {
            const juce::File file (juce::String::fromUTF8 (argv[a]));
            ma::AnalysisSnapshot s;
            if (analyseFile (file, s))
            {
                std::printf ("%-40s %6.1f LUFS  tilt %5.2f\n", file.getFileName().toRawUTF8(), s.integratedLufs, s.spectralTiltDbPerOct);
                snapshots.push_back (std::move (s));
                names.add (file.getFileName());
            }
        }

        if (snapshots.size() < 2)
        {
            std::printf ("Servono almeno 2 brani analizzati.\n");
            return 1;
        }

        const auto profile = ma::TargetProfile::fromSnapshots (snapshots, name, names);
        if (! out.replaceWithText (profile.toJson()))
        {
            std::printf ("Impossibile scrivere %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }

        std::printf ("\nTolleranza per banda (dB):");
        for (int b = 0; b < ma::kNumBands; ++b)
            std::printf (" %s %.1f", ma::kBandNames[(size_t) b], (*profile.bandTolerance)[(size_t) b]);
        std::printf ("\nProfilo salvato in %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    int checkAlbum (int argc, char* argv[])
    {
        if (argc < 4)
        {
            std::printf ("Uso: MasterAgentAnalyze --album <file> <file> [...]\n");
            return 1;
        }

        std::vector<ma::AnalysisSnapshot> snapshots;
        juce::StringArray names;
        for (int a = 2; a < argc; ++a)
        {
            const juce::File file (juce::String::fromUTF8 (argv[a]));
            ma::AnalysisSnapshot s;
            juce::String error;
            // lettura a blocchi: un album intero non sta comodamente in memoria
            if (! ma::analyseFile (file, s, error))
            {
                std::printf ("Saltato %s: %s\n", file.getFileName().toRawUTF8(), error.toRawUTF8());
                continue;
            }
            std::printf ("%-40s %6.1f LUFS  forte %6.1f LUFS\n", file.getFileName().toRawUTF8(), s.integratedLufs, s.maxShortTermLufs);
            snapshots.push_back (std::move (s));
            names.add (file.getFileName());
        }

        const auto album = ma::checkAlbum (snapshots, names);
        if (! album.isValid())
        {
            std::printf ("Servono almeno 2 brani analizzati.\n");
            return 1;
        }

        std::printf ("\n%s", ma::buildAlbumTextReport (album).toRawUTF8());
        return album.critical > 0 ? 2 : 0;
    }
}

namespace
{
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

    juce::var findingsToVar (const ma::ComparisonResult& result)
    {
        juce::Array<juce::var> list;
        for (const auto& f : result.findings)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("category", f.category);
            o->setProperty ("metric", f.metric);
            o->setProperty ("value", f.value);
            o->setProperty ("target", f.target);
            o->setProperty ("delta", toVar (f.delta));
            o->setProperty ("severity", (int) f.severity);
            o->setProperty ("message", f.message);
            o->setProperty ("action", f.action);
            o->setProperty ("key", f.key);
            o->setProperty ("id", f.id);
            o->setProperty ("timeStart", toVar (f.timeStart));
            o->setProperty ("timeEnd", toVar (f.timeEnd));
            list.add (juce::var (o));
        }
        return list;
    }

    juce::var markersToVar (const std::vector<ma::Marker>& markers)
    {
        juce::Array<juce::var> list;
        for (const auto& mk : markers)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("type", (int) mk.type);
            o->setProperty ("start", mk.start);
            o->setProperty ("end", mk.end);
            o->setProperty ("value", toVar (mk.value));
            o->setProperty ("channel", mk.channel);
            o->setProperty ("text", ma::describeMarker (mk));
            list.add (juce::var (o));
        }
        return list;
    }

    bool writeFloatWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, (unsigned int) audio.getNumChannels(),
                                                                              32, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();   // ora appartiene al writer
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    int verify (int argc, char* argv[])
    {
        if (argc < 4)
        {
            std::printf ("Uso: MasterAgentAnalyze --verify <cartella> <file> [...]\n");
            return 1;
        }

        const juce::File outDir (juce::String::fromUTF8 (argv[2]));
        outDir.createDirectory();
        ma::ProfileLibrary lib;
        // MA_VERIFY_NO_WAV=1: solo JSON (studi su molti brani, dove il WAV float occuperebbe decine di GB)
        const bool noWav = juce::SystemStats::getEnvironmentVariable ("MA_VERIFY_NO_WAV", {}).isNotEmpty();

        for (int a = 3; a < argc; ++a)
        {
            const juce::File file (juce::String::fromUTF8 (argv[a]));
            const auto stem = juce::String (a - 2).paddedLeft ('0', 2);

            // stessi campioni per il calcolo indipendente (il decoder MP3 è quello di JUCE)
            juce::AudioBuffer<float> audio;
            double rate = 0.0;
            juce::String error;
            ma::AnalysisSnapshot s;
            ma::TrackTimeline timeline;
            if (! ma::readAudioFile (file, audio, rate, error)
                || ! ma::analyseFile (file, s, error, {}, {}, 20.0 * 60.0, &timeline))
            {
                std::printf ("Saltato %s: %s\n", file.getFileName().toRawUTF8(), error.toRawUTF8());
                continue;
            }
            if (! noWav && ! writeFloatWav (outDir.getChildFile (stem + ".wav"), audio, rate))
            {
                std::printf ("Impossibile scrivere il WAV in %s\n", outDir.getFullPathName().toRawUTF8());
                return 1;
            }

            auto* m = new juce::DynamicObject();
            m->setProperty ("integratedLufs", toVar (s.integratedLufs));
            m->setProperty ("loudnessRange", toVar (s.loudnessRange));
            m->setProperty ("maxMomentaryLufs", toVar (s.maxMomentaryLufs));
            m->setProperty ("maxShortTermLufs", toVar (s.maxShortTermLufs));
            m->setProperty ("rmsDb", toVar (s.rmsDb));
            m->setProperty ("samplePeakDb", toVarArray (s.samplePeakDb));
            m->setProperty ("truePeakDb", toVarArray (s.truePeakDb));
            m->setProperty ("truePeakMaxDb", toVar (s.truePeakMaxDb));
            m->setProperty ("overs1dB", s.overs1dB);
            m->setProperty ("overs0dB", s.overs0dB);
            m->setProperty ("clipEvents", s.clipEvents);
            m->setProperty ("clipEventsFullScale", s.clipEventsFullScale);
            m->setProperty ("longCeilingClips", s.longCeilingClips);
            m->setProperty ("ceilingClipDb", toVar (s.ceilingClipDb));
            m->setProperty ("crestDb", toVar (s.crestDb));
            m->setProperty ("plr", toVar (s.plr));
            m->setProperty ("minPsr", toVar (s.minPsr));
            m->setProperty ("drValue", toVar (s.drValue));
            m->setProperty ("drValid", s.drValid);
            m->setProperty ("bandCrestDb", toVarArray (s.bandCrestDb));
            m->setProperty ("thirdOctaveDb", toVarArray (s.thirdOctaveDb));
            m->setProperty ("bandEnergyDb", toVarArray (s.bandEnergyDb));
            m->setProperty ("spectralTilt", toVar (s.spectralTiltDbPerOct));
            m->setProperty ("spectralCentroidHz", toVar (s.spectralCentroidHz));
            m->setProperty ("subRumbleDb", toVar (s.subRumbleDb));
            m->setProperty ("correlation", toVar (s.correlation));
            m->setProperty ("widthPercent", toVar (s.widthPercent));
            m->setProperty ("balanceDb", toVar (s.balanceDb));
            m->setProperty ("monoLossDb", toVar (s.monoLossDb));
            m->setProperty ("lowEndWidthPercent", toVar (s.lowEndWidthPercent));
            m->setProperty ("lowEndCorrelation", toVar (s.lowEndCorrelation));
            m->setProperty ("dcOffsetDb", toVarArray (s.dcOffsetDb));
            m->setProperty ("hasDigitalSilence", s.hasDigitalSilence);
            m->setProperty ("keyPitchClass", s.keyPitchClass);

            auto* t = new juce::DynamicObject();
            t->setProperty ("shortTermLufs", toVarArray (timeline.shortTermLufs));
            t->setProperty ("truePeakDb", toVarArray (timeline.truePeakDb));
            t->setProperty ("levelDb", toVarArray (timeline.levelDb));
            t->setProperty ("correlation", toVarArray (timeline.correlation));
            juce::Array<juce::var> clips, dropouts;
            for (const auto& c : timeline.clips)
                clips.add (juce::Array<juce::var> { (juce::int64) c.start, (juce::int64) c.end, c.channel });
            for (const auto& d : timeline.dropouts)
                dropouts.add (juce::Array<juce::var> { (juce::int64) d.start, (juce::int64) d.end, d.channel });
            t->setProperty ("clips", clips);
            t->setProperty ("dropouts", dropouts);

            // per ogni profilo, fase mix e master: diagnosi, voto e segni con le soglie usate dall'editor
            auto* perProfile = new juce::DynamicObject();
            for (const auto& p : lib.getProfiles())
            {
                auto* entry = new juce::DynamicObject();
                for (auto phase : { ma::WorkPhase::mix, ma::WorkPhase::master })
                {
                    ma::CompareOptions options;
                    options.phase = phase;
                    const auto result = ma::compare (s, p, options);

                    ma::MarkerSettings settings;
                    if (phase == ma::WorkPhase::master)
                    {
                        if (const auto* tp = p.getMetric (ma::metric::truePeakMax)) settings.ceilingDb = tp->max;
                        if (const auto* psr = p.getMetric (ma::metric::minPsr)) settings.minPsr = psr->min;
                    }

                    juce::Array<juce::var> eq;
                    for (const auto& mv : result.eqMoves)
                    {
                        auto* o = new juce::DynamicObject();
                        o->setProperty ("text", mv.describe (mv.shownGainDb));
                        o->setProperty ("frequency", toVar (mv.frequency));
                        o->setProperty ("gainDb", toVar (mv.gainDb));
                        o->setProperty ("shownGainDb", toVar (mv.shownGainDb));
                        o->setProperty ("bands", toVarArray (mv.bands));
                        o->setProperty ("fixInMix", mv.fixInMix);
                        eq.add (juce::var (o));
                    }

                    auto* r = new juce::DynamicObject();
                    r->setProperty ("score", result.score);
                    r->setProperty ("findings", findingsToVar (result));
                    r->setProperty ("eqMoves", eq);
                    r->setProperty ("bandTonalDelta", toVarArray (result.bandTonalDelta));
                    r->setProperty ("markers", markersToVar (ma::findMarkers (timeline, settings)));
                    r->setProperty ("ceilingDb", toVar (settings.ceilingDb));
                    r->setProperty ("minPsrSetting", toVar (settings.minPsr));
                    entry->setProperty (phase == ma::WorkPhase::mix ? "mix" : "master", juce::var (r));
                }
                perProfile->setProperty (p.id, juce::var (entry));
            }

            auto* root = new juce::DynamicObject();
            root->setProperty ("file", file.getFullPathName());
            root->setProperty ("sampleRate", rate);
            root->setProperty ("channels", audio.getNumChannels());
            root->setProperty ("seconds", s.secondsAnalyzed);
            root->setProperty ("metrics", juce::var (m));
            root->setProperty ("timeline", juce::var (t));
            root->setProperty ("profiles", juce::var (perProfile));

            outDir.getChildFile (stem + ".json").replaceWithText (juce::JSON::toString (juce::var (root)));
            std::printf ("%s  %s  %.1f LUFS\n", stem.toRawUTF8(), file.getFileName().toRawUTF8(), s.integratedLufs);
        }
        return 0;
    }
}

int main (int argc, char* argv[])
{
    if (argc > 1 && juce::String (argv[1]) == "--verify")
        return verify (argc, argv);
    if (argc > 1 && juce::String (argv[1]) == "--make-profile")
        return makeProfile (argc, argv);
    if (argc > 1 && juce::String (argv[1]) == "--album")
        return checkAlbum (argc, argv);

    ma::ProfileLibrary lib;
    const auto profileId = juce::SystemStats::getEnvironmentVariable ("MA_PROFILE", "pop");
    const auto* profile = lib.findById (profileId);

    // MA_PROFILE_FILE: profilo da un file JSON (es. creato con --make-profile), senza installarlo
    std::optional<ma::TargetProfile> fileProfile;
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("MA_PROFILE_FILE", {}); path.isNotEmpty())
    {
        fileProfile = ma::TargetProfile::fromJson (juce::File (path).loadFileAsString());
        profile = fileProfile ? &*fileProfile : nullptr;
    }

    if (profile == nullptr)
    {
        std::printf ("Profilo non trovato.\n");
        return 1;
    }

    ma::CompareOptions options;
    options.phase = juce::SystemStats::getEnvironmentVariable ("MA_PHASE", "master").equalsIgnoreCase ("mix") ? ma::WorkPhase::mix
                                                                                                           : ma::WorkPhase::master;

    for (int a = 1; a < argc; ++a)
    {
        const juce::File file (juce::String::fromUTF8 (argv[a]));
        ma::AnalysisSnapshot s;
        if (! analyseFile (file, s))
            continue;

        std::printf ("\n=== %s ===\n", file.getFileName().toRawUTF8());
        std::printf ("Integrated %.1f LUFS  TP %.2f dBTP  LRA %.1f  PLR %.1f  tilt %.2f dB/oct  sub<30 %.1f dB\n",
                     s.integratedLufs, s.truePeakMaxDb, s.loudnessRange, s.plr, s.spectralTiltDbPerOct, s.subRumbleDb);

        std::printf ("\nTerzi d'ottava (normalizzati):\n");
        for (int i = 0; i < ma::kNumThirdOctaves; ++i)
            std::printf ("%6s %6.1f%s", ma::kThirdOctaveLabels[(size_t) i], s.thirdOctaveDb[(size_t) i], i % 6 == 5 ? "\n" : "  ");

        std::printf ("\n\nScostamento per banda (master - target, dB):\n%-18s", "profilo");
        for (auto* name : ma::kBandNames) std::printf ("%9s", name);
        std::printf ("\n");
        for (const auto& p : lib.getProfiles())
        {
            if (! p.tonalCurve) continue;
            std::printf ("%-18s", p.id.substring (0, 17).toRawUTF8());
            for (int b = 0; b < ma::kNumBands; ++b)
                std::printf ("%9.1f", ma::bandTonalDelta (s.thirdOctaveDb, *p.tonalCurve, b));
            std::printf ("\n");
        }

        std::printf ("\nNel tempo (raffiche dB / zona più carica):\n");
        for (int b = 0; b < ma::kNumBands; ++b)
        {
            const auto& d = s.bandDynamics[(size_t) b];
            std::printf ("  %-10s %5.1f dB", ma::kBandNames[(size_t) b], d.burstDb);
            if (d.worstStartSec >= 0.0f)
                std::printf ("   %s-%s (+%.1f dB)", ma::formatSongTime (d.worstStartSec).toRawUTF8(),
                             ma::formatSongTime (d.worstEndSec).toRawUTF8(), d.worstExcessDb);
            std::printf ("\n");
        }
        std::printf ("Sezione più forte: %.0f s\n", s.loudestSectionSeconds);

        const auto result = ma::compare (s, *profile, options);
        std::printf ("\nMosse EQ (%s, fase %s):\n", profile->name.toRawUTF8(), ma::phaseName (options.phase).toRawUTF8());
        for (const auto& m : result.eqMoves)
            std::printf ("  %s   (stimato %+.1f dB)%s\n", m.describe (m.shownGainDb).toRawUTF8(), m.gainDb, m.fixInMix ? "  il resto nel mix" : "");

        std::printf ("\nDiagnosi:\n");
        for (const auto& f : result.findings)
            if (f.severity >= ma::Severity::warning || f.key.startsWith ("resonance"))
                std::printf ("- [%s] %s: %s\n  %s\n%s%s%s", ma::severityName (f.severity).toRawUTF8(), f.metric.toRawUTF8(),
                             f.value.toRawUTF8(), f.message.toRawUTF8(),
                             f.action.isNotEmpty() ? "  -> " : "", f.action.toRawUTF8(), f.action.isNotEmpty() ? "\n" : "");
    }
    return 0;
}
