#include "Theme.h"
#include "HelpTexts.h"
#include "../Common/Text.h"

#include "MasterAgentLang.h"

#include <optional>

namespace ma::ui
{

namespace fonts
{
    juce::Font label (float size)  { return juce::Font (juce::FontOptions (size)); }
    juce::Font value (float size)  { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
    juce::Font title()             { return juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.08f); }
}

//==============================================================================
namespace
{
    UiSettings currentSettings;
    int themeVersion = 0;

    struct AccentPair { juce::Colour accent, accent2; };

    /** Accenti: sul tema chiaro servono tinte più scure per restare leggibili su fondo bianco. */
    AccentPair accentColours (AccentId id, bool light)
    {
        switch (id)
        {
            case AccentId::teal:   return light ? AccentPair { juce::Colour (0xff0d9488), juce::Colour (0xff0a7fc4) }
                                                : AccentPair { juce::Colour (0xff2dd4bf), juce::Colour (0xff5ac8fa) };
            case AccentId::indigo: return light ? AccentPair { juce::Colour (0xff4f5bd5), juce::Colour (0xff0a7fc4) }
                                                : AccentPair { juce::Colour (0xff8c9bff), juce::Colour (0xff5ac8fa) };
            case AccentId::silver: return light ? AccentPair { juce::Colour (0xff475569), juce::Colour (0xff94a3b8) }
                                                : AccentPair { juce::Colour (0xffc9d1dc), juce::Colour (0xff8b94a5) };
            case AccentId::blue:
            default:               return light ? AccentPair { juce::Colour (0xff0a7fc4), juce::Colour (0xff5b5bd6) }
                                                : AccentPair { juce::Colour (0xff5ac8fa), juce::Colour (0xff7b8cff) };
        }
    }

    void setDarkPalette()
    {
        using namespace colours;
        backgroundTop = juce::Colour (0xff0f1218);  backgroundBottom = juce::Colour (0xff0a0c10);  background = juce::Colour (0xff0c0f14);
        panelTop = juce::Colour (0xff191d26);       panelBottom = juce::Colour (0xff141820);       panel = juce::Colour (0xff171b23);
        panelBorder = juce::Colour (0x14ffffff);    well = juce::Colour (0xff0e1117);
        grid = juce::Colour (0xff1e232d);           axis = juce::Colour (0xff242a36);
        text = juce::Colour (0xffe9ecf1);           textDim = juce::Colour (0xff8b94a5);           textFaint = juce::Colour (0xff5a6273);
        reference = juce::Colour (0xffd59cf5);      target = juce::Colour (0xfff5d76e);
        ok = juce::Colour (0xff4cd38a);             info = juce::Colour (0xff5aa9fa);
        warning = juce::Colour (0xfff5b84a);        critical = juce::Colour (0xffff5f6d);
        headerTop = juce::Colour (0xff161a22);      headerBottom = juce::Colour (0xff11141a);
        line = juce::Colour (0x1affffff);           stripe = juce::Colour (0x08ffffff);
        button = juce::Colour (0xff1f2430);         popup = juce::Colour (0xff1a1e27);             tooltip = juce::Colour (0xff222734);
        hover = juce::Colour (0xff1d222c);          readout = juce::Colour (0xee1f2430);           shadow = juce::Colour (0x73000000);
        liveOn = juce::Colour (0xff6a4a12);
    }

