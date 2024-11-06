module;
#include <algorithm>
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <chrono>
#include <memory>
#include <ranges>
#include <thread>
#include <vector>

module pr.server;
import pr.tcp;

using namespace pr;
using namespace pr::server;

// =============================================================================
//  Networking
// =============================================================================
void Server::Kick(net::TCPConnexion& client, DisconnectReason reason) {
    client.send(packets::sc::Disconnect{reason});
    client.disconnect();
}

void Server::TickNetworking() {
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
    constexpr usz PlayersNeeded = 2;

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
    auto erased = std::erase_if(pending_connexions, [&](auto& x) {
        return client == x.conn;
    });
    if (erased != 1) {
        Kick(client, DisconnectReason::InvalidPacket);
        return;
    }
    if (login.password != password) {
        Kick(client, DisconnectReason::WrongPassword);
        return;
    }
    for (auto& p : players) {
        if (p->name == login.name) {
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
    players.push_back(std::make_unique<Player>(client, std::move(login.name)));
}

// =============================================================================
//  Game Logic
// =============================================================================
void Server::Tick() {
    // Some players are not connected; wait for them.
    if (not rgs::all_of(players, [](const auto& p) { return p->connected(); }))
        return;
}

// =============================================================================
//  API
// =============================================================================
Server::Server(u16 port, std::string password) : server(net::TCPServer::Create(port, 200).value()), password(std::move(password)) {
    server.set_callbacks(*this);
}

void Server::Run() {
    constexpr chr::milliseconds ServerTickDuration = 33ms;
    Log("Server listening on port {}", server.port());
    for (;;) {
        const auto start_of_tick = chr::system_clock::now();

        TickNetworking();
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
