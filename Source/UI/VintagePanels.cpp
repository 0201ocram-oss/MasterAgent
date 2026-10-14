#include "VintagePanels.h"
#include "Vintage.h"
#include "../Common/Text.h"

namespace ma::ui
{

namespace
{
    // colori fissi degli strumenti: carta retroilluminata, inchiostro, aghi
    const juce::Colour kFaceLight { 0xfff7ebc8 }, kFaceDark { 0xffd9c089 };
    const juce::Colour kInk { 0xff2b2219 }, kInkRed { 0xffc23a2b }, kNeedle { 0xff16120e }, kNeedleRed { 0xffb3261b };
    const juce::Colour kGreenZone { 0xff4a8f3c };
    const juce::Colour kPhosphor { 0xff7dff8c };
    const juce::Colour kLedGreen { 0xff62e05a }, kLedAmber { 0xffffb02e }, kLedRed { 0xffff4a36 };

    //==========================================================================
    // strumenti ad ago

    /** Scala di uno strumento: valore -> posizione 0..1 lungo l'arco, tacche, zone colorate e scritte. */
    struct MeterScale
    {
        std::function<float (float)> position;
        std::vector<std::pair<float, juce::String>> major;
        std::vector<float> minor;
        float redFrom = 0.0f, redTo = 0.0f;
        float greenFrom = 0.0f, greenTo = 0.0f;
        juce::String title, caption;
        std::vector<std::pair<juce::String, juce::Colour>> legend;   // accanto alla didascalia (aghi L / R)
    };

    struct Needle  { float position; juce::Colour colour; };
    struct Pointer { float value; juce::Colour colour; };   // indice fisso sulla scala (integrated, reference)

    struct MeterGeometry
    {
        juce::Rectangle<float> face;
        juce::Point<float> pivot;
        float radius = 0.0f, span = 0.0f;   // span: metà dell'angolo dell'arco

        float angle (float position) const  { return -span + 2.0f * span * juce::jlimit (-0.04f, 1.04f, position); }

        juce::Point<float> pointAt (float position, float r) const
        {
            const float a = angle (position);
            return { pivot.x + r * std::sin (a), pivot.y - r * std::cos (a) };
        }
    };

    MeterGeometry meterGeometry (juce::Rectangle<float> face)
    {
        // l'arco parte al 27% dell'altezza; il perno è sotto la carta, nascosto dal copri-perno
        MeterGeometry m;
        m.face = face;
        m.span = juce::degreesToRadians (47.0f);
        const float h = face.getHeight();
        m.radius = std::min (face.getWidth() * 0.41f / std::sin (m.span), h * 1.05f);
        m.pivot = { face.getCentreX(), face.getY() + h * 0.27f + m.radius };
        return m;
    }

    /** Disegna lo strumento in bounds (cornice compresa) e restituisce l'area della carta. */
    juce::Rectangle<float> drawMeter (juce::Graphics& g, juce::Rectangle<float> bounds, const MeterScale& scale,
                                      const std::vector<Needle>& needles, const std::vector<Pointer>& pointers)
    {
        // cornice
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d372f), 0.0f, bounds.getY(), juce::Colour (0xff0e0c0a), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine ((int) bounds.getY() + 1, bounds.getX() + 5.0f, bounds.getRight() - 5.0f);

        const auto face = bounds.reduced (std::max (4.0f, bounds.getHeight() * 0.045f));
        const auto m = meterGeometry (face);
        const float h = face.getHeight();

        // carta retroilluminata: più chiara al centro, come con una lampadina dietro
        g.setGradientFill (juce::ColourGradient (kFaceLight, face.getCentreX(), face.getY() + h * 0.42f,
                                                 kFaceDark, face.getX() - face.getWidth() * 0.05f, face.getBottom(), true));
        g.fillRect (face);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (face.toNearestInt());

        const auto pos = [&] (float v) { return scale.position (v); };
        const auto arc = [&] (float p0, float p1, float r)
        {
            juce::Path a;
            a.addCentredArc (m.pivot.x, m.pivot.y, r, r, 0.0f, m.angle (p0), m.angle (p1), true);
            return a;
        };

        // zone colorate appena fuori dall'arco
        const float band = std::max (2.0f, h * 0.045f);
        if (scale.greenTo > scale.greenFrom)
        {
            g.setColour (kGreenZone.withAlpha (0.8f));
            g.strokePath (arc (pos (scale.greenFrom), pos (scale.greenTo), m.radius + band * 0.5f), juce::PathStrokeType (band));
        }
        if (scale.redTo > scale.redFrom)
        {
            g.setColour (kInkRed);
            g.strokePath (arc (pos (scale.redFrom), pos (scale.redTo), m.radius + band * 0.5f), juce::PathStrokeType (band));
        }

        // arco, tacche e numeri
        const bool hasRed = scale.redTo > scale.redFrom;
        const auto inkFor = [&] (float v) { return hasRed && v >= scale.redFrom - 1.0e-3f && v <= scale.redTo + 1.0e-3f ? kInkRed : kInk; };
        g.setColour (kInk);
        g.strokePath (arc (0.0f, 1.0f, m.radius), juce::PathStrokeType (std::max (1.0f, h * 0.008f)));

        for (auto v : scale.minor)
        {
            g.setColour (inkFor (v));
            g.drawLine ({ m.pointAt (pos (v), m.radius), m.pointAt (pos (v), m.radius + h * 0.04f) }, std::max (0.8f, h * 0.006f));
        }

        g.setFont (vintage::font (std::max (8.0f, h * 0.088f), true));
        for (const auto& [v, text] : scale.major)
        {
            g.setColour (inkFor (v));
            g.drawLine ({ m.pointAt (pos (v), m.radius - h * 0.012f), m.pointAt (pos (v), m.radius + h * 0.07f) }, std::max (1.2f, h * 0.011f));
            const auto c = m.pointAt (pos (v), m.radius + h * 0.135f);
            g.drawText (text, juce::Rectangle<float> (h * 0.3f, h * 0.11f).withCentre (c), juce::Justification::centred);
        }

