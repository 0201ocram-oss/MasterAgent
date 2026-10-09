// Strumento di sviluppo: analizza un master sintetico e un reference e salva la dashboard in PNG.
// Uso: MasterAgentPreview <output.png>

#include "Analysis/ReferenceAnalyzer.h"
#include "Compare/Comparator.h"
#include "Compare/AlbumCheck.h"
#include "UI/AlbumView.h"
#include "UI/HeaderBar.h"
#include "UI/HelpTexts.h"
#include "Common/Text.h"
#include "Analysis/SnapshotIO.h"
#include "UI/LoudnessPanel.h"
#include "UI/ReportPanel.h"
#include "UI/SpectrumPanel.h"
#include "UI/StereoPanel.h"
#include "UI/StartView.h"
#include "UI/WaveformPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
    juce::AudioBuffer<float> makeTrack (double sr, double seconds, float bassGain, float highGain, float drive, int seed)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random rng (seed);
        const double twoPi = juce::MathConstants<double>::twoPi;
        double pinkL = 0.0, pinkR = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double beat = std::fmod (t, 0.5);
            const double kick = 0.7 * std::exp (-beat * 14.0) * std::sin (twoPi * (50.0 + 80.0 * std::exp (-beat * 30.0)) * beat);
            const double bassF = std::fmod (t, 4.0) < 2.0 ? 55.0 : 73.4;
            const double bass = bassGain * 0.3 * std::sin (twoPi * bassF * t);
            const double chordL = 0.06 * (std::sin (twoPi * 220.0 * t) + std::sin (twoPi * 277.2 * t) + std::sin (twoPi * 329.6 * t));
            const double chordR = 0.06 * (std::sin (twoPi * 220.0 * t + 0.4) + std::sin (twoPi * 277.2 * t + 0.9) + std::sin (twoPi * 329.6 * t + 0.2));
            const double hatEnv = std::exp (-std::fmod (t, 0.125) * 40.0);
            const double nl = rng.nextDouble() * 2.0 - 1.0, nr = rng.nextDouble() * 2.0 - 1.0;
            pinkL = 0.6 * pinkL + 0.4 * nl;
            pinkR = 0.6 * pinkR + 0.4 * nr;
            const double hatsL = highGain * 0.25 * hatEnv * (nl - pinkL);
            const double hatsR = highGain * 0.25 * hatEnv * (nr - pinkR);
            const double section = std::fmod (t, 16.0) < 8.0 ? 0.6 : 1.0;   // strofa / ritornello

            const double l = section * (kick + bass + chordL + hatsL);
            const double r = section * (kick + bass + chordR + hatsR);
            b.setSample (0, i, (float) (std::tanh (l * drive) / std::tanh (drive) * 0.95));
            b.setSample (1, i, (float) (std::tanh (r * drive) / std::tanh (drive) * 0.95));
        }
        return b;
    }

    class Dashboard : public juce::Component
    {
    public:
        Dashboard()
        {
            for (auto* c : std::initializer_list<juce::Component*> { &header, &loudness, &peak, &dynamics, &spectrum, &stereo, &streaming, &report })
                addAndMakeVisible (*c);
            addChildComponent (waveform);
            addChildComponent (start);
            header.setTargets ({ { "pop", "Pop commerciale" } }, "pop");
            header.setReferenceStatus ("reference_mix.wav", ma::ui::colours::reference, true, true);
            setSize (1440, 900);
        }

        void setData (const ma::ui::DashboardData& d)
        {
            for (auto* p : std::initializer_list<ma::ui::Panel*> { &loudness, &peak, &dynamics, &spectrum, &stereo, &streaming, &report, &waveform })
                p->setData (d);
        }

        /** Come l'app standalone: forma d'onda sopra la dashboard, oppure la schermata iniziale. */
        void setStandalone (bool on, bool startScreen)
        {
            standalone = on;
            header.setStandalone (on);
            start.setVisible (on && startScreen);
            waveform.setVisible (on && ! startScreen);
            for (auto* c : std::initializer_list<juce::Component*> { &loudness, &peak, &dynamics, &spectrum, &stereo, &streaming, &report })
                c->setVisible (! (on && startScreen));
            setSize (1440, on ? 960 : 900);
            resized();
            repaint();
        }

        // stesso sfondo e layout dell'editor del plugin
        void paint (juce::Graphics& g) override
        {
            g.setGradientFill (juce::ColourGradient (ma::ui::colours::backgroundTop, 0.0f, 0.0f, ma::ui::colours::backgroundBottom, 0.0f, (float) getHeight(), false));
            g.fillAll();
            if (! ma::ui::getUiSettings().effects)
                return;
            const juce::DropShadow shadow (ma::ui::colours::shadow, 14, { 0, 4 });
            for (auto* c : std::initializer_list<juce::Component*> { &loudness, &peak, &dynamics, &spectrum, &stereo, &streaming, &report, &waveform })
            {
                if (! c->isVisible())
                    continue;
                juce::Path p;
                p.addRoundedRectangle (c->getBounds().toFloat(), 10.0f);
                shadow.drawForPath (g, p);
            }
        }

        void resized() override
        {
            constexpr int gap = 12;
            auto r = getLocalBounds();
            header.setBounds (r.removeFromTop (60));
            start.setBounds (r);
            r = r.reduced (12);
            report.setBounds (r.removeFromRight ((int) (r.getWidth() * 0.27f)));
            r.removeFromRight (gap);
            if (standalone)
            {
                waveform.setBounds (r.removeFromTop (std::max (150, (int) (r.getHeight() * 0.19f))));
                r.removeFromTop (gap);
            }
            auto row1 = r.removeFromTop ((int) (r.getHeight() * 0.34f));
            r.removeFromTop (gap);
            auto row3 = r.removeFromBottom (std::max (196, (int) (r.getHeight() * 0.3f)));
            r.removeFromBottom (gap);
            const int w1 = row1.getWidth() - 2 * gap;
            loudness.setBounds (row1.removeFromLeft ((int) (w1 * 0.40f)));
            row1.removeFromLeft (gap);
            peak.setBounds (row1.removeFromLeft ((int) (w1 * 0.28f)));
            row1.removeFromLeft (gap);
            dynamics.setBounds (row1);
            spectrum.setBounds (r.removeFromLeft ((int) ((r.getWidth() - gap) * 0.62f)));
            r.removeFromLeft (gap);
            stereo.setBounds (r);
            streaming.setBounds (row3);
        }

        ma::ui::HeaderBar header;
        ma::ui::LoudnessPanel loudness;
        ma::ui::PeakPanel peak;
        ma::ui::DynamicsPanel dynamics;
        ma::ui::SpectrumPanel spectrum;
        ma::ui::StereoPanel stereo;
        ma::ui::StreamingPanel streaming;
        ma::ui::ReportPanel report;
        ma::ui::WaveformPanel waveform;
        ma::ui::StartView start;
        bool standalone = false;
    };
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    ma::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    const double sr = 48000.0;
    ma::ui::DashboardData data;
    ma::analyseBuffer (makeTrack (sr, 48.0, 1.3, 0.7, 3.5, 1), sr, data.master);
    ma::analyseBuffer (makeTrack (sr, 48.0, 0.9, 1.2, 1.6, 2), sr, data.reference);
    data.hasReference = true;

    ma::ProfileLibrary lib;
    data.profile = lib.findById ("pop");
    data.targetName = data.profile->name;
    data.comparison = ma::compare (data.master, *data.profile);

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    Dashboard dash;
    auto render = [&] (const juce::String& name)
    {
        dash.setData (data);
        const auto image = dash.createComponentSnapshot (dash.getLocalBounds(), true, 1.0f);
        const auto out = outDir.getChildFile (name);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };

    render ("preview.png");

    // evidenziazione da report: banda tonale (zona sullo spettro) e PLR
    data.highlightKey = ma::highlight::band (3);
    data.highlightColour = ma::ui::colours::critical;
    render ("preview_highlight_band.png");

    data.highlightKey = ma::highlight::plr;
    data.highlightColour = ma::ui::colours::warning;
    render ("preview_highlight_plr.png");

    // evidenziazione di una mossa EQ (nodo sullo spettro) e di una diagnosi con tempo (tratto sulla cronologia)
    if (! data.comparison.eqMoves.empty())
    {
        data.highlightKey = ma::highlight::eqMove (0);
        data.highlightColour = ma::ui::colours::warning;
        render ("preview_highlight_eq.png");
    }
    {
        // una diagnosi con il suo tratto di brano (il segnale sintetico non ne produce sempre una: in quel caso 16-24 s)
        data.highlightKey = ma::highlight::band (1);
        data.highlightTimeStart = 16.0f;
        data.highlightTimeEnd = 24.0f;
        for (const auto& f : data.comparison.findings)
            if (f.hasTime())
            {
                data.highlightKey = f.key;
                data.highlightTimeStart = f.timeStart;
                data.highlightTimeEnd = f.timeEnd;
                break;
            }
        render ("preview_highlight_time.png");
        data.highlightTimeStart = data.highlightTimeEnd = -1.0f;
    }

    // fase di mix: stesso materiale sul mix bus
    {
        data.highlightKey = {};
        data.phase = ma::WorkPhase::mix;
        ma::CompareOptions mixOptions;
        mixOptions.phase = ma::WorkPhase::mix;
        mixOptions.reference = &data.reference;
        data.comparison = ma::compare (data.master, *data.profile, mixOptions);
        dash.header.setMixPhase (true);
        render ("preview_mix.png");
        dash.header.setMixPhase (false);
        data.phase = ma::WorkPhase::master;
    }

    // modalità live
    data.highlightKey = {};
    data.master.liveMode = true;
    data.master.liveWindowSeconds = 20.0;
    data.comparison = ma::compare (data.master, *data.profile);
    render ("preview_live.png");

    // temi e accenti dell'interfaccia (menu Opzioni > Interfaccia)
    {
        data.master.liveMode = false;
        data.comparison = ma::compare (data.master, *data.profile);
        auto renderTheme = [&] (ma::ui::UiSettings ui, const juce::String& name)
        {
            ma::ui::applyTheme (ui);
            lnf.refreshColours();
            dash.sendLookAndFeelChange();
            dash.header.setReferenceStatus ("reference_mix.wav", ma::ui::colours::reference, true, true);
            render (name);
        };
        renderTheme ({ ma::ui::ThemeId::light, ma::ui::AccentId::blue, true }, "preview_theme_light.png");
        renderTheme ({ ma::ui::ThemeId::graphite, ma::ui::AccentId::teal, true }, "preview_theme_graphite.png");
        renderTheme ({ ma::ui::ThemeId::highContrast, ma::ui::AccentId::blue, false }, "preview_theme_contrast.png");

        // confronto con una versione salvata (meno limitata e con meno bassi) + master da file
        {
            ma::ui::applyTheme ({});
            lnf.refreshColours();
            dash.sendLookAndFeelChange();
            ma::analyseBuffer (makeTrack (sr, 48.0, 1.6, 0.7, 5.0, 1), sr, data.version);
            data.hasVersion = true;
            data.versionName = "v1  14:32  mix_v1.wav";
            data.versionComparison = ma::compareVersions (ma::compare (data.version, *data.profile), data.comparison);
            data.masterFromFile = true;
            data.masterFileName = "mix_v2.wav";
            render ("preview_versions.png");

            ma::ui::UiSettings ms;
            ms.spectrumMidSide = true;
            ma::ui::applyTheme (ms);
            render ("preview_midside.png");

            ma::ui::applyTheme ({});
            data.hasVersion = false;
            data.masterFromFile = false;
        }

        // diagnosi ignorata (clic destro > "Ignora: è una scelta voluta")
        {
            ma::ui::applyTheme ({});
            lnf.refreshColours();
            dash.sendLookAndFeelChange();
            ma::CompareOptions options;
            for (const auto& f : data.comparison.findings)
                if (f.key == ma::highlight::width)
                    options.ignoredIds.add (f.id);
            data.comparison = ma::compare (data.master, *data.profile, options);
            render ("preview_ignored.png");
            data.comparison = ma::compare (data.master, *data.profile);
        }

        // coerenza album: 5 brani, uno più brillante e meno limitato, uno più forte
        {
            struct Spec { const char* name; float bass, high, drive; int seed; };
            const Spec specs[] { { "01 Apertura.wav", 1.3f, 0.7f, 3.5f, 11 }, { "02 Notte.wav", 1.2f, 0.8f, 3.3f, 12 },
                                 { "03 Vetro.wav", 0.9f, 1.6f, 1.6f, 13 }, { "04 Corsa.wav", 1.3f, 0.75f, 6.5f, 14 },
                                 { "05 Ritorno.wav", 1.25f, 0.7f, 3.4f, 15 } };
            std::vector<ma::AnalysisSnapshot> snaps;
            juce::StringArray names;
            for (const auto& sp : specs)
            {
                ma::AnalysisSnapshot s;
                ma::analyseBuffer (makeTrack (sr, 32.0, sp.bass, sp.high, sp.drive, sp.seed), sr, s);
                snaps.push_back (std::move (s));
                names.add (sp.name);
            }
            const auto album = ma::checkAlbum (snaps, names);
            std::printf ("%s\n", ma::buildAlbumTextReport (album).toRawUTF8());

            ma::ui::AlbumView view;
            view.setBounds (0, 0, 1416, 816);
            view.setData (data);
            view.setAlbum (album, { "06 Demo.wav (silenzio)" });
            for (auto theme : { ma::ui::ThemeId::dark, ma::ui::ThemeId::light })
            {
                ma::ui::applyTheme ({ theme, ma::ui::AccentId::blue, true });
                lnf.refreshColours();
                view.sendLookAndFeelChange();
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                const auto out = outDir.getChildFile (theme == ma::ui::ThemeId::dark ? "preview_album.png" : "preview_album_light.png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            ma::ui::applyTheme ({});
            lnf.refreshColours();
            view.sendLookAndFeelChange();
            view.setProgress (true, "Album: 3/12  03 Vetro.wav 40%", 0.4f);
            const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
            const auto out = outDir.getChildFile ("preview_album_progress.png");
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }

        // app standalone: schermata iniziale, brano con forma d'onda, segni dei problemi e lettore
        {
            ma::ui::applyTheme ({});
            lnf.refreshColours();
            dash.sendLookAndFeelChange();

            // ritornello più forte (24-32 s) oltre il ceiling, un clip a 6 s, un buco a 14 s, fase invertita tra 20 e 23 s
            auto track = makeTrack (sr, 40.0, 1.3, 0.7, 3.5, 21);
            auto gainRange = [&] (double from, double to, float gain)
            {
                track.applyGain ((int) (from * sr), (int) ((to - from) * sr), gain);
            };
            gainRange (0.0, 40.0, 0.86f);
            gainRange (24.0, 32.0, 1.13f);
            for (int i = (int) (6.0 * sr); i < (int) (6.0 * sr) + 30; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    track.setSample (ch, i, 0.99f);
            track.clear ((int) (14.0 * sr), (int) (0.06 * sr));
            for (int i = (int) (20.0 * sr); i < (int) (23.0 * sr); ++i)
                track.setSample (1, i, -track.getSample (0, i));

            ma::ui::DashboardData sdata;
            auto timeline = std::make_shared<ma::TrackTimeline>();
            ma::analyseBuffer (track, sr, sdata.master, {}, {}, timeline.get());
            sdata.profile = data.profile;
            sdata.targetName = data.targetName;
            sdata.comparison = ma::compare (sdata.master, *sdata.profile);
            sdata.masterFromFile = true;
            sdata.masterFileName = "07 Finale_master_v3.wav";

            ma::MarkerSettings settings;
            if (const auto* tp = sdata.profile->getMetric (ma::metric::truePeakMax))
                settings.ceilingDb = tp->max;
            if (const auto* psr = sdata.profile->getMetric (ma::metric::minPsr))
                settings.minPsr = psr->min;
            const auto markers = ma::findMarkers (*timeline, settings);
            for (const auto& m : markers)
                std::printf ("segno: %s\n", ma::describeMarker (m).replace ("\n", " | ").toRawUTF8());

            dash.setStandalone (true, false);
            dash.waveform.setTimeline (timeline);
            dash.waveform.setMarkers (markers);
            std::set<juce::String> ignored;
            for (const auto& m : markers)
                if (m.type == ma::MarkerType::truePeak)
                {
                    ignored.insert (m.id());   // il primo true peak segnato come scelta voluta
                    break;
                }
            dash.waveform.setIgnored (ignored, {});
            dash.waveform.setPlayback (17.3, true);
            dash.setData (sdata);
            for (auto theme : { ma::ui::ThemeId::dark, ma::ui::ThemeId::light })
            {
                ma::ui::applyTheme ({ theme, ma::ui::AccentId::blue, true });
                lnf.refreshColours();
                dash.sendLookAndFeelChange();
                const auto image = dash.createComponentSnapshot (dash.getLocalBounds(), true, 1.0f);
                const auto out = outDir.getChildFile (theme == ma::ui::ThemeId::dark ? "preview_standalone_track.png" : "preview_standalone_track_light.png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
            ma::ui::applyTheme ({});
            lnf.refreshColours();
            dash.sendLookAndFeelChange();

            // analisi in corso
            dash.waveform.setTimeline (nullptr);
            dash.waveform.setMarkers ({});
            dash.waveform.setStatus ("Analisi di 07 Finale_master_v3.wav   42%", 0.42f);
            {
                const auto image = dash.createComponentSnapshot (dash.getLocalBounds(), true, 1.0f);
                const auto out = outDir.getChildFile ("preview_standalone_analysing.png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            // schermata iniziale (con un album già analizzato)
            dash.setStandalone (true, true);
            dash.start.setAlbumAvailable (true, 5);
            for (auto theme : { ma::ui::ThemeId::dark, ma::ui::ThemeId::light })
            {
                ma::ui::applyTheme ({ theme, ma::ui::AccentId::blue, true });
                lnf.refreshColours();
                dash.sendLookAndFeelChange();
                const auto image = dash.createComponentSnapshot (dash.getLocalBounds(), true, 1.0f);
                const auto out = outDir.getChildFile (theme == ma::ui::ThemeId::dark ? "preview_standalone_start.png" : "preview_standalone_start_light.png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
            ma::ui::applyTheme ({});
            lnf.refreshColours();
            dash.sendLookAndFeelChange();
            dash.setStandalone (false, false);
            dash.setData (data);
        }

        // finestra di dialogo con il tema chiaro (LookAndFeel di default del plugin)
        {
            ma::ui::applyTheme ({ ma::ui::ThemeId::light, ma::ui::AccentId::blue, true });
            lnf.refreshColours();
            juce::AlertWindow alert ("Profilo salvato", "Il profilo \"Pop 2026\" è ora disponibile nel selettore Target."_u,
                                     juce::MessageBoxIconType::InfoIcon);
            alert.addTextEditor ("name", "Pop 2026", "Nome del profilo:");
            alert.addButton ("Salva", 1);
            alert.addButton ("Annulla", 0);
            const auto image = alert.createComponentSnapshot (alert.getLocalBounds(), true, 1.0f);
            const auto out = outDir.getChildFile ("preview_dialog_light.png");
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }

        // tooltip della guida "?" su un tema chiaro e uno scuro
        for (auto theme : { ma::ui::ThemeId::light, ma::ui::ThemeId::dark })
        {
            ma::ui::applyTheme ({ theme, ma::ui::AccentId::blue, true });
            lnf.refreshColours();
            const auto tip = ma::ui::helpTextFor ("plr");
            const auto bounds = lnf.getTooltipBounds (tip, { 0, 0 }, { 0, 0, 2000, 2000 });
            juce::Image image (juce::Image::ARGB, bounds.getWidth() + 20, bounds.getHeight() + 20, true);
            {
                juce::Graphics g (image);
                g.fillAll (ma::ui::colours::background);
                g.setOrigin (10, 10);
                lnf.drawTooltip (g, tip, bounds.getWidth(), bounds.getHeight());
            }
            const auto out = outDir.getChildFile (theme == ma::ui::ThemeId::light ? "preview_tooltip_light.png" : "preview_tooltip_dark.png");
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }

        // ogni chiave di guida registrata dai pannelli deve avere un testo
        int missing = 0;
        for (const auto* key : { "integrated", "shortTerm", "momentary", "lra", "maxShortTerm", "maxMomentary", "time", "history",
                                 "truePeak", "tpMeters", "samplePeak", "recentTp", "overs1", "overs", "clip", "dc", "noiseFloor",
                                 "sampleRate", "refTp", "plr", "psr", "dr", "psrNow", "crestFactor", "rms", "crest:0", "crest:2",
                                 "stHistogram", "spectrum", "rumble", "tilt", "centroid", "band:0", "band:7", "eq:0", "resonance:440",
                                 "goniometer", "correlation", "width", "balance", "monoLoss", "lowEndWidth", "bandWidth:1", "bandCorr:1",
                                 "streaming:platform", "streaming:target", "streaming:gain", "streaming:playback", "streaming:tp",
                                 "score", "counters", "report:list", "panel:loudness", "panel:peak", "panel:dynamics",
                                 "panel:spectrum", "panel:stereo", "panel:streaming", "panel:report",
                                 "panel:album", "album:summary", "album:row", "album:median", "album:integrated", "album:loudest",
                                 "album:delta", "album:gain", "album:tp", "album:width", "album:status", "album:details",
                                 "album:tonalChart", "album:band:0", "album:band:7",
                                 "panel:waveform", "wave:overview", "wave:legend", "wave:transport" })
            if (ma::ui::helpTextFor (key).isEmpty())
            {
                std::printf ("testo guida mancante: %s\n", key);
                ++missing;
            }
        std::printf ("guida: %d chiavi senza testo\n", missing);
        ma::ui::applyTheme ({});
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("Integrated %.2f LUFS, TP %.2f dBTP, score %d, mosse EQ %d\n", data.master.integratedLufs, data.master.truePeakMaxDb,
                 data.comparison.score, (int) data.comparison.eqMoves.size());
    std::printf ("Versione salvata nel progetto: %d byte\n", (int) ma::saveSnapshot (data.master).getSize());
    return 0;
}