    void setGraphitePalette()
    {
        // grigi neutri, senza la dominante blu del tema scuro
        using namespace colours;
        setDarkPalette();
        backgroundTop = juce::Colour (0xff1b1b1c);  backgroundBottom = juce::Colour (0xff141414);  background = juce::Colour (0xff171717);
        panelTop = juce::Colour (0xff262627);       panelBottom = juce::Colour (0xff202021);       panel = juce::Colour (0xff232324);
        well = juce::Colour (0xff161616);           grid = juce::Colour (0xff2e2e30);              axis = juce::Colour (0xff3a3a3d);
        text = juce::Colour (0xffededed);           textDim = juce::Colour (0xff9d9d9f);           textFaint = juce::Colour (0xff68686b);
        headerTop = juce::Colour (0xff222223);      headerBottom = juce::Colour (0xff1b1b1c);
        button = juce::Colour (0xff2c2c2e);         popup = juce::Colour (0xff252526);             tooltip = juce::Colour (0xff2d2d2f);
        hover = juce::Colour (0xff2a2a2c);          readout = juce::Colour (0xee2c2c2e);
    }

    void setHighContrastPalette()
    {
        // nero pieno, bordi marcati, testi e stati più saturi
        using namespace colours;
        setDarkPalette();
        backgroundTop = juce::Colour (0xff000000);  backgroundBottom = juce::Colour (0xff000000);  background = juce::Colour (0xff000000);
        panelTop = juce::Colour (0xff0b0b0b);       panelBottom = juce::Colour (0xff0b0b0b);       panel = juce::Colour (0xff0b0b0b);
        panelBorder = juce::Colour (0x66ffffff);    well = juce::Colour (0xff000000);
        grid = juce::Colour (0xff3a3a3a);           axis = juce::Colour (0xff6a6a6a);
        text = juce::Colour (0xffffffff);           textDim = juce::Colour (0xffd0d0d0);           textFaint = juce::Colour (0xffa0a0a0);
        reference = juce::Colour (0xffe8b4ff);      target = juce::Colour (0xffffe14d);
        ok = juce::Colour (0xff3cff8f);             info = juce::Colour (0xff6cc0ff);
        warning = juce::Colour (0xffffc23d);        critical = juce::Colour (0xffff4d5e);
        headerTop = juce::Colour (0xff000000);      headerBottom = juce::Colour (0xff000000);
        line = juce::Colour (0x59ffffff);           stripe = juce::Colour (0x14ffffff);
        button = juce::Colour (0xff1a1a1a);         popup = juce::Colour (0xff111111);             tooltip = juce::Colour (0xff1a1a1a);
        hover = juce::Colour (0xff1c1c1c);          readout = juce::Colour (0xf0000000);
    }

    void setLightPalette()
    {
        using namespace colours;
        backgroundTop = juce::Colour (0xffeef1f5);  backgroundBottom = juce::Colour (0xffe3e7ed);  background = juce::Colour (0xffe9ecf1);
        panelTop = juce::Colour (0xffffffff);       panelBottom = juce::Colour (0xfff8f9fb);       panel = juce::Colour (0xfffcfcfd);
        panelBorder = juce::Colour (0x1f0f172a);    well = juce::Colour (0xfff0f2f6);
        grid = juce::Colour (0xffdde2e9);           axis = juce::Colour (0xffc3cad4);
        text = juce::Colour (0xff18202c);           textDim = juce::Colour (0xff566173);           textFaint = juce::Colour (0xff8a93a3);
        reference = juce::Colour (0xff9b45c9);      target = juce::Colour (0xffa87b00);
        ok = juce::Colour (0xff17994f);             info = juce::Colour (0xff2b78d0);
        warning = juce::Colour (0xffc77700);        critical = juce::Colour (0xffd92b3c);
        headerTop = juce::Colour (0xffffffff);      headerBottom = juce::Colour (0xfff4f6f9);
        line = juce::Colour (0x220f172a);           stripe = juce::Colour (0x0a0f172a);
        button = juce::Colour (0xfff1f3f6);         popup = juce::Colour (0xffffffff);             tooltip = juce::Colour (0xffffffff);
        hover = juce::Colour (0xffe8edf4);          readout = juce::Colour (0xf2ffffff);           shadow = juce::Colour (0x22000000);
        liveOn = juce::Colour (0xfffbe3b8);
    }

    juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MasterAgent")
                   .getChildFile ("ui.settings");
    }

