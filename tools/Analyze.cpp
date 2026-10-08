// Analizza file audio con lo stesso motore del plugin e stampa bilanciamento tonale, mosse EQ e diagnosi.
// Uso:
//   MasterAgentAnalyze <file audio> [<file audio> ...]
//       variabili d'ambiente: MA_PROFILE (id del profilo, default pop), MA_PHASE (mix | master, default master)
//   MasterAgentAnalyze --make-profile "<nome>" <out.json> <file audio> <file audio> [...]
//       crea un profilo da più brani di riferimento (target = media, tolleranze = dispersione tra i brani)
//   MasterAgentAnalyze --album <file audio> <file audio> [...]
//       coerenza album: ogni brano confrontato con la mediana dell'album (stampa il report testuale)
// Utile per calibrare i profili su master commerciali e per provare i consigli su un mix.

#include "Analysis/ReferenceAnalyzer.h"
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

int main (int argc, char* argv[])
{
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
