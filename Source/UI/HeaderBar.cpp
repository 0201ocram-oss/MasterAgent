#include "HeaderBar.h"
#include "../Common/Text.h"

namespace ma::ui
{

void IconButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), isMouseOver, isButtonDown);

    const auto area = getLocalBounds().toFloat().reduced (8.0f);
    const auto colour = getToggleState() ? colours::text : (isMouseOver ? colours::text : colours::textDim);
    g.setColour (colour);

    if (icon == Icon::help)
    {
        g.setFont (fonts::value (15.0f));
        g.drawText ("?", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // quattro angoli: verso l'esterno (entra) o verso l'interno (esci)
    const float len = area.getWidth() * 0.36f;
    const bool outward = icon == Icon::enterFullScreen;
    juce::Path p;
    for (int corner = 0; corner < 4; ++corner)
    {
        const float sx = (corner % 2 == 0) ? -1.0f : 1.0f, sy = (corner < 2) ? -1.0f : 1.0f;
        const auto c = area.getCentre();
        const juce::Point<float> tip (c.x + sx * area.getWidth() * 0.5f, c.y + sy * area.getHeight() * 0.5f);
        if (outward)
        {
            p.startNewSubPath (tip.x - sx * len, tip.y);
            p.lineTo (tip);
            p.lineTo (tip.x, tip.y - sy * len);
        }
        else
        {
            const juce::Point<float> inner (tip.x - sx * len, tip.y - sy * len);
            p.startNewSubPath (tip.x - sx * len, tip.y);
            p.lineTo (inner);
            p.lineTo (tip.x, inner.y);
        }
    }
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

//==============================================================================
HeaderBar::HeaderBar()
{
    addAndMakeVisible (targetBox);
    targetBox.onChange = [this] { if (onTargetChanged) onTargetChanged (getSelectedTargetId()); };
    targetBox.setTooltip ("Standard di confronto: profilo di mercato o brano di riferimento");

    // selettore di modalità a segmenti
    for (auto* b : { &wholeSongButton, &liveButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
    }
    wholeSongButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    liveButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    wholeSongButton.setToggleState (true, juce::dontSendNotification);
    wholeSongButton.setTooltip ("Accumula le misure su tutto il brano dall'ultimo reset: per il verdetto finale");
    liveButton.setTooltip ("Finestra mobile sugli ultimi secondi: vedi subito l'effetto di ciò che regoli, senza Reset"_u);
    liveButton.onClick = [this] { if (liveButton.getToggleState() && onLiveModeChanged) onLiveModeChanged (true); };
    wholeSongButton.onClick = [this] { if (wholeSongButton.getToggleState() && onLiveModeChanged) onLiveModeChanged (false); };

    // fase di lavoro: mix bus durante il mixaggio oppure master
    for (auto* b : { &mixButton, &masterButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1002);
    }
    mixButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    masterButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    masterButton.setToggleState (true, juce::dontSendNotification);
    mixButton.setTooltip ("Sul mix bus mentre mixi: niente target di loudness, controllo del margine per il mastering, "
                          "consigli sulle singole tracce"_u);
    masterButton.setTooltip ("Sul master: target di loudness e picco, correzioni piccole sul bus stereo");
    mixButton.onClick = [this] { if (mixButton.getToggleState() && onMixPhaseChanged) onMixPhaseChanged (true); };
    masterButton.onClick = [this] { if (masterButton.getToggleState() && onMixPhaseChanged) onMixPhaseChanged (false); };

    for (auto* b : std::initializer_list<juce::Component*> { &loadRefButton, &clearRefButton, &listenButton, &saveProfileButton,
                                                              &optionsButton, &resetButton, &exportButton, &helpButton, &fullScreenButton })
        addAndMakeVisible (*b);

    loadRefButton.onClick = [this] { if (onLoadReference) onLoadReference(); };
    loadRefButton.setTooltip ("Carica un brano di riferimento (oppure trascinalo sulla finestra)");
    clearRefButton.onClick = [this] { if (onClearReference) onClearReference(); };
    clearRefButton.setTooltip ("Rimuovi il reference");
    saveProfileButton.onClick = [this] { if (onSaveProfile) onSaveProfile(); };
    saveProfileButton.setTooltip ("Salva l'analisi del reference come profilo target riutilizzabile");
    optionsButton.onClick = [this] { if (onOptions) onOptions(); };
    optionsButton.setTooltip ("Impostazioni di analisi, confronto, ascolto A/B e interfaccia (tema, colori)");
    resetButton.onClick = [this] { if (onReset) onReset(); };
    resetButton.setTooltip ("Azzera l'analisi");
    exportButton.onClick = [this] { if (onExport) onExport(); };
    exportButton.setTooltip ("Esporta il report (TXT, JSON) o uno screenshot (PNG)");

    listenButton.setClickingTogglesState (true);
    listenButton.onClick = [this] { if (onListenChanged) onListenChanged (listenButton.getToggleState()); };
    listenButton.setTooltip ("Ascolta il reference allineato in loudness al master. Disattivalo prima dell'export!");

    helpButton.setClickingTogglesState (true);
    helpButton.setTooltip ("Guida: attivala e lascia il mouse su un valore per un secondo per sapere cosa misura");
    helpButton.onClick = [this] { if (onHelpModeChanged) onHelpModeChanged (helpButton.getToggleState()); };

    fullScreenButton.setTooltip ("Schermo intero (Esc per uscire)");
    fullScreenButton.onClick = [this] { if (onToggleFullScreen) onToggleFullScreen(); };

    addAndMakeVisible (refStatus);
    refStatus.setFont (fonts::label (12.0f));
    refStatus.setMinimumHorizontalScale (0.7f);
    refStatus.setText ("Nessun reference", juce::dontSendNotification);

    applyColours();
}

void HeaderBar::applyColours()
{
    liveButton.setColour (juce::TextButton::buttonOnColourId, colours::liveOn);
    mixButton.setColour (juce::TextButton::buttonOnColourId, colours::mixOn);
    exportButton.setColour (juce::TextButton::buttonColourId, colours::accentDim);
    listenButton.setColour (juce::TextButton::buttonOnColourId, colours::listenOn);
    refStatus.setColour (juce::Label::textColourId, colours::textDim);
}

void HeaderBar::lookAndFeelChanged()
{
    applyColours();
    repaint();
}

void HeaderBar::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (colours::headerTop, 0.0f, 0.0f, colours::headerBottom, 0.0f, (float) getHeight(), false));
    g.fillRect (getLocalBounds());
    g.setColour (colours::line.withMultipliedAlpha (0.6f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (16, 0);

    // logo: anello con gradiente + scritta
    auto logo = r.removeFromLeft (22).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
    g.setGradientFill (juce::ColourGradient (colours::accent, logo.getX(), logo.getY(), colours::accent2, logo.getRight(), logo.getBottom(), false));
    g.drawEllipse (logo.reduced (1.5f), 3.0f);
    g.fillEllipse (logo.withSizeKeepingCentre (5.0f, 5.0f));

    r.removeFromLeft (8);
    g.setColour (colours::text);
    g.setFont (fonts::value (17.0f));
    g.drawText ("Master", r.removeFromLeft (62), juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.drawText ("Agent", r.removeFromLeft (60), juce::Justification::centredLeft);

    // separatori verticali tra i gruppi
    g.setColour (colours::line.withMultipliedAlpha (0.7f));
    for (auto* c : std::initializer_list<juce::Component*> { &wholeSongButton, &mixButton, &loadRefButton, &optionsButton, &helpButton })
        g.fillRect (c->getX() - groupGap / 2 - 1, 14, 1, getHeight() - 28);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromLeft (160);   // logo

    targetBox.setBounds (r.removeFromLeft (184));
    r.removeFromLeft (groupGap);
    wholeSongButton.setBounds (r.removeFromLeft (100));
    liveButton.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (groupGap);
    mixButton.setBounds (r.removeFromLeft (48));
    masterButton.setBounds (r.removeFromLeft (66));
    r.removeFromLeft (groupGap);

    // lato destro
    fullScreenButton.setBounds (r.removeFromRight (32));
    r.removeFromRight (4);
    helpButton.setBounds (r.removeFromRight (32));
    r.removeFromRight (groupGap);
    exportButton.setBounds (r.removeFromRight (80));
    r.removeFromRight (6);
    resetButton.setBounds (r.removeFromRight (62));
    r.removeFromRight (6);
    optionsButton.setBounds (r.removeFromRight (74));
    r.removeFromRight (groupGap);

    loadRefButton.setBounds (r.removeFromLeft (122));
    r.removeFromLeft (4);
    clearRefButton.setBounds (r.removeFromLeft (30));
    r.removeFromLeft (6);
    saveProfileButton.setBounds (r.removeFromRight (100));
    r.removeFromRight (6);
    listenButton.setBounds (r.removeFromRight (56));
    r.removeFromRight (8);
    refStatus.setBounds (r);
}

void HeaderBar::setTargets (const std::vector<std::pair<juce::String, juce::String>>& idAndName, const juce::String& selectedId)
{
    targetBox.clear (juce::dontSendNotification);
    targetIds.clear();

    int itemId = 1;
    for (const auto& [id, name] : idAndName)
    {
        targetBox.addItem (name, itemId++);
        targetIds.add (id);
    }

    const int index = targetIds.indexOf (selectedId);
    targetBox.setSelectedItemIndex (std::max (0, index), juce::dontSendNotification);
}

juce::String HeaderBar::getSelectedTargetId() const
{
    const int index = targetBox.getSelectedItemIndex();
    return juce::isPositiveAndBelow (index, targetIds.size()) ? targetIds[index] : juce::String();
}

void HeaderBar::setReferenceStatus (const juce::String& text, juce::Colour colour, bool referenceReady, bool hasReference)
{
    refStatus.setText (text, juce::dontSendNotification);
    refStatus.setColour (juce::Label::textColourId, colour);
    listenButton.setEnabled (referenceReady);
    saveProfileButton.setEnabled (referenceReady);
    clearRefButton.setEnabled (hasReference);
}

void HeaderBar::setListening (bool listening)
{
    listenButton.setToggleState (listening, juce::dontSendNotification);
}

void HeaderBar::setLiveMode (bool live)
{
    (live ? liveButton : wholeSongButton).setToggleState (true, juce::dontSendNotification);
}

void HeaderBar::setMixPhase (bool mix)
{
    (mix ? mixButton : masterButton).setToggleState (true, juce::dontSendNotification);
}

void HeaderBar::setModeEnabled (bool enabled)
{
    wholeSongButton.setEnabled (enabled);
    liveButton.setEnabled (enabled);
}

void HeaderBar::setHelpMode (bool on)
{
    helpButton.setToggleState (on, juce::dontSendNotification);
}

void HeaderBar::setFullScreen (bool on)
{
    fullScreenButton.setIcon (on ? IconButton::Icon::exitFullScreen : IconButton::Icon::enterFullScreen);
    fullScreenButton.setTooltip (on ? "Esci dallo schermo intero (Esc)" : "Schermo intero (Esc per uscire)");
}

} // namespace ma::ui
