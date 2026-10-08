#pragma once

#include "Theme.h"

#include <optional>

namespace ma::ui
{

/**
    Punteggio con pagella per area, priorità d'intervento ed elenco delle diagnosi ordinate per severità.
    Il passaggio del mouse su una diagnosi evidenzia l'elemento collegato nella dashboard; un clic su
    un'area filtra l'elenco; il clic destro su una diagnosi la segna come scelta voluta (ignorata).
*/
class ReportPanel : public Panel
{
public:
    ReportPanel();
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
    void setData (const DashboardData& d) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** Chiamato quando il mouse entra/esce da una diagnosi (nullptr = nessuna). */
    std::function<void (const Finding* finding, juce::Colour colour)> onHover;

    /** L'utente ha segnato (o ripristinato) una diagnosi come scelta voluta. */
    std::function<void (const juce::String& findingId, bool ignored)> onIgnoreChanged;

    /** Versioni del master: salva l'analisi attuale / scegli con quale confrontare (-1 = nessuna). */
    std::function<void()> onSaveVersion;
    std::function<void (int index)> onCompareVersionChanged;
    void setVersions (const juce::StringArray& names, int selected);

private:
    class FindingList : public juce::Component,
                        public juce::TooltipClient
    {
    public:
        explicit FindingList (ReportPanel& owner) : owner (owner) {}

        juce::String getTooltip() override;

        void setFindings (const ComparisonResult& result, bool showOk, int width);
        void paint (juce::Graphics& g) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        enum class ItemType { finding, header, ignoredToggle, text };

        struct Item
        {
            ItemType type = ItemType::finding;
            Finding finding;
            juce::TextLayout layout;
            float y = 0.0f, height = 0.0f;
            juce::String headerText;
        };

        int itemAt (float y) const;
        void setHovered (int index);

        ReportPanel& owner;
        std::vector<Item> items;
        juce::String signature;
        int hovered = -1;
        bool showIgnored = false;
    };

    int areaRowAt (juce::Point<float> p) const;

    juce::ToggleButton showOkButton { "Mostra anche i parametri nel range" };
    juce::TextButton saveVersionButton;
    juce::ComboBox versionBox;
    juce::Viewport viewport;
    FindingList list { *this };

    std::optional<Area> areaFilter;                                // diagnosi di una sola area (clic sulla pagella)
    std::array<juce::Rectangle<float>, kNumAreas> areaRows {};    // geometria dell'ultimo paint, per il mouse
    int hoveredArea = -1;
};

} // namespace ma::ui
