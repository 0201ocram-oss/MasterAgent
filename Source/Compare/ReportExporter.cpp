#include "ReportExporter.h"
#include "../Analysis/StreamingPreview.h"
#include "../Common/Text.h"

namespace ma
{

namespace
{
    juce::String num (float v, int d = 1) { return v <= kSilenceDb + 0.5f ? juce::String ("-inf") : juce::String (v, d); }
}

juce::String buildTextReport (const AnalysisSnapshot& s, const ComparisonResult& c, const juce::String& targetName)
{
    juce::String t;
    t << "MASTERAGENT - REPORT DI ANALISI\n"
      << "Data: " << juce::Time::getCurrentTime().toString (true, true) << "\n"
      << "Target: " << targetName << "   Fase: " << phaseName (c.phase) << (c.tonalUsesLoudest ? "   (tonale sulla sezione più forte)"_u : juce::String()) << "\n"
      << "Durata analizzata: " << juce::String (s.secondsAnalyzed, 1) << " s   Sample rate: " << juce::String (juce::roundToInt (s.sampleRate)) << " Hz\n"
      << "Punteggio indicativo: " << c.score << "/100   (critici " << c.count (Severity::critical)
      << ", attenzione " << c.count (Severity::warning) << ")\n";

    t << "Pagella:"_t;
    for (int a = 0; a < kNumAreas; ++a)
    {
        const auto& area = c.areas[(size_t) a];
        t << "  " << areaName ((Area) a) << " " << (area.evaluated ? juce::String (area.score) : juce::String ("n/d"));
    }
    t << "\n\n";

    t << "== LOUDNESS ==\n"
      << "Integrated: " << num (s.integratedLufs) << " LUFS   LRA: " << num (s.loudnessRange) << " LU\n"
      << "Max momentary: " << num (s.maxMomentaryLufs) << " LUFS   Max short-term: " << num (s.maxShortTermLufs) << " LUFS\n"
      << "RMS: " << num (s.rmsDb) << " dBFS\n\n";

    t << "== PEAK ==\n"
      << "True peak L/R: " << num (s.truePeakDb[0], 2) << " / " << num (s.truePeakDb[1], 2) << " dBTP\n"
      << "Sample peak L/R: " << num (s.samplePeakDb[0], 2) << " / " << num (s.samplePeakDb[1], 2) << " dBFS\n"
      << "Eventi > -1 dBTP: " << s.overs1dB << "   > 0 dBTP: " << s.overs0dB << "   Clip: " << s.clipEvents << "\n\n";

    t << "== DINAMICA ==\n"
      << "PLR: " << num (s.plr) << " dB   PSR min: " << num (s.minPsr) << " dB   Crest: " << num (s.crestDb) << " dB   DR: "
      << (s.drValid ? juce::String (s.drValue, 1) : juce::String ("n/d")) << "\n"
      << "Crest per banda (low/mid/high): " << num (s.bandCrestDb[0]) << " / " << num (s.bandCrestDb[1]) << " / " << num (s.bandCrestDb[2]) << " dB\n\n";

    t << "== TONALE ==\n"
      << "Tilt: " << juce::String (s.spectralTiltDbPerOct, 2) << " dB/oct   Centroide: " << juce::String (juce::roundToInt (s.spectralCentroidHz)) << " Hz\n";
    for (int b = 0; b < kNumBands; ++b)
    {
        t << "  " << juce::String (kBandNames[(size_t) b]).paddedRight (' ', 10) << num (s.bandEnergyDb[(size_t) b]) << " dB";
        if (c.hasTonalTarget)
            t << "   (vs target " << (c.bandTonalDelta[(size_t) b] >= 0 ? "+" : "") << juce::String (c.bandTonalDelta[(size_t) b], 1) << " dB)";
        t << "\n";
    }

    t << "Comportamento nel tempo (raffiche dB / zona più carica):\n"_u;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& d = s.bandDynamics[(size_t) b];
        t << "  " << juce::String (kBandNames[(size_t) b]).paddedRight (' ', 10) << juce::String (d.burstDb, 1) << " dB";
        if (d.worstStartSec >= 0.0f)
            t << "   " << formatSongTime (d.worstStartSec) << "-" << formatSongTime (d.worstEndSec) << " (+" << juce::String (d.worstExcessDb, 1) << " dB)";
        t << "\n";
    }

    if (! s.resonances.empty())
    {
        t << "Picchi stretti:";
        for (const auto& r : s.resonances)
            t << "  " << juce::roundToInt (r.frequency) << " Hz +" << juce::String (r.excessDb, 1) << " dB ("
              << (r.musical ? "nota " + noteName (r.midiNote) : juce::String ("risonanza")) << ")";
        t << "\n";
    }