        // indici fissi: triangolini sotto l'arco che puntano alla scala
        for (const auto& p : pointers)
        {
            const float pp = pos (p.value);
            if (pp < -0.02f || pp > 1.02f)
                continue;
            const auto tip = m.pointAt (pp, m.radius - h * 0.018f);
            const auto base = m.pointAt (pp, m.radius - h * 0.085f);
            const auto side = juce::Point<float> (std::cos (m.angle (pp)), std::sin (m.angle (pp))) * (h * 0.032f);
            juce::Path t;
            t.addTriangle (tip, base + side, base - side);
            g.setColour (p.colour);
            g.fillPath (t);
        }

        // scritte del quadrante
        g.setColour (kInk);
        g.setFont (vintage::font (h * 0.165f, true));
        g.drawText (scale.title, juce::Rectangle<float> (face.getX(), face.getY() + h * 0.43f, face.getWidth(), h * 0.19f),
                    juce::Justification::centred);
        {
            g.setFont (vintage::font (std::max (8.0f, h * 0.068f)));
            const juce::Rectangle<float> captionRow (face.getX(), face.getY() + h * 0.62f, face.getWidth(), h * 0.085f);
            float legendW = 0.0f;
            for (const auto& [text, colour] : scale.legend)
                legendW += juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + h * 0.2f;
            const float captionW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), scale.caption);
            auto row = captionRow.withSizeKeepingCentre (captionW + legendW + (legendW > 0.0f ? h * 0.08f : 0.0f), captionRow.getHeight());
            g.drawText (scale.caption, row.removeFromLeft (captionW), juce::Justification::centredLeft);
            row.removeFromLeft (h * 0.08f);
            for (const auto& [text, colour] : scale.legend)
            {
                // tratto del colore dell'ago, poi la lettera
                g.setColour (colour);
                g.fillRect (row.removeFromLeft (h * 0.12f).withSizeKeepingCentre (h * 0.1f, std::max (1.5f, h * 0.014f)));
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
                g.drawText (text, row.removeFromLeft (w + h * 0.08f).withTrimmedLeft (h * 0.03f), juce::Justification::centredLeft);
            }
        }

        // aghi, con l'ombra sulla carta
        const float needleWidth = std::max (1.2f, h * 0.011f);
        for (const auto& n : needles)
        {
            const auto tip = m.pointAt (n.position, m.radius + h * 0.075f);
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.drawLine ({ m.pivot.translated (h * 0.02f, h * 0.02f), tip.translated (h * 0.02f, h * 0.02f) }, needleWidth);
            g.setColour (n.colour);
            g.drawLine ({ m.pivot, tip }, needleWidth);
        }

        // copri-perno scuro in basso
        const float coverRadius = std::max (h * 0.14f, m.pivot.y - (face.getBottom() - h * 0.17f));
        const auto cover = juce::Rectangle<float> (coverRadius * 2.0f, coverRadius * 2.0f).withCentre (m.pivot);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff35302a), 0.0f, m.pivot.y - coverRadius,
                                                 juce::Colour (0xff0e0c0a), 0.0f, m.pivot.y - coverRadius * 0.5f, false));
        g.fillEllipse (cover);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (cover, 1.0f);

        // incasso della carta e riflesso del vetro
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0.0f, face.getY(),
                                                 juce::Colours::black.withAlpha (0.0f), 0.0f, face.getY() + h * 0.09f, false));
        g.fillRect (face.withHeight (h * 0.09f));
        juce::Path glare;
        glare.startNewSubPath (face.getTopLeft());
        glare.lineTo (face.getX() + face.getWidth() * 0.62f, face.getY());
        glare.lineTo (face.getX(), face.getY() + h * 0.75f);
        glare.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.2f), face.getX(), face.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), face.getX() + face.getWidth() * 0.3f, face.getY() + h * 0.35f, false));
        g.fillPath (glare);

        return face;
    }

    /** Scala VU: l'ago si muove con la tensione, quindi i dB non sono equidistanti (-20 .. +3 VU). */
    float vuPosition (float vu)
    {
        constexpr float lo = 0.1f, hi = 1.41254f;   // -20 VU e +3 VU in ampiezza
        return (std::pow (10.0f, vu / 20.0f) - lo) / (hi - lo);
    }

    float peakPosition (float db)  { return (db + 24.0f) / 27.0f; }
    float phasePosition (float c)  { return (c + 1.0f) * 0.5f; }

    /** 0 VU: il centro del target di loudness sul master; senza target (o nel mix) -18 LUFS, come lo 0 VU degli studi. */
    struct VuReference
    {
        float lufs = -18.0f;
        const MetricRange* target = nullptr;
    };

    VuReference vuReference (const DashboardData& d)
    {
        VuReference v;
        if (d.phase == WorkPhase::master && d.profile != nullptr)
            if (const auto* r = d.profile->getMetric (metric::integratedLufs))
            {
                v.target = r;
                v.lufs = (r->target < -1.0f && r->target >= r->min && r->target <= r->max) ? r->target : (r->min + r->max) * 0.5f;
            }
        return v;
    }

    juce::String compactNumber (float v)
    {
        return std::abs (v - std::round (v)) < 0.05f ? juce::String (juce::roundToInt (v)) : juce::String (v, 1);
    }

    //==========================================================================
    // display e spie

    /** Spia di una misura valutata: accesa col colore della diagnosi, spenta se non ce n'è. */
    struct Lamp
    {
        bool show = false, lit = false;
        juce::Colour colour;
    };

    Lamp lampFor (const DashboardData& d, const juce::String& key)
    {
        const auto c = d.colourFor (key, juce::Colours::transparentBlack);
        return { true, ! c.isTransparent(), c.isTransparent() ? colours::ok : c };
    }

    /** Cifre ambra; rosse se la misura è critica. */
    juce::Colour digitsFor (const DashboardData& d, const juce::String& key)
    {
        return key.isNotEmpty() && d.severityOf (key) == Severity::critical ? colours::critical : colours::accent;
    }

    /** Etichetta serigrafata con spia e unità, sotto la finestra del display. */
    void drawReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& unit,
                      const juce::String& value, juce::Colour digits, Lamp lamp, int cells)
    {
        auto top = r.removeFromTop (14.0f);
        if (lamp.show)
        {
            vintage::drawLamp (g, { top.getX() + 4.0f, top.getCentreY() }, 3.0f, lamp.colour, lamp.lit);
            top.removeFromLeft (11.0f);
        }
        g.setFont (vintage::font (9.5f));
        const float unitW = unit.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), unit) + 4.0f;
        g.setColour (colours::textFaint);
        g.drawText (unit, top.removeFromRight (unitW), juce::Justification::centredRight);
        g.setColour (colours::textDim);
        g.setFont (vintage::silkscreen (10.0f));
        g.drawFittedText (label, top.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

        r.removeFromTop (3.0f);
        vintage::drawDisplayWindow (g, r);
        vintage::drawSevenSegment (g, r.reduced (7.0f, std::max (3.0f, r.getHeight() * 0.17f)), value, digits, cells);
    }

    //==========================================================================
    // terzi d'ottava

    /** Livello di ogni terzo d'ottava dallo spettro di visualizzazione (1/12 d'ottava): media in potenza. */
    std::array<float, kNumThirdOctaves> thirdOctaveLevels (const std::array<float, kNumDisplayPoints>& spectrum)
    {
        std::array<float, kNumThirdOctaves> levels {};
        for (int i = 0; i < kNumThirdOctaves; ++i)
        {
            const float pos = 12.0f * std::log2 (thirdOctaveCentre (i) / 20.0f);
            const int p0 = juce::jlimit (0, kNumDisplayPoints - 1, (int) std::ceil (pos - 2.0f));
            const int p1 = juce::jlimit (0, kNumDisplayPoints - 1, (int) std::floor (pos + 1.99f));
            double power = 0.0;
            int count = 0;
            for (int p = p0; p <= p1; ++p)
            {
                power += std::pow (10.0, spectrum[(size_t) p] / 10.0);
                ++count;
            }
            levels[(size_t) i] = count > 0 ? powerToDb (power / count) : kSilenceDb;
        }
        return levels;
    }

    /** Etichette di frequenza ogni ottava (31.5, 63, 125 ... 16k). */
    void drawFrequencyLabels (juce::Graphics& g, juce::Rectangle<float> row, float firstX, float columnWidth)
    {
        g.setColour (colours::textFaint);
        g.setFont (vintage::font (9.5f));
        for (int i = 2; i < kNumThirdOctaves; i += 3)
            g.drawText (kThirdOctaveLabels[(size_t) i], juce::Rectangle<float> (firstX + ((float) i + 0.5f) * columnWidth - 18.0f, row.getY(), 36.0f, row.getHeight()),
                        juce::Justification::centred);
    }

    double nowSeconds()  { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

//==============================================================================
void MeterBridgePanel::setData (const DashboardData& d)
{
    // badge come nel pannello loudness classico (va aggiornato prima del paint)
    if (d.masterFromFile)
    {
        badge = d.playing ? "LIVE" : "FILE";
        badgeColour = d.playing ? colours::critical : colours::accent2;
    }
    else
    {
        const bool live = d.master.liveMode;
        badge = live ? "LIVE " + juce::String (juce::roundToInt (d.master.liveWindowSeconds)) + " s" : "BRANO INTERO"_t;
        badgeColour = live ? colours::critical : colours::ok;
    }

    // aghi: VU integra in 300 ms, i picchi salgono subito e scendono di 13 dB/s, la fase ha un po' di inerzia
    const auto& now = d.instant();
    const float vuTarget = juce::jlimit (-0.04f, 1.04f, vuPosition (now.momentaryLufs - vuReference (d).lufs));
    const float phaseTarget = juce::jlimit (0.0f, 1.0f, phasePosition (now.correlationInstant));

    const double t = nowSeconds();
    const bool snap = lastUpdate <= 0.0 || t - lastUpdate > 1.0;
    const float dt = (float) juce::jlimit (0.0, 0.2, t - lastUpdate);
    lastUpdate = t;

    if (snap)
    {
        vuNeedle = vuTarget;
        phaseNeedle = phaseTarget;
        for (size_t ch = 0; ch < 2; ++ch)
            peakNeedleDb[ch] = std::max (-40.0f, now.truePeakDb[ch]);
    }
    else
    {
        vuNeedle += (vuTarget - vuNeedle) * (1.0f - std::exp (-dt / 0.065f));
        phaseNeedle += (phaseTarget - phaseNeedle) * (1.0f - std::exp (-dt / 0.05f));
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float target = std::max (-40.0f, now.truePeakDb[ch]);
            peakNeedleDb[ch] = target >= peakNeedleDb[ch] ? target : std::max (target, peakNeedleDb[ch] - 13.0f * dt);
        }
    }

    Panel::setData (d);
}

void MeterBridgePanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& s = d.master;
    const bool live = s.liveMode;

    constexpr float gap = 18.0f;
    const float sectionW = (area.getWidth() - 2.0f * gap) / 3.0f;
    const float readoutH = juce::jlimit (40.0f, 58.0f, area.getHeight() * 0.25f);

    // strumento in alto nella sezione (proporzioni da VU vero), display sotto, larghi quanto lo strumento
    const auto sectionAt = [&] (int i)
    {
        const auto r = juce::Rectangle<float> (area.getX() + (float) i * (sectionW + gap), area.getY(), sectionW, area.getHeight());
        const auto room = r.withTrimmedBottom (readoutH + 10.0f);
        const float w = std::min (room.getWidth(), room.getHeight() * 1.8f);
        const float h = std::min (room.getHeight(), w / 1.5f);
        const auto meter = juce::Rectangle<float> (r.getCentreX() - w * 0.5f, room.getY(), w, h);
        const auto readouts = juce::Rectangle<float> (meter.getX(), meter.getBottom() + 10.0f, meter.getWidth(), readoutH);
        return std::pair { meter, readouts };
    };

    const auto readoutRow = [] (juce::Rectangle<float> row, std::initializer_list<float> weights)
    {
        std::vector<juce::Rectangle<float>> cells;
        constexpr float cellGap = 8.0f;
        float total = 0.0f;
        for (auto w : weights) total += w;
        const float usable = row.getWidth() - cellGap * (float) (weights.size() - 1);
        for (auto w : weights)
        {
            cells.push_back (row.removeFromLeft (usable * w / total));
            row.removeFromLeft (cellGap);
        }
        return cells;
    };

    // --- loudness: VU con lo 0 sul target -------------------------------------------------------
    {
        const auto [meter, readouts] = sectionAt (0);
        const auto ref = vuReference (d);

        MeterScale scale;
        scale.position = vuPosition;
        scale.major = { { -20.0f, "20" }, { -10.0f, "10" }, { -7.0f, "7" }, { -5.0f, "5" }, { -3.0f, "3" }, { -2.0f, "2" },
                        { -1.0f, "1" }, { 0.0f, "0" }, { 1.0f, "+1" }, { 2.0f, "+2" }, { 3.0f, "+3" } };
        scale.minor = { -15.0f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f };
        scale.redFrom = 0.0f;
        scale.redTo = 3.0f;
        if (ref.target != nullptr)
        {
            scale.greenFrom = ref.target->min - ref.lufs;
            scale.greenTo = ref.target->max - ref.lufs;
        }
        scale.title = "VU";
        scale.caption = "0 VU = " + compactNumber (ref.lufs) + " LUFS" + (ref.target != nullptr ? " (target)" : "");

        std::vector<Pointer> pointers;
        if (s.integratedLufs > -70.0f)
            pointers.push_back ({ s.integratedLufs - ref.lufs, kInk });
        if (d.hasReference && d.reference.integratedLufs > -70.0f)
            pointers.push_back ({ d.reference.integratedLufs - ref.lufs, colours::reference.darker (0.35f) });

        addHelp (meter, "vu:loudness");
        drawMeter (g, meter, scale, { { vuNeedle, kNeedle } }, pointers);

        const auto cells = readoutRow (readouts, { 1.25f, 1.0f, 0.8f });
        markHighlight (g, cells[0], highlight::integrated);
        drawReadout (g, cells[0], live ? "INTEGRATED (finestra)"_t : juce::String ("INTEGRATED"), "LUFS",
                     formatDb (s.integratedLufs), digitsFor (d, highlight::integrated), lampFor (d, highlight::integrated), 4);
        addHelp (cells[1], "shortTerm");
        drawReadout (g, cells[1], "SHORT-TERM", "LUFS", formatDb (d.instant().shortTermLufs), colours::accent, {}, 4);
        markHighlight (g, cells[2], highlight::lra);
        drawReadout (g, cells[2], "LRA", "LU", live ? juce::String ("--") : juce::String (s.loudnessRange, 1),
                     digitsFor (d, highlight::lra), lampFor (d, highlight::lra), 3);
    }

    // --- picchi: doppio ago L / R in dBTP, spia OVER sul quadrante ------------------------------------------
    {
        const auto [meter, readouts] = sectionAt (1);
        float ceiling = d.phase == WorkPhase::mix ? -3.0f : -1.0f;
        if (d.phase == WorkPhase::master && d.profile != nullptr)
            if (const auto* tp = d.profile->getMetric (metric::truePeakMax))
                ceiling = tp->max;

        MeterScale scale;
        scale.position = peakPosition;
        scale.major = { { -24.0f, "-24" }, { -18.0f, "-18" }, { -12.0f, "-12" }, { -9.0f, "-9" }, { -6.0f, "-6" },
                        { -3.0f, "-3" }, { 0.0f, "0" }, { 3.0f, "+3" } };
        for (float v = -11.0f; v <= 2.0f; v += 1.0f)
            if (std::fmod (v, 3.0f) != 0.0f)
                scale.minor.push_back (v);
        scale.minor.insert (scale.minor.end(), { -21.0f, -15.0f });
        scale.redFrom = ceiling;
        scale.redTo = 3.0f;
        scale.title = "PEAK";
        scale.caption = "dBTP";
        scale.legend = { { "L", kNeedle }, { "R", kNeedleRed } };

        std::vector<Pointer> pointers;
        if (s.truePeakMaxDb > -70.0f)
            pointers.push_back ({ s.truePeakMaxDb, kInk });
        if (d.hasReference && d.reference.truePeakMaxDb > -70.0f)
            pointers.push_back ({ d.reference.truePeakMaxDb, colours::reference.darker (0.35f) });

        addHelp (meter, "vu:peak");
        const auto face = drawMeter (g, meter, scale, { { peakPosition (peakNeedleDb[0]), kNeedle }, { peakPosition (peakNeedleDb[1]), kNeedleRed } },
                                     pointers);

        // spia OVER: picchi oltre 0 dBTP o clipping a fondo scala
        {
            const float h = face.getHeight();
            const juce::Point<float> c (face.getRight() - h * 0.13f, face.getY() + h * 0.13f);
            const float r = std::max (3.0f, h * 0.035f);
            const auto zone = juce::Rectangle<float> (h * 0.22f, h * 0.2f).withCentre (c.translated (0.0f, h * 0.04f));
            markHighlight (g, zone, s.clipEventsFullScale > 0 ? highlight::clip : highlight::overs);
            addHelp (zone, "vu:over");
            vintage::drawLamp (g, c, r, kLedRed, s.overs0dB > 0 || s.clipEventsFullScale > 0);
            g.setColour (kInk);
            g.setFont (vintage::font (std::max (7.5f, h * 0.06f), true));
            g.drawText ("OVER", juce::Rectangle<float> (h * 0.3f, h * 0.08f).withCentre (c.translated (0.0f, r + h * 0.05f)),
                        juce::Justification::centred);
        }

        const auto cells = readoutRow (readouts, { 1.2f, 0.9f, 0.9f });
        markHighlight (g, cells[0], highlight::truePeak);
        const auto tpSeverity = std::max (d.severityOf (highlight::truePeak), d.severityOf (highlight::overs));
        drawReadout (g, cells[0], "TRUE PEAK MAX", "dBTP", formatDb (s.truePeakMaxDb),
                     tpSeverity == Severity::critical ? colours::critical : colours::accent, lampFor (d, highlight::truePeak), 4);
        markHighlight (g, cells[1], highlight::plr);
        drawReadout (g, cells[1], "PLR", "dB", s.valid ? juce::String (s.plr, 1) : juce::String ("--"),
                     digitsFor (d, highlight::plr), lampFor (d, highlight::plr), 3);
        markHighlight (g, cells[2], highlight::dr);
        drawReadout (g, cells[2], "DR", {}, (s.drValid && ! live) ? juce::String (s.drValue, 1) : juce::String ("--"),
                     digitsFor (d, highlight::dr), lampFor (d, highlight::dr), 3);
    }

    // --- fase: correlazione -1 .. +1 -------------------------------------------------------------------------
    {
        const auto [meter, readouts] = sectionAt (2);
        MeterScale scale;
        scale.position = phasePosition;
        scale.major = { { -1.0f, "-1" }, { -0.5f, "-.5" }, { 0.0f, "0" }, { 0.5f, "+.5" }, { 1.0f, "+1" } };
        for (int i = -9; i <= 9; ++i)
            if (i % 5 != 0)
                scale.minor.push_back ((float) i * 0.1f);
        scale.redFrom = -1.0f;
        scale.redTo = 0.0f;
        scale.title = "FASE"_t;
        scale.caption = "correlazione"_t;

        std::vector<Pointer> pointers;
        if (s.valid)
            pointers.push_back ({ s.correlation, kInk });
        if (d.hasReference)
            pointers.push_back ({ d.reference.correlation, colours::reference.darker (0.35f) });

        addHelp (meter, "vu:phase");
        drawMeter (g, meter, scale, { { phaseNeedle, kNeedle } }, pointers);

        const auto cells = readoutRow (readouts, { 1.0f, 1.0f });
        markHighlight (g, cells[0], highlight::correlation);
        drawReadout (g, cells[0], "CORRELAZIONE"_t, {}, s.valid ? juce::String (s.correlation, 2) : juce::String ("--"),
                     digitsFor (d, highlight::correlation), lampFor (d, highlight::correlation), 4);
        markHighlight (g, cells[1], highlight::width);
        drawReadout (g, cells[1], "LARGHEZZA"_t, "%", s.valid ? juce::String (juce::roundToInt (s.widthPercent)) : juce::String ("--"),
                     digitsFor (d, highlight::width), lampFor (d, highlight::width), 3);
    }
}

