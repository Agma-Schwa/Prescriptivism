#ifndef PRESCRIPTIVISM_SHARED_PACKETS_HH
#define PRESCRIPTIVISM_SHARED_PACKETS_HH

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Serialisation.hh>
#include <Shared/TCP.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>

#include <cstring>
#include <utility>

#define COMMON_PACKETS(X) \
    X(Disconnect)         \
    X(WordChoice)

#define SC_PACKETS(X)    \
    SC_CONFIG_PACKETS(X) \
    SC_PLAY_PACKETS(X)

#define SC_CONFIG_PACKETS(X) \
    COMMON_PACKETS(X)        \
    X(HeartbeatRequest)      \
    X(StartGame)

#define SC_PLAY_PACKETS(X) \
    X(StartTurn)           \
    X(EndTurn)             \
    X(Draw)                \
    X(AddSoundToStack)     \
    X(StackLockChanged)    \
    X(WordChanged)         \
    X(DiscardAll)          \
    X(CardChoice)          \
    X(RemoveCard)          \
    X(PromptNegation)

#define CS_PACKETS(X)    \
    CS_CONFIG_PACKETS(X) \
    CS_PLAY_PACKETS(X)

#define CS_CONFIG_PACKETS(X) \
    COMMON_PACKETS(X)        \
    X(HeartbeatResponse)     \
    X(Login)

#define CS_PLAY_PACKETS(X) \
    X(PlaySingleTarget)    \
    X(PlayPlayerTarget)    \
    X(PlayNoTarget)        \
    X(Pass)                \
    X(CardChoiceReply)

namespace pr {
using PlayerId = u8;
}

// =============================================================================
//  Common Packet API
// =============================================================================
namespace pr::net::detail {
using IDType = u8;

template <IDType Id>
struct PacketBase {
    /// Id of the packet.
    ///
    /// Note that server packets and client packets have different IDs!
    IDType id = Id;
};
} // namespace pr::net::detail

#define DefinePacket(name) struct name : ::pr::net::detail::PacketBase<+ID::name>
#define Ctor(name)             \
private:                       \
    friend net::ReceiveBuffer; \
    name() = default;          \
public:                        \
    name

#define Serialisable(...) PR_SERIALISE(id __VA_OPT__(, ) __VA_ARGS__)

// =============================================================================
//  Enumerations
// =============================================================================
namespace pr::packets::common {
enum struct ID : net::detail::IDType {
#define X(name) name,
    COMMON_PACKETS(X)
#undef X
};

#define X(name) struct name;
COMMON_PACKETS(X)
#undef X
} // namespace pr::packets::common

namespace pr::packets::sc {
enum struct ID : std::underlying_type_t<common::ID> {
#define X(name) name,
    SC_PACKETS(X)
#undef X
};

#define X(name) using common::name;
COMMON_PACKETS(X)
#undef X
} // namespace pr::packets::sc

namespace pr::packets::cs {
enum struct ID : std::underlying_type_t<common::ID> {
#define X(name) name,
    CS_PACKETS(X)
#undef X
};

#define X(name) using common::name;
COMMON_PACKETS(X)
#undef X
} // namespace pr::packets::cs

// =============================================================================
//  Common Types
// =============================================================================
namespace pr::packets {
struct CardChoiceChallenge {
    PR_SERIALISE(title, cards, count, mode);
    enum struct Mode {
        Exact,   ///< Choose exactly `count` cards.
        AtMost,  ///< Choose at most (up to and including) `count` cards.
        AtLeast, ///< Choose at least `count` cards.
    };

    /// The title of the challenge.
    std::string title;

    /// The cards to choose from.
    std::vector<CardId> cards;

    /// The number of cards to choose.
    u32 count;

    /// The mode of the choice.
    Mode mode;
};
} // namespace pr::packets

