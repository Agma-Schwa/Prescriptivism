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
void Server::receive(net::TCPConnexion& client, net::ReceiveBuffer& buffer) {
    auto sz = buffer.size();
    auto data = buffer.read(sz);
    Log(
        "Received {} bytes from {}: {}",
        sz,
        client.address(),
        std::string_view(reinterpret_cast<const char*>(data.data()), sz)
    );
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
