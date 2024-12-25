#ifndef PRESCRIPTIVISM_SHARED_CARDS_HH
#define PRESCRIPTIVISM_SHARED_CARDS_HH

#include <Shared/Utils.hh>

#include <base/Serialisation.hh>

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

    LIBBASE_SERIALISE(value);
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
    static auto Sound(
        CardId id,
        usz count,
        i8 place,
        i8 manner,
        std::string_view name,
        std::string_view center,
        std::vector<std::vector<CardId>> converts_to = {}
    ) -> CardData;

    /// Create a power card.
    static auto Power(CardId id, usz count, std::string_view name);
};
} // namespace pr

namespace pr::impl {
extern const CardData CardDatabaseImpl[+CardIdValue::$$Count];
} // namespace pr::impl

namespace pr {
constexpr std::span<const CardData> CardDatabase = impl::CardDatabaseImpl;
constexpr std::span<const CardData> CardDatabaseVowels = CardDatabase.subspan(+CardIdValue::$$VowelStart, +CardIdValue::$$VowelEnd - +CardIdValue::$$VowelStart + 1);
constexpr std::span<const CardData> CardDatabaseConsonants = CardDatabase.subspan(+CardIdValue::$$ConsonantStart, +CardIdValue::$$ConsonantEnd - +CardIdValue::$$ConsonantStart + 1);
constexpr std::span<const CardData> CardDatabasePowers = CardDatabase.subspan(+CardIdValue::$$PowersStart, +CardIdValue::$$PowersEnd - +CardIdValue::$$PowersStart + 1);
}

#endif // PRESCRIPTIVISM_SHARED_CARDS_HH
