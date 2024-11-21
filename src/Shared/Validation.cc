#include <Shared/Validation.hh>

#include <algorithm>
#include <array>
#include <ranges>

using namespace pr;

bool validation::AlwaysPlayable(CardId card) {
    switch (card.value) {
        default: return false;
        case CardIdValue::P_Babel:
        case CardIdValue::P_Whorf:
            return true;
    }
}

auto validation::ValidateInitialWord(constants::Word word, constants::Word original)
    -> InitialWordValidationResult {
    using enum InitialWordValidationResult;

    // The word is a permutation of the original word
    rgs::sort(original);
    constants::Word w2 = word;
    rgs::sort(w2);
    if (w2 != original) return NotAPermutation;

    // No cluster or hiatus shall be longer than 2 sounds
    if (rgs::any_of( // clang-format off
        vws::chunk_by(word, [](auto a, auto b) { return a.is_consonant() == b.is_consonant(); }),
        [](auto x) { return rgs::distance(x) > 2; }
    )) return ClusterTooLong; // clang-format on

    // M1 and M2 CANNOT start a consonant cluster word-initially.
    if (word[0].is_consonant() and word[1].is_consonant() and CardDatabase[+word[0]].manner_or_height <= 2)
        return BadInitialClusterManner;

    // Two consonants with the same coordinates CANNOT cluster word-initially.
    if (
        word[0].is_consonant() and word[1].is_consonant() and
        CardDatabase[+word[0]].manner_or_height == CardDatabase[+word[1]].manner_or_height and
        CardDatabase[+word[0]].place_or_frontness == CardDatabase[+word[1]].place_or_frontness
    ) return BadInitialClusterCoordinates;

    // Else all seems good
    return Valid;
}
