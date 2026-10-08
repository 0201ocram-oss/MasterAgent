#include "TestSignals.h"
#include "Analysis/AnalysisEngine.h"
#include "Compare/Comparator.h"

#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    ma::AnalysisSnapshot fakeMix (float integrated, float truePeak)
    {
        ma::AnalysisSnapshot s;
        s.valid = true;
        s.numChannels = 2;
        s.secondsAnalyzed = 180.0;
        s.integratedLufs = integrated;
        s.truePeakMaxDb = truePeak;
        s.plr = truePeak - integrated;
        s.minPsr = s.plr - 1.0f;
        s.loudnessRange = 5.0f;
        s.correlation = 0.6f;
        s.widthPercent = 20.0f;
        s.bandCorrelation.fill (0.9f);
        s.bandWidthPercent = { 1.0f, 2.0f, 10.0f, 18.0f, 25.0f, 28.0f, 30.0f, 32.0f };
        s.lowEndWidthPercent = 1.0f;
        s.spectralTiltDbPerOct = -2.3f;
        s.subRumbleDb = -30.0f;
        s.dcOffsetDb = { -120.0f, -120.0f };
        return s;
    }

    const ma::Finding* findMetric (const ma::ComparisonResult& r, const juce::String& metric)
    {
        for (const auto& f : r.findings)
            if (f.metric == metric)
                return &f;
        return nullptr;
    }

    const ma::Finding* findKey (const ma::ComparisonResult& r, const juce::String& key)
    {
        for (const auto& f : r.findings)
            if (f.key == key)
                return &f;
        return nullptr;
    }

    ma::CompareOptions phase (ma::WorkPhase p)
    {
        ma::CompareOptions o;
        o.phase = p;
        return o;
    }

    /** Rumore filtrato passa-banda (biquad RBJ), stereo correlato. */
    juce::AudioBuffer<float> bandNoise (double seconds, float centre, float q, float amplitude, int seed)
    {
        auto b = test::whiteNoise (seconds, amplitude, true, seed);
        juce::dsp::IIR::Filter<float> filter (juce::dsp::IIR::Coefficients<float>::makeBandPass (test::kSampleRate, centre, q));
        auto* l = b.getWritePointer (0);
        auto* r = b.getWritePointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            l[i] = filter.processSample (l[i]);
            r[i] = l[i];
        }
        return b;
    }

    /** Somma src a dst moltiplicato per gate(t) (0..1). */
    template <typename Gate>
    void addGated (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src, Gate gate)
    {
        const int n = std::min (dst.getNumSamples(), src.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                dst.addSample (ch, i, src.getSample (ch, i) * gate (i / test::kSampleRate));
    }
}

//==============================================================================
TEST_CASE ("Fase Mix: la loudness non è un obiettivo, conta il margine per il mastering", "[mix]")
{
    ma::ProfileLibrary lib;
    const auto* pop = lib.findById ("pop");
    REQUIRE (pop != nullptr);

    const auto mix = ma::compare (fakeMix (-18.0f, -4.5f), *pop, phase (ma::WorkPhase::mix));
    const auto* integrated = findKey (mix, ma::highlight::integrated);
    REQUIRE (integrated != nullptr);
    CHECK (integrated->severity == ma::Severity::info);

    const auto* headroom = findMetric (mix, "Headroom per il mastering");
    REQUIRE (headroom != nullptr);
    CHECK (headroom->severity == ma::Severity::ok);

    for (const auto& f : mix.findings)
    {
        INFO (f.metric.toStdString());
        CHECK_FALSE ((f.severity >= ma::Severity::warning && (f.category == "Loudness" || f.metric.startsWith ("PLR") || f.metric.startsWith ("True peak"))));
    }

    // lo stesso mix valutato come master è troppo basso per il pop
    const auto master = ma::compare (fakeMix (-18.0f, -4.5f), *pop, phase (ma::WorkPhase::master));
    REQUIRE (findKey (master, ma::highlight::integrated) != nullptr);
    CHECK (findKey (master, ma::highlight::integrated)->severity >= ma::Severity::warning);
}

TEST_CASE ("Fase Mix: picchi vicini a 0 dBTP sono critici, con un'azione concreta", "[mix]")
{
    ma::ProfileLibrary lib;
    const auto r = ma::compare (fakeMix (-14.0f, -0.5f), *lib.findById ("pop"), phase (ma::WorkPhase::mix));
    const auto* headroom = findMetric (r, "Headroom per il mastering");
    REQUIRE (headroom != nullptr);
    CHECK (headroom->severity == ma::Severity::critical);
    CHECK (headroom->action.contains ("master fader"));

    const auto r2 = ma::compare (fakeMix (-16.0f, -2.0f), *lib.findById ("pop"), phase (ma::WorkPhase::mix));
    CHECK (findMetric (r2, "Headroom per il mastering")->severity == ma::Severity::warning);
}