    if (! c.eqMoves.empty())
    {
        t << "\n== MOSSE EQ SUGGERITE ==\n";
        for (size_t i = 0; i < c.eqMoves.size(); ++i)
        {
            const auto& m = c.eqMoves[i];
            juce::StringArray bands;
            for (int b : m.bands) bands.add (kBandNames[(size_t) b]);
            t << "  " << (int) (i + 1) << ". " << m.describe (m.shownGainDb) << "   (stimato " << juce::String (m.gainDb, 1)
              << " dB; corregge " << bands.joinIntoString (", ") << ")" << (m.fixInMix ? "  -> il resto nel mix" : "") << "\n";
        }
    }

    t << "\n== STEREO ==\n"
      << "Correlazione: " << juce::String (s.correlation, 2) << "   Larghezza: " << juce::String (juce::roundToInt (s.widthPercent)) << "%   Bilanciamento: "
      << juce::String (s.balanceDb, 2) << " dB   Perdita mono: " << juce::String (s.monoLossDb, 1) << " dB\n";
    for (int b = 0; b < kNumBands; ++b)
        t << "  " << juce::String (kBandNames[(size_t) b]).paddedRight (' ', 10) << "corr " << juce::String (s.bandCorrelation[(size_t) b], 2)
          << "   larghezza " << juce::String (juce::roundToInt (s.bandWidthPercent[(size_t) b])) << "%\n";

    t << "\n== STREAMING ==\n";
    for (const auto& r : computeStreamingPreview (s.integratedLufs, s.truePeakMaxDb))
        t << "  " << juce::String (r.name).paddedRight (' ', 14) << "guadagno " << (r.gainDb >= 0 ? "+" : "") << juce::String (r.gainDb, 1)
          << " dB -> " << juce::String (r.playbackLufs, 1) << " LUFS, TP " << juce::String (r.playbackTruePeak, 1) << " dBTP\n";

    if (! c.priorities.empty())
    {
        t << "\n== DA FARE PRIMA ==\n";
        int n = 1;
        for (const auto& f : c.priorities)
            t << "  " << n++ << ". " << f.metric << ": " << f.value << "\n";
    }

    t << "\n== DIAGNOSI ==\n";
    for (const auto& f : c.findings)
    {
        if (f.severity == Severity::ok || f.ignored)
            continue;
        t << "[" << severityName (f.severity).toUpperCase() << "] " << f.category << " - " << f.metric << ": " << f.value
          << "  (target " << f.target << ")\n    " << f.message << "\n";
        if (f.action.isNotEmpty())
            t << "    -> " << f.action << "\n";
    }

    t << "\n== NEL RANGE ==\n";
    for (const auto& f : c.findings)
        if (f.severity == Severity::ok && ! f.ignored)
            t << "  " << f.category << " - " << f.metric << ": " << f.value << "\n";

    if (c.countIgnored() > 0)
    {
        t << "\n" << "== IGNORATE (scelte volute) =="_t << "\n";
        for (const auto& f : c.findings)
            if (f.ignored)
                t << "  " << f.category << " - " << f.metric << ": " << f.value << "\n";
    }

    return t;
}

