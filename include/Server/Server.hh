#ifndef PRESCRIPTIVISM_SERVER_SERVER_HH
#define PRESCRIPTIVISM_SERVER_SERVER_HH

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Packets.hh>
#include <Shared/TCP.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>
#include <base/Properties.hh>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <random>
#include <ranges>
#include <source_location>
#include <vector>

namespace pr::server {
class Card;
class Player;
class Server;
class Stack;
class Word;

using DisconnectReason = packets::sc::Disconnect::Reason;
} // namespace pr::server

class pr::server::Card {
    LIBBASE_MOVE_ONLY(Card);
    ComputedReadonly(const CardData&, data, CardDatabase[+id]);

public:
    CardId id;
    explicit Card(CardId t) : id(t) {}
};

class pr::server::Stack {
    // FIXME: This doesn’t handle empty stacks, which will be a problem
    // as soon as we introduce deleting entire stacks.
    ComputedReadonly(CardId, top, cards.back().id);
    ComputedReadonly(bool, full, cards.size() == constants::MaxSoundStackSize);

public:
    std::vector<Card> cards;

    /// Stack is locked by a spelling reform.
    bool locked = false;

    /// Get the nth card id in the stack.
    auto operator[](usz n) -> CardId {
        Assert(n < cards.size());
        return cards[n].id;
    }

    /// Add a card to the stack.
    void push(Card card) { cards.push_back(std::move(card)); }
};

class pr::server::Word {
public:
    std::vector<Stack> stacks;

    /// Add a stack to the word.
    void add_stack(Card c) { stacks.emplace_back().push(std::move(c)); }

    /// Get the card ids of the topmost card in each stack.
    auto ids() const { return stacks | vws::transform(&Stack::get_top); }
};

namespace pr::server::challenge {
/// Currently, this is only used for Substratum.
struct CardChoice {
    PlayerId target_player; // Player whose hand to take the cards from.
    packets::CardChoiceChallenge data;
};

/// Used to prompt a player to negate an effect.
struct NegatePowerCard {
    CardId id;
};

using Challenge = Variant< // clang-format off
    CardChoice,
    NegatePowerCard
>; // clang-format on
} // namespace pr::server::challenge

class pr::server::Player {
    LIBBASE_IMMOVABLE(Player);

    /// The connexion for this player, if there is an established
    /// one. This may be unset if the player has (temporarily) left
    /// the game.
    Property(net::TCPConnexion, connexion);

    /// Whether the player is currently connected.
    ComputedReadonly(bool, connected, not connexion.disconnected);
    ComputedReadonly(bool, disconnected, not connected);

    /// The currently pending challenges for the player, if any.
    ///
    /// A 'challenge' is a message sent to a client for which the
    /// server expects a reply; no other action may be taken by the
    /// client (except disconnection and logging back in) until the
    /// challenge is resolved. Attempts to do something else will
    /// result in the client being disconnected.
    ///
    /// Challenges are processed one-by-one, i.e. after one has been
    /// resolved, the next one is sent and so on.
    Queue<challenge::Challenge> challenges;

public:
    /// The player's name.
    std::string name;

    /// The player’s hand
    std::vector<Card> hand;

    /// The player’s word
    Word word;

    /// The player submitted their word
    bool submitted_word = false;

    /// The id of this player.
    u8 id{};

    /// Create a new player.
    Player(net::TCPConnexion conn, std::string name)
        : name(std::move(name)) {
        connexion = std::move(conn);
    }

    /// Add a challenge to the player.
    void add_challenge(challenge::Challenge c);

    /// Remove the currently active challenge.
    void clear_active_challenge();

    /// Check if this player has a pending challenge.
    auto has_active_challenge() -> bool { return not challenges.empty(); }

    /// Get the current challenge, provided it is of the given type.
    template <typename T>
    auto get_active_challenge() -> T* {
        if (challenges.empty()) return nullptr;
        return challenges.front().get_if<T>();
    }

    /// Send a packet to the player.
    template <typename T>
    void send(const T& t) { _connexion.send(t); }

    /// Send the active challenge, if any.
    void send_active_challenge();
};

/// Prescriptivism server instance.
class pr::server::Server : net::TCPServerCallbacks {
    LIBBASE_IMMOVABLE(Server);

    struct Validator;
    friend Validator;

    struct PendingConnexion {
        net::TCPConnexion conn;
        chr::steady_clock::time_point established;
    };

    struct CanPlayCardResult {
        Player* p{};
        Card* card{};
        Player* target{};
        explicit operator bool() { return p != nullptr; }
    };

    enum struct State {
        // We are waiting for enough players to join for the first time.
        WaitingForPlayerRegistration,

        // We are waiting for players to submit their words.
        WaitingForWords,

        // The game is running.
        Running,
    };

    /// TCP server that manages client connexions.
    net::TCPServer server;

    /// The list of players. Once a player has been added, they are
    /// typically never removed.
    StableVector<Player> players;

    /// The current player
    PlayerId current_player = 0;

    /// The random number generator.
    std::mt19937 rng{std::random_device{}()};

    /// List of connexions that have not yet been assigned a player.
    std::vector<PendingConnexion> pending_connexions;

    /// The password of the server.
    std::string password;

    /// Deck and discard pile.
    std::vector<Card> deck;
    std::vector<Card> discard{};

    State state = State::WaitingForPlayerRegistration;

public:
    /// Create and start the server.
    Server(u16 port, std::string password);

    /// Disconnect a client.
    void Kick(
        net::TCPConnexion& client,
        DisconnectReason reason,
        std::source_location sloc = std::source_location::current()
    );

    /// Run the server for ever.
    [[noreturn]] void Run();

#define X(name) void handle(net::TCPConnexion& client, packets::cs::name);
    CS_PACKETS(X)
#undef X

private:
    bool AllPlayersConnected() {
        return rgs::all_of(players, &Player::get_connected);
    }

    bool AllWordsSubmitted() {
        return rgs::all_of(players, &Player::submitted_word);
    }

    template <typename T>
    void Broadcast(const T& packet) {
        for (auto& p : players) p.send(packet);
    }

    /// Perform basic checks to see if a play is valid:
    ///
    /// - Is it the player’s turn?
    /// - Is the card index valid?
    /// - Is the target player index valid?
    /// - Does the player have a pending challenge?
    auto CanPlayCard(
        net::TCPConnexion& client,
        std::optional<u32> card_index = std::nullopt,
        std::optional<PlayerId> target_player = std::nullopt
    ) -> CanPlayCardResult;

    void DoP_Babel(Player& p);

    void Draw(Player& p, usz count = 1);

    void HandlePlaySoundCard(
        net::TCPConnexion& client,
        Card& card,
        Player& target_player,
        u32 target_index
    );

    void NextPlayer();

    /// Remove a card a player’s hand.
    void RemoveCard(Player& p, Card& c, bool to_discard_pile = true, bool notify = true);

    bool PromptNegation(Player& p, CardId power_card);
    void SendGameState(Player& p);
    void SetUpGame();
    void Tick();
    auto ValidatorFor(Player& p) -> Validator;

    auto player() -> Player& { return players[current_player]; }

    bool accept(net::TCPConnexion& connexion) override;
    void receive(net::TCPConnexion& client, net::ReceiveBuffer& buffer) override;
};

#endif // PRESCRIPTIVISM_SERVER_SERVER_HH
