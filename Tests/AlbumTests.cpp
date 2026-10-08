#include "TestSignals.h"
#include "Compare/AlbumCheck.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    /** Brano sintetico: intro piano (10 s a -10 dB) e sezione forte (20 s); alte modificate da uno shelf, guadagno finale. */
    juce::AudioBuffer<float> makeTrack (int seed, float shelfDb, float gainDb)
    {
        auto b = test::pinkNoise (30.0, 0.5f, seed);
        b.applyGain (0, (int) (10.0 * test::kSampleRate), juce::Decibels::decibelsToGain (-10.0f));

        if (shelfDb != 0.0f)
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                juce::IIRFilter shelf;
                shelf.setCoefficients (juce::IIRCoefficients::makeHighShelf (test::kSampleRate, 4000.0, 0.707,
                                                                             juce::Decibels::decibelsToGain (shelfDb)));
                shelf.processSamples (b.getWritePointer (ch), b.getNumSamples());
            }

        b.applyGain (juce::Decibels::decibelsToGain (gainDb));
        return b;
    }
}

TEST_CASE ("Coerenza album: solo il brano fuori posto viene segnalato, con il guadagno giusto", "[album]")
{
    std::vector<ma::AnalysisSnapshot> snapshots;
    juce::StringArray names;
    for (int i = 0; i < 5; ++i)
    {
        if (i == 2)
            continue;
        snapshots.push_back (test::analyse (makeTrack (10 + i, 0.0f, 0.0f)));
        names.add ("brano" + juce::String (i + 1) + ".wav");
    }
    const float baseLoudest = snapshots.front().maxShortTermLufs;

    // brano 3: +3 dB sulle alte, poi riportato alla stessa sezione forte degli altri e alzato di 2.5 LU
    const auto shelved = test::analyse (makeTrack (12, 3.0f, 0.0f));
    const float match = baseLoudest - shelved.maxShortTermLufs;
    snapshots.insert (snapshots.begin() + 2, test::analyse (makeTrack (12, 3.0f, match + 2.5f)));
    names.insert (2, "brano3.wav");

    const auto album = ma::checkAlbum (snapshots, names);
    REQUIRE (album.isValid());
    REQUIRE (album.tracks.size() == 5);
    CHECK (album.usesLoudestSection);
    CHECK (album.medianLoudestLufs == Approx (baseLoudest).margin (0.3));

    for (size_t i = 0; i < album.tracks.size(); ++i)
    {
        const auto& t = album.tracks[i];
        INFO (t.name.toStdString());
        if (i == 2)
        {
            CHECK (t.loudness == ma::Severity::warning);
            CHECK (t.loudestDeltaLu == Approx (2.5f).margin (0.3));
            CHECK (t.gainDb == Approx (-2.5f).margin (0.3));
            CHECK (t.tonal >= ma::Severity::warning);
            CHECK (t.bandDelta[6] > 1.5f);   // Brilliance
            CHECK (t.bandDelta[0] < 1.0f);   // le basse non si muovono

            // diagnosi: sezione più forte (abbassare) e alte (shelf in giù)
            bool hasLoudest = false, hasTonal = false;
            for (const auto& f : t.findings)
            {
                if (f.id == "album|loudest")
                {
                    hasLoudest = true;
                    CHECK (f.action.contains ("Abbassalo"));
                    CHECK (f.action.contains ("2.5"));
                }
                if (f.id.startsWith ("album|band:") && f.delta > 0.0f)
                {
                    hasTonal = true;
                    CHECK (f.message.contains ("brillante"));
                    CHECK (f.action.contains ("-"));
                }
            }
            CHECK (hasLoudest);
            CHECK (hasTonal);
        }
        else
        {
            CHECK (t.loudness == ma::Severity::ok);
            CHECK (t.tonal == ma::Severity::ok);
            CHECK (t.worst < ma::Severity::warning);
            CHECK (std::abs (t.loudestDeltaLu) < 0.5f);
        }
    }

    CHECK (album.warning + album.critical == 1);

    // export: testo e CSV contengono tutti i brani
    const auto text = ma::buildAlbumTextReport (album);
    for (const auto& n : names)
        CHECK (text.contains (n));
    CHECK (text.contains ("Abbassalo"));

    const auto csv = juce::StringArray::fromLines (ma::buildAlbumCsv (album).trim());
    CHECK (csv.size() == 6);   // intestazione + 5 brani
    CHECK (csv[3].startsWith ("3;\"brano3.wav\""));
}

TEST_CASE ("Coerenza album: brano molto più piano = critico con guadagno positivo", "[album]")
{
    std::vector<ma::AnalysisSnapshot> snapshots;
    for (int i = 0; i < 3; ++i)
        snapshots.push_back (test::analyse (makeTrack (20 + i, 0.0f, i == 1 ? -4.0f : 0.0f)));

    const auto album = ma::checkAlbum (snapshots, { "a.wav", "b.wav", "c.wav" });
    REQUIRE (album.isValid());
    CHECK (album.tracks[1].loudness == ma::Severity::critical);
    CHECK (album.tracks[1].gainDb == Approx (4.0f).margin (0.3));
    CHECK (album.tracks[0].loudness == ma::Severity::ok);
    CHECK (album.tracks[2].loudness == ma::Severity::ok);
    CHECK (album.loudestSpreadLu == Approx (4.0f).margin (0.3));

    // stesso bilanciamento tonale: il guadagno non cambia la curva normalizzata
    CHECK (album.tracks[1].tonal == ma::Severity::ok);

    // meno di 2 brani: nessun confronto possibile
    CHECK_FALSE (ma::checkAlbum ({ snapshots.front() }, { "a.wav" }).isValid());
}