juce::String buildJsonReport (const AnalysisSnapshot& s, const ComparisonResult& c, const juce::String& targetName)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("target", targetName);
    root->setProperty ("date", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("secondsAnalyzed", s.secondsAnalyzed);
    root->setProperty ("sampleRate", s.sampleRate);
    root->setProperty ("score", c.score);
    {
        static const char* areaKeys[] = { "loudness", "dynamics", "tonal", "stereo", "technical" };
        auto* areas = new juce::DynamicObject();
        for (int a = 0; a < kNumAreas; ++a)
        {
            const auto& area = c.areas[(size_t) a];
            areas->setProperty (areaKeys[a], area.evaluated ? juce::var (area.score) : juce::var());
        }
        root->setProperty ("areas", juce::var (areas));
    }
    root->setProperty ("phase", phaseName (c.phase));
    root->setProperty ("tonalUsesLoudestSection", c.tonalUsesLoudest);

    auto* m = new juce::DynamicObject();
    m->setProperty ("integratedLufs", s.integratedLufs);
    m->setProperty ("loudnessRange", s.loudnessRange);
    m->setProperty ("maxMomentaryLufs", s.maxMomentaryLufs);
    m->setProperty ("maxShortTermLufs", s.maxShortTermLufs);
    m->setProperty ("truePeakMax", s.truePeakMaxDb);
    m->setProperty ("samplePeakL", s.samplePeakDb[0]);
    m->setProperty ("samplePeakR", s.samplePeakDb[1]);
    m->setProperty ("overs0dB", s.overs0dB);
    m->setProperty ("overs1dB", s.overs1dB);
    m->setProperty ("clipEvents", s.clipEvents);
    m->setProperty ("plr", s.plr);
    m->setProperty ("minPsr", s.minPsr);
    m->setProperty ("crest", s.crestDb);
    if (s.drValid) m->setProperty ("dr", s.drValue);
    m->setProperty ("spectralTilt", s.spectralTiltDbPerOct);
    m->setProperty ("spectralCentroid", s.spectralCentroidHz);
    m->setProperty ("correlation", s.correlation);
    m->setProperty ("widthPercent", s.widthPercent);
    m->setProperty ("balanceDb", s.balanceDb);
    m->setProperty ("monoLossDb", s.monoLossDb);

    juce::Array<juce::var> third, bandWidth, bandCorr, bandEnergy;
    for (auto v : s.thirdOctaveDb) third.add (v);
    for (auto v : s.bandWidthPercent) bandWidth.add (v);
    for (auto v : s.bandCorrelation) bandCorr.add (v);
    for (auto v : s.bandEnergyDb) bandEnergy.add (v);
    m->setProperty ("thirdOctaveDb", third);
    m->setProperty ("bandWidthPercent", bandWidth);
    m->setProperty ("bandCorrelation", bandCorr);
    m->setProperty ("bandEnergyDb", bandEnergy);

    juce::Array<juce::var> loudest, dynamics, resonances;
    for (auto v : s.thirdOctaveLoudestDb) loudest.add (v);
    m->setProperty ("thirdOctaveLoudestDb", loudest);
    m->setProperty ("loudestSectionSeconds", s.loudestSectionSeconds);
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& d = s.bandDynamics[(size_t) b];
        auto* o = new juce::DynamicObject();
        o->setProperty ("band", kBandNames[(size_t) b]);
        o->setProperty ("burstDb", d.burstDb);
        if (d.worstStartSec >= 0.0f)
        {
            o->setProperty ("worstStart", d.worstStartSec);
            o->setProperty ("worstEnd", d.worstEndSec);
            o->setProperty ("worstExcessDb", d.worstExcessDb);
        }
        dynamics.add (juce::var (o));
    }
    m->setProperty ("bandDynamics", dynamics);
    for (const auto& r : s.resonances)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("frequency", r.frequency);
        o->setProperty ("excessDb", r.excessDb);
        o->setProperty ("musical", r.musical);
        o->setProperty ("note", noteName (r.midiNote));
        o->setProperty ("cents", r.cents);
        resonances.add (juce::var (o));
    }
    m->setProperty ("resonances", resonances);
    root->setProperty ("metrics", juce::var (m));

    juce::Array<juce::var> moves;
    for (const auto& mv : c.eqMoves)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", mv.typeName());
        o->setProperty ("frequency", mv.frequency);
        o->setProperty ("gainDb", mv.shownGainDb);
        o->setProperty ("estimatedGainDb", mv.gainDb);
        o->setProperty ("q", mv.q);
        o->setProperty ("fixInMix", mv.fixInMix);
        juce::Array<juce::var> bands;
        for (int b : mv.bands) bands.add (kBandNames[(size_t) b]);
        o->setProperty ("bands", bands);
        moves.add (juce::var (o));
    }
    root->setProperty ("eqMoves", moves);

    juce::Array<juce::var> findings;
    for (const auto& f : c.findings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("severity", severityName (f.severity));
        o->setProperty ("category", f.category);
        o->setProperty ("metric", f.metric);
        o->setProperty ("value", f.value);
        o->setProperty ("target", f.target);
        o->setProperty ("delta", f.delta);
        o->setProperty ("message", f.message);
        if (f.action.isNotEmpty())
            o->setProperty ("action", f.action);
        if (f.hasTime())
        {
            o->setProperty ("timeStart", f.timeStart);
            o->setProperty ("timeEnd", f.timeEnd);
        }
        o->setProperty ("stage", f.stage);
        o->setProperty ("id", f.id);
        if (f.ignored)
            o->setProperty ("ignored", true);
        findings.add (juce::var (o));
    }
    root->setProperty ("findings", findings);

    return juce::JSON::toString (juce::var (root));
}

} // namespace ma
