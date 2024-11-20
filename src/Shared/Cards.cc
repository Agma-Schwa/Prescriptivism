#include <Shared/Cards.hh>

using namespace pr;

auto CardData::Sound(
    CardId id,
    usz count,
    i8 place,
    i8 manner,
    std::string_view name,
    std::string_view center,
    std::vector<std::vector<CardId>> converts_to
) -> CardData {
    return {id, count, place, manner, name, center, std::move(converts_to)};
}

auto CardData::Power(
    CardId id,
    usz count,
    std::string_view name
) {
    return CardData{id, count, 0, 0, name, "", {}};
}

namespace pr::impl {
using enum CardIdValue;
using enum CardType;
}

#define Sound(x, ...) [+x] = CardData::Sound({x}, __VA_ARGS__)
#define Power(x, ...) [+x] = CardData::Power({x}, __VA_ARGS__)

const CardData impl::CardDatabaseImpl[+$$Count]{
    // clang-format off
    // CONSONANTS - M4     Count  P  M  Name                                        Center   Conversions
    Sound(C_p,             4,     4, 4, "Voiceless bilabial stop",                "p",     {{C_m}}),
    Sound(C_b,             2,     4, 4, "Voiced bilabial stop",                   "b",     {{C_m}}),
    Sound(C_t,             4,     3, 4, "Voiceless alveolar stop",                "t",     {{C_n}}),
    Sound(C_d,             2,     3, 4, "Voiced alveolar stop",                   "d",     {{C_n}}),
    Sound(C_tʃ,            4,     2, 4, "Voiceless post-alveolar affricate",      "tʃ",    {{C_ɲ}}),
    Sound(C_dʒ,            2,     2, 4, "Voiced post-alveolar affricate",         "dʒ",    {{C_ɲ}}),
    Sound(C_k,             4,     1, 4, "Voiceless velar stop",                   "k",     {{C_ŋ}}),
    Sound(C_g,             2,     1, 4, "Voiced velar stop",                      "g",     {{C_ŋ}}),

    // CONSONANTS - M3      Count  P  M  Name                                       Center   Conversions
    Sound(C_f,              4,     4, 3, "Voiceless labial fricative",            "f",     {{C_h}}),
    Sound(C_v,              2,     4, 3, "Voiced labial fricative",               "v",     {}),
    Sound(C_s,              4,     3, 3, "Voiceless alveolar fricative",          "s",     {}),
    Sound(C_z,              2,     3, 3, "Voiced alveolar fricative",             "z",     {}),
    Sound(C_ʃ,              4,     2, 3, "Voiceless post-alveolar fricative",     "ʃ",     {}),
    Sound(C_ʒ,              2,     2, 3, "Voiced post-alveolar fricative",        "ʒ",     {}),
    Sound(C_h,              2,     1, 3, "Voiceless glottal fricative",           "ʒ",     {{C_f}}),

    // CONSONANTS - M2      Count  P  M  Name                                       Center   Conversions
    Sound(C_w,              4,     4, 2, "Voiced labio-velar approximant",        "w",     {{C_ʟ}, {V_u, V_u}}),
    Sound(C_r,              4,     3, 2, "Voiced alveolar trill",                 "r",     {}),
    Sound(C_j,              4,     2, 2, "Voiced palatal approximant",            "j",     {{V_i, V_i}}),
    Sound(C_ʟ,              4,     1, 2, "Voiced velar approximant",              "ʟ",     {{C_w}}),

    // CONSONANTS - M1      Count  P  M  Name                                       Center   Conversions
    Sound(C_m,              4,     4, 1, "Voiced bilabial nasal",                 "m",     {{C_p}}),
    Sound(C_n,              4,     3, 1, "Voiced alveolar nasal",                 "n",     {{C_t}}),
    Sound(C_ɲ,              4,     2, 1, "Voiced palatal nasal",                  "ɲ",     {{C_tʃ}}),
    Sound(C_ŋ,              4,     1, 1, "Voiced velar nasal",                    "ŋ",     {{C_k}}),

    // VOWELS - O3          Count O  A  Name                                        Center Conversions
    Sound(V_i,              7,    3, 3, "Close Front Unrounded Vowel",           "i",   {{C_j, C_j}}),
    Sound(V_y,              3,    3, 3, "Close Front Rounded Vowel",             "y",   {}),
    Sound(V_ɨ,              5,    3, 2, "Close Central Unrounded Vowel",         "ɨ",   {}),
    Sound(V_u,              7,    3, 1, "Close Back Rounded Vowel",              "u",   {{C_w, C_w}}),
    Sound(V_ʊ,              3,    3, 1, "Near-Close Near-Back Rounded Vowel",    "ʊ",   {}),

    // VOWELS - O2          Count O  A  Name                                        Center Conversions
    Sound(V_e,              7,    2, 3, "Close-Mid Front Unrounded Vowel",       "e",   {}),
    Sound(V_ɛ,              3,    2, 3, "Open-Mid Front Unrounded Vowel",        "ɛ",   {}),
    Sound(V_ə,              7,    2, 2, "Mid Central Vowel",                      "ə",   {}),
    Sound(V_ɜ,              3,    2, 2, "Open-Mid Central Unrounded Vowel",      "ɜ",   {}),
    Sound(V_o,              7,    2, 1, "Close-Mid Back Rounded Vowel",          "o",   {}),
    Sound(V_ɔ,              7,    2, 1, "Open-Mid Back Rounded Vowel",           "ɔ",   {}),

    // VOWELS - O1          Count O  A  Name                                        Center Conversions
    Sound(V_æ,              5,     1, 3, "Near-Open Near-Front Unrounded Vowel", "æ",  {}),
    Sound(V_a,              7,     1, 2, "Open Central Unrounded Vowel",         "a",  {}),
    Sound(V_ɑ,              5,     1, 1, "Open Back Unrounded Vowel",            "ɑ",  {}),

    // POWER CARDS          Count  Name
    Power(P_Assimilation,   1,     "Assimilation"),
    Power(P_Babel,          1,     "Tower of Babel"),
    Power(P_Brasil,         1,     "Go to Brasil"),
    Power(P_Campbell,       1,     "Campbell’s Lie"),
    Power(P_Chomsky,        1,     "A Kiss from Noam Chomsky"),
    Power(P_Darija,         1,     "Darija Damage"),
    Power(P_Descriptivism,  4,     "Descriptivism"),
    Power(P_Dissimilation,  1,     "Dissimilation"),
    Power(P_Elision,        5,     "Elision"),
    Power(P_Epenthesis,     3,     "Epenthesis"),
    Power(P_GVS,            1,     "Great Vowel Shift"),
    Power(P_Grimm,          1,     "The Grimm Reaper"),
    Power(P_Gvprtskvni,     1,     "Gvprtskvni"),
    Power(P_Heffer,         1,     "Heffer’s Last Stand"),
    Power(P_LinguaFranca,   3,     "Lingua Franca"),
    Power(P_Negation,       3,     "Negation"),
    Power(P_Owl,            1,     "An Offering to the Owl"),
    Power(P_Pinker,         1,     "Pinker’s Construct"),
    Power(P_ProtoWorld,     1,     "Proto-World"),
    Power(P_REA,            1,     "Real Academia Española"),
    Power(P_Reconstruction, 1,     "Unattested Reconstruction"),
    Power(P_Regression,     1,     "Regression"),
    Power(P_Revival,        1,     "Revival"),
    Power(P_Rosetta,        1,     "Rosetta Stone"),
    Power(P_Schleicher,     1,     "Schleicher’s Shears"),
    Power(P_Schleyer,       1,     "Schleyer’s Folly"),
    Power(P_SpellingReform, 10,    "Spelling Reform"),
    Power(P_Substratum,     1,     "Substratum"),
    Power(P_Superstratum,   1,     "Superstratum"),
    Power(P_Urheimat,       1,     "Urheimat"),
    Power(P_Vajda,          1,     "Vajda’s Vow"),
    Power(P_Vernacular,     1,     "Victory of the Vernacular"),
    Power(P_Whorf,          1,     "Whorf’s Fever Dream"),
    Power(P_Zamnenhoff,     1,     "ZAMN-enhoff"),
}; // clang-format on
