#pragma once

#include <juce_core/juce_core.h>

namespace ma::ui
{

/**
    Spiegazioni della guida (tasto "?" nella barra in alto): cosa misura ogni elemento della dashboard.
    La chiave è quella delle zone registrate dai pannelli (Panel::addHelp / markHighlight), comprese
    le chiavi di highlight:: con indice ("band:3", "crest:0", ...). Formato: "Titolo\nspiegazione".
    Restituisce una stringa vuota per le chiavi sconosciute.
*/
juce::String helpTextFor (const juce::String& key);

} // namespace ma::ui
