module;
#include <algorithm>
#include <base/Macros.hh>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

module pr.server;
import pr.tcp;

using namespace pr;
using namespace pr::server;
// ============================================================================
// Constants
// ============================================================================

constexpr usz PlayersNeeded = 2;

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

    // As the last step, accept new connexions and close stale ones.
    server.update_connexions();
}

bool Server::accept(net::TCPConnexion& connexion) {
    // Make sure we’re not full yet.
    if (players.size() + pending_connexions.size() == PlayersNeeded) {
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

            Log("Player {} loging back in", login.name);
            p->client_connexion = client;
            return;
        }
    }

    // Create a new player. This is also the only place where we can
    // reach the player limit for the first time, so perform game
    // initialisation here if we have enough players.
    players.push_back(std::make_unique<Player>(client, std::move(login.name)));
    if (players.size() == PlayersNeeded) SetUpGame();
}

// =============================================================================
//  Game Logic
// =============================================================================
void Server::SetUpGame() {
    // Consonants.
    for (u8 i = 0; i < 2; ++i) {
        deck.emplace_back(CardType::C_b);
        deck.emplace_back(CardType::C_d);
        deck.emplace_back(CardType::C_dʒ);
        deck.emplace_back(CardType::C_g);
        deck.emplace_back(CardType::C_v);
        deck.emplace_back(CardType::C_z);
        deck.emplace_back(CardType::C_ʒ);
    }

    for (u8 i = 0; i < 4; ++i) {
        deck.emplace_back(CardType::C_p);
        deck.emplace_back(CardType::C_t);
        deck.emplace_back(CardType::C_tʃ);
        deck.emplace_back(CardType::C_k);
        deck.emplace_back(CardType::C_f);
        deck.emplace_back(CardType::C_s);
        deck.emplace_back(CardType::C_ʃ);
        deck.emplace_back(CardType::C_h);
        deck.emplace_back(CardType::C_w);
        deck.emplace_back(CardType::C_r);
        deck.emplace_back(CardType::C_j);
        deck.emplace_back(CardType::C_ʟ);
        deck.emplace_back(CardType::C_m);
        deck.emplace_back(CardType::C_n);
        deck.emplace_back(CardType::C_ɲ);
        deck.emplace_back(CardType::C_ŋ);
    }

    auto num_consonants = deck.size();

    // Vowels.
    for (u8 i = 0; i < 3; ++i) {
        deck.emplace_back(CardType::V_y);
        deck.emplace_back(CardType::V_ʊ);
        deck.emplace_back(CardType::V_ɛ);
        deck.emplace_back(CardType::V_ɐ);
        deck.emplace_back(CardType::V_ɔ);
    }

    for (u8 i = 0; i < 5; ++i) {
        deck.emplace_back(CardType::V_ɨ);
        deck.emplace_back(CardType::V_æ);
        deck.emplace_back(CardType::V_ɑ);
    }

    for (u8 i = 0; i < 7; ++i) {
        deck.emplace_back(CardType::V_i);
        deck.emplace_back(CardType::V_u);
        deck.emplace_back(CardType::V_e);
        deck.emplace_back(CardType::V_ə);
        deck.emplace_back(CardType::V_o);
        deck.emplace_back(CardType::V_a);
    }

    // Draw the cards for reach player’s word.
    rgs::shuffle(deck.begin(), deck.begin() + num_consonants, rng);
    rgs::shuffle(deck.begin() + num_consonants, deck.end(), rng);
    for (auto& p : players) {
        for (u8 i = 0; i < 3; ++i) {
            p->word.push_back(std::move(deck.back()));
            deck.pop_back();
            p->word.push_back(std::move(deck.front()));
            deck.erase(deck.begin());
        }
    }

    // Special cards.
    deck.emplace_back(CardType::P_Babel);
    deck.emplace_back(CardType::P_Superstratum);
    deck.emplace_back(CardType::P_Substratum);
    deck.emplace_back(CardType::P_Whorf);
    deck.emplace_back(CardType::P_Academia);
    deck.emplace_back(CardType::P_Heffer);
    deck.emplace_back(CardType::P_GVS);
    deck.emplace_back(CardType::P_Darija);
    deck.emplace_back(CardType::P_Brasil);
    deck.emplace_back(CardType::P_Gvprtskvni);
    deck.emplace_back(CardType::P_Reconstruction);
    deck.emplace_back(CardType::P_Chomsky);
    deck.emplace_back(CardType::P_Pinker);
    deck.emplace_back(CardType::P_Campbell);
    deck.emplace_back(CardType::P_Schleicher);
    deck.emplace_back(CardType::P_Schleyer);
    deck.emplace_back(CardType::P_Grimm);
    deck.emplace_back(CardType::P_Vajda);
    deck.emplace_back(CardType::P_Zamnenhoff);
    deck.emplace_back(CardType::P_Owl);
    deck.emplace_back(CardType::P_Revival);
    deck.emplace_back(CardType::P_Rosetta);
    deck.emplace_back(CardType::P_Urheimat);
    deck.emplace_back(CardType::P_ProtoWorld);
    deck.emplace_back(CardType::P_Vernacular);
    deck.emplace_back(CardType::P_Assimilation);
    deck.emplace_back(CardType::P_Dissimilation);
    deck.emplace_back(CardType::P_Regression);
    deck.emplace_back(CardType::P_Descriptivism);
    deck.emplace_back(CardType::P_Elision);
    deck.emplace_back(CardType::P_Elision);

    for (u8 i = 0; i < 3; ++i) {
        deck.emplace_back(CardType::P_Nope);
        deck.emplace_back(CardType::P_LinguaFranca);
        deck.emplace_back(CardType::P_Epenthesis);
        deck.emplace_back(CardType::P_Descriptivism);
        deck.emplace_back(CardType::P_Elision);
    }

    for (u8 i = 0; i < 10; ++i) deck.emplace_back(CardType::P_SpellingReform);

    // Draw each player’s hand.
    rgs::shuffle(deck, rng);
    for (auto& p : players) {
        for (u8 i = 0; i < 7; ++i) {
            p->hand.push_back(std::move(deck.back()));
            deck.pop_back();
        }
    }

    // TODO Let the players make their words
    rgs::shuffle(players, rng);
    player().client_connexion.send(sc::StartTurn());
}

void Server::NextPlayer() {
    player().client_connexion.send(sc::EndTurn{});
    // TODO fill back player deck to 7 cards
    current_player = (current_player + 1) % players.size();
    player().client_connexion.send(sc::StartTurn{});
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
