module;
#include <algorithm>
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <chrono>
#include <map>
#include <memory>
#include <ranges>
#include <thread>
#include <vector>
module pr.server;

import pr.constants;
import pr.tcp;
import pr.validation;

using namespace pr;
using namespace pr::server;
// ============================================================================
// Constants
// ============================================================================
constexpr usz PlayersNeeded = pr::constants::PlayersPerGame;

// ============================================================================
// Helpers
// ============================================================================
auto BuildWordArray(std::span<const Card> card_range) -> constants::Word {
    Assert(rgs::distance(card_range) == constants::StartingWordSize, "Invalid word size");
    constants::Word w;
    for (auto [i, el] : card_range | vws::enumerate) w[i] = el.id;
    return w;
}

// =============================================================================
//  Networking
// =============================================================================
void Server::Kick(net::TCPConnexion& client, DisconnectReason reason) {
    client.send(packets::sc::Disconnect{reason});
    client.disconnect();
}

void Server::Tick() {
    // Receive incoming data.
    server.receive();

    // For any pending connexions, disconnect any that haven’t sent
    // a login packet within the timeout.
    auto now = chr::steady_clock::now();
    for (auto& [conn, established] : pending_connexions) {
        if (now - established > 30s) {
            // Don’t even bother sending a packet here; if they didn’t
            // respond within the time frame, they’re likely not actually
            // a game client, but rather some random other connexion.
            Log("Client {} took too long to send a login packet", conn.address());
            conn.disconnect();
        }
    }

    // Clear out pending connexions that may have gone away.
    std::erase_if(pending_connexions, [](const auto& c) { return c.conn.disconnected(); });

    // Clear out player connexions of players that are disconnected.
    std::erase_if(player_map, [](const auto& x) { return x.first.disconnected(); });

    // As the last step, accept new connexions and close stale ones.
    server.update_connexions();

    // Start the game if we have enough connected players.
    //
    // This check is here because it needs to be invoked in at least
    // two places: when we receive the last word and all players are
    // connected, or when a player reconnects after we have received
    // all words.
    MaybeStartGame();
}

bool Server::accept(net::TCPConnexion& connexion) {
    // Make sure we’re not full yet.
    auto connected_players = rgs::distance(players | vws::filter([](auto& p) { return p->connected(); }));
    if (connected_players + pending_connexions.size() == PlayersNeeded) {
        connexion.send(packets::sc::Disconnect{DisconnectReason::ServerFull});
        return false;
    }

    pending_connexions.emplace_back(connexion, chr::steady_clock::now());
    return true;
}

void Server::receive(net::TCPConnexion& client, net::ReceiveBuffer& buf) {
    while (not client.disconnected() and not buf.empty()) {
        auto res = packets::HandleServerSidePacket(*this, client, buf);

        // If there was an error, close the connexion.
        if (not res) {
            Log("Packet error while processing {}: {}", client.address(), res.error());
            return Kick(client, DisconnectReason::InvalidPacket);
        }

        // And stop if the packet was incomplete.
        if (not res.value()) break;
    }
}

// =============================================================================
//  Packet Handlers
// =============================================================================
namespace sc = packets::sc;
namespace cs = packets::cs;

void Server::handle(net::TCPConnexion& client, cs::Disconnect) {
    Log("Client {} disconnected", client.address());
    client.disconnect();
}

void Server::handle(net::TCPConnexion& client, sc::WordChoice wc) {
    // Player has already submitted a word.
    if (not player_map.contains(client) or player_map[client]->submitted_word) {
        Kick(client, DisconnectReason::UnexpectedPacket);
        return;
    }

    // Word is invalid.
    constants::Word original;
    for (auto [i, c] : player_map[client]->word | vws::enumerate) original[i] = c.id;
    if (validation::ValidateInitialWord(wc.word, original) != validation::InitialWordValidationResult::Valid) {
        Kick(client, DisconnectReason::InvalidPacket);
        return;
    }

    // Word is valid. Mark it as submitted.
    for (auto [i, c] : wc.word | vws::enumerate) player_map[client]->word[i] = Card{c};
    player_map[client]->submitted_word = true;
    Log("Client gave back word");
}

void Server::handle(net::TCPConnexion& client, cs::HeartbeatResponse res) {
    Log("Received heartbeat response from client {}", res.seq_no);
}

