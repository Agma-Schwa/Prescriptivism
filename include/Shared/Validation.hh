#ifndef PRESCRIPTIVISM_SHARED_VALIDATION_HH
#define PRESCRIPTIVISM_SHARED_VALIDATION_HH

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Utils.hh>

#include <cmath>
#include <span>

namespace pr::validation {
enum struct InitialWordValidationResult {
    Valid,
    NotAPermutation,
    ClusterTooLong,
    BadInitialClusterManner,
    BadInitialClusterCoordinates,
};
enum struct PlaySoundCardValidationResult {
    Valid,
    NeedsOtherCard,
    Invalid,
};

/// Concept that denotes anything that is a valid ‘word’ to be
/// used as an input for most validation functions.
template <typename T>
concept WordValidator = requires (const T& t, usz idx) {
    { t[idx] } -> std::convertible_to<CardId>;
    { t.is_own_word() } -> std::convertible_to<bool>;
    { t.stack_is_locked(idx) } -> std::convertible_to<bool>;
    { t.stack_is_full(idx) } -> std::convertible_to<bool>;
    { t.size() } -> std::convertible_to<usz>;
};

auto ValidateInitialWord(constants::Word word, constants::Word original) -> InitialWordValidationResult;

template <WordValidator T>
auto ValidatePlaySoundCard(CardId played, const T& on, usz at) -> PlaySoundCardValidationResult {
    using enum PlaySoundCardValidationResult;

    // If the stack is locked or full, we can’t play a card on it.
    // TODO: Handle the many ways of unlocking a card.
    if (on.stack_is_locked(at) or on.stack_is_full(at)) return Invalid;

    // Is this played on a /h/ or a /ə/ and the played sound is adjacent?
    if (
        (on[at] == CardId::C_h or on[at] == CardId::V_ə) and
        ((at > 0 and on[at - 1] == played) or (at < on.size() - 1 and on[at + 1] == played))
    ) return Valid;

    // Is this a special sound change? If so yes
    for (auto& c : CardDatabase[+on[at]].converts_to)
        if (played == c[0]) return c.size() > 1 ? NeedsOtherCard : Valid;

    // Is this an adjacent phoneme or a different phoneme with the same coordinates?
    auto d1 = std::abs(CardDatabase[+played].place_or_frontness - CardDatabase[+on[at]].place_or_frontness);
    auto d2 = std::abs(CardDatabase[+played].manner_or_height - CardDatabase[+on[at]].manner_or_height);
    if (
        played.is_consonant() == on[at].is_consonant() and
        d1 + d2 < 2 and
        played != on[at]
    ) return Valid;

    // Otherwise, no.
    return Invalid;
}

/// Returns whether the spelling reform is valid.
template <WordValidator T>
bool ValidateP_SpellingReform(const T& on, usz at) {
    // We can only play spelling reforms on our own word
    // on a stack that is not already locked.
    return on.is_own_word() and not on.stack_is_locked(at);
}
} // namespace pr::validation

#endif // PRESCRIPTIVISM_SHARED_VALIDATION_HH