    /** Tooltip: la prima riga è il titolo se il testo ne ha più di una (spiegazioni della guida "?"). */
    juce::TextLayout layoutTip (const juce::String& text)
    {
        constexpr float maxWidth = 360.0f;
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.setLineSpacing (2.0f);

        if (text.containsChar ('\n'))
        {
            s.append (text.upToFirstOccurrenceOf ("\n", false, false) + "\n", fonts::value (13.0f), colours::accent);
            s.append (text.fromFirstOccurrenceOf ("\n", false, false), fonts::label (12.5f), colours::text);
        }
        else
        {
            s.append (text, fonts::label (12.5f), colours::text);
        }

        juce::TextLayout layout;
        layout.createLayout (s, maxWidth);
        return layout;
    }
}

juce::String themeName (ThemeId id)
{
    switch (id)
    {
        case ThemeId::dark:         return "Scuro"_t;
        case ThemeId::light:        return "Chiaro"_t;
        case ThemeId::graphite:     return "Grafite"_t;
        case ThemeId::highContrast: return "Alto contrasto"_t;
    }
    return {};
}

juce::String accentName (AccentId id)
{
    switch (id)
    {
        case AccentId::blue:   return "Azzurro"_t;
        case AccentId::teal:   return "Turchese"_t;
        case AccentId::indigo: return "Indaco"_t;
        case AccentId::silver: return "Argento"_t;
    }
    return {};
}

juce::String languageName (Language l)
{
    // ogni lingua col suo nome, così si riconosce anche senza capire l'altra
    return l == Language::english ? "English" : "Italiano";
}

namespace
{
    void applyLanguage (Language language)
    {
        static std::optional<Language> applied;
        if (applied == language)
            return;
        applied = language;

        if (language == Language::english)
            juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (
                juce::String::fromUTF8 (LangData::en_txt, LangData::en_txtSize), false));
        else
            juce::LocalisedStrings::setCurrentMappings (nullptr);
    }
}

void applyTheme (const UiSettings& settings)
{
    currentSettings = settings;
    applyLanguage (settings.language);

    switch (settings.theme)
    {
        case ThemeId::light:        setLightPalette(); break;
        case ThemeId::graphite:     setGraphitePalette(); break;
        case ThemeId::highContrast: setHighContrastPalette(); break;
        case ThemeId::dark:
        default:                    setDarkPalette(); break;
    }

    using namespace colours;
    const auto pair = accentColours (settings.accent, isLightTheme());
    accent = pair.accent;
    accent2 = pair.accent2;

    if (isLightTheme())
    {
        // sul chiaro i pulsanti attivi sono tinte tenui: il testo resta scuro e leggibile
        accentDim = accent.interpolatedWith (juce::Colours::white, 0.78f);
        mixOn = accent2.interpolatedWith (juce::Colours::white, 0.78f);
        listenOn = reference.interpolatedWith (juce::Colours::white, 0.75f);
    }
    else
    {
        accentDim = (settings.theme == ThemeId::dark && settings.accent == AccentId::blue)
                        ? juce::Colour (0xff24506a)
                        : accent.interpolatedWith (background, 0.62f);
        mixOn = accent2.darker (0.9f);
        listenOn = reference.darker (0.5f);
    }

    ++themeVersion;
}

const UiSettings& getUiSettings()  { return currentSettings; }
bool isLightTheme()                { return currentSettings.theme == ThemeId::light; }
int getThemeVersion()              { return themeVersion; }

UiSettings loadUiSettings()
{
    UiSettings s;
    if (auto xml = juce::parseXML (settingsFile()))
    {
        s.theme = (ThemeId) juce::jlimit (0, 3, xml->getIntAttribute ("theme", 0));
        s.accent = (AccentId) juce::jlimit (0, 3, xml->getIntAttribute ("accent", 0));
        s.effects = xml->getBoolAttribute ("effects", true);
        s.language = xml->getIntAttribute ("language", 0) == 1 ? Language::english : Language::italian;
        s.spectrumMidSide = xml->getBoolAttribute ("spectrumMidSide", false);

        const int zoom = xml->getIntAttribute ("zoom", 100);
        for (int step : kZoomSteps)
            if (step == zoom)
                s.zoomPercent = zoom;
    }
    return s;
}