// =============================================================================
//  Common Packets
// =============================================================================
namespace pr::packets::common {
/// Packet sent by either side to disconnect the other.
DefinePacket(Disconnect) {
    enum class Reason : u8 {
        Unspecified,      ///< Unknown.
        InvalidPacket,    ///< Peer sent invalid packet.
        ServerFull,       ///< Server already has the maximum number of players.
        UsernameInUse,    ///< Username is already in use.
        WrongPassword,    ///< The password was incorrect.
        UnexpectedPacket, ///< That packet wasn’t supposed to be sent at that point.
        PacketTooLarge,   ///< Packet was too large.
        BufferFull,       ///< Data limit exceeded.
    };

    Ctor(Disconnect)(Reason reason) : reason(reason) {}
    Serialisable(reason);

    Reason reason = Reason::Unspecified;
};

DefinePacket(WordChoice) {
    Ctor(WordChoice)(const auto& word) {
        Assert(word.size() == constants::StartingWordSize, "Invalid word size");
        rgs::copy(word, this->word.begin());
    }

    Serialisable(word);

    constants::Word word;
};
} // namespace pr::packets::common
// =============================================================================
//  Server -> Client
// =============================================================================
namespace pr::packets::sc {
/// Packet sent by the server to see if it’s still there. These
/// are mostly used when a client is idling because it’s not their
/// turn.
DefinePacket(HeartbeatRequest) {
    Ctor(HeartbeatRequest)(u32 seq_no) : seq_no(seq_no) {}
    Serialisable(seq_no);

    /// Sequence number of the heartbeat packet; used to see if the
    /// client is actually responding to the right packet.
    u32 seq_no;
};

DefinePacket(StartTurn) {
    Serialisable();
};

DefinePacket(EndTurn) {
    Serialisable();
};

DefinePacket(Draw) {
    Ctor(Draw)(CardId card) : card(card) {}
    Serialisable(card);

    CardId card;
};

DefinePacket(StartGame) {
    struct PlayerInfo {
        PR_SERIALISE(word, name);
        constants::Word word;
        std::string name;
    };

    Ctor(StartGame)(
        std::array<PlayerInfo, constants::PlayersPerGame> words,
        std::vector<CardId> hand,
        PlayerId player
    ) : player_data(words),
        hand(hand),
        player_id(player) {}

    Serialisable(player_data, hand, player_id);

    /// Player data, in order of player ID.
    std::array<PlayerInfo, constants::PlayersPerGame> player_data;

    /// This player’s starting hand.
    std::vector<CardId> hand;

    /// ID of the player that this is sent to.
    PlayerId player_id;
};

DefinePacket(AddSoundToStack) {
    Ctor(AddSoundToStack)(PlayerId player, u32 stack_index, CardId card)
        : player(player),
          stack_index(stack_index),
          card(card) {}

    Serialisable(player, stack_index, card);

    /// The player whose word we’re adding the sound to.
    PlayerId player;

    /// The index of the stack we’re adding the sound to.
    u32 stack_index;

    /// The card we’re adding.
    CardId card;
};

DefinePacket(StackLockChanged) {
    Ctor(StackLockChanged)(PlayerId player, u32 stack_index, bool locked)
        : player(player),
          stack_index(stack_index),
          locked(locked) {}

    Serialisable(player, stack_index, locked);

    /// The player whose stack we’re locking.
    PlayerId player;

    /// The index of the stack we’re locking.
    u32 stack_index;

    /// Whether the stack is locked or unlocked.
    bool locked;
};

DefinePacket(WordChanged) {
    Ctor(WordChanged)(PlayerId player, std::vector<std::vector<CardId>> new_word)
        : player(player),
          new_word(std::move(new_word)) {}

    Serialisable(player, new_word);

    /// The player whose word is being changed.
    PlayerId player;

    /// The new word.
    std::vector<std::vector<CardId>> new_word;
};

DefinePacket(DiscardAll) { Serialisable(); };

DefinePacket(CardChoice) {
    Ctor(CardChoice)(CardChoiceChallenge challenge)
        : challenge(std::move(challenge)) {}

    Serialisable(challenge);

    /// The challenge.
    CardChoiceChallenge challenge;
};

/// This is only used for cards that are removed through means other
/// than playing them (e.g. discarding at random, another player stealing
/// them, etc.). Normally, the client automatically discards cards after
/// they have been played.
DefinePacket(RemoveCard) {
    Ctor(RemoveCard)(u32 card_index) : card_index(card_index) {}
    Serialisable(card_index);

    /// The index of the card to remove.
    u32 card_index;
};

DefinePacket(PromptNegation) {
    Ctor(PromptNegation)(CardId card_id) : card_id(card_id) {}
    Serialisable(card_id);

    // The card to negate.
    CardId card_id;
};
} // namespace pr::packets::sc

