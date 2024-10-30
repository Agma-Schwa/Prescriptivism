#include <base/Assert.hh>
#include <print>

import pr.server;
import pr.tcp;

int main() {
    auto server = pr::server::Server::Create(pr::net::DefaultPort);
    if (not server) Fatal("Failed to start server: {}", server.error());
    server.value().Run();
}