TEST_CASE ("Fase Mix: un mix già limitato viene segnalato", "[mix]")
{
    ma::ProfileLibrary lib;
    const auto r = ma::compare (fakeMix (-10.0f, -4.0f), *lib.findById ("pop"), phase (ma::WorkPhase::mix));   // PLR 6 dB
    const auto* limiting = findMetric (r, "Limiting sul mix bus");
    REQUIRE (limiting != nullptr);
    CHECK (limiting->severity == ma::Severity::warning);
    CHECK (limiting->action.contains ("senza"));
}

TEST_CASE ("Priorità nell'ordine di lavoro: prima la fase, poi tonale, per ultimo il loudness", "[compare]")
{
    ma::ProfileLibrary lib;
    auto s = fakeMix (-5.0f, 0.5f);   // troppo forte, overs
    s.overs0dB = 4;
    s.correlation = -0.4f;              // fase invertita
    const auto r = ma::compare (s, *lib.findById ("spotify"));

    REQUIRE (r.priorities.size() >= 2);
    for (size_t i = 1; i < r.priorities.size(); ++i)
        CHECK (r.priorities[i - 1].stage <= r.priorities[i].stage);
    CHECK (r.priorities.front().metric == "Fase invertita");
}

//==============================================================================
TEST_CASE ("Raffiche: sibilanti intermittenti contro enfasi costante", "[dynamics]")
{
    const double seconds = 40.0;
    const auto base = test::pinkNoise (seconds, 0.4f, 3);
    const auto hiss = bandNoise (seconds, 7500.0f, 1.2f, 0.6f, 4);

    auto bursty = base;   // raffiche di 100 ms ogni secondo
    addGated (bursty, hiss, [] (double t) { return std::fmod (t, 1.0) < 0.1 ? 1.0f : 0.0f; });
    auto steady = base;   // stessa energia media, sempre presente
    addGated (steady, hiss, [] (double) { return std::sqrt (0.1f); });

    const auto sb = test::analyse (bursty);
    const auto ss = test::analyse (steady);
    INFO ("burst " << sb.bandDynamics[6].burstDb << " dB, steady " << ss.bandDynamics[6].burstDb << " dB");
    CHECK (sb.bandDynamics[6].burstDb > ss.bandDynamics[6].burstDb + 3.0f);
    CHECK (ss.bandDynamics[6].burstDb < 1.5f);

    const auto profile = ma::TargetProfile::fromSnapshot (test::analyse (base), "Base");
    const auto rb = ma::compare (sb, profile);
    const auto rs = ma::compare (ss, profile);
    const auto* fb = findKey (rb, ma::highlight::band (6));
    const auto* fs = findKey (rs, ma::highlight::band (6));
    REQUIRE (fb != nullptr);
    REQUIRE (fs != nullptr);
    INFO (fb->action.toStdString());
    CHECK (fb->severity >= ma::Severity::warning);
    CHECK (fb->action.contains ("a tratti"));
    CHECK (fb->action.contains ("de-esser"));
    CHECK (fs->severity >= ma::Severity::warning);
    CHECK_FALSE (fs->action.contains ("a tratti"));
}

TEST_CASE ("Dove: eccesso a 300 Hz solo tra 20 e 30 s", "[dynamics]")
{
    auto signal = test::pinkNoise (50.0, 0.4f, 5);
    addGated (signal, bandNoise (50.0, 320.0f, 2.0f, 0.8f, 6), [] (double t) { return t >= 20.0 && t < 30.0 ? 1.0f : 0.0f; });

    const auto s = test::analyse (signal);
    const auto& d = s.bandDynamics[2];
    INFO ("finestra " << d.worstStartSec << " - " << d.worstEndSec << " s, +" << d.worstExcessDb << " dB");
    CHECK (d.worstStartSec == Approx (20.0f).margin (2.0f));
    CHECK (d.worstEndSec == Approx (30.0f).margin (2.0f));
    CHECK (d.worstExcessDb > 3.0f);

    // il consiglio indica la sezione invece di un taglio su tutto il brano, e la diagnosi porta il suo tratto di tempo
    const auto profile = ma::TargetProfile::fromSnapshot (test::analyse (test::pinkNoise (50.0, 0.4f, 5)), "Base");
    const auto r = ma::compare (s, profile, phase (ma::WorkPhase::mix));
    const auto* lowMid = findKey (r, ma::highlight::band (2));
    REQUIRE (lowMid != nullptr);
    INFO (lowMid->action.toStdString());
    CHECK (lowMid->severity >= ma::Severity::warning);
    CHECK (lowMid->action.contains ("concentrato tra 0:2"));
    CHECK (lowMid->hasTime());
    CHECK (lowMid->timeStart == Approx (20.0f).margin (2.0f));
}

