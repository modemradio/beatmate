#pragma once
#include <vector>
#include <juce_core/juce_core.h>

namespace BeatMate::UI {

// Regroupe les libellés de genre bruts (« disco888 », « Disco/Funk », « disco 444 »…)
inline juce::String genreFamilyOf(const juce::String& rawIn)
{
    const juce::String g = rawIn.trim().toLowerCase();
    if (g.isEmpty()) return {};

    struct Fam { const char* name; std::vector<const char*> keys; };
    static const std::vector<Fam> families = {
        { "Zouk",                { "zouk" } },
        { "Kompa / Compas",      { "kompa", "compas", "konpa" } },
        { "Kizomba",             { "kizomba", "semba" } },
        { "Reggae / Dancehall",  { "reggae", "reggea", "dancehall", "ragga", "ska", "dub" } },
        { "Afro",                { "afro", "coupe", "coup\xc3\xa9", "ndombolo", "makossa" } },
        { "Salsa / Latino",      { "salsa", "latin", "bachata", "merengue", "reggaeton", "cumbia", "kuduro" } },
        { "House",               { "house" } },
        { "Techno",              { "techno" } },
        { "Trance",              { "trance" } },
        { "Disco / Funk",        { "disco", "funk", "boogie" } },
        { "Soul / R&B",          { "soul", "motown", "r&b", "rnb", "r'n'b", "rhythm and blues" } },
        { "Hip-Hop / Rap",       { "hip", "rap", "trap", "drill" } },
        { "M\xc3\xa9tal",        { "metal", "m\xc3\xa9tal" } },
        { "Rock",                { "rock", "punk", "grunge" } },
        { "\xc3\x89lectro / Dance", { "electro", "\xc3\xa9lectro", "edm", "eurodance", "dancefloor", "dance", "club" } },
        { "Slow / Ballade",      { "slow", "ballad", "balade", "love" } },
        { "Vari\xc3\xa9t\xc3\xa9 fran\xc3\xa7\x61ise", { "vari\xc3\xa9t", "variet", "chanson", "fran\xc3\xa7", "francais" } },
        { "Pop",                 { "pop" } },
        { "Jazz",                { "jazz", "swing" } },
        { "Blues",               { "blues" } },
        { "Country",             { "country" } },
        { "Classique",           { "classique", "classical", "orchestr" } },
        { "Gospel",              { "gospel" } },
        { "Th\xc3\xa8me / B.O.", { "theme", "th\xc3\xa8me", "bande originale", "soundtrack", "g\xc3\xa9n\xc3\xa9rique", "generique" } },
    };

    for (const auto& f : families)
        for (const char* k : f.keys)
            if (g.contains(juce::String::fromUTF8(k)))
                return juce::String::fromUTF8(f.name);

    return rawIn.trim();
}

} // namespace BeatMate::UI
