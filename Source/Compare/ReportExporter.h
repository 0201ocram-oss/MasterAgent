#pragma once

#include "../Analysis/AnalysisSnapshot.h"
#include "AlbumCheck.h"
#include "Comparator.h"
#include "VersionCompare.h"

#include <juce_core/juce_core.h>

namespace ma
{

juce::String buildTextReport (const AnalysisSnapshot& s, const ComparisonResult& c, const juce::String& targetName);
juce::String buildJsonReport (const AnalysisSnapshot& s, const ComparisonResult& c, const juce::String& targetName);

/** Tutto ciò che serve al report HTML, copiato al momento dell'export: il file corrisponde a ciò che si vede. */
struct ReportContext
{
    AnalysisSnapshot snapshot;
    ComparisonResult comparison;
    juce::String targetName, targetDescription;
    ProfileContext profileContext = ProfileContext::music;
    juce::String source;                 // nome del file analizzato, o l'ingresso della DAW
    juce::String pluginVersion;
    juce::Time date = juce::Time::getCurrentTime();

    bool hasVersion = false;             // confronto con una versione salvata
    juce::String versionName;
    VersionComparison versionComparison;

    juce::MemoryBlock screenshotPng;     // PNG della dashboard, vuoto = senza immagine
};

/**
    Report in un unico file HTML: CSS chiaro e indipendente dal tema, pronto per la stampa in PDF dal browser.
    Pagella, priorità, misure, bilanciamento tonale con grafico SVG, mosse EQ, diagnosi, anteprima streaming,
    confronto con la versione salvata e screenshot della dashboard (PNG incorporato in base64).
*/
juce::String buildHtmlReport (const ReportContext& context);

/** Coerenza album in HTML: riepilogo, tabella dei brani con le celle colorate e diagnosi per brano. */
juce::String buildAlbumHtmlReport (const AlbumResult& album, const juce::String& pluginVersion,
                                   const juce::MemoryBlock& screenshotPng = {}, juce::Time date = juce::Time::getCurrentTime());

/** Testo sicuro dentro l'HTML: & < > " diventano entità. */
juce::String escapeHtml (const juce::String& text);

} // namespace ma