void Server::handle(net::TCPConnexion& client, packets::cs::Login login) {
    Log("Login: name = {}, password = {}", login.name, login.password);

    // Mark this as no longer pending and also check whether it was
    // pending in the first place. Clients that are already connected
    // to a player are not supposed to send a login packet.
    auto erased = std::erase_if(pending_connexions, [&](auto& x) {
        return client == x.conn;
    });
    if (erased != 1) return Kick(client, DisconnectReason::InvalidPacket);

    // Check that the password matches.
    if (login.password != password) {
        Kick(client, DisconnectReason::WrongPassword);
        return;
    }

    // Try to match this connexion to an existing player.
    for (auto& p : players) {
        if (p->name == login.name) {
            // Someone is trying to connect to a player that is
            // already connected.
            if (p->connected()) {
                Log("{} is already connected", login.name);
                Kick(client, DisconnectReason::UsernameInUse);
                return;
            }

            Log("Player {} logging back in", login.name);
            p->client_connexion = client;
            player_map[client] = p.get();
            if (set_up and not p->submitted_word)
                p->client_connexion.send(sc::WordChoice{BuildWordArray(p->word)});
            return;
        }
    }

    // Create a new player. This is also the only place where we can
    // reach the player limit for the first time, so perform game
    // initialisation here if we have enough players.
    players.push_back(std::make_unique<Player>(client, std::move(login.name)));
    player_map[client] = players.back().get();
    if (players.size() == PlayersNeeded) SetUpGame();
}

// =============================================================================
//  Game Logic
// =============================================================================
void Server::MaybeStartGame() {
    // Start the game iff all players are connected and all words have been received.
    if (
        started or
        not set_up or
        not AllPlayersConnected() or
        not AllWordsSubmitted()
    ) return;
    started = true;

    // Send each player’s word to every player.
    std::array<sc::StartGame::PlayerInfo, constants::PlayersPerGame> player_infos;
    for (auto [i, p] : players | vws::enumerate) {
        player_infos[i].name = p->name;
        for (auto [j, c] : p->word | vws::enumerate)
            player_infos[i].word[j] = c.id;
    }

    for (auto [i, p] : players | vws::enumerate)
        p->client_connexion.send(sc::StartGame{player_infos, u8(i)});
}

void Server::SetUpGame() {
    set_up = true;

    // Consonants.
    auto AddCards = [&](std::span<const CardData> s) {
        for (auto& c : s) {
            for (u8 i = 0; i < c.count_in_deck; ++i) {
                deck.emplace_back(c.id);
            }
        }
    };

    AddCards(CardDatabaseConsonants);
    auto num_consonants = deck.size();

    // Vowels.
    AddCards(CardDatabaseVowels);

    // Draw the cards for reach player’s word. Alternate vowels
    // and consonants so the player always gets a valid word to
    // begin with.
    rgs::shuffle(deck.begin(), deck.begin() + num_consonants, rng);
    rgs::shuffle(deck.begin() + num_consonants, deck.end(), rng);
    for (auto& p : players) {
        for (u8 i = 0; i < 3; ++i) {
            // Add consonant.
            p->word.push_back(std::move(deck.front()));
            deck.erase(deck.begin());

            // Add vowel.
            p->word.push_back(std::move(deck.back()));
            deck.pop_back();
        }

        // Send the player their word.
        p->client_connexion.send(sc::WordChoice{BuildWordArray(p->word)});
    }

    // Special cards.
    AddCards(CardDatabasePowers);

    // Draw each player’s hand.
    rgs::shuffle(deck, rng);
    for (auto& p : players) Draw(*p, 7);
    rgs::shuffle(players, rng);
}

void Server::NextPlayer() {
    player().client_connexion.send(sc::EndTurn{});
    // TODO fill back player deck to 7 cards
    current_player = (current_player + 1) % players.size();
    player().client_connexion.send(sc::StartTurn{});
}

void Server::Draw(Player& p, usz count) {
    for (usz i = 0; i < count; ++i) {
        if (deck.empty()) return;
        p.hand.push_back(std::move(deck.back()));
        deck.pop_back();
        p.client_connexion.send(sc::Draw{p.hand.back().id});
    }
}

// =============================================================================
//  API
// =============================================================================
Server::Server(u16 port, std::string password)
    : server(net::TCPServer::Create(port, 200).value()),
      password(std::move(password)) {
    server.set_callbacks(*this);
}

void Server::Run() {
    constexpr chr::milliseconds ServerTickDuration = 33ms;
    Log("Server listening on port {}", server.port());
    for (;;) {
        const auto start_of_tick = chr::system_clock::now();

        Tick();

        // Sleep for a bit.
        const auto end_of_tick = chr::system_clock::now();
        const auto tick_duration = chr::duration_cast<chr::milliseconds>(end_of_tick - start_of_tick);
        if (tick_duration < ServerTickDuration) {
            std::this_thread::sleep_for(ServerTickDuration - tick_duration);
        } else {
            Log("Server tick took too long: {}ms", tick_duration.count());
        }
    }
}
