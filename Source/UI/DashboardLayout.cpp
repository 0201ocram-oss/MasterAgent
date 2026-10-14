#include "DashboardLayout.h"

namespace ma::ui
{

std::vector<Panel*> DashboardPanels::all() const
{
    std::vector<Panel*> panels;
    for (auto* p : { loudness, peak, dynamics, spectrum, stereo, meters, tonalEq, analyzer, scope, streaming, report, waveform })
        if (p != nullptr)
            panels.push_back (p);
    return panels;
}

void DashboardPanels::setVisibility (bool trackScreen, const UiSettings& settings) const
{
    const bool vintageStyle = settings.style == UiStyle::vintage;
    for (auto* p : { loudness, peak, dynamics, spectrum, stereo })
        if (p != nullptr)
            p->setVisible (trackScreen && ! vintageStyle);
    for (auto* p : { meters, tonalEq, analyzer, scope })
        if (p != nullptr)
            p->setVisible (trackScreen && vintageStyle);

    if (streaming != nullptr)
        streaming->setVisible (trackScreen && settings.showStreaming);
    if (report != nullptr)
        report->setVisible (trackScreen && settings.showReport);
    if (waveform != nullptr)
        waveform->setVisible (trackScreen);
}

void DashboardPanels::layout (juce::Rectangle<int> area, const UiSettings& settings) const
{
    constexpr int gap = 12;
    auto r = area.reduced (12);

    // report a destra per tutta l'altezza; nascosto, i pannelli di misura prendono tutta la larghezza
    if (report != nullptr && settings.showReport)
    {
        report->setBounds (r.removeFromRight ((int) ((float) r.getWidth() * 0.27f)));
        r.removeFromRight (gap);
    }

    // standalone: la forma d'onda del brano sopra i pannelli di misura
    if (waveform != nullptr)
    {
        waveform->setBounds (r.removeFromTop (std::max (150, (int) ((float) r.getHeight() * 0.19f))));
        r.removeFromTop (gap);
    }

    const bool withStreaming = streaming != nullptr && settings.showStreaming;

    if (settings.style == UiStyle::vintage)
    {
        // meter in alto; sotto EQ a bande, analizzatore e oscilloscopio; streaming in fondo
        if (withStreaming)
        {
            streaming->setBounds (r.removeFromBottom (std::max (170, (int) ((float) r.getHeight() * 0.26f))));
            r.removeFromBottom (gap);
        }
        if (meters != nullptr)
            meters->setBounds (r.removeFromTop ((int) ((float) r.getHeight() * 0.5f)));
        r.removeFromTop (gap);

        if (scope != nullptr)
        {
            scope->setBounds (r.removeFromRight (std::min (r.getHeight() + 16, (int) ((float) r.getWidth() * 0.27f))));
            r.removeFromRight (gap);
        }
        if (tonalEq != nullptr)
            tonalEq->setBounds (r.removeFromLeft ((int) ((float) (r.getWidth() - gap) * 0.55f)));
        r.removeFromLeft (gap);
        if (analyzer != nullptr)
            analyzer->setBounds (r);
        return;
    }

    // classico: loudness, peak e dinamica; spettro e stereo; streaming in fondo.
    // Senza lo streaming le due righe si dividono lo spazio nelle stesse proporzioni.
    auto row1 = r.removeFromTop ((int) ((float) r.getHeight() * (withStreaming ? 0.34f : 0.42f)));
    r.removeFromTop (gap);
    if (withStreaming)
    {
        streaming->setBounds (r.removeFromBottom (std::max (196, (int) ((float) r.getHeight() * 0.3f))));
        r.removeFromBottom (gap);
    }
    auto row2 = r;

    const int w1 = row1.getWidth() - 2 * gap;
    if (loudness != nullptr) loudness->setBounds (row1.removeFromLeft ((int) ((float) w1 * 0.40f)));
    row1.removeFromLeft (gap);
    if (peak != nullptr) peak->setBounds (row1.removeFromLeft ((int) ((float) w1 * 0.28f)));
    row1.removeFromLeft (gap);
    if (dynamics != nullptr) dynamics->setBounds (row1);

    if (spectrum != nullptr) spectrum->setBounds (row2.removeFromLeft ((int) ((float) (row2.getWidth() - gap) * 0.62f)));
    row2.removeFromLeft (gap);
    if (stereo != nullptr) stereo->setBounds (row2);
}

} // namespace ma::ui
