#include "TestSignals.h"
#include "Compare/AlbumCheck.h"
#include "Compare/ReportExporter.h"
#include "Compare/TargetProfile.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    int count (const juce::String& text, const juce::String& what)
    {
        int n = 0;
        for (int i = text.indexOf (what); i >= 0; i = text.indexOf (i + what.length(), what))
            ++n;
        return n;
    }

    ma::ReportContext makeContext (const char* profileId, const juce::String& source)
    {
        static ma::ProfileLibrary lib;
        const auto& profile = *lib.findById (profileId);

        ma::ReportContext ctx;
        ctx.snapshot = test::analyse (test::pinkNoise (20.0, 0.5f));
        ctx.comparison = ma::compare (ctx.snapshot, profile);
        ctx.targetName = profile.name;
        ctx.targetDescription = profile.description;
        ctx.profileContext = profile.context;
        ctx.source = source;
        ctx.pluginVersion = "1.0.0";
        return ctx;
    }
}

TEST_CASE ("HTML: il testo viene messo in sicurezza", "[report][html]")
{
    CHECK (ma::escapeHtml ("a<b & \"c\">") == "a&lt;b &amp; &quot;c&quot;&gt;");
    CHECK (ma::escapeHtml ("&amp;") == "&amp;amp;");

    // un nome di file con caratteri speciali non rompe il documento
    const auto html = ma::buildHtmlReport (makeContext ("pop", "<mix&v3>.wav"));
    CHECK (html.contains ("&lt;mix&amp;v3&gt;.wav"));
    CHECK_FALSE (html.contains ("<mix&v3>"));
}

TEST_CASE ("HTML: documento completo con misure e grafico tonale, senza consigli", "[report][html]")
{
    const auto ctx = makeContext ("pop", "mix_v3.wav");
    const auto html = ma::buildHtmlReport (ctx);

    CHECK (html.startsWith ("<!DOCTYPE html>"));
    CHECK (html.trimEnd().endsWith ("</html>"));
    CHECK (html.contains ("<meta charset=\"utf-8\">"));
    CHECK (html.contains ("@media print"));
    CHECK (count (html, "<section") == count (html, "</section>"));
    CHECK (count (html, "<table") == count (html, "</table>"));
    CHECK (count (html, "<div") == count (html, "</div>"));

    CHECK (html.contains ("mix_v3.wav"));
    CHECK (html.contains ("Integrated"));
    CHECK (html.contains ("True peak max"));
    CHECK (html.contains ("PLR"));

    // grafico tonale: target, fascia di tolleranza e master
    CHECK (html.contains ("<svg"));
    CHECK (html.contains ("<polygon"));
    CHECK (count (html, "<polyline") == 2);
    for (auto* band : ma::kBandNames)
        CHECK (html.contains (band));

    // solo i dati del master: niente pagella, priorità, mosse EQ né diagnosi con i consigli
    REQUIRE_FALSE (ctx.comparison.findings.empty());
    CHECK_FALSE (html.contains ("class=\"ring"));
    CHECK_FALSE (html.contains ("Pagella"));
    CHECK_FALSE (html.contains ("<div class=\"card"));
    CHECK_FALSE (html.contains ("Mosse EQ"));
    for (const auto& f : ctx.comparison.findings)
    {
        if (f.action.isNotEmpty())
            CHECK_FALSE (html.contains (ma::escapeHtml (f.action)));
        if (f.severity != ma::Severity::ok)
            CHECK_FALSE (html.contains (ma::escapeHtml (f.message)));
    }

    CHECK (html.contains ("Spotify"));            // anteprima streaming
    CHECK_FALSE (html.contains ("data:image"));   // nessuno screenshot fornito
}

TEST_CASE ("HTML: immagine incorporata e versione confrontata", "[report][html]")
{
    auto ctx = makeContext ("pop", "master.wav");

    const char png[] = { '\x89', 'P', 'N', 'G', 1, 2, 3 };
    ctx.screenshotPng = juce::MemoryBlock (png, sizeof (png));

    ctx.hasVersion = true;
    ctx.versionName = "v1 <prima>";
    ctx.versionComparison.improved = 2;
    ctx.versionComparison.scoreBefore = 40;
    ctx.versionComparison.scoreNow = 72;
    ma::FindingChange change;
    change.metric = "PLR";
    change.valueBefore = "6.1 dB";
    change.valueNow = "8.0 dB";
    change.verdict = ma::Verdict::better;
    ctx.versionComparison.changes.push_back (change);

    const auto html = ma::buildHtmlReport (ctx);
    CHECK (html.contains ("data:image/png;base64," + juce::Base64::toBase64 (png, sizeof (png))));
    CHECK (html.contains ("v1 &lt;prima&gt;"));
    CHECK (html.contains ("6.1 dB"));
    CHECK (html.contains ("8.0 dB"));
    CHECK_FALSE (html.contains ("<b>72</b>"));   // il punteggio resta nel plugin
}

TEST_CASE ("HTML: con un target broadcast niente curva tonale e streaming solo indicativo", "[report][html][broadcast]")
{
    const auto html = ma::buildHtmlReport (makeContext ("ebu_r128", "spot_tv.wav"));
    CHECK (html.contains ("EBU R128"));
    CHECK (html.contains ("<svg"));               // lo spettro resta, come informazione
    CHECK_FALSE (html.contains ("<polygon"));     // ma senza target né tolleranze
    CHECK (count (html, "<polyline") == 1);
    CHECK (html.contains ("standard di consegna"));
}

TEST_CASE ("HTML: coerenza album con la tabella dei brani, senza consigli", "[report][html][album]")
{
    std::vector<ma::AnalysisSnapshot> snapshots;
    juce::StringArray names;
    for (int i = 0; i < 3; ++i)
    {
        auto b = test::pinkNoise (20.0, 0.5f, 20 + i);
        if (i == 1)
            b.applyGain (juce::Decibels::decibelsToGain (4.0f));   // un brano molto più forte degli altri
        snapshots.push_back (test::analyse (b));
        names.add ("0" + juce::String (i + 1) + " brano & co.wav");
    }

    const auto album = ma::checkAlbum (snapshots, names);
    REQUIRE (album.isValid());
    const auto html = ma::buildAlbumHtmlReport (album, "1.0.0");

    CHECK (html.startsWith ("<!DOCTYPE html>"));
    CHECK (html.trimEnd().endsWith ("</html>"));
    CHECK (count (html, "<table") == count (html, "</table>"));
    CHECK (count (html, "<div") == count (html, "</div>"));
    CHECK (html.contains ("02 brano &amp; co.wav"));
    CHECK (html.contains ("c-critical"));         // la sezione forte del brano 2 è fuori dall'album
    CHECK_FALSE (html.contains ("<div class=\"card"));
    for (const auto& f : album.tracks[1].findings)
        if (f.action.isNotEmpty())
            CHECK_FALSE (html.contains (ma::escapeHtml (f.action)));
}
