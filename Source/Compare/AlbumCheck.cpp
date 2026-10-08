#include "AlbumCheck.h"
#include "../Common/Text.h"

#include <algorithm>

namespace ma
{

namespace
{
    float median (std::vector<float> values)
    {
        if (values.empty())
            return 0.0f;
        std::sort (values.begin(), values.end());
        const size_t n = values.size();
        return n % 2 == 1 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
    }

    juce::String num (float v, int decimals = 1)
    {
        return v <= kSilenceDb + 0.5f ? juce::String ("-inf") : juce::String (v, decimals);
    }

    juce::String signedNum (float v, int decimals = 1)
    {
        const auto rounded = juce::String (v, decimals);
        return (rounded.getFloatValue() > 0.0f ? "+" : "") + rounded;
    }

    juce::String capitalisedFirst (const juce::String& text)
    {
        return text.substring (0, 1).toUpperCase() + text.substring (1);
    }

    /** "Presence" oppure "Presence - Air" */
    juce::String bandSpan (int first, int last)
    {
        return first == last ? juce::String (kBandNames[(size_t) first])
                             : juce::String (kBandNames[(size_t) first]) + " - " + kBandNames[(size_t) last];
    }

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** Come suona il brano rispetto agli altri quando una zona di frequenze è sopra (up) o sotto. */
    juce::String tonalCharacter (int centreBand, bool up)
    {
        if (centreBand <= 1) return up ? "più bassi degli altri brani: suona più pesante e rimbombante"_t
                                       : "meno bassi degli altri brani: suona più leggero e sottile"_t;
        if (centreBand <= 3) return up ? "medi più pieni degli altri brani: suona più chiuso, \"in scatola\""_t
                                       : "medi più scavati degli altri brani: suona più vuoto"_t;
        if (centreBand <= 5) return up ? "più presente e aggressivo degli altri brani"_t
                                       : "più distante e morbido degli altri brani"_t;
        return up ? "più brillante degli altri brani"_t : "più scuro degli altri brani"_t;
    }

    void addTonalFindings (AlbumTrack& t)
    {
        // una diagnosi per ogni gruppo di bande vicine spostate nella stessa direzione: una sola mossa EQ le corregge
        int b = 0;
        while (b < kNumBands)
        {
            if (t.bandSeverity[(size_t) b] < Severity::warning)
            {
                ++b;
                continue;
            }

            const bool up = t.bandDelta[(size_t) b] > 0.0f;
            int last = b;
            while (last + 1 < kNumBands && t.bandSeverity[(size_t) last + 1] >= Severity::warning
                   && (t.bandDelta[(size_t) last + 1] > 0.0f) == up)
                ++last;

            float sum = 0.0f, tolerance = 0.0f;
            auto severity = Severity::warning;
            for (int i = b; i <= last; ++i)
            {
                sum += t.bandDelta[(size_t) i];
                tolerance += t.bandTolerance[(size_t) i];
                severity = std::max (severity, t.bandSeverity[(size_t) i]);
            }
            const float mean = sum / (float) (last - b + 1);
            tolerance /= (float) (last - b + 1);   // tolleranza media del gruppo, confrontabile con lo scarto medio
            // a passi di 0.5 dB; oltre maxEqDb la differenza va risolta nel mix, non con l'EQ di mastering
            const float wanted = -std::round (mean * 2.0f) / 2.0f;
            const float gain = juce::jlimit (-album::maxEqDb, album::maxEqDb, wanted);

            juce::String action;
            if (b == 0)
                action = "Shelf basso sotto %1 di circa %2 dB per riportarlo in linea con l'album"_t
                             .replace ("%1", hzText (kBandEdges[(size_t) last + 1])).replace ("%2", signedNum (gain));
            else if (last == kNumBands - 1)
                action = "Shelf alto da %1 di circa %2 dB per riportarlo in linea con l'album"_t
                             .replace ("%1", hzText (kBandEdges[(size_t) b])).replace ("%2", signedNum (gain));
            else
                action = "Campana larga (Q 0.7) a %1 di circa %2 dB per riportarlo in linea con l'album"_t
                             .replace ("%1", hzText (std::sqrt (kBandEdges[(size_t) b] * kBandEdges[(size_t) last + 1])))
                             .replace ("%2", signedNum (gain));
            if (std::abs (wanted) > album::maxEqDb)
                action << "; " << "il resto nel mix: %1 dB sono troppi per il solo mastering"_t.replace ("%1", juce::String (std::abs (wanted), 1));

            Finding f;
            f.category = "Tonale"_t;
            f.metric = "Bilanciamento"_t + " " + bandSpan (b, last);
            f.value = signedNum (mean) + " dB";
            f.target = "album"_t + " " + juce::String::charToString (0x00B1) + juce::String (tolerance, 1) + " dB";
            f.delta = mean;
            f.severity = severity;
            f.message = capitalisedFirst (tonalCharacter ((b + last + 1) / 2, up)) + ". "
                      + "In sequenza il cambio di colore tra un brano e l'altro si sente subito."_t;
            f.action = action;
            f.key = highlight::band (b);
            f.stage = stage::tonal;
            f.id = "album|band:" + juce::String (b) + "-" + juce::String (last);
            f.area = Area::tonal;
            t.findings.push_back (std::move (f));

            b = last + 1;
        }
    }

