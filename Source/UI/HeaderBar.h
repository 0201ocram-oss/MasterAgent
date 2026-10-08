#pragma once

#include "Theme.h"

namespace ma::ui
{

/** Pulsante quadrato con icona disegnata (guida "?", schermo intero). */
class IconButton : public juce::Button
{
public:
    enum class Icon { help, enterFullScreen, exitFullScreen };

    IconButton (const juce::String& name, Icon icon) : juce::Button (name), icon (icon) {}

    void setIcon (Icon newIcon) { icon = newIcon; repaint(); }

protected:
    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    Icon icon;
};

/** Barra superiore: target, modalità (brano intero / live), reference, A/B, opzioni, reset, export, guida, schermo intero. */
class HeaderBar : public juce::Component
{
public:
    HeaderBar();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** Popola il selettore (id dei profili + voce reference). */
    void setTargets (const std::vector<std::pair<juce::String, juce::String>>& idAndName, const juce::String& selectedId);
    juce::String getSelectedTargetId() const;

    void setReferenceStatus (const juce::String& text, juce::Colour colour, bool referenceReady, bool hasReference);
    void setListening (bool listening);
    void setLiveMode (bool live);
    void setMixPhase (bool mix);
    /** Brano intero / Live hanno senso solo per l'ingresso live (non per un file analizzato). */
    void setModeEnabled (bool enabled);
    void setHelpMode (bool on);
    void setFullScreen (bool on);

    std::function<void (const juce::String&)> onTargetChanged;
    std::function<void()> onLoadReference, onClearReference, onSaveProfile, onReset, onExport, onOptions, onToggleFullScreen;
    std::function<void (bool)> onListenChanged, onLiveModeChanged, onMixPhaseChanged, onHelpModeChanged;

    juce::Component& getOptionsButton() noexcept { return optionsButton; }

private:
    void applyColours();

    static constexpr int groupGap = 14;   // spazio tra i gruppi di controlli (con il separatore al centro)

    juce::ComboBox targetBox;
    juce::StringArray targetIds;

    juce::TextButton wholeSongButton { "Brano intero" };
    juce::TextButton liveButton { "Live" };

    juce::TextButton mixButton { "Mix" };
    juce::TextButton masterButton { "Master" };

    juce::TextButton loadRefButton { "Carica reference" };
    juce::TextButton clearRefButton { "X" };
    juce::TextButton listenButton { "A/B" };
    juce::TextButton saveProfileButton { "Salva profilo" };
    juce::TextButton optionsButton { "Opzioni" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton exportButton { "Esporta" };
    IconButton helpButton { "Guida", IconButton::Icon::help };
    IconButton fullScreenButton { "Schermo intero", IconButton::Icon::enterFullScreen };
    juce::Label refStatus;
};

} // namespace ma::ui