TEST_CASE ("Dove: il tempo segue il transport dell'host", "[dynamics][live]")
{
    auto signal = test::pinkNoise (40.0, 0.4f, 7);
    addGated (signal, bandNoise (40.0, 320.0f, 2.0f, 0.8f, 8), [] (double t) { return t >= 20.0 && t < 30.0 ? 1.0f : 0.0f; });

    ma::LiveAnalysis live;
    live.prepare (test::kSampleRate, 2);
    constexpr int block = 512;
    const double start = 60.0;   // la riproduzione parte da 1:00
    for (int pos = 0; pos < signal.getNumSamples(); pos += block)
    {
        const int n = std::min (block, signal.getNumSamples() - pos);
        live.push (signal.getReadPointer (0, pos), signal.getReadPointer (1, pos), n, true, start + pos / test::kSampleRate);
    }

    ma::AnalysisSnapshot s;
    for (int i = 0; i < 1000; ++i)
    {
        live.getSnapshot (s);
        if (s.valid && s.secondsAnalyzed >= 39.8)
            break;
        juce::Thread::sleep (10);
    }
    juce::Thread::sleep (250);
    live.getSnapshot (s);
    live.release();

    CHECK (s.historyAligned);
    CHECK (s.historyStartSongSec == Approx (63.0).margin (0.2));
    CHECK (s.bandDynamics[2].worstStartSec == Approx (80.0f).margin (2.0f));
    CHECK (s.bandDynamics[2].worstEndSec == Approx (90.0f).margin (2.0f));
}