    void addLoudnessFinding (AlbumTrack& t, float medianLoudest)
    {
        if (t.loudness == Severity::ok)
            return;

        const bool louder = t.loudestDeltaLu > 0.0f;
        const float amount = std::abs (t.gainDb);

        Finding f;
        f.category = "Loudness"_t;
        f.metric = "Sezione più forte"_t;
        f.value = num (t.loudestLufs) + " LUFS  (" + signedNum (t.loudestDeltaLu) + " LU)";
        f.target = "mediana album"_t + " " + num (medianLoudest) + " LUFS " + juce::String::charToString (0x00B1)
                 + juce::String (album::loudestWarnLu, 1);
        f.delta = t.loudestDeltaLu;
        f.severity = t.loudness;
        f.key = highlight::integrated;
        f.stage = stage::loudness;
        f.id = "album|loudest";
        f.area = Area::loudness;

        if (louder)
        {
            f.message = "Il ritornello (o il drop) suona più forte di quello degli altri brani: ascoltando l'album in sequenza "
                        "questo brano salta fuori."_t;
            f.action = "Abbassalo di circa %1 dB (guadagno finale o ingresso del limiter), poi riascolta il passaggio dal brano precedente"_t
                           .replace ("%1", juce::String (amount, 1));
        }
        else
        {
            f.message = "Il ritornello (o il drop) suona più debole di quello degli altri brani. Se è una ballata o un brano "
                        "volutamente più intimo può essere una scelta: decidi a orecchio, nel passaggio dal brano precedente."_t;
            const float newPeak = t.truePeakDb + amount;
            if (newPeak > -1.0f)
                f.action = "Alzalo di circa %1 dB spingendo il limiter (ingresso +%1 dB, ceiling invariato): il true peak attuale "
                           "(%2 dBTP) non lascia spazio a un guadagno semplice"_t
                               .replace ("%1", juce::String (amount, 1)).replace ("%2", num (t.truePeakDb));
            else
                f.action = "Alzalo di circa %1 dB: c'è spazio, il true peak arriverebbe a %2 dBTP"_t
                               .replace ("%1", juce::String (amount, 1)).replace ("%2", num (newPeak));
        }

        t.findings.push_back (std::move (f));
    }

}

AlbumResult checkAlbum (const std::vector<AnalysisSnapshot>& snapshots, const juce::StringArray& names)
{
    AlbumResult result;
    if (snapshots.size() < 2)
        return result;

    // --- riferimenti dell'album: mediane ------------------------------------------------------------
    std::vector<float> loudest, integrated, peaks, widths;
    result.usesLoudestSection = true;
    for (const auto& s : snapshots)
    {
        loudest.push_back (s.maxShortTermLufs);
        integrated.push_back (s.integratedLufs);
        peaks.push_back (s.truePeakMaxDb);
        widths.push_back (s.widthPercent);
        result.usesLoudestSection = result.usesLoudestSection && s.loudestSectionSeconds > 0.0f;
    }
    result.medianLoudestLufs = median (loudest);
    result.medianIntegratedLufs = median (integrated);
    result.medianTruePeakDb = median (peaks);
    result.medianWidthPercent = median (widths);
    result.loudestSpreadLu = *std::max_element (loudest.begin(), loudest.end()) - *std::min_element (loudest.begin(), loudest.end());

    // curva dell'album: ritornello contro ritornello quando tutti i brani hanno una sezione più forte
    auto curveOf = [&result] (const AnalysisSnapshot& s) -> const std::array<float, kNumThirdOctaves>&
    {
        return result.usesLoudestSection ? s.thirdOctaveLoudestDb : s.thirdOctaveDb;
    };
    for (int i = 0; i < kNumThirdOctaves; ++i)
    {
        std::vector<float> values;
        for (const auto& s : snapshots)
            values.push_back (curveOf (s)[(size_t) i]);
        result.albumCurve[(size_t) i] = median (values);
    }

    // tolleranza tonale fissa, più larga agli estremi dello spettro (stessa scala dei profili)
    TargetProfile tolerances;
    tolerances.tonalTolerance = album::tonalMinToleranceDb;

    // --- ogni brano contro l'album ------------------------------------------------------------------
    for (size_t i = 0; i < snapshots.size(); ++i)
    {
        const auto& s = snapshots[i];
        AlbumTrack t;
        t.name = names[(int) i].isNotEmpty() ? names[(int) i] : "Brano"_t + " " + juce::String ((int) i + 1);
        t.integratedLufs = s.integratedLufs;
        t.loudestLufs = s.maxShortTermLufs;
        t.loudestDeltaLu = s.maxShortTermLufs - result.medianLoudestLufs;
        t.gainDb = -t.loudestDeltaLu;
        t.truePeakDb = s.truePeakMaxDb;
        t.truePeakDeltaDb = s.truePeakMaxDb - result.medianTruePeakDb;
        t.widthPercent = s.widthPercent;
        t.widthDelta = s.widthPercent - result.medianWidthPercent;
        t.plr = s.plr;
        t.lra = s.loudnessRange;

        const float absLoudest = std::abs (t.loudestDeltaLu);
        t.loudness = absLoudest > album::loudestCriticalLu ? Severity::critical
                   : (absLoudest > album::loudestWarnLu ? Severity::warning : Severity::ok);

        for (int b = 0; b < kNumBands; ++b)
        {
            const float tol = std::max (album::tonalMinToleranceDb, tonalToleranceForBand (tolerances, b));
            const float d = bandTonalDelta (curveOf (s), result.albumCurve, b);
            t.bandDelta[(size_t) b] = d;
            t.bandTolerance[(size_t) b] = tol;
            t.bandSeverity[(size_t) b] = std::abs (d) <= tol ? Severity::ok : (std::abs (d) <= 2.0f * tol ? Severity::warning : Severity::critical);
            t.tonal = std::max (t.tonal, t.bandSeverity[(size_t) b]);
        }

        addLoudnessFinding (t, result.medianLoudestLufs);
        addTonalFindings (t);

        // informative: integrated, ceiling e larghezza diversi dal resto dell'album
        if (const float d = s.integratedLufs - result.medianIntegratedLufs; std::abs (d) > 2.0f)
        {
            Finding f;
            f.category = "Loudness"_t;
            f.metric = "Integrated";
            f.value = num (s.integratedLufs) + " LUFS  (" + signedNum (d) + " LU)";
            f.target = "mediana album"_t + " " + num (result.medianIntegratedLufs) + " LUFS";
            f.delta = d;
            f.severity = Severity::info;
            f.message = "La loudness media è diversa dagli altri brani. È normale per brani con più dinamica o ballate: "
                        "per la coerenza dell'album conta la sezione più forte."_t;
            f.key = highlight::integrated;
            f.stage = stage::analysis;
            f.id = "album|integrated";
            f.area = Area::loudness;
            t.findings.push_back (std::move (f));
        }

        if (std::abs (t.truePeakDeltaDb) > album::truePeakInfoDb)
        {
            Finding f;
            f.category = "Peak";
            f.metric = "Ceiling true peak"_t;
            f.value = num (s.truePeakMaxDb, 1) + " dBTP";
            f.target = "mediana album"_t + " " + num (result.medianTruePeakDb, 1) + " dBTP";
            f.delta = t.truePeakDeltaDb;
            f.severity = Severity::info;
            f.message = "Il picco massimo è diverso da quello degli altri brani: in un album di solito il limiter ha lo stesso "
                        "ceiling su tutti i brani."_t;
            f.action = "Controlla il ceiling del limiter di questo brano"_t;
            f.key = highlight::truePeak;
            f.stage = stage::loudness;
            f.id = "album|truePeak";
            f.area = Area::loudness;
            t.findings.push_back (std::move (f));
        }

        if (std::abs (t.widthDelta) > album::widthInfoPercent)
        {
            Finding f;
            f.category = "Stereo";
            f.metric = "Larghezza stereo"_t;
            f.value = juce::String (juce::roundToInt (s.widthPercent)) + "%";
            f.target = "mediana album"_t + " " + juce::String (juce::roundToInt (result.medianWidthPercent)) + "%";
            f.delta = t.widthDelta;
            f.severity = Severity::info;
            f.message = (t.widthDelta > 0.0f ? "Immagine stereo più larga degli altri brani."_t
                                             : "Immagine stereo più stretta degli altri brani."_t)
                      + " " + "Può essere voluto, ma nel passaggio da un brano all'altro si nota."_t;
            f.key = highlight::width;
            f.stage = stage::lowEnd;
            f.id = "album|width";
            f.area = Area::stereo;
            t.findings.push_back (std::move (f));
        }

        std::stable_sort (t.findings.begin(), t.findings.end(), [] (const Finding& a, const Finding& b) { return a.severity > b.severity; });
        t.worst = t.findings.empty() ? Severity::ok : t.findings.front().severity;
        result.critical += t.worst == Severity::critical ? 1 : 0;
        result.warning += t.worst == Severity::warning ? 1 : 0;
        result.tracks.push_back (std::move (t));
    }

    return result;
}

//==============================================================================
juce::String buildAlbumTextReport (const AlbumResult& album)
{
    juce::String t;
    t << "MASTERAGENT - " << "COERENZA ALBUM"_t << "\n"
      << "Data"_t << ": " << juce::Time::getCurrentTime().toString (true, true) << "\n"
      << "Brani"_t << ": " << (int) album.tracks.size()
      << "   " << "Sezione più forte, mediana"_t << ": " << num (album.medianLoudestLufs) << " LUFS"
      << "   " << "differenza massima"_t << ": " << juce::String (album.loudestSpreadLu, 1) << " LU\n"
      << "Integrated, mediana"_t << ": " << num (album.medianIntegratedLufs) << " LUFS"
      << "   " << "True peak, mediana"_t << ": " << num (album.medianTruePeakDb) << " dBTP\n"
      << "Confronto tonale"_t << ": " << (album.usesLoudestSection ? "sezione più forte di ogni brano"_t : "brano intero"_t) << "\n"
      << "Brani da sistemare"_t << ": " << album.critical + album.warning
      << "  (" << "critici"_t << " " << album.critical << ", " << "da verificare"_t << " " << album.warning << ")\n\n";

    t << juce::String ("#").paddedRight (' ', 4) << juce::String ("Brano"_t).paddedRight (' ', 32)
      << juce::String ("Int.").paddedLeft (' ', 7) << juce::String ("Forte"_t).paddedLeft (' ', 8)
      << juce::String ("Delta").paddedLeft (' ', 7) << juce::String ("Gain").paddedLeft (' ', 7)
      << juce::String ("TP").paddedLeft (' ', 7) << juce::String ("Largh."_t).paddedLeft (' ', 8);
    for (auto* band : kBandNames)
        t << juce::String (band).substring (0, 6).paddedLeft (' ', 8);
    t << "\n";

    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& tr = album.tracks[i];
        t << juce::String ((int) i + 1).paddedRight (' ', 4) << tr.name.substring (0, 31).paddedRight (' ', 32)
          << num (tr.integratedLufs).paddedLeft (' ', 7) << num (tr.loudestLufs).paddedLeft (' ', 8)
          << signedNum (tr.loudestDeltaLu).paddedLeft (' ', 7)
          << (tr.loudness == Severity::ok ? juce::String ("-") : signedNum (tr.gainDb)).paddedLeft (' ', 7)
          << num (tr.truePeakDb).paddedLeft (' ', 7) << (juce::String (juce::roundToInt (tr.widthPercent)) + "%").paddedLeft (' ', 8);
        for (int b = 0; b < kNumBands; ++b)
            t << (signedNum (tr.bandDelta[(size_t) b]) + (tr.bandSeverity[(size_t) b] >= Severity::warning ? "*" : " ")).paddedLeft (' ', 8);
        t << "\n";
    }
    t << "(* = " << "fuori tolleranza rispetto all'album"_t << ")\n";

    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& tr = album.tracks[i];
        if (tr.findings.empty())
            continue;