void saveUiSettings (const UiSettings& s)
{
    juce::XmlElement xml ("MasterAgentUi");
    xml.setAttribute ("theme", (int) s.theme);
    xml.setAttribute ("accent", (int) s.accent);
    xml.setAttribute ("effects", s.effects);
    xml.setAttribute ("zoom", s.zoomPercent);
    xml.setAttribute ("language", (int) s.language);
    xml.setAttribute ("spectrumMidSide", s.spectrumMidSide);

    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    xml.writeTo (file);
}

juce::Colour hoverTint (juce::Colour c, float amount)
{
    return isLightTheme() ? c.darker (amount * 0.6f) : c.brighter (amount);
}

juce::Colour severityColour (Severity s)
{
    switch (s)
    {
        case Severity::ok:       return colours::ok;
        case Severity::info:     return colours::info;
        case Severity::warning:  return colours::warning;
        case Severity::critical: return colours::critical;
    }
    return colours::text;
}

juce::Colour DashboardData::colourFor (const juce::String& key, juce::Colour fallback) const
{
    const auto s = severityOf (key);
    if (s >= Severity::warning)
        return severityColour (s);

    for (const auto& f : comparison.findings)
        if (f.key == key && ! f.ignored)
            return severityColour (s);   // presente e nel range -> verde

    return fallback;
}

juce::String formatDb (float v, int decimals)
{
    if (v <= kSilenceDb + 0.5f)
        return "--";
    return decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    refreshColours();
}

