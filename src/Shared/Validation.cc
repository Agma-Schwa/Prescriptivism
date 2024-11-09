module;
#include <algorithm>
#include <array>
#include <ranges>
module pr.validation;

bool pr::validation::ValidateInitialWord(Word word, Word original) {
    static auto IsConsonant = [](CardId id) -> bool {
        return CardDatabase[+id].is_consonant();
    };
    // The word is a permutation of the original word
    rgs::sort(original);
    Word w2 = word;
    rgs::sort(w2);
    if (w2 != original) return false;

    // No cluster or hiatus shall be longer than 2 sounds
    if (rgs::any_of( // clang-format off
        vws::chunk_by(word, [](auto a, auto b) {
            return IsConsonant(a) == IsConsonant(b);
        }),
        [](auto x) { return rgs::distance(x) > 2; })
    ) return false; // clang-format on

    // M1 and M2 CANNOT start a consonant cluster word-initially.
    if(IsConsonant(word[0]) and IsConsonant(word[1]) and CardDatabase[+word[0]].manner_or_height <= 2) return false;

    // Two consonants with the same coordinates CANNOT cluster word-initially.
    if (
        IsConsonant(word[0]) and IsConsonant(word[1]) and
        CardDatabase[+word[0]].manner_or_height == CardDatabase[+word[1]].manner_or_height and
        CardDatabase[+word[0]].place_or_frontness == CardDatabase[+word[1]].place_or_frontness
    ) return false;

    // Else all seems good
    return true;
}
