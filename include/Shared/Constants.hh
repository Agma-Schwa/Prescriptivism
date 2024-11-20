#ifndef PRESCRIPTIVISM_SHARED_CONSTANTS_HH
#define PRESCRIPTIVISM_SHARED_CONSTANTS_HH

#include <Shared/Cards.hh>

#include <base/Base.hh>

#include <array>

namespace pr::constants {
constexpr usz StartingWordSize = 6;
constexpr usz PlayersPerGame = 2;
constexpr usz MaxSoundStackSize = 7;
using Word = std::array<CardId, StartingWordSize>;
}

#endif // PRESCRIPTIVISM_SHARED_CONSTANTS_HH
