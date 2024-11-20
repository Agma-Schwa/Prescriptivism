#ifndef PRESCRIPTIVISM_SHARED_CARDS_HH
#define PRESCRIPTIVISM_SHARED_CARDS_HH

#include <Shared/Serialisation.hh>
#include <Shared/Utils.hh>

#include <array>
#include <ranges>
#include <string_view>
#include <vector>

namespace pr {
enum struct CardIdValue : u16 {
    // Consonants
    // clang-format off

    $$ConsonantStart,
    C_p = $$ConsonantStart,
    C_b,
    C_t,
    C_d,
    C_tʃ,
    C_dʒ,
    C_k,
    C_g,
    C_f,
    C_v,
    C_s,
    C_z,
    C_ʃ,
    C_ʒ,
    C_h,
    C_w,
    C_r,
    C_j,
    C_ʟ,
    C_m,
    C_n,
    C_ɲ,
    C_ŋ,
    $$ConsonantEnd = C_ŋ,

    $$VowelStart,
    V_i = $$VowelStart,
    V_y,
    V_ɨ,
    V_u,
    V_ʊ,
    V_e,
    V_ɛ,
    V_ə,
    V_ɜ, // V_ɜ is referred to as ʌ in the docs, but also as a central vowel, which is wrong
    V_o,
    V_ɔ,
    V_æ,
    V_a,
    V_ɑ,
    $$VowelEnd = V_ɑ,


    $$PowersStart,
    P_Assimilation = $$PowersStart,
    P_Babel,
    P_Brasil,
    P_Campbell,
    P_Chomsky,
    P_Darija,
    P_Descriptivism,
    P_Dissimilation,
    P_Elision,
    P_Epenthesis,
    P_GVS,
    P_Grimm,
    P_Gvprtskvni,
    P_Heffer,
    P_LinguaFranca,
    P_Negation,
    P_Owl,
    P_Pinker,
    P_ProtoWorld,
    P_REA,
    P_Reconstruction,
    P_Regression,
    P_Revival,
    P_Rosetta,
    P_Schleicher,
    P_Schleyer,
    P_SpellingReform,
    P_Substratum,
    P_Superstratum,
    P_Urheimat,
    P_Vajda,
    P_Vernacular,
    P_Whorf,
    P_Zamnenhoff,
    $$PowersEnd = P_Zamnenhoff,
    // clang-format on

    $$Count
};

enum struct CardType : u8 {
    SoundCard,
    PowerCard,
};

/// This only exists because we can’t put members on an enum...
struct CardId {
    using enum CardIdValue;

    PR_SERIALISE(value);
    CardIdValue value = $$Count;

    constexpr CardId() = default;
    constexpr CardId(CardIdValue v) : value(v) {}

    /// Check if this is a consonant.
    [[nodiscard]] bool is_consonant() const {
        return $$ConsonantStart <= value and value <= $$ConsonantEnd;
    }

    /// Check if this is a vowel.
    [[nodiscard]] bool is_vowel() const {
        return $$VowelStart <= value and value <= $$VowelEnd;
    }

    /// Check if this is a power card.
    [[nodiscard]] bool is_power() const {
        return $$PowersStart <= value and value <= $$PowersEnd;
    }

    /// Check if this is a sound card.
    [[nodiscard]] bool is_sound() const {
        return is_consonant() or is_vowel();
    }

    /// Get the type of this card.
    [[nodiscard]] CardType type() const {
        return is_sound() ? CardType::SoundCard : CardType::PowerCard;
    }

    [[nodiscard]] u16 operator+() const { return +value; }
    [[nodiscard]] friend auto operator<=>(CardId, CardId) = default;
};

struct CardData {
    /// The ID of this card.
    CardId id;

    /// How many of this card there are in the deck at the start
    /// of the game.
    usz count_in_deck;

    /// The place/frontness and manner/height, if this is a sound card.
    i8 place_or_frontness{}, manner_or_height{};

