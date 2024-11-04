module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

module pr.server;
import pr.tcp;

using namespace pr;
using namespace pr::server;

// =============================================================================
//  Impl
// =============================================================================
void Server::Kick(net::TCPConnexion& client) {
    client.send(packets::sc::Disconnect{});
    client.disconnect();
}

void Server::receive(net::TCPConnexion& client, net::ReceiveBuffer& buf) {
    while (not client.disconnected() and not buf.empty()) {
        auto res = packets::HandleServerSidePacket(*this, client, buf);

        // If there was an error, close the connexion.
        if (not res) {
            Log("Packet error while processing {}: {}", client.address(), res.error());
            return Kick(client);
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

// =============================================================================
//  API
// =============================================================================
Server::Server(u16 port): server(net::TCPServer::Create(port, 200).value()) {
    server.set_callbacks(*this);
}

void Server::Run() {
    constexpr chr::milliseconds ServerTickDuration = 33ms;
    Log("Server listening on port {}", server.port());
    for (;;) {
        const auto start_of_tick = chr::system_clock::now();

        // Receive incoming data.
        server.receive();

        // As the last step, update the list of connected clients. We
        // need to do this at the end, *after* clearing out any references
        // to stale connexions.
        server.update_connexions();

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
