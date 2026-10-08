#pragma once

#include "../Analysis/AnalysisSnapshot.h"
#include "../Compare/Comparator.h"
#include "../Compare/VersionCompare.h"
#include "../Compare/TargetProfile.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ma::ui
{

/**
    Palette attiva. Sono variabili (non costanti) perché il tema si cambia dal menu Opzioni:
    applyTheme() le riscrive e i componenti le rileggono al paint successivo.
*/
namespace colours
{
    inline juce::Colour backgroundTop    { 0xff0f1218 };
    inline juce::Colour backgroundBottom { 0xff0a0c10 };
    inline juce::Colour background       { 0xff0c0f14 };
    inline juce::Colour panelTop         { 0xff191d26 };
    inline juce::Colour panelBottom      { 0xff141820 };
    inline juce::Colour panel            { 0xff171b23 };
    inline juce::Colour panelBorder      { 0x14ffffff };
    inline juce::Colour well             { 0xff0e1117 };   // aree incassate (grafici, meter)
    inline juce::Colour grid             { 0xff1e232d };
    inline juce::Colour axis             { 0xff242a36 };   // linea dello zero nelle barre
    inline juce::Colour text             { 0xffe9ecf1 };
    inline juce::Colour textDim          { 0xff8b94a5 };
    inline juce::Colour textFaint        { 0xff5a6273 };
    inline juce::Colour accent           { 0xff5ac8fa };
    inline juce::Colour accent2          { 0xff7b8cff };
    inline juce::Colour accentDim        { 0xff24506a };
    inline juce::Colour reference        { 0xffd59cf5 };
    inline juce::Colour target           { 0xfff5d76e };
    inline juce::Colour ok               { 0xff4cd38a };
    inline juce::Colour info             { 0xff5aa9fa };
    inline juce::Colour warning          { 0xfff5b84a };
    inline juce::Colour critical         { 0xffff5f6d };

    // superfici e dettagli dei controlli
    inline juce::Colour headerTop        { 0xff161a22 };
    inline juce::Colour headerBottom     { 0xff11141a };
    inline juce::Colour line             { 0x1affffff };   // bordi sottili e separatori
    inline juce::Colour stripe           { 0x08ffffff };   // righe alternate delle tabelle
    inline juce::Colour button           { 0xff1f2430 };
    inline juce::Colour popup            { 0xff1a1e27 };
    inline juce::Colour tooltip          { 0xff222734 };
    inline juce::Colour hover            { 0xff1d222c };   // diagnosi sotto il mouse
    inline juce::Colour readout          { 0xee1f2430 };   // riquadro del cursore sullo spettro
    inline juce::Colour shadow           { 0x73000000 };
    inline juce::Colour liveOn           { 0xff6a4a12 };   // pulsanti attivi con colore proprio
    inline juce::Colour mixOn            { 0xff1d2140 };
    inline juce::Colour listenOn         { 0xff6a4e7a };
}

/** Personalizzazione dell'interfaccia (menu Opzioni > Interfaccia), comune a tutte le istanze del plugin. */
enum class ThemeId  { dark, light, graphite, highContrast };
enum class AccentId { blue, teal, indigo, silver };
enum class Language { italian, english };

/** Dimensioni dell'interfaccia proposte nel menu (percentuale). */
constexpr std::array<int, 6> kZoomSteps { 75, 85, 100, 115, 130, 150 };

struct UiSettings
{
    ThemeId theme = ThemeId::dark;
    AccentId accent = AccentId::blue;
    bool effects = true;   // ombre e sfumature dei pannelli (off = stile piatto)
    int zoomPercent = 100;
    Language language = Language::italian;
    bool spectrumMidSide = false;   // spettro: vista Mid/Side invece di L+R

    float zoom() const noexcept { return (float) zoomPercent / 100.0f; }

    bool operator== (const UiSettings&) const = default;
};

juce::String themeName (ThemeId);
juce::String accentName (AccentId);
juce::String languageName (Language);

/** Imposta palette e lingua. Aggiorna anche il numero di versione del tema. */
void applyTheme (const UiSettings&);
const UiSettings& getUiSettings();
bool isLightTheme();

/** Cresce a ogni applyTheme(): chi tiene in cache layout o immagini colorate lo confronta per rigenerarli. */
int getThemeVersion();

/** Preferenze salvate in %APPDATA%\MasterAgent\ui.settings (condivise da tutte le istanze). */
UiSettings loadUiSettings();
void saveUiSettings (const UiSettings&);

/** Colore per l'hover: più chiaro sui temi scuri, più scuro su quello chiaro. */
juce::Colour hoverTint (juce::Colour c, float amount);

namespace fonts
{
    juce::Font label (float size = 11.0f);
    juce::Font value (float size);
    juce::Font title();
}

juce::Colour severityColour (Severity s);

/** Tutti i dati che la dashboard visualizza a ogni refresh. */
struct DashboardData
{
    AnalysisSnapshot master;
    bool hasReference = false;
    AnalysisSnapshot reference;
    ComparisonResult comparison;
    const TargetProfile* profile = nullptr;   // profilo attivo (può essere generato dal reference)
    juce::String targetName;

    WorkPhase phase = WorkPhase::master;

    juce::String highlightKey;                // elemento evidenziato (hover su una diagnosi)
    juce::Colour highlightColour { colours::accent };
    float highlightTimeStart = -1.0f;         // tratto del brano collegato alla diagnosi (s), -1 = nessuno
    float highlightTimeEnd = -1.0f;

    bool helpMode = false;                    // tasto "?": i pannelli spiegano cosa misura l'elemento sotto il mouse

    bool masterFromFile = false;              // il master è un file analizzato offline, non l'ingresso live
    juce::String masterFileName;

    bool hasVersion = false;                  // versione salvata con cui confrontare il master
    AnalysisSnapshot version;
    juce::String versionName;
    VersionComparison versionComparison;

    Severity severityOf (const juce::String& key) const { return comparison.severityOfKey (key); }
    juce::Colour colourFor (const juce::String& key, juce::Colour fallback = colours::text) const;
};

juce::String formatDb (float v, int decimals = 1);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    /** Rilegge la palette attiva (dopo un cambio di tema). */
    void refreshColours();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;

    // tooltip a due livelli: se il testo ha più righe, la prima è il titolo
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

/**
    LookAndFeel unico per tutti gli editor aperti (via juce::SharedResourcePointer), impostato anche come
    default: così avvisi e finestre di dialogo, che non hanno un componente padre, seguono il tema.
    Il default vale solo per questo plugin (ogni DLL ha la sua copia di JUCE).
*/
struct SharedLookAndFeel
{
    SharedLookAndFeel()  { juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel); }
    ~SharedLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }

    LookAndFeel lookAndFeel;
};