void LookAndFeel::refreshColours()
{
    setColourScheme ({ colours::background, colours::panel, colours::panel, colours::panelBorder,
                       colours::text, colours::accent, colours::text, colours::accentDim, colours::text });

    setColour (juce::ComboBox::backgroundColourId, colours::well);
    setColour (juce::ComboBox::outlineColourId, colours::panelBorder);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::TextButton::buttonColourId, colours::button);
    setColour (juce::TextButton::buttonOnColourId, colours::accentDim);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::text);
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, colours::textFaint);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::PopupMenu::backgroundColourId, colours::popup);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::headerTextColourId, colours::textDim);
    setColour (juce::ScrollBar::thumbColourId, colours::text.withAlpha (0.19f));
    setColour (juce::TooltipWindow::backgroundColourId, colours::tooltip);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::backgroundColourId, colours::popup);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::line);
    setColour (juce::TextEditor::backgroundColourId, colours::well);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextEditor::highlightColourId, colours::accentDim);
    setColour (juce::CaretComponent::caretColourId, colours::text);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (std::min (13.5f, buttonHeight * 0.5f), juce::Font::bold));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.5f));
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = 7.0f;

    auto base = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.45f);
    else if (isButtonDown)
        base = hoverTint (base, 0.15f);
    else if (isMouseOverButton)
        base = hoverTint (base, 0.08f);

    const bool flatLeft = button.isConnectedOnLeft(), flatRight = button.isConnectedOnRight();
    juce::Path p;
    p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius, radius,
                           ! flatLeft, ! flatRight, ! flatLeft, ! flatRight);

    if (getUiSettings().effects)
        g.setGradientFill (juce::ColourGradient (base.brighter (0.06f), 0.0f, bounds.getY(), base.darker (0.06f), 0.0f, bounds.getBottom(), false));
    else
        g.setColour (base);
    g.fillPath (p);

    const auto onOutline = isLightTheme() ? colours::accent.withAlpha (0.55f) : base.brighter (0.4f).withAlpha (0.6f);
    g.setColour (button.getToggleState() ? onOutline : colours::line);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const auto fill = box.findColour (juce::ComboBox::backgroundColourId);
    g.setColour (box.isMouseOver() ? hoverTint (fill, 0.05f) : fill);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 16.0f, cy = height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (colours::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (colours::line);
    g.drawRect (0, 0, width, height);
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>&, juce::TextLayout& textLayout)
{
    // come LookAndFeel_V4, ma con un'icona piccola e i colori della palette
    const auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (alert.findColour (juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (alert.findColour (juce::AlertWindow::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    int iconSpaceUsed = 0;
    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
    {
        const bool warning = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon;
        const auto colour = warning ? colours::warning : colours::accent;
        const auto icon = juce::Rectangle<float> (24.0f, 28.0f, 34.0f, 34.0f);
        g.setColour (colour.withAlpha (0.16f));
        g.fillEllipse (icon);
        g.setColour (colour);
        g.drawEllipse (icon.reduced (0.75f), 1.5f);
        g.setFont (fonts::value (18.0f));
        const char glyph = warning ? '!' : (alert.getAlertType() == juce::MessageBoxIconType::InfoIcon ? 'i' : '?');
        g.drawText (juce::String::charToString ((juce::juce_wchar) glyph), icon, juce::Justification::centred);
        iconSpaceUsed = 80;
    }

    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    const juce::Rectangle<int> textBounds (iconSpaceUsed, 30, alert.getWidth() - iconSpaceUsed,
                                           alert.getHeight() - getAlertWindowButtonHeight() - 20);
    textLayout.draw (g, textBounds.toFloat());
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTip (tipText);
    const int w = (int) std::ceil (layout.getWidth()) + 24;
    const int h = (int) std::ceil (layout.getHeight()) + 18;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 20,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 14,
                                 w, h)
               .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

    layoutTip (text).draw (g, bounds.reduced (12.0f, 9.0f));
}

//==============================================================================
Panel::Panel (juce::String t, juce::String help) : title (std::move (t)), helpKey (std::move (help))
{
    setOpaque (false);
}

juce::Rectangle<float> Panel::getContentArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedTop (22.0f);
}

bool Panel::isHighlighted (const juce::String& key) const
{
    return data != nullptr && key.isNotEmpty() && data->highlightKey == key;
}

void Panel::addHelp (juce::Rectangle<float> r, const juce::String& key) const
{
    if (key.isNotEmpty() && ! r.isEmpty())
        helpZones.emplace_back (r, key);
}

void Panel::markHighlight (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& key) const
{
    addHelp (r, key);

    if (! isHighlighted (key))
        return;

    highlightedThisPaint = true;
    const auto c = data->highlightColour;
    auto area = r.expanded (4.0f, 3.0f);
    g.setColour (c.withAlpha (0.13f));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (c.withAlpha (0.9f));
    g.drawRoundedRectangle (area, 6.0f, 1.5f);
}

juce::String Panel::getTooltip()
{
    if (data == nullptr || ! data->helpMode)
        return {};

    // la zona più piccola che contiene il mouse: un valore dentro un blocco vince sul blocco
    const auto pos = getMouseXYRelative().toFloat();
    const juce::String* best = nullptr;
    float bestArea = std::numeric_limits<float>::max();
    for (const auto& [r, key] : helpZones)
        if (r.contains (pos) && r.getWidth() * r.getHeight() < bestArea)
        {
            bestArea = r.getWidth() * r.getHeight();
            best = &key;
        }

    if (best != nullptr)
        if (auto text = helpTextFor (*best); text.isNotEmpty())
            return text;

    return helpTextFor (helpKey);
}

void Panel::fillWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, radius);
    g.setColour (colours::line.withMultipliedAlpha (0.4f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void Panel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    constexpr float radius = 10.0f;

    if (getUiSettings().effects)
        g.setGradientFill (juce::ColourGradient (colours::panelTop, 0.0f, 0.0f, colours::panelBottom, 0.0f, bounds.getBottom(), false));
    else
        g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, radius);

    // titolo con punto accento
    auto titleArea = bounds.reduced (14.0f, 10.0f).removeFromTop (16.0f);
    g.setColour (colours::accent);
    g.fillEllipse (titleArea.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
    titleArea.removeFromLeft (8.0f);

    if (badge.isNotEmpty())
    {
        g.setFont (fonts::value (10.0f));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), badge) + 14.0f;
        auto b = titleArea.removeFromRight (w).withSizeKeepingCentre (w, 16.0f);
        g.setColour (badgeColour.withAlpha (0.16f));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (badgeColour);
        g.drawText (badge, b, juce::Justification::centred);
    }

    g.setColour (colours::textDim);
    g.setFont (fonts::title());
    g.drawText (title.toUpperCase(), titleArea, juce::Justification::centredLeft);

    highlightedThisPaint = false;
    helpZones.clear();
    if (data != nullptr)
        paintContent (g, getContentArea());

    // bordo: si accende se il pannello contiene l'elemento evidenziato dal report
    if (highlightedThisPaint)
    {
        g.setColour (data->highlightColour.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 3.0f);
        g.setColour (data->highlightColour.withAlpha (0.8f));
        g.drawRoundedRectangle (bounds, radius, 1.2f);
    }
    else
    {
        g.setColour (colours::panelBorder);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

void Panel::drawValue (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label,
                       const juce::String& value, const juce::String& unit, juce::Colour colour, float valueSize)
{
    g.setColour (colours::textDim);
    g.setFont (fonts::label (11.0f));
    g.drawText (label, r.removeFromTop (14.0f), juce::Justification::centredLeft);

    g.setColour (colour);
    g.setFont (fonts::value (valueSize));
    const auto valueWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), value);
    auto valueArea = r.removeFromTop (valueSize + 6.0f);
    g.drawText (value, valueArea, juce::Justification::centredLeft);

    if (unit.isNotEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::label (std::max (10.0f, valueSize * 0.38f)));
        g.drawText (unit, valueArea.withTrimmedLeft (valueWidth + 5.0f).withTrimmedBottom (valueSize * 0.14f),
                    juce::Justification::bottomLeft);
    }
}