    /// The name of this card.
    ///
    /// This may contain line breaks to aid in formatting the cards; if
    /// you want to get the card name w/o any line breaks, just replace
    /// them w/ spaces. ;Þ
    std::string_view name;

    /// The text to display at the center of the card; this is only
    /// relevant for sound cards.
    std::string_view center;

    /// Set of special changes for this sound card.
    std::vector<std::vector<CardId>> converts_to;

    /// Create a sound card.
    static constexpr auto Sound(
        CardId id,
        usz count,
        i8 place,
        i8 manner,
        std::string_view name,
        std::string_view center,
        std::vector<std::vector<CardId>> converts_to = {}
    ) -> CardData {
        return {id, count, place, manner, name, center, std::move(converts_to)};
    }

    /// Create a power card.
    static constexpr auto Power(
        CardId id,
        usz count,
        std::string_view name
    ) {
        return CardData{id, count, 0, 0, name, "", {}};
    }
};
} // namespace pr

namespace pr::impl {
using enum CardIdValue;
using enum CardType;

#define Sound(x, ...) [+x] = CardData::Sound({x}, __VA_ARGS__)
#define Power(x, ...) [+x] = CardData::Power({x}, __VA_ARGS__)

const CardData CardDatabaseImpl[+$$Count]{
    // clang-format off
    // CONSONANTS - M4     Count  P  M  Name                                        Center   Conversions
    Sound(C_p,             4,     4, 4, "Voiceless\nbilabial\nstop",                "p",     {{C_m}}),
    Sound(C_b,             2,     4, 4, "Voiced\nbilabial\nstop",                   "b",     {{C_m}}),
    Sound(C_t,             4,     3, 4, "Voiceless\nalveolar\nstop",                "t",     {{C_n}}),
    Sound(C_d,             2,     3, 4, "Voiced\nalveolar\nstop",                   "d",     {{C_n}}),
    Sound(C_tʃ,            4,     2, 4, "Voiceless\npost-alveolar\naffricate",      "tʃ",    {{C_ɲ}}),
    Sound(C_dʒ,            2,     2, 4, "Voiced\npost-alveolar\naffricate",         "dʒ",    {{C_ɲ}}),
    Sound(C_k,             4,     1, 4, "Voiceless\nvelar\nstop",                   "k",     {{C_ŋ}}),
    Sound(C_g,             2,     1, 4, "Voiced\nvelar\nstop",                      "g",     {{C_ŋ}}),

    // CONSONANTS - M3      Count  P  M  Name                                       Center   Conversions
    Sound(C_f,              4,     4, 3, "Voiceless\nlabial\nfricative",            "f",     {{C_h}}),
    Sound(C_v,              2,     4, 3, "Voiced\nlabial\nfricative",               "v",     {}),
    Sound(C_s,              4,     3, 3, "Voiceless\nalveolar\nfricative",          "s",     {}),
    Sound(C_z,              2,     3, 3, "Voiced\nalveolar\nfricative",             "z",     {}),
    Sound(C_ʃ,              4,     2, 3, "Voiceless\npost-alveolar\nfricative",     "ʃ",     {}),
    Sound(C_ʒ,              2,     2, 3, "Voiced\npost-alveolar\nfricative",        "ʒ",     {}),
    Sound(C_h,              2,     1, 3, "Voiceless\nglottal\nfricative",           "ʒ",     {{C_f}}),

    // CONSONANTS - M2      Count  P  M  Name                                       Center   Conversions
    Sound(C_w,              4,     4, 2, "Voiced\nlabio-velar\napproximant",        "w",     {{C_ʟ}, {V_u, V_u}}),
    Sound(C_r,              4,     3, 2, "Voiced\nalveolar\ntrill",                 "r",     {}),
    Sound(C_j,              4,     2, 2, "Voiced\npalatal\napproximant",            "j",     {{V_i, V_i}}),
    Sound(C_ʟ,              4,     1, 2, "Voiced\nvelar\napproximant",              "ʟ",     {{C_w}}),

    // CONSONANTS - M1      Count  P  M  Name                                       Center   Conversions
    Sound(C_m,              4,     4, 1, "Voiced\nbilabial\nnasal",                 "m",     {{C_p}}),
    Sound(C_n,              4,     3, 1, "Voiced\nalveolar\nnasal",                 "n",     {{C_t}}),
    Sound(C_ɲ,              4,     2, 1, "Voiced\npalatal\nnasal",                  "ɲ",     {{C_tʃ}}),
    Sound(C_ŋ,              4,     1, 1, "Voiced\nvelar\nnasal",                    "ŋ",     {{C_k}}),

    // VOWELS - O3          Count O  A  Name                                        Center Conversions
    Sound(V_i,              7,    3, 3, "Close\nFront\nUnrounded\nVowel",           "i",   {{C_j, C_j}}),
    Sound(V_y,              3,    3, 3, "Close\nFront\nRounded\nVowel",             "y",   {}),
    Sound(V_ɨ,              5,    3, 2, "Close\nCentral\nUnrounded\nVowel",         "ɨ",   {}),
    Sound(V_u,              7,    3, 1, "Close\nBack\nRounded\nVowel",              "u",   {{C_w, C_w}}),
    Sound(V_ʊ,              3,    3, 1, "Near-Close\nNear-Back\nRounded\nVowel",    "ʊ",   {}),

    // VOWELS - O2          Count O  A  Name                                        Center Conversions
    Sound(V_e,              7,    2, 3, "Close-Mid\nFront\nUnrounded\nVowel",       "e",   {}),
    Sound(V_ɛ,              3,    2, 3, "Open-Mid\nFront\nUnrounded\nVowel",        "ɛ",   {}),
    Sound(V_ə,              7,    2, 2, "Mid\nCentral\nVowel",                      "ə",   {}),
    Sound(V_ɜ,              3,    2, 2, "Open-Mid\nCentral\nUnrounded\nVowel",      "ɜ",   {}),
    Sound(V_o,              7,    2, 1, "Close-Mid\nBack\nRounded\nVowel",          "o",   {}),
    Sound(V_ɔ,              7,    2, 1, "Open-Mid\nBack\nRounded\nVowel",           "ɔ",   {}),

    // VOWELS - O1          Count O  A  Name                                        Center Conversions
    Sound(V_æ,              5,     1, 3, "Near-Open\nNear-Front\nUnrounded\nVowel", "æ",  {}),
    Sound(V_a,              7,     1, 2, "Open\nCentral\nUnrounded\nVowel",         "a",  {}),
    Sound(V_ɑ,              5,     1, 1, "Open\nBack\nUnrounded\nVowel",            "ɑ",  {}),

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

/*// Integrity check.
static_assert([] {
    for (auto [i, c] : CardDatabase | vws::enumerate) {
        // Every card must have a type equal to its index.
        if (CardType(i) != c.type) return false;

        // Every card must occur at least once in the deck.
        if (c.count_in_deck < 1) return false;

        // Sound cards must have place and manner set.
        if (c.type == SoundCard and (c.place == 0 or c.manner == 0)) return false;

        // Card name may not be empty (but other fields can be).
        if (c.name.empty()) return false;
    }
    return true;
}(), "Card database integrity check");*/
} // namespace pr::impl

namespace pr {
constexpr std::span<const CardData> CardDatabase = impl::CardDatabaseImpl;
constexpr std::span<const CardData> CardDatabaseVowels = CardDatabase.subspan(+CardIdValue::$$VowelStart, +CardIdValue::$$VowelEnd - +CardIdValue::$$VowelStart + 1);
constexpr std::span<const CardData> CardDatabaseConsonants = CardDatabase.subspan(+CardIdValue::$$ConsonantStart, +CardIdValue::$$ConsonantEnd - +CardIdValue::$$ConsonantStart + 1);
constexpr std::span<const CardData> CardDatabasePowers = CardDatabase.subspan(+CardIdValue::$$PowersStart, +CardIdValue::$$PowersEnd - +CardIdValue::$$PowersStart + 1);
}

#endif // PRESCRIPTIVISM_SHARED_CARDS_HH