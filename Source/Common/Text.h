#pragma once

#include <juce_core/juce_core.h>

/** Letterale UTF-8 -> juce::String ("perché"_u). juce::String (const char*) interpreta i byte come ASCII/Latin-1. */
inline juce::String operator""_u (const char* text, size_t length)
{
    return juce::String::fromUTF8 (text, (int) length);
}

/**
    Testo visibile all'utente, tradotto nella lingua dell'interfaccia (Resources/lang/*.txt).
    Il testo sorgente è l'italiano: senza traduzioni caricate (test, strumenti) resta invariato.
    Al posto di TRANS() di JUCE, che leggerebbe i byte UTF-8 come Latin-1.
*/
inline juce::String tr (const juce::String& text)
{
    return juce::translate (text);
}

inline juce::String tr (const char* utf8)
{
    return juce::translate (juce::String::fromUTF8 (utf8));
}

/** "Brano intero"_t == tr ("Brano intero") */
inline juce::String operator""_t (const char* text, size_t length)
{
    return juce::translate (juce::String::fromUTF8 (text, (int) length));
}