void Panel::drawRow (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value,
                     juce::Colour colour, const juce::String& refValue)
{
    g.setFont (fonts::label (12.0f));
    g.setColour (colours::textDim);
    g.drawText (label, r, juce::Justification::centredLeft);

    if (refValue.isNotEmpty())
    {
        g.setColour (colours::reference);
        g.drawText (refValue, r.removeFromRight (52.0f), juce::Justification::centredRight);
        r.removeFromRight (8.0f);
    }

    g.setColour (colour);
    g.setFont (fonts::value (12.0f));
    g.drawText (value, r, juce::Justification::centredRight);
}

void Panel::drawHBar (juce::Graphics& g, juce::Rectangle<float> r, float value, float minV, float maxV,
                      juce::Colour colour, bool fromCentre)
{
    const float radius = r.getHeight() * 0.5f;
    g.setColour (colours::grid);
    g.fillRoundedRectangle (r, radius);

    const auto norm = [&] (float v) { return juce::jlimit (0.0f, 1.0f, (v - minV) / (maxV - minV)); };
    const float x = r.getX() + norm (value) * r.getWidth();

    if (fromCentre)
    {
        const float c = r.getX() + norm ((minV + maxV) * 0.5f) * r.getWidth();
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (std::min (c, x), r.getY(), std::max (c, x), r.getBottom()), radius);
        g.setColour (colours::textFaint);
        g.fillRect (c - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (colour.darker (0.3f), r.getX(), 0.0f, colour, x, 0.0f, false));
        g.fillRoundedRectangle (r.withRight (std::max (r.getX() + r.getHeight(), x)), radius);
    }
}

} // namespace ma::ui