        t << "\n== " << juce::String ((int) i + 1) << ". " << tr.name << " ==\n";
        for (const auto& f : tr.findings)
        {
            t << "[" << severityName (f.severity) << "] " << f.metric << ": " << f.value << "   (" << f.target << ")\n"
              << "  " << f.message << "\n";
            if (f.action.isNotEmpty())
                t << "  -> " << f.action << "\n";
        }
    }

    if (album.critical + album.warning == 0)
        t << "\n" << "Nessun brano fuori dall'album: loudness della sezione più forte e bilanciamento tonale sono coerenti."_t << "\n";

    return t;
}

juce::String buildAlbumCsv (const AlbumResult& album)
{
    auto n = [] (float v, int d = 2) { return v <= kSilenceDb + 0.5f ? juce::String ("-inf") : juce::String (v, d); };
    auto quoted = [] (const juce::String& s) { return "\"" + s.replace ("\"", "\"\"") + "\""; };

    juce::String t;
    t << "track;name;integrated_lufs;loudest_lufs;loudest_delta_lu;gain_db;true_peak_dbtp;width_percent;plr_db;lra_lu";
    for (auto* band : kBandNames)
        t << ";tonal_" << juce::String (band).toLowerCase().replace ("-", "_") << "_db";
    t << ";worst\n";

    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& tr = album.tracks[i];
        t << (int) i + 1 << ";" << quoted (tr.name) << ";" << n (tr.integratedLufs) << ";" << n (tr.loudestLufs) << ";"
          << n (tr.loudestDeltaLu) << ";" << n (tr.gainDb) << ";" << n (tr.truePeakDb) << ";" << n (tr.widthPercent, 1) << ";"
          << n (tr.plr) << ";" << n (tr.lra);
        for (int b = 0; b < kNumBands; ++b)
            t << ";" << n (tr.bandDelta[(size_t) b]);
        t << ";" << severityName (tr.worst) << "\n";
    }
    return t;
}

} // namespace ma
