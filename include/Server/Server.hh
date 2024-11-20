#ifndef PRESCRIPTIVISM_SERVER_SERVER_HH
#define PRESCRIPTIVISM_SERVER_SERVER_HH

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Packets.hh>
#include <Shared/TCP.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>

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

class pr::server::Player {
    LIBBASE_IMMOVABLE(Player);

    ComputedReadonly(bool, connected, not client_connexion.disconnected);
    ComputedReadonly(bool, disconnected, not connected);

public:
    /// The connexion for this player, if there is an established
    /// one. This may be unset if the player has (temporarily) left
    /// the game.
    net::TCPConnexion client_connexion;

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
    Player(net::TCPConnexion client_connexion, std::string name)
        : client_connexion(std::move(client_connexion)),
          name(std::move(name)) {}

    /// Send a packet to the player.
    template <typename T>
    void send(const T& t) { client_connexion.send(t); }
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
    std::vector<std::unique_ptr<Player>> players;

    /// A map from connexions to players, to figure out which player sent that packet.
    std::map<net::TCPConnexion, Player*> player_map;

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
    COMMON_PACKETS(X)
    CS_PACKETS(X)
#undef X

private:
    bool AllPlayersConnected() {
        return rgs::all_of(players, [](const auto& p) { return p->connected; });
    }

    bool AllWordsSubmitted() {
        return rgs::all_of(players, [](auto& x) { return x->submitted_word; });
    }

    template <typename T>
    void Broadcast(const T& packet) {
        for (auto& p : players) p->send(packet);
    }

    void Draw(Player& p, usz count = 1);

    void HandlePlaySoundCard(
        net::TCPConnexion& client,
        Card& card,
        Player& target_player,
        u32 target_index
    );

    void NextPlayer();

    /// Remove a card a player’s hand.
    void RemoveCard(Player& p, Card& c) {
        auto it = rgs::find_if(p.hand, [&](Card& x) { return &x == &c; });
        Assert(it != p.hand.end(), "Card not in hand");
        discard.emplace_back(c.id);
        p.hand.erase(it);
    }

    void SendGameState(Player& p);
    void SetUpGame();
    void Tick();
    auto ValidatorFor(Player& p) -> Validator;

    auto player() -> Player& { return *players[current_player]; }

    bool accept(net::TCPConnexion& connexion) override;
    void receive(net::TCPConnexion& client, net::ReceiveBuffer& buffer) override;
};

#endif // PRESCRIPTIVISM_SERVER_SERVER_HH
