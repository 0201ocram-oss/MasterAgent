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

//==============================================================================
// Report HTML

juce::String escapeHtml (const juce::String& text)
{
    return text.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
}

namespace
{
    // colori del tema chiaro dell'interfaccia; il documento non segue il tema scelto, così si stampa sempre bene
    const char* const kCss = R"css(
:root { --text:#18202c; --dim:#566173; --faint:#8a93a3; --line:#dde2e9; --well:#f0f2f6; --accent:#0a7fc4;
        --ok:#17994f; --info:#2b78d0; --warning:#c77700; --critical:#d92b3c; --target:#a87b00; }
* { box-sizing: border-box; }
body { margin: 0; background: #e9ecf1; color: var(--text); font: 14px/1.45 "Segoe UI", system-ui, -apple-system, Roboto, Arial, sans-serif; }
.page { max-width: 1040px; margin: 24px auto; background: #fff; padding: 34px 44px; border-radius: 10px; box-shadow: 0 2px 14px rgba(15,23,42,.08); }
header.top { display: flex; justify-content: space-between; align-items: flex-end; gap: 24px; border-bottom: 2px solid var(--accent); padding-bottom: 14px; margin-bottom: 20px; }
.brand { font-size: 12px; letter-spacing: .14em; text-transform: uppercase; color: var(--accent); font-weight: 700; }
h1 { font-size: 26px; margin: 2px 0 0; }
.subtitle { color: var(--dim); margin-top: 2px; }
h2 { font-size: 17px; margin: 30px 0 10px; padding-bottom: 5px; border-bottom: 1px solid var(--line); }
h3 { font-size: 12px; margin: 0 0 6px; color: var(--dim); text-transform: uppercase; letter-spacing: .06em; }
.meta { font-size: 13px; color: var(--dim); text-align: right; white-space: nowrap; }
.meta b { color: var(--text); font-weight: 600; }
table { border-collapse: collapse; width: 100%; }
td, th { padding: 4px 8px; text-align: left; vertical-align: top; }
th { font-size: 11px; text-transform: uppercase; letter-spacing: .05em; color: var(--faint); font-weight: 600; border-bottom: 1px solid var(--line); }
tbody tr:nth-child(even) { background: #f8f9fb; }
.num { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
td.tgt { color: var(--faint); font-size: 12.5px; }
.metrics td:first-child { white-space: nowrap; }
.summary { display: flex; gap: 30px; align-items: center; }
.ring { width: 124px; height: 124px; border-radius: 50%; border: 10px solid var(--ok); display: flex; flex-direction: column; align-items: center; justify-content: center; flex: none; }
.ring.warning { border-color: var(--warning); } .ring.critical { border-color: var(--critical); }
.ring .score { font-size: 38px; font-weight: 700; line-height: 1; }
.ring .of { font-size: 12px; color: var(--faint); }
.areas { flex: 1; }
.area { display: grid; grid-template-columns: 160px 1fr 48px; align-items: center; gap: 10px; margin: 4px 0; }
.bar { height: 8px; background: var(--well); border-radius: 4px; overflow: hidden; }
.bar > span { display: block; height: 100%; border-radius: 4px; background: var(--ok); }
.bar > span.warning { background: var(--warning); } .bar > span.critical { background: var(--critical); }
.counts { font-size: 13px; color: var(--dim); margin-top: 10px; }
.counts span { margin-right: 16px; }
.dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 5px; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 18px 30px; }
.card { border: 1px solid var(--line); border-left: 4px solid var(--faint); border-radius: 6px; padding: 8px 12px; margin: 8px 0; break-inside: avoid; }
.card.critical { border-left-color: var(--critical); } .card.warning { border-left-color: var(--warning); }
.card.info { border-left-color: var(--info); } .card.ok { border-left-color: var(--ok); }
.card .head { font-weight: 600; }
.card .tgt { color: var(--faint); font-size: 12.5px; }
.card p { margin: 4px 0 0; }
.card .action { color: #075e92; }
.card .when { font-size: 12px; color: var(--dim); }
.tag { display: inline-block; font-size: 10.5px; font-weight: 700; text-transform: uppercase; letter-spacing: .05em; padding: 1px 7px; border-radius: 9px; color: #fff; background: var(--ok); margin-right: 7px; vertical-align: 1px; }
.tag.critical, .dot.critical { background: var(--critical); } .tag.warning, .dot.warning { background: var(--warning); }
.tag.info, .dot.info { background: var(--info); } .dot.ok { background: var(--ok); } .dot.faint { background: var(--faint); }
.v-critical { color: var(--critical); font-weight: 600; } .v-warning { color: var(--warning); font-weight: 600; }
.v-ok { color: var(--ok); } .v-info { color: var(--info); }
td.c-critical { background: rgba(217,43,60,.13); } td.c-warning { background: rgba(199,119,0,.13); }
.muted { color: var(--faint); }
.note { font-size: 12.5px; color: var(--dim); background: var(--well); border-radius: 6px; padding: 8px 12px; margin: 8px 0; }
.chart { width: 100%; height: auto; display: block; margin: 6px 0 2px; }
.legend { font-size: 12px; color: var(--dim); }
.legend i { display: inline-block; width: 20px; height: 0; vertical-align: middle; margin: 0 6px 0 14px; border-top: 2px solid; }
.legend i:first-child { margin-left: 0; }
ol.prio { padding-left: 22px; margin: 6px 0; }
ol.prio li { margin: 7px 0; }
.shot img { width: 100%; border: 1px solid var(--line); border-radius: 6px; }
footer { margin-top: 34px; padding-top: 10px; border-top: 1px solid var(--line); font-size: 11.5px; color: var(--faint); }
@media print {
  @page { size: A4; margin: 13mm; }
  body { background: #fff; font-size: 11.5px; }
  .page { box-shadow: none; margin: 0; padding: 0; max-width: none; border-radius: 0; }
  h2, h3 { break-after: avoid; }
  .grid2 > div { break-inside: avoid; }
  td, th { padding: 3px 6px; }
  td.tgt { font-size: 10.5px; }
  .card, tr, .area, svg, .summary { break-inside: avoid; }
  .shot { break-before: page; }
  * { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
}
)css";

    const char* severityClass (Severity s)
    {
        switch (s)
        {
            case Severity::critical: return "critical";
            case Severity::warning:  return "warning";
            case Severity::info:     return "info";
            case Severity::ok:       break;
        }
        return "ok";
    }

    juce::String severityLabel (Severity s)
    {
        switch (s)
        {
            case Severity::critical: return "Critico"_t;
            case Severity::warning:  return "Da verificare"_t;
            case Severity::info:     return "Info"_t;
            case Severity::ok:       break;
        }
        return "Nel range"_t;
    }

    /** Stesse soglie dell'anello del punteggio nel pannello Report. */
    const char* scoreClass (int score) { return score >= 85 ? "ok" : (score >= 60 ? "warning" : "critical"); }

    juce::String signedNum (float v, int d = 1) { return (v > 0.0f ? "+" : "") + num (v, d); }

    juce::String formatDate (juce::Time t) { return t.formatted ("%d/%m/%Y %H:%M"); }

    juce::String htmlBegin (const juce::String& title)
    {
        juce::String h;
        // codice della lingua del documento: la traduzione inglese lo sostituisce
        h << "<!DOCTYPE html>\n<html lang=\"" << "it"_t << "\">\n<head>\n<meta charset=\"utf-8\">\n"
          << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
          << "<title>" << escapeHtml (title) << "</title>\n<style>" << kCss << "</style>\n</head>\n<body>\n<div class=\"page\">\n";
        return h;
    }

    juce::String htmlEnd (const juce::String& pluginVersion, juce::Time date)
    {
        juce::String h;
        h << "<footer>"
          << escapeHtml ("Generato da %1 il %2. Valori e consigli sono indicativi: la decisione finale spetta al tuo ascolto."_t
                             .replace ("%1", ("MasterAgent " + pluginVersion).trim()).replace ("%2", formatDate (date)))
          << "</footer>\n</div>\n</body>\n</html>\n";
        return h;
    }

    juce::String metaLine (const juce::String& label, const juce::String& value)
    {
        return escapeHtml (label) + ": <b>" + escapeHtml (value) + "</b><br>\n";
    }

    juce::String screenshotSection (const juce::MemoryBlock& png, const juce::String& title)
    {
        if (png.isEmpty())
            return {};
        juce::String h;
        h << "<section class=\"shot\">\n<h2>" << escapeHtml (title) << "</h2>\n<img alt=\"" << escapeHtml (title)
          << "\" src=\"data:image/png;base64," << juce::Base64::toBase64 (png.getData(), png.getSize()) << "\">\n</section>\n";
        return h;
    }

    /** Diagnosi come scheda: gravità, misura, valore e target, spiegazione, intervento, punto del brano. */
    juce::String findingCard (const Finding& f, bool showSeverity = true)
    {
        juce::String h;
        h << "<div class=\"card " << severityClass (f.severity) << "\">\n<div class=\"head\">";
        if (showSeverity)
            h << "<span class=\"tag " << severityClass (f.severity) << "\">" << escapeHtml (severityLabel (f.severity)) << "</span>";
        h << escapeHtml (f.category) << " &middot; " << escapeHtml (f.metric) << "</div>\n"
          << "<div><span class=\"num\">" << escapeHtml (f.value) << "</span>";
        if (f.target.isNotEmpty() && f.target != "-")   // le mosse EQ hanno come "target" le bande che correggono
            h << " &nbsp;<span class=\"tgt\">" << (f.key.startsWith ("eq:") ? juce::String() : escapeHtml ("target"_t) + " ")
              << escapeHtml (f.target) << "</span>";
        h << "</div>\n";
        if (f.message.isNotEmpty() && f.severity != Severity::ok)
            h << "<p>" << escapeHtml (f.message) << "</p>\n";
        if (f.action.isNotEmpty())
            h << "<p class=\"action\">&rarr; " << escapeHtml (f.action) << "</p>\n";
        // il punto del brano, se la spiegazione non lo cita già
        if (f.hasTime() && ! (f.message + f.action).contains (formatSongTime (f.timeStart)))
            h << "<p class=\"when\">" << escapeHtml ("Punto del brano: %1 - %2"_t.replace ("%1", formatSongTime (f.timeStart))
                                                                                   .replace ("%2", formatSongTime (f.timeEnd))) << "</p>\n";
        h << "</div>\n";
        return h;
    }

    /** Riga di una tabella di misure, colorata come la dashboard se c'è una diagnosi collegata. */
    struct MetricRow
    {
        juce::String label, value;
        juce::String key, metricPrefix;   // diagnosi collegata (chiave della dashboard e, se serve, inizio del nome della misura)
    };

    const Finding* findingFor (const ComparisonResult& c, const juce::String& key, const juce::String& metricPrefix)
    {
        if (key.isEmpty())
            return nullptr;
        for (const auto& f : c.findings)
            if (f.key == key && ! f.ignored && (metricPrefix.isEmpty() || f.metric.startsWith (metricPrefix)))
                return &f;
        return nullptr;
    }

    juce::String metricTable (const juce::String& title, const std::vector<MetricRow>& rows, const ComparisonResult& c)
    {
        juce::String h;
        h << "<div>\n<h3>" << escapeHtml (title) << "</h3>\n<table class=\"metrics\"><tbody>\n";
        for (const auto& r : rows)
        {
            const auto* f = findingFor (c, r.key, r.metricPrefix);
            const bool problem = f != nullptr && f->severity >= Severity::warning;
            h << "<tr><td>" << escapeHtml (r.label) << "</td><td class=\"num" << (problem ? juce::String (" v-") + severityClass (f->severity) : juce::String())
              << "\">" << escapeHtml (r.value) << "</td><td class=\"tgt\">"
              << (f != nullptr && f->target != "-" ? escapeHtml (f->target) : juce::String()) << "</td></tr>\n";
        }
        h << "</tbody></table>\n</div>\n";
        return h;
    }

    /** Bilanciamento tonale: master (allineato come nel confronto), target tratteggiato e fascia di tolleranza di ogni banda. */
    juce::String tonalChartSvg (const AnalysisSnapshot& s, const ComparisonResult& c)
    {
        constexpr float width = 900.0f, height = 300.0f, left = 44.0f, right = 12.0f, top = 24.0f, bottom = 26.0f;
        constexpr float minDb = -36.0f, maxDb = 12.0f;
        const float plotW = width - left - right, plotH = height - top - bottom;
        auto xOf = [&] (float hz) { return left + plotW * std::log2 (hz / 20.0f) / std::log2 (1000.0f); };
        auto yOf = [&] (float db) { return top + plotH * (maxDb - juce::jlimit (minDb, maxDb, db)) / (maxDb - minDb); };
        auto pt = [] (float x, float y) { return juce::String (x, 1) + "," + juce::String (y, 1) + " "; };

        juce::String svg;
        svg << "<svg class=\"chart\" viewBox=\"0 0 " << (int) width << " " << (int) height << "\" xmlns=\"http://www.w3.org/2000/svg\" "
            << "font-family=\"Segoe UI, system-ui, Arial, sans-serif\" font-size=\"10.5\">\n"
            << "<rect x=\"" << left << "\" y=\"" << top << "\" width=\"" << plotW << "\" height=\"" << plotH << "\" fill=\"#f8f9fb\"/>\n";

        // griglia: dB ogni 6, frequenze per ottava
        for (float db = minDb; db <= maxDb + 0.1f; db += 6.0f)
        {
            const float y = yOf (db);
            svg << "<line x1=\"" << left << "\" x2=\"" << (left + plotW) << "\" y1=\"" << juce::String (y, 1) << "\" y2=\"" << juce::String (y, 1)
                << "\" stroke=\"" << (std::abs (db) < 0.1f ? "#c3cad4" : "#e3e7ed") << "\"/>\n"
                << "<text x=\"" << (left - 6.0f) << "\" y=\"" << juce::String (y + 3.5f, 1) << "\" text-anchor=\"end\" fill=\"#8a93a3\">"
                << (db > 0.0f ? "+" : "") << juce::roundToInt (db) << "</text>\n";
        }
        const std::pair<float, const char*> ticks[] { { 31.5f, "31.5" }, { 63.0f, "63" }, { 125.0f, "125" }, { 250.0f, "250" }, { 500.0f, "500" },
                                                      { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 4000.0f, "4k" }, { 8000.0f, "8k" }, { 16000.0f, "16k" } };
        for (const auto& [hz, label] : ticks)
        {
            const float x = xOf (hz);
            svg << "<line x1=\"" << juce::String (x, 1) << "\" x2=\"" << juce::String (x, 1) << "\" y1=\"" << top << "\" y2=\"" << (top + plotH)
                << "\" stroke=\"#e3e7ed\"/>\n<text x=\"" << juce::String (x, 1) << "\" y=\"" << (height - 8.0f) << "\" text-anchor=\"middle\" fill=\"#8a93a3\">"
                << label << "</text>\n";
        }

        // bande: confini e nomi, colorati se la banda è fuori tolleranza
        for (int b = 0; b < kNumBands; ++b)
        {
            const float x0 = xOf (kBandEdges[(size_t) b]), x1 = xOf (kBandEdges[(size_t) b + 1]);
            if (b > 0)
                svg << "<line x1=\"" << juce::String (x0, 1) << "\" x2=\"" << juce::String (x0, 1) << "\" y1=\"" << (top - 14.0f) << "\" y2=\"" << (top + plotH)
                    << "\" stroke=\"#c3cad4\" stroke-dasharray=\"3 3\"/>\n";
            const auto sev = c.hasTonalTarget ? c.severityOfKey (highlight::band (b)) : Severity::ok;
            const char* colour = sev == Severity::critical ? "#d92b3c" : (sev == Severity::warning ? "#c77700" : "#566173");
            svg << "<text x=\"" << juce::String (0.5f * (x0 + x1), 1) << "\" y=\"" << (top - 7.0f) << "\" text-anchor=\"middle\" fill=\"" << colour << "\""
                << (sev >= Severity::warning ? " font-weight=\"700\"" : "") << ">" << kBandNames[(size_t) b] << "</text>\n";
        }

        if (c.hasTonalTarget)
        {
            juce::String upper, lower, target;
            for (int i = 0; i < kNumThirdOctaves; ++i)
            {
                const int b = bandOfThirdOctave (i);
                const float x = xOf (thirdOctaveCentre (i));
                target << pt (x, yOf (c.tonalTarget[(size_t) i]));
                if (b >= 0)
                    upper << pt (x, yOf (c.tonalTarget[(size_t) i] + c.bandTolerance[(size_t) b]));
            }
            for (int i = kNumThirdOctaves - 1; i >= 0; --i)
                if (const int b = bandOfThirdOctave (i); b >= 0)
                    lower << pt (xOf (thirdOctaveCentre (i)), yOf (c.tonalTarget[(size_t) i] - c.bandTolerance[(size_t) b]));

            svg << "<polygon points=\"" << upper << lower.trimEnd() << "\" fill=\"#a87b00\" fill-opacity=\"0.13\"/>\n"
                << "<polyline points=\"" << target.trimEnd() << "\" fill=\"none\" stroke=\"#a87b00\" stroke-width=\"1.8\" stroke-dasharray=\"6 4\"/>\n";
        }

        juce::String master;
        for (int i = 0; i < kNumThirdOctaves; ++i)
        {
            const float db = c.hasTonalTarget ? c.tonalTarget[(size_t) i] + c.tonalDelta[(size_t) i] : s.thirdOctaveDb[(size_t) i];
            master << pt (xOf (thirdOctaveCentre (i)), yOf (db));
        }
        svg << "<polyline points=\"" << master.trimEnd() << "\" fill=\"none\" stroke=\"#0a7fc4\" stroke-width=\"2.4\" stroke-linejoin=\"round\"/>\n"
            << "</svg>\n";
        return svg;
    }

    juce::String bandRange (int b)
    {
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : juce::String ((int) f) + " Hz"; };
        return hz (kBandEdges[(size_t) b]) + " - " + hz (kBandEdges[(size_t) b + 1]);
    }

    juce::String streamingSection (const ReportContext& ctx)
    {
        const auto& s = ctx.snapshot;
        const auto results = computeStreamingPreview (s.integratedLufs, s.truePeakMaxDb);
        if (results.empty())
            return {};

        juce::String h;
        h << "<h2>" << escapeHtml ("Anteprima normalizzazione streaming"_t) << "</h2>\n";
        if (ctx.profileContext == ProfileContext::broadcast || ctx.profileContext == ProfileContext::podcast)
            h << "<p class=\"note\">" << escapeHtml ("Il target è uno standard di consegna (broadcast o podcast): l'anteprima streaming è solo indicativa."_t) << "</p>\n";
        else if (ctx.comparison.phase == WorkPhase::mix)
            h << "<p class=\"note\">" << escapeHtml ("Fase di mix: anteprima solo indicativa, la loudness finale la decide il mastering."_t) << "</p>\n";

        h << "<table>\n<thead><tr><th>" << escapeHtml ("Piattaforma"_t) << "</th><th class=\"num\">" << escapeHtml ("Target"_t)
          << "</th><th class=\"num\">" << escapeHtml ("Guadagno"_t) << "</th><th class=\"num\">" << escapeHtml ("In riproduzione"_t)
          << "</th><th class=\"num\">" << escapeHtml ("TP risultante"_t) << "</th></tr></thead>\n<tbody>\n";
        for (const auto& r : results)
        {
            const char* gainClass = r.tooLoud ? (r.gainDb < -3.0f ? "v-warning" : "v-info") : (r.tooQuiet ? "v-warning" : "v-ok");
            h << "<tr><td>" << escapeHtml (juce::String (r.name)) << "</td><td class=\"num\">" << juce::roundToInt (r.targetLufs) << " LUFS</td>"
              << "<td class=\"num " << gainClass << "\">" << signedNum (r.gainDb) << " dB</td>"
              << "<td class=\"num\">" << juce::String (r.playbackLufs, 1) << " LUFS</td>"
              << "<td class=\"num" << (r.playbackTruePeak > -1.0f ? " v-warning" : "") << "\">" << juce::String (r.playbackTruePeak, 1) << " dBTP</td></tr>\n";
        }
        h << "</tbody></table>\n<p class=\"muted\">"
          << escapeHtml ("Guadagno negativo = la piattaforma abbasserà il brano: il loudness oltre il target non dà vantaggi e costa dinamica."_t) << "</p>\n";
        return h;
    }
}

juce::String buildHtmlReport (const ReportContext& ctx)
{
    const auto& s = ctx.snapshot;
    const auto& c = ctx.comparison;
    juce::String h = htmlBegin ("MasterAgent - " + ctx.source);

    // --- intestazione ----------------------------------------------------------------------------
    h << "<header class=\"top\">\n<div>\n<div class=\"brand\">MasterAgent</div>\n<h1>" << escapeHtml ("Report di analisi"_t) << "</h1>\n"
      << "<div class=\"subtitle\">" << escapeHtml (ctx.source) << "</div>\n</div>\n<div class=\"meta\">\n"
      << metaLine ("Target"_t, ctx.targetName)
      << metaLine ("Fase"_t, phaseName (c.phase))
      << metaLine ("Data"_t, formatDate (ctx.date));
    if (s.liveMode)
        h << metaLine ("Analisi"_t, "Live, ultimi %1 s"_t.replace ("%1", juce::String (juce::roundToInt (s.liveWindowSeconds))));
    else
        h << metaLine ("Durata analizzata"_t, formatSongTime ((float) s.secondsAnalyzed));
    h << metaLine ("Sample rate"_t, juce::String (juce::roundToInt (s.sampleRate)) + " Hz");
    if (ctx.pluginVersion.isNotEmpty())
        h << metaLine ("Versione"_t, ctx.pluginVersion);
    h << "</div>\n</header>\n";

    if (ctx.targetDescription.isNotEmpty())
        h << "<p class=\"note\">" << escapeHtml (ctx.targetName) << ": " << escapeHtml (ctx.targetDescription) << "</p>\n";

    // --- pagella ------------------------------------------------------------------------------------
    h << "<section class=\"summary\">\n<div class=\"ring " << scoreClass (c.score) << "\"><div class=\"score\">" << c.score
      << "</div><div class=\"of\">/ 100</div></div>\n<div class=\"areas\">\n<h3>" << escapeHtml ("Pagella"_t) << "</h3>\n";
    for (int a = 0; a < kNumAreas; ++a)
    {
        const auto& area = c.areas[(size_t) a];
        h << "<div class=\"area\"><span>" << escapeHtml (areaName ((Area) a)) << "</span><div class=\"bar\">";
        if (area.evaluated)
            h << "<span class=\"" << scoreClass (area.score) << "\" style=\"width:" << std::max (2, area.score) << "%\"></span></div>"
              << "<span class=\"num v-" << scoreClass (area.score) << "\">" << area.score << "</span></div>\n";
        else
            h << "</div><span class=\"num muted\">n/d</span></div>\n";
    }
    h << "<div class=\"counts\"><span><i class=\"dot critical\"></i>" << c.count (Severity::critical) << " " << escapeHtml ("critici"_t) << "</span>"
      << "<span><i class=\"dot warning\"></i>" << c.count (Severity::warning) << " " << escapeHtml ("da verificare"_t) << "</span>"
      << "<span><i class=\"dot ok\"></i>" << c.count (Severity::ok) << " " << escapeHtml ("nel range"_t) << "</span>";
    if (const int ignored = c.countIgnored(); ignored > 0)
        h << "<span><i class=\"dot faint\"></i>" << ignored << " " << escapeHtml ("ignorate"_t) << "</span>";
    h << "</div>\n</div>\n</section>\n";

    // --- priorità -------------------------------------------------------------------------------------
    if (! c.priorities.empty())
    {
        h << "<h2>" << escapeHtml ("Da fare prima, in quest'ordine"_t) << "</h2>\n<ol class=\"prio\">\n";
        for (const auto& f : c.priorities)
        {
            // l'intervento concreto, a meno che ripeta il valore (mosse EQ): allora la spiegazione
            const auto& text = f.action.isNotEmpty() && ! f.action.contains (f.value) ? f.action : f.message;
            h << "<li><span class=\"tag " << severityClass (f.severity) << "\">" << escapeHtml (severityLabel (f.severity)) << "</span><b>"
              << escapeHtml (f.metric) << "</b>: " << escapeHtml (f.value) << "<br>" << escapeHtml (text) << "</li>\n";
        }
        h << "</ol>\n";
    }

    // --- confronto con la versione salvata ----------------------------------------------------------
    if (ctx.hasVersion)
    {
        const auto& vc = ctx.versionComparison;
        h << "<h2>" << escapeHtml ("Rispetto a %1"_t.replace ("%1", ctx.versionName)) << "</h2>\n"
          << "<p><span class=\"v-ok\">&#9650; " << vc.improved << " " << escapeHtml ("migliorate"_t) << "</span> &nbsp; "
          << "<span class=\"" << (vc.worsened > 0 ? "v-critical" : "muted") << "\">&#9660; " << vc.worsened << " " << escapeHtml ("peggiorate"_t) << "</span> &nbsp; "
          << "<span class=\"muted\">" << vc.unchanged << " " << escapeHtml ("invariate"_t) << "</span> &nbsp; &middot; &nbsp; "
          << escapeHtml ("Punteggio"_t) << " " << vc.scoreBefore << " &rarr; <b>" << vc.scoreNow << "</b></p>\n";
        if (vc.changes.empty())
        {
            h << "<p class=\"muted\">" << escapeHtml ("Nessuna differenza rilevante rispetto alla versione salvata."_t) << "</p>\n";
        }
        else
        {
            h << "<table>\n<thead><tr><th></th><th>" << escapeHtml ("Misura"_t) << "</th><th class=\"num\">" << escapeHtml ("Prima"_t)
              << "</th><th class=\"num\">" << escapeHtml ("Adesso"_t) << "</th></tr></thead>\n<tbody>\n";
            for (const auto& ch : vc.changes)
            {
                const bool better = ch.verdict == Verdict::better;
                h << "<tr><td class=\"" << (better ? "v-ok" : "v-critical") << "\">" << (better ? "&#9650;" : "&#9660;") << "</td><td>"
                  << escapeHtml (ch.metric) << "</td><td class=\"num v-" << severityClass (ch.severityBefore) << "\">" << escapeHtml (ch.valueBefore)
                  << "</td><td class=\"num v-" << severityClass (ch.severityNow) << "\">" << escapeHtml (ch.valueNow) << "</td></tr>\n";
            }
            h << "</tbody></table>\n";
        }
    }

    // --- misure -----------------------------------------------------------------------------------------
    h << "<h2>" << escapeHtml ("Misure"_t) << "</h2>\n<div class=\"grid2\">\n";
    h << metricTable ("Loudness"_t, {
            { "Integrated"_t, num (s.integratedLufs) + " LUFS", highlight::integrated, {} },
            { "Loudness Range (LRA)"_t, s.liveMode ? juce::String ("n/d") : num (s.loudnessRange) + " LU", highlight::lra, {} },
            { "Max short-term"_t, num (s.maxShortTermLufs) + " LUFS", {}, {} },
            { "Max momentary"_t, num (s.maxMomentaryLufs) + " LUFS", {}, {} },
            { "RMS"_t, num (s.rmsDb) + " dBFS", {}, {} } }, c);
    h << metricTable ("Picchi e controlli tecnici"_t, {
            { "True peak max"_t, num (s.truePeakMaxDb, 2) + " dBTP", highlight::truePeak, {} },
            { "True peak L / R"_t, num (s.truePeakDb[0], 2) + " / " + num (s.truePeakDb[1], 2) + " dBTP", {}, {} },
            { "Sample peak L / R"_t, num (s.samplePeakDb[0], 2) + " / " + num (s.samplePeakDb[1], 2) + " dBFS", {}, {} },
            { "Eventi oltre -1 dBTP"_t, juce::String (s.overs1dB), {}, {} },
            { "Eventi oltre 0 dBTP"_t, juce::String (s.overs0dB), highlight::overs, {} },
            { "Clipping (plateau)"_t, juce::String (s.clipEvents) + " (" + "%1 a fondo scala"_t.replace ("%1", juce::String (s.clipEventsFullScale)) + ")",
              highlight::clip, {} },
            { "DC offset L / R"_t, num (s.dcOffsetDb[0]) + " / " + num (s.dcOffsetDb[1]) + " dBFS", highlight::dc, {} } }, c);
    h << metricTable ("Dinamica"_t, {
            { "PLR (picco / loudness)"_t, num (s.plr) + " dB", highlight::plr, {} },
            { "PSR minimo (sezione più forte)"_t, num (s.minPsr) + " dB", highlight::psr, {} },
            { "DR (Dynamic Range)"_t, s.drValid && ! s.liveMode ? juce::String (s.drValue, 1) : juce::String ("n/d"), highlight::dr, {} },
            { "Crest factor"_t, num (s.crestDb) + " dB", {}, {} },
            { "Crest factor bassi (<200 Hz)"_t, num (s.bandCrestDb[0]) + " dB", highlight::crestLow, {} },
            { "Crest factor medi"_t, num (s.bandCrestDb[1]) + " dB", highlight::crestMid, {} },
            { "Crest factor alti (>4 kHz)"_t, num (s.bandCrestDb[2]) + " dB", highlight::crestHigh, {} } }, c);
    if (s.numChannels > 1)
        h << metricTable ("Stereo"_t, {
                { "Correlazione di fase"_t, juce::String (s.correlation, 2), highlight::correlation, {} },
                { "Larghezza stereo"_t, juce::String (juce::roundToInt (s.widthPercent)) + "%", highlight::width, {} },
                { "Side sotto 100 Hz"_t, juce::String (juce::roundToInt (s.lowEndWidthPercent)) + "%", highlight::lowEndWidth, "Side" },
                { "Correlazione sotto 100 Hz"_t, juce::String (s.lowEndCorrelation, 2), highlight::lowEndWidth, "Correlazione" },
                { "Bilanciamento L/R"_t, signedNum (s.balanceDb) + " dB", highlight::balance, "Bilanciamento" },
                { "Perdita in mono"_t, num (s.monoLossDb) + " dB", highlight::monoLoss, {} } }, c);
    else
        h << "<div>\n<h3>" << escapeHtml ("Stereo"_t) << "</h3>\n<p class=\"muted\">" << escapeHtml ("File mono: nessuna misura stereo."_t) << "</p>\n</div>\n";
    h << "</div>\n";

    // --- bilanciamento tonale ---------------------------------------------------------------------------
    h << "<h2>" << escapeHtml ("Bilanciamento tonale"_t) << "</h2>\n<p>"
      << escapeHtml ("Tilt spettrale"_t) << ": <b class=\"" << (c.severityOfKey (highlight::tilt) >= Severity::warning
                                                                   ? juce::String ("v-") + severityClass (c.severityOfKey (highlight::tilt)) : juce::String())
      << "\">" << juce::String (s.spectralTiltDbPerOct, 2) << " dB/oct</b> &nbsp;&middot;&nbsp; "
      << escapeHtml ("Centroide"_t) << ": <b>" << juce::roundToInt (s.spectralCentroidHz) << " Hz</b>";
    if (c.hasTonalTarget)
        h << " &nbsp;&middot;&nbsp; " << escapeHtml ("Confronto"_t) << ": <b>"
          << escapeHtml (c.tonalUsesLoudest ? "sezione più forte"_t : "brano intero"_t) << "</b>";
    h << "</p>\n" << tonalChartSvg (s, c) << "<div class=\"legend\"><i style=\"border-color:#0a7fc4\"></i>" << escapeHtml ("Master"_t);
    if (c.hasTonalTarget)
        h << "<i style=\"border-color:#a87b00;border-top-style:dashed\"></i>" << escapeHtml ("Target"_t)
          << "<i style=\"border-top:8px solid rgba(168,123,0,.2)\"></i>" << escapeHtml ("Tolleranza della banda"_t);
    h << " &nbsp; <span class=\"muted\">" << escapeHtml ("Terzi d'ottava, curve normalizzate (media 63 Hz - 12.5 kHz = 0 dB)."_t) << "</span></div>\n";
    if (! c.hasTonalTarget)
        h << "<p class=\"note\">" << escapeHtml ("Il target non ha una curva tonale: lo spettro è solo informativo."_t) << "</p>\n";

    h << "<table>\n<thead><tr><th>" << escapeHtml ("Banda"_t) << "</th><th>" << escapeHtml ("Frequenze"_t) << "</th><th class=\"num\">"
      << escapeHtml ("Energia"_t) << "</th>";
    if (c.hasTonalTarget)
        h << "<th class=\"num\">" << escapeHtml ("Rispetto al target"_t) << "</th><th class=\"num\">" << escapeHtml ("Tolleranza"_t) << "</th><th>"
          << escapeHtml ("Stato"_t) << "</th>";
    h << "</tr></thead>\n<tbody>\n";
    for (int b = 0; b < kNumBands; ++b)
    {
        h << "<tr><td>" << kBandNames[(size_t) b] << "</td><td class=\"muted\">" << bandRange (b) << "</td><td class=\"num\">"
          << num (s.bandEnergyDb[(size_t) b]) << " dB</td>";
        if (c.hasTonalTarget)
        {
            const auto sev = c.severityOfKey (highlight::band (b));
            h << "<td class=\"num" << (sev >= Severity::warning ? juce::String (" v-") + severityClass (sev) : juce::String()) << "\">"
              << signedNum (c.bandTonalDelta[(size_t) b]) << " dB</td><td class=\"num muted\">&plusmn;" << juce::String (c.bandTolerance[(size_t) b], 1)
              << " dB</td><td><span class=\"tag " << severityClass (sev) << "\">" << escapeHtml (severityLabel (sev)) << "</span></td>";
        }
        h << "</tr>\n";
    }
    h << "</tbody></table>\n";

    // --- mosse EQ -----------------------------------------------------------------------------------------
    if (! c.eqMoves.empty())
    {
        h << "<h2>" << escapeHtml ("Mosse EQ suggerite"_t) << "</h2>\n<table>\n<thead><tr><th>#</th><th>" << escapeHtml ("Mossa"_t)
          << "</th><th class=\"num\">" << escapeHtml ("Stimato"_t) << "</th><th>" << escapeHtml ("Corregge"_t) << "</th></tr></thead>\n<tbody>\n";
        for (size_t i = 0; i < c.eqMoves.size(); ++i)
        {
            const auto& m = c.eqMoves[i];
            juce::StringArray bands;
            for (int b : m.bands)
                bands.add (kBandNames[(size_t) b]);
            h << "<tr><td>" << (int) (i + 1) << "</td><td><b>" << escapeHtml (m.describe (m.shownGainDb)) << "</b>"
              << (m.fixInMix ? " <span class=\"muted\">" + escapeHtml ("(il resto va sistemato nel mix)"_t) + "</span>" : juce::String())
              << "</td><td class=\"num\">" << signedNum (m.gainDb) << " dB</td><td>" << escapeHtml (bands.joinIntoString (", ")) << "</td></tr>\n";
        }
        h << "</tbody></table>\n<p class=\"muted\">"
          << escapeHtml ("Il guadagno indicato è quello da usare; \"stimato\" è lo scostamento misurato, spesso da correggere in parte nel mix."_t) << "</p>\n";
    }

    // --- diagnosi -----------------------------------------------------------------------------------------
    h << "<h2>" << escapeHtml ("Diagnosi"_t) << "</h2>\n";
    int problems = 0;
    for (const auto& f : c.findings)
        if (! f.ignored && f.severity != Severity::ok)
        {
            h << findingCard (f);
            ++problems;
        }
    if (problems == 0)
        h << "<p class=\"muted\">" << escapeHtml ("Nessun problema: tutte le misure sono nel range del target."_t) << "</p>\n";

    auto findingTable = [&h] (const juce::String& title, const std::vector<const Finding*>& list)
    {
        if (list.empty())
            return;
        h << "<h2>" << escapeHtml (title) << " (" << (int) list.size() << ")</h2>\n<table>\n<thead><tr><th>" << escapeHtml ("Area"_t) << "</th><th>"
          << escapeHtml ("Misura"_t) << "</th><th class=\"num\">" << escapeHtml ("Valore"_t) << "</th><th>" << escapeHtml ("Target"_t)
          << "</th></tr></thead>\n<tbody>\n";
        for (const auto* f : list)
            h << "<tr><td class=\"muted\">" << escapeHtml (f->area != Area::none ? areaName (f->area) : f->category) << "</td><td>" << escapeHtml (f->metric) << "</td><td class=\"num\">"
              << escapeHtml (f->value) << "</td><td class=\"tgt\">" << escapeHtml (f->target != "-" ? f->target : juce::String()) << "</td></tr>\n";
        h << "</tbody></table>\n";
    };

    std::vector<const Finding*> inRange, ignored;
    for (const auto& f : c.findings)
    {
        if (f.ignored)
            ignored.push_back (&f);
        else if (f.severity == Severity::ok)
            inRange.push_back (&f);
    }
    findingTable ("Nel range"_t, inRange);
    findingTable ("Ignorate (scelte volute)"_t, ignored);

    h << streamingSection (ctx);
    h << screenshotSection (ctx.screenshotPng, "Dashboard"_t);
    h << htmlEnd (ctx.pluginVersion, ctx.date);
    return h;
}

juce::String buildAlbumHtmlReport (const AlbumResult& album, const juce::String& pluginVersion, const juce::MemoryBlock& screenshotPng, juce::Time date)
{
    juce::String h = htmlBegin ("MasterAgent - " + "Coerenza album"_t);

    h << "<header class=\"top\">\n<div>\n<div class=\"brand\">MasterAgent</div>\n<h1>" << escapeHtml ("Coerenza album"_t) << "</h1>\n"
      << "<div class=\"subtitle\">" << escapeHtml ("%1 brani confrontati con la mediana dell'album"_t.replace ("%1", juce::String ((int) album.tracks.size())))
      << "</div>\n</div>\n<div class=\"meta\">\n" << metaLine ("Data"_t, formatDate (date));
    if (pluginVersion.isNotEmpty())
        h << metaLine ("Versione"_t, pluginVersion);
    h << "</div>\n</header>\n";

    const int toFix = album.critical + album.warning;
    h << "<div class=\"grid2\">\n<div><table><tbody>\n"
      << "<tr><td>" << escapeHtml ("Sezione più forte, mediana"_t) << "</td><td class=\"num\">" << num (album.medianLoudestLufs) << " LUFS</td></tr>\n"
      << "<tr><td>" << escapeHtml ("differenza massima"_t) << "</td><td class=\"num\">" << juce::String (album.loudestSpreadLu, 1) << " LU</td></tr>\n"
      << "<tr><td>" << escapeHtml ("Integrated, mediana"_t) << "</td><td class=\"num\">" << num (album.medianIntegratedLufs) << " LUFS</td></tr>\n"
      << "<tr><td>" << escapeHtml ("True peak, mediana"_t) << "</td><td class=\"num\">" << num (album.medianTruePeakDb) << " dBTP</td></tr>\n"
      << "</tbody></table></div>\n<div><table><tbody>\n"
      << "<tr><td>" << escapeHtml ("Confronto tonale"_t) << "</td><td>"
      << escapeHtml (album.usesLoudestSection ? "sezione più forte di ogni brano"_t : "brano intero"_t) << "</td></tr>\n"
      << "<tr><td>" << escapeHtml ("Brani da sistemare"_t) << "</td><td><b class=\"" << (album.critical > 0 ? "v-critical" : (toFix > 0 ? "v-warning" : "v-ok"))
      << "\">" << toFix << "</b> &nbsp;<span class=\"muted\">(" << escapeHtml ("critici"_t) << " " << album.critical << ", "
      << escapeHtml ("da verificare"_t) << " " << album.warning << ")</span></td></tr>\n"
      << "</tbody></table></div>\n</div>\n";

    // tabella dei brani: celle colorate come nella vista album
    auto cell = [] (const juce::String& text, Severity sev)
    {
        return "<td class=\"num" + (sev >= Severity::warning ? juce::String (" c-") + severityClass (sev) : juce::String()) + "\">" + text + "</td>";
    };

    h << "<h2>" << escapeHtml ("Brani"_t) << "</h2>\n<table>\n<thead><tr><th>#</th><th>" << escapeHtml ("Brano"_t) << "</th><th class=\"num\">Int.</th><th class=\"num\">"
      << escapeHtml ("Forte"_t) << "</th><th class=\"num\">Delta</th><th class=\"num\">Gain</th><th class=\"num\">TP</th><th class=\"num\">"
      << escapeHtml ("Largh."_t) << "</th>";
    for (auto* band : kBandNames)
        h << "<th class=\"num\">" << band << "</th>";
    h << "</tr></thead>\n<tbody>\n";
    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& tr = album.tracks[i];
        h << "<tr><td>" << (int) i + 1 << "</td><td><span class=\"dot " << severityClass (tr.worst) << "\"></span>" << escapeHtml (tr.name) << "</td>"
          << "<td class=\"num\">" << num (tr.integratedLufs) << "</td>" << cell (num (tr.loudestLufs), tr.loudness)
          << cell (signedNum (tr.loudestDeltaLu), tr.loudness) << cell (tr.loudness == Severity::ok ? juce::String ("-") : signedNum (tr.gainDb), tr.loudness)
          << "<td class=\"num\">" << num (tr.truePeakDb) << "</td><td class=\"num\">" << juce::roundToInt (tr.widthPercent) << "%</td>";
        for (int b = 0; b < kNumBands; ++b)
            h << cell (signedNum (tr.bandDelta[(size_t) b]), tr.bandSeverity[(size_t) b]);
        h << "</tr>\n";
    }
    h << "</tbody></table>\n<p class=\"muted\">"
      << escapeHtml ("Delta e bande: scostamento rispetto alla mediana dell'album; celle colorate = fuori tolleranza rispetto all'album."_t) << "</p>\n";

    // diagnosi per brano
    h << "<h2>" << escapeHtml ("Diagnosi per brano"_t) << "</h2>\n";
    bool any = false;
    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& tr = album.tracks[i];
        if (tr.findings.empty())
            continue;
        any = true;
        h << "<h3>" << (int) i + 1 << ". " << escapeHtml (tr.name) << "</h3>\n";
        for (const auto& f : tr.findings)
            h << findingCard (f);
    }
    if (! any)
        h << "<p class=\"muted\">"
          << escapeHtml ("Nessun brano fuori dall'album: loudness della sezione più forte e bilanciamento tonale sono coerenti."_t) << "</p>\n";

    h << screenshotSection (screenshotPng, "Vista album"_t);
    h << htmlEnd (pluginVersion, date);
    return h;
}

} // namespace ma