//==============================================================================
int TonalEqPanel::bandAt (juce::Point<float> p) const
{
    for (int b = 0; b < kNumBands; ++b)
        if (bandCells[(size_t) b].contains (p))
            return b;
    return -1;
}

void TonalEqPanel::mouseMove (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);
    setMouseCursor (band >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (band != hoveredBand)
    {
        hoveredBand = band;
        repaint();
    }
}

void TonalEqPanel::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
    repaint();
}

void TonalEqPanel::mouseDown (const juce::MouseEvent& e)
{
    if (const int band = bandAt (e.position); band >= 0 && onSoloBand)
        onSoloBand (band);
}

juce::String TonalEqPanel::getTooltip()
{
    // fuori dalla guida: le bande si possono ascoltare da sole
    if (data != nullptr && ! data->helpMode)
        if (const int b = bandAt (getMouseXYRelative().toFloat()); b >= 0)
            return data->monitor == MonitorChoice { MonitorMode::band, b }
                       ? "Clic: torna all'ascolto normale"_t
                       : "Clic: ascolta solo questa banda"_t + " (" + kBandNames[(size_t) b] + ")";
    return Panel::getTooltip();
}

void TonalEqPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& c = d.comparison;
    const bool hasTarget = c.hasTonalTarget;
    const float range = hasTarget ? 6.0f : 12.0f;   // senza target i cursori mostrano la curva del master, più ampia

    // riga in alto: cosa mostrano i cursori, tilt a destra
    auto info = area.removeFromTop (15.0f);
    {
        const auto tiltText = "TILT " + juce::String (d.master.spectralTiltDbPerOct, 2) + " dB/oct";
        g.setFont (vintage::font (11.0f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tiltText) + 6.0f;
        auto tiltArea = info.removeFromRight (w);
        markHighlight (g, tiltArea, highlight::tilt);
        g.setColour (d.colourFor (highlight::tilt, colours::textDim));
        g.drawText (tiltText, tiltArea, juce::Justification::centredRight);

        g.setColour (colours::textFaint);
        g.setFont (vintage::font (10.5f));
        g.drawFittedText (hasTarget ? (c.tonalUsesLoudest ? "scostamento dalla sezione più forte del target (dB)"_t : "scostamento dal target (dB)"_t)
                                    : "curva del master, senza un target tonale (dB)"_t,
                          info.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    area.removeFromTop (6.0f);

    auto bandRow = area.removeFromBottom (45.0f);
    auto freqRow = area.removeFromBottom (14.0f);
    auto scaleColumn = area.removeFromLeft (28.0f);
    const auto faders = area.reduced (0.0f, 7.0f);
    addHelp (faders, "eq:graphic");

    const float colW = faders.getWidth() / (float) kNumThirdOctaves;
    const auto xFor = [&] (int i) { return faders.getX() + ((float) i + 0.5f) * colW; };
    const auto yFor = [&] (float db) { return faders.getCentreY() - juce::jlimit (-1.0f, 1.0f, db / range) * faders.getHeight() * 0.5f; };

    // scala stampata sul frontalino
    g.setFont (vintage::font (9.5f));
    for (float db : { range, range * 0.5f, 0.0f, -range * 0.5f, -range })
    {
        const float y = yFor (db);
        g.setColour (colours::textFaint);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> (scaleColumn.getX(), y - 6.0f, scaleColumn.getWidth() - 6.0f, 12.0f), juce::Justification::centredRight);
        g.setColour (db == 0.0f ? colours::axis : colours::grid);
        g.drawHorizontalLine ((int) y, faders.getX(), faders.getRight());
    }

    // bande: tolleranza, hover, ascolto in solo, evidenziazione dal report
    const int highlightBand = hoveredBand >= 0 ? hoveredBand
                            : d.highlightKey.startsWith ("band:") ? d.highlightKey.fromFirstOccurrenceOf (":", false, false).getIntValue() : -1;
    for (int b = 0; b < kNumBands; ++b)
    {
        int first = -1, last = -1;
        for (int i = 0; i < kNumThirdOctaves; ++i)
            if (bandOfThirdOctave (i) == b)
            {
                if (first < 0) first = i;
                last = i;
            }
        if (first < 0)
        {
            bandCells[(size_t) b] = {};
            continue;
        }

        const float x1 = faders.getX() + (float) first * colW, x2 = faders.getX() + (float) (last + 1) * colW;
        const auto column = juce::Rectangle<float>::leftTopRightBottom (x1, faders.getY() - 6.0f, x2, bandRow.getBottom());
        bandCells[(size_t) b] = column;

        if (hasTarget)
        {
            const float tol = c.bandTolerance[(size_t) b];
            g.setColour (colours::ok.withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1 + 1.0f, yFor (tol), x2 - 1.0f, yFor (-tol)));
        }
        if (b == highlightBand)
        {
            const auto col = hoveredBand >= 0 ? colours::accent : d.highlightColour;
            g.setColour (col.withAlpha (0.1f));
            g.fillRect (column.reduced (1.0f, 0.0f));
        }
        markHighlight (g, column.reduced (2.0f, 0.0f), highlight::band (b));

        if (d.monitor == MonitorChoice { MonitorMode::band, b })
        {
            g.setColour (colours::info.withAlpha (0.85f));
            g.drawRect (column.reduced (1.0f, 0.0f), 1.5f);
        }

        // sotto: graffa, nome, spia e scostamento della banda (le bande strette hanno nome corto e valore intero)
        auto cell = juce::Rectangle<float>::leftTopRightBottom (x1, bandRow.getY(), x2, bandRow.getBottom()).reduced (2.0f, 0.0f);
        const bool narrow = cell.getWidth() < 46.0f;
        g.setColour (colours::textFaint);
        g.drawHorizontalLine ((int) cell.getY() + 2, cell.getX() + 1.0f, cell.getRight() - 1.0f);
        g.drawVerticalLine ((int) cell.getX() + 1, cell.getY() + 2.0f, cell.getY() + 6.0f);
        g.drawVerticalLine ((int) cell.getRight() - 1, cell.getY() + 2.0f, cell.getY() + 6.0f);
        cell.removeFromTop (6.0f);

        static const char* shortNames[] { "SUB", "BASS", "LO-MID", "MID", "HI-MID", "PRES", "BRILL", "AIR" };
        g.setColour (b == highlightBand ? colours::text : colours::textDim);
        g.setFont (vintage::silkscreen (9.5f));
        g.drawFittedText (narrow ? juce::String (shortNames[b]) : juce::String (kBandNames[(size_t) b]).toUpperCase(),
                          cell.removeFromTop (13.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);

        const auto lamp = lampFor (d, highlight::band (b));
        vintage::drawLamp (g, cell.removeFromTop (11.0f).getCentre(), 2.8f, lamp.colour, lamp.lit && hasTarget);

        if (hasTarget && d.master.valid)
        {
            const float delta = c.bandTonalDelta[(size_t) b];
            const auto text = (delta >= 0.0f ? "+" : "") + (narrow ? juce::String (juce::roundToInt (delta)) : juce::String (delta, 1));
            g.setFont (vintage::font (11.0f, true));
            g.setColour (d.severityOf (highlight::band (b)) >= Severity::warning ? lamp.colour : colours::text);
            g.drawFittedText (text, cell.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }

    // cursori: scanalatura, cappuccio con la riga indice colorata se la banda è fuori tolleranza
    const float capW = juce::jlimit (6.0f, 16.0f, colW * 0.78f);
    const float capH = juce::jlimit (6.0f, 11.0f, colW * 0.62f);
    for (int i = 0; i < kNumThirdOctaves; ++i)
    {
        const float x = xFor (i);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRoundedRectangle (x - 1.5f, faders.getY() - 4.0f, 3.0f, faders.getHeight() + 8.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine ((int) (x + 2.0f), faders.getY() - 4.0f, faders.getBottom() + 4.0f);

        float value = 0.0f;
        if (d.master.valid)
            value = hasTarget ? c.tonalDelta[(size_t) i] : d.master.thirdOctaveDb[(size_t) i];

        const int band = bandOfThirdOctave (i);
        const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ x, yFor (value) });
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (1.0f, 2.0f), 1.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6257), 0.0f, cap.getY(), juce::Colour (0xff25211c), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (cap, 1.5f, 0.8f);

        auto indicator = colours::text;
        if (band >= 0 && hasTarget)
            if (const auto s = d.severityOf (highlight::band (band)); s >= Severity::warning)
                indicator = severityColour (s);
        g.setColour (indicator);
        g.fillRect (cap.withSizeKeepingCentre (capW - 3.0f, 1.6f));

        // fuori scala: freccia all'estremità
        if (std::abs (value) > range)
        {
            const float y = value > 0.0f ? faders.getY() - 9.0f : faders.getBottom() + 9.0f;
            juce::Path arrow;
            arrow.addTriangle (x - 3.5f, y + (value > 0.0f ? 3.0f : -3.0f), x + 3.5f, y + (value > 0.0f ? 3.0f : -3.0f), x, y + (value > 0.0f ? -2.0f : 2.0f));
            g.setColour (colours::critical);
            g.fillPath (arrow);
        }
    }

    drawFrequencyLabels (g, freqRow, faders.getX(), colW);
}

//==============================================================================
void AnalyzerPanel::setData (const DashboardData& d)
{
    // tenuta dei picchi: scende di 10 dB al secondo
    const auto levels = thirdOctaveLevels (d.instant().spectrumInstantDb);
    const double t = nowSeconds();
    const bool snap = lastUpdate <= 0.0 || t - lastUpdate > 1.0;
    const float dt = (float) juce::jlimit (0.0, 0.2, t - lastUpdate);
    lastUpdate = t;
    for (size_t i = 0; i < levels.size(); ++i)
        peakHold[i] = (snap || levels[i] >= peakHold[i]) ? levels[i] : std::max (levels[i], peakHold[i] - 10.0f * dt);

    Panel::setData (d);
}

void AnalyzerPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& d = *data;
    const auto& s = d.master;

    // legenda in alto a destra
    auto legend = area.removeFromTop (15.0f);
    {
        g.setFont (vintage::font (10.0f));
        auto item = [&] (const juce::String& text, juce::Colour colour, float markW, float markH)
        {
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            auto r = legend.removeFromRight (w + markW + 16.0f);
            g.setColour (colour);
            g.fillRect (r.removeFromLeft (markW).withSizeKeepingCentre (markW, markH));
            g.setColour (colours::textFaint);
            g.drawText (text, r.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        };
        item ("picco"_t, kLedAmber, 8.0f, 3.0f);
        item ("media del brano"_t, colours::text, 8.0f, 2.0f);
        item ("ascolto"_t, kLedGreen, 6.0f, 8.0f);
    }
    area.removeFromTop (6.0f);

    auto freqRow = area.removeFromBottom (14.0f);
    auto scaleColumn = area.removeFromLeft (28.0f);
    const auto screen = area;
    vintage::drawDisplayWindow (g, screen);
    addHelp (screen, "rta");

    const auto plot = screen.reduced (4.0f, 5.0f);
    const float colW = plot.getWidth() / (float) kNumThirdOctaves;
    const int numSegments = std::max (8, (int) (plot.getHeight() / 6.0f));
    const float segH = plot.getHeight() / (float) numSegments;

    // range verticale: segue il massimo della media del brano, come lo spettro classico
    float top = -20.0f;
    for (auto v : s.spectrumLongTermDb) top = std::max (top, v);
    top = std::ceil ((top + 3.0f) / 6.0f) * 6.0f;
    constexpr float range = 54.0f;
    const float bottom = top - range;
    const auto segmentFor = [&] (float db) { return (int) std::round ((db - bottom) / range * (float) numSegments); };

    g.setFont (vintage::font (9.5f));
    g.setColour (colours::textFaint);
    for (float db = top; db >= bottom; db -= 18.0f)
    {
        const float y = plot.getBottom() - (db - bottom) / range * plot.getHeight();
        g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (scaleColumn.getX(), y - 6.0f, scaleColumn.getWidth() - 6.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    const auto levels = thirdOctaveLevels (d.instant().spectrumInstantDb);
    const auto average = thirdOctaveLevels (s.spectrumLongTermDb);
    const float ledW = std::max (2.0f, colW * 0.72f);

    for (int i = 0; i < kNumThirdOctaves; ++i)
    {
        const float x = plot.getX() + ((float) i + 0.5f) * colW - ledW * 0.5f;
        const int lit = s.valid ? segmentFor (levels[(size_t) i]) : 0;
        const int peak = s.valid ? segmentFor (peakHold[(size_t) i]) - 1 : -1;

        for (int k = 0; k < numSegments; ++k)
        {
            const float frac = (float) k / (float) numSegments;
            const auto colour = frac < 0.62f ? kLedGreen : (frac < 0.84f ? kLedAmber : kLedRed);
            const auto led = juce::Rectangle<float> (x, plot.getBottom() - (float) (k + 1) * segH + 1.0f, ledW, segH - 2.0f);
            g.setColour (k < lit ? colour : (k == peak ? kLedAmber : colour.withAlpha (0.08f)));
            g.fillRect (led);
        }

        // media del brano: tacca chiara
        if (s.valid && average[(size_t) i] > bottom)
        {
            const float y = plot.getBottom() - juce::jlimit (0.0f, 1.0f, (average[(size_t) i] - bottom) / range) * plot.getHeight();
            g.setColour (colours::text.withAlpha (0.9f));
            g.fillRect (x - 1.0f, y - 1.0f, ledW + 2.0f, 2.0f);
        }
    }

    drawFrequencyLabels (g, freqRow, plot.getX(), colW);
}

//==============================================================================
void ScopePanel::mouseDown (const juce::MouseEvent& e)
{
    if (data == nullptr || ! onMonitorChanged)
        return;

    for (int i = 0; i < 3; ++i)
        if (monitorButtons[(size_t) i].contains (e.position))
        {
            const auto mode = i == 0 ? MonitorMode::off : (i == 1 ? MonitorMode::mono : MonitorMode::side);
            // di nuovo sullo stesso: si torna all'ascolto normale
            onMonitorChanged (mode == data->monitor.mode ? MonitorChoice() : MonitorChoice { mode, -1 });
            return;
        }
}

void ScopePanel::mouseMove (const juce::MouseEvent& e)
{
    const bool over = std::any_of (monitorButtons.begin(), monitorButtons.end(), [&] (const auto& r) { return r.contains (e.position); });
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ScopePanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    // tasti d'ascolto in basso, con la spia del monitor attivo
    auto buttons = area.removeFromBottom (26.0f);
    area.removeFromBottom (10.0f);

    const float size = std::min (area.getWidth(), area.getHeight());
    const auto scope = area.withSizeKeepingCentre (size, size);
    addHelp (scope, "goniometer");
    drawScope (g, scope);

    const auto choice = data->monitor;
    const int selected = choice.mode == MonitorMode::off ? 0 : (choice.mode == MonitorMode::mono ? 1 : (choice.mode == MonitorMode::side ? 2 : -1));
    const float w = std::min (buttons.getWidth(), 240.0f);
    auto row = buttons.withSizeKeepingCentre (w, buttons.getHeight());
    addHelp (row, "monitor");
    static const char* names[] { "STEREO", "MONO", "SIDE" };
    const float segment = w / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto r = row.removeFromLeft (segment);
        monitorButtons[(size_t) i] = r;
        const bool on = i == selected;
        vintage::drawKey (g, r.reduced (0.5f), colours::button, on, i > 0, i < 2);
        vintage::drawLamp (g, { r.getX() + 10.0f, r.getCentreY() }, 3.0f, i == 0 ? colours::ok : colours::info, on);
        g.setColour (colours::text);
        g.setFont (vintage::font (11.0f, true));
        g.drawText (names[i], r.withTrimmedLeft (16.0f).translated (0.0f, on ? 1.0f : 0.0f), juce::Justification::centred);
    }
}

void ScopePanel::drawScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& s = data->instant();

    // ghiera metallica e schermo a fosfori
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6258), r.getX(), r.getY(), juce::Colour (0xff141210), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (juce::Colours::black);
    g.drawEllipse (r.reduced (0.5f), 1.0f);

    const auto screen = r.reduced (r.getWidth() * 0.065f);
    g.setColour (juce::Colours::black);
    g.fillEllipse (screen.expanded (2.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0f2d17), screen.getCentreX(), screen.getCentreY(),
                                             juce::Colour (0xff030a05), screen.getRight(), screen.getCentreY(), true));
    g.fillEllipse (screen);

    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addEllipse (screen);
    g.reduceClipRegion (clip);

    const auto c = screen.getCentre();
    const float radius = screen.getWidth() * 0.5f - 6.0f;

    // reticolo
    g.setColour (kPhosphor.withAlpha (0.16f));
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 1.0f);
    g.drawEllipse (juce::Rectangle<float> (radius, radius).withCentre (c), 1.0f);
    g.drawLine (c.x, c.y - radius, c.x, c.y + radius);
    g.drawLine (c.x - radius, c.y, c.x + radius, c.y);
    const float diag = radius * 0.707f;
    g.drawLine (c.x - diag, c.y - diag, c.x + diag, c.y + diag);
    g.drawLine (c.x + diag, c.y - diag, c.x - diag, c.y + diag);

    g.setColour (kPhosphor.withAlpha (0.45f));
    g.setFont (vintage::font (9.5f, true));
    g.drawText ("M", juce::Rectangle<float> (c.x + 3.0f, c.y - radius + 2.0f, 12.0f, 10.0f), juce::Justification::left);
    g.drawText ("L", juce::Rectangle<float> (c.x - diag - 4.0f, c.y - diag + 2.0f, 10.0f, 10.0f), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (c.x + diag - 6.0f, c.y - diag + 2.0f, 10.0f, 10.0f), juce::Justification::centred);

    // traccia: guadagno automatico sul picco dei punti, i più recenti più luminosi
    float peak = 1.0e-6f;
    for (auto v : s.goniometer) peak = std::max (peak, std::abs (v));
    const float scale = radius / std::max (peak * 1.414f, 0.05f);

    for (int i = 0; i < kGoniometerPoints; ++i)
    {
        const int idx = (s.goniometerWritePos + i) % kGoniometerPoints;   // dal più vecchio
        const float l = s.goniometer[(size_t) idx * 2], rr = s.goniometer[(size_t) idx * 2 + 1];
        const float x = c.x + (rr - l) * 0.707f * scale;
        const float y = c.y - (l + rr) * 0.707f * scale;
        const float age = (float) i / kGoniometerPoints;
        g.setColour (kPhosphor.withAlpha (0.06f + 0.16f * age));
        g.fillRect (x - 1.6f, y - 1.6f, 3.2f, 3.2f);
        g.setColour (kPhosphor.withAlpha (0.1f + 0.75f * age));
        g.fillRect (x - 0.7f, y - 0.7f, 1.4f, 1.4f);
    }

    // riflesso sul vetro curvo
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.1f), screen.getX(), screen.getY(),
                                             juce::Colours::white.withAlpha (0.0f), screen.getCentreX(), screen.getCentreY(), false));
    g.fillEllipse (screen.withTrimmedRight (screen.getWidth() * 0.3f).withTrimmedBottom (screen.getHeight() * 0.45f));
}

} // namespace ma::ui