// =============================================================================
//  Client -> Server
// =============================================================================
namespace pr::packets::cs {
DefinePacket(HeartbeatResponse) {
    Ctor(HeartbeatResponse)(u32 seq_no) : seq_no(seq_no) {}
    Serialisable(seq_no);

    /// The sequence number of the corresponding server packet.
    u32 seq_no;
};

DefinePacket(Login) {
    Ctor(Login)(std::string name, std::string password)
        : name(std::move(name)),
          password(std::move(password)) {}

    Serialisable(name, password);

    std::string name;
    std::string password;
};

DefinePacket(PlayPlayerTarget) {
    Ctor(PlayPlayerTarget)(u32 card_index, PlayerId player)
        : card_index(card_index),
          player(player) {}

    Serialisable(card_index, player);

    /// The index of the card in hand to play.
    u32 card_index;

    /// The player that this card is played on.
    PlayerId player;
};

DefinePacket(PlaySingleTarget) {
    Ctor(PlaySingleTarget)(u32 card_index, PlayerId player, u32 target_card_index)
        : card_index(card_index),
          player(player),
          target_stack_index(target_card_index) {}

    Serialisable(card_index, player, target_stack_index);

    /// The index of the card in hand to play.
    u32 card_index;

    /// The player whose word this card is played on.
    PlayerId player;

    /// The index of the card on the player’s word on which
    /// this one is played.
    u32 target_stack_index;
};

DefinePacket(PlayNoTarget) {
    Ctor(PlayNoTarget)(u32 card_index) : card_index(card_index) {}
    Serialisable(card_index);

    /// The index of the card in hand to play.
    u32 card_index;
};

DefinePacket(Pass) {
    Ctor(Pass)(u32 card_index) : card_index(card_index) {}
    Serialisable(card_index);

    /// The index of the card in hand that is discarded.
    u32 card_index;
};

DefinePacket(CardChoiceReply) {
    Ctor(CardChoiceReply)(std::vector<u32> card_indices)
        : card_indices(std::move(card_indices)) {}

    Serialisable(card_indices);

    /// The indices of the cards chosen.
    std::vector<u32> card_indices;
};

} // namespace pr::packets::cs

// =============================================================================
//  Packet Processing
// =============================================================================
namespace pr::packets {
static_assert(std::is_same_v<net::detail::IDType, u8>, "TODO: Handle larger packet IDs");

template <typename Packet, typename Handler, typename... Args>
auto Dispatch(Handler& h, net::ReceiveBuffer& buf, Args&&... args) -> Result<bool> {
    auto pack = buf.read<Packet>();
    if (not pack.has_value()) return false;
    h.handle(std::forward<Args>(args)..., std::move(pack.value()));
    return true;
}

/// Deserialise and handle a packet received from the server.
///
/// \return An error indicating that something went wrong (in which case
/// we should close the connexion) or a bool indicating whether the packet
/// is complete (if this is false, stop processing packets until we have
/// more data).
template <typename ConfigHandler, typename PlayHandler>
auto HandleClientSidePacket(ConfigHandler& ch, PlayHandler& ph, net::ReceiveBuffer& buf) -> Result<bool> {
    Assert(not buf.empty(), "No packet to handle?");
    switch (auto ty = buf.peek<sc::ID>().value()) {
        default: return Error("Server sent unrecognised packet: {}", +ty);
#define X(name) \
    case pr::packets::sc::ID::name: return Dispatch<pr::packets::sc::name>(ch, buf);
            SC_CONFIG_PACKETS(X)
#undef X
#define X(name) \
    case pr::packets::sc::ID::name: return Dispatch<pr::packets::sc::name>(ph, buf);
            SC_PLAY_PACKETS(X)
#undef X
    }
}

/// Deserialise and handle a packet received from a client.
///
/// \return An error indicating that something went wrong (in which case
/// we should close the connexion) or a bool indicating whether the packet
/// is complete (if this is false, stop processing packets until we have
/// more data).
template <typename Handler>
auto HandleServerSidePacket(Handler& h, net::TCPConnexion& client, net::ReceiveBuffer& buf) -> Result<bool> {
    Assert(not buf.empty(), "No packet to handle?");
    switch (auto ty = buf.peek<cs::ID>().value()) {
        default: return Error("Client sent unrecognised packet: {}", +ty);
#define X(name) \
    case pr::packets::cs::ID::name: return Dispatch<pr::packets::cs::name>(h, buf, client);
            CS_PACKETS(X)
#undef X
    }
}
} // namespace pr::packets

#undef Serialisable
#undef DefinePacket
#undef Ctor

#endif // PRESCRIPTIVISM_SHARED_PACKETS_HH