//==============================================================================
TEST_CASE ("Sezione più forte: lo spettro del ritornello non è contaminato dalla strofa", "[spectrum]")
{
    // strofa: rumore bianco (brillante) più debole; ritornello: rumore rosa (piatto) più forte
    const auto verse = test::whiteNoise (20.0, 0.05f, true, 9);
    const auto chorus = test::pinkNoise (20.0, 0.5f, 10);
    juce::AudioBuffer<float> song (2, verse.getNumSamples() + chorus.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
    {
        song.copyFrom (ch, 0, verse, ch, 0, verse.getNumSamples());
        song.copyFrom (ch, verse.getNumSamples(), chorus, ch, 0, chorus.getNumSamples());
    }

    const auto s = test::analyse (song);
    REQUIRE (s.loudestSectionSeconds >= 8.0f);
    for (int i = 5; i <= 28; ++i)
    {
        INFO ("terzo " << i << ": " << s.thirdOctaveLoudestDb[(size_t) i] << " dB");
        CHECK (s.thirdOctaveLoudestDb[(size_t) i] == Approx (0.0f).margin (1.0f));
    }
    // sul brano intero la strofa brillante alza le alte rispetto alla sola sezione forte
    CHECK (s.thirdOctaveDb[27] > s.thirdOctaveLoudestDb[27] + 0.5f);
}

TEST_CASE ("Modalità Live: il tonale si confronta con la sezione più forte del target", "[spectrum][compare]")
{
    const auto verse = test::whiteNoise (20.0, 0.05f, true, 11);
    const auto chorus = test::pinkNoise (20.0, 0.5f, 12);
    juce::AudioBuffer<float> song (2, verse.getNumSamples() + chorus.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
    {
        song.copyFrom (ch, 0, verse, ch, 0, verse.getNumSamples());
        song.copyFrom (ch, verse.getNumSamples(), chorus, ch, 0, chorus.getNumSamples());
    }
    const auto profile = ma::TargetProfile::fromSnapshot (test::analyse (song), "Ref");
    REQUIRE (profile.tonalCurveLoudest.has_value());

    // un ritornello in loop (Live) uguale a quello del reference: nessuno scostamento tonale
    auto loop = test::analyse (test::pinkNoise (15.0, 0.5f, 13));
    loop.liveMode = true;
    loop.liveWindowSeconds = 20.0;
    const auto r = ma::compare (loop, profile);
    CHECK (r.tonalUsesLoudest);
    for (int b = 1; b < ma::kNumBands; ++b)
    {
        INFO ("banda " << b << ": " << r.bandTonalDelta[(size_t) b]);
        CHECK (std::abs (r.bandTonalDelta[(size_t) b]) <= r.bandTolerance[(size_t) b]);
    }
}

//==============================================================================
TEST_CASE ("Note e risonanze: la tonica non è un difetto, un picco fuori nota sì", "[spectrum][resonance]")
{
    SECTION ("La2 con la sua ottava")
    {
        auto b = test::pinkNoise (30.0, 0.3f, 14);
        const auto a2 = test::sine (110.0, -18.0, 30.0);
        const auto a3 = test::sine (220.0, -24.0, 30.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            b.addFrom (ch, 0, a2, ch, 0, b.getNumSamples());
            b.addFrom (ch, 0, a3, ch, 0, b.getNumSamples());
        }
        const auto s = test::analyse (b);

        const ma::Resonance* peak = nullptr;
        for (const auto& r : s.resonances)
            if (std::abs (r.frequency - 110.0f) < 3.0f)
                peak = &r;
        REQUIRE (peak != nullptr);
        CHECK (peak->frequency == Approx (110.0f).margin (0.5f));
        CHECK (peak->musical);
        CHECK (peak->midiNote == 45);
        CHECK (ma::noteName (peak->midiNote) == "La (A2)");
    }

    SECTION ("Picco a 452 Hz, tra La e La#")
    {
        auto b = test::pinkNoise (30.0, 0.3f, 15);
        const auto tone = test::sine (452.0, -18.0, 30.0);
        for (int ch = 0; ch < 2; ++ch)
            b.addFrom (ch, 0, tone, ch, 0, b.getNumSamples());
        const auto s = test::analyse (b);

        const ma::Resonance* peak = nullptr;
        for (const auto& r : s.resonances)
            if (std::abs (r.frequency - 452.0f) < 5.0f)
                peak = &r;
        REQUIRE (peak != nullptr);
        INFO ("cents " << peak->cents);
        CHECK_FALSE (peak->musical);
        CHECK (std::abs (peak->cents) > 35.0f);

        ma::ProfileLibrary lib;
        const auto r = ma::compare (s, *lib.findById ("pop"));
        const ma::Finding* finding = nullptr;
        for (const auto& f : r.findings)
            if (f.metric.startsWith ("Risonanza"))
                finding = &f;
        REQUIRE (finding != nullptr);
        CHECK (finding->severity == ma::Severity::warning);

        // se anche il reference ha lo stesso picco, è un tratto del brano: solo informazione
        ma::CompareOptions options;
        options.reference = &s;
        const auto withRef = ma::compare (s, *lib.findById ("pop"), options);
        for (const auto& f : withRef.findings)
            if (f.metric.startsWith ("Risonanza"))
                CHECK (f.severity == ma::Severity::info);
    }
}

//==============================================================================
TEST_CASE ("Profilo da più reference: tolleranze dalla dispersione reale", "[profiles]")
{
    // tre "brani" con bilanciamenti diversi solo sulle alte
    std::vector<ma::AnalysisSnapshot> refs;
    juce::StringArray names;
    for (int k = 0; k < 3; ++k)
    {
        auto b = test::pinkNoise (20.0, 0.4f, 20 + k);
        juce::dsp::IIR::Filter<float> shelfL (juce::dsp::IIR::Coefficients<float>::makeHighShelf (test::kSampleRate, 6000.0f, 0.707f,
                                                                                                 juce::Decibels::decibelsToGain (-3.0f + 3.0f * (float) k)));
        auto* l = b.getWritePointer (0);
        auto* r = b.getWritePointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            l[i] = shelfL.processSample (l[i]);
            r[i] = l[i];
        }
        refs.push_back (test::analyse (b));
        names.add ("ref" + juce::String (k) + ".wav");
    }

    const auto p = ma::TargetProfile::fromSnapshots (refs, "Genere", names);
    REQUIRE (p.bandTolerance.has_value());
    CHECK (p.sourceCount == 3);
    CHECK (p.isReference);
    // alte molto variabili tra i brani: tolleranza più larga che sui medi, mai sotto il minimo
    CHECK ((*p.bandTolerance)[7] > (*p.bandTolerance)[3] + 0.5f);
    CHECK ((*p.bandTolerance)[3] >= 1.0f);

    // round-trip JSON con i nuovi campi
    const auto parsed = ma::TargetProfile::fromJson (p.toJson());
    REQUIRE (parsed.has_value());
    REQUIRE (parsed->bandTolerance.has_value());
    CHECK ((*parsed->bandTolerance)[7] == Approx ((*p.bandTolerance)[7]).margin (0.01f));
    CHECK (parsed->sources.size() == 3);
    CHECK (parsed->sourceCount == 3);
    CHECK (parsed->tonalCurveLoudest.has_value() == p.tonalCurveLoudest.has_value());

    // nessuna delle sorgenti è un problema tonale critico rispetto al profilo
    for (const auto& s : refs)
    {
        const auto r = ma::compare (s, p);
        for (int b = 0; b < ma::kNumBands; ++b)
            CHECK (r.bandTonalDelta[(size_t) b] <= 2.0f * r.bandTolerance[(size_t) b]);
    }
}