/** Pannello della dashboard: sfondo con gradiente, titolo, evidenziazione degli elementi collegati al report. */
class Panel : public juce::Component,
              public juce::TooltipClient
{
public:
    explicit Panel (juce::String title, juce::String helpKey);

    void paint (juce::Graphics& g) override;
    virtual void paintContent (juce::Graphics& g, juce::Rectangle<float> area) = 0;
    virtual void setData (const DashboardData& d) { data = &d; repaint(); }

    /** Badge testuale a destra del titolo (es. "LIVE 20 s"). */
    juce::String badge;
    juce::Colour badgeColour { colours::accent };

    /** In modalità guida: spiegazione dell'elemento sotto il mouse (o del pannello). */
    juce::String getTooltip() override;

protected:
    juce::Rectangle<float> getContentArea() const;

    bool isHighlighted (const juce::String& key) const;

    /** Se key è evidenziata, disegna l'alone attorno a r e segna il pannello come coinvolto. Registra anche la zona per la guida. */
    void markHighlight (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& key) const;

    /** Registra una zona per la guida "?" (vedi HelpTexts). Va chiamata durante il paint: le zone si azzerano a ogni paint. */
    void addHelp (juce::Rectangle<float> r, const juce::String& key) const;

    /** Valore grande con etichetta. */
    static void drawValue (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label,
                           const juce::String& value, const juce::String& unit, juce::Colour colour, float valueSize);

    /** Riga etichetta / valore (con valore reference opzionale a destra). */
    static void drawRow (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value,
                         juce::Colour colour, const juce::String& refValue = {});

    /** Barra orizzontale arrotondata. */
    static void drawHBar (juce::Graphics& g, juce::Rectangle<float> r, float value, float minV, float maxV,
                          juce::Colour colour, bool fromCentre = false);

    static void fillWell (juce::Graphics& g, juce::Rectangle<float> r, float radius = 6.0f);

    juce::String title;
    juce::String helpKey;
    const DashboardData* data = nullptr;

private:
    mutable bool highlightedThisPaint = false;
    mutable std::vector<std::pair<juce::Rectangle<float>, juce::String>> helpZones;
};

} // namespace ma::ui
