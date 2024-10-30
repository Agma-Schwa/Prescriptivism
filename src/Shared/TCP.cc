module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <cerrno>
#include <cstring>
#include <memory>
#include <print>
#include <span>
#include <vector>

// =============================================================================
//  Linux
// =============================================================================
#ifdef __linux__
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>

// =============================================================================
//  Windows
// =============================================================================
#else
#    error TODO: Non-linux support
#endif

module pr.tcp;

using namespace pr;
using namespace pr::net;

namespace pr::net::impl {
// =============================================================================
//  Impl - Linux Decls
// =============================================================================
#ifdef __linux__
using Socket = int;
constexpr Socket InvalidSocket = -1;

// =============================================================================
//  Impl - Windows Decls
// =============================================================================
#else
#endif
} // namespace pr::net::impl

// =============================================================================
//  Impl - Common Decls
// =============================================================================
namespace pr::net::impl {
/// RAII wrapper for a socket.
class SocketHolder {
    Socket socket = InvalidSocket;

public:
    explicit SocketHolder(Socket socket) : socket(socket) {}
    ~SocketHolder() { Close(); }
    SocketHolder(const SocketHolder&) = delete;
    SocketHolder& operator=(const SocketHolder&) = delete;
    SocketHolder(SocketHolder&& other) noexcept : socket(std::exchange(other.socket, InvalidSocket)) {}
    SocketHolder& operator=(SocketHolder&& other) noexcept {
        if (this != &other) {
            Close();
            socket = std::exchange(other.socket, InvalidSocket);
        }
        return *this;
    }

    auto handle() const -> Socket { return socket; }

private:
    void Close();
};

struct AcceptedConnexion {
    SocketHolder socket;
    std::string ip_address;
};

auto AcceptConnexion(Socket sock, bool& done) -> std::optional<AcceptedConnexion>;
auto ConnectToServer(std::string_view remote_ip, u16 port) -> Result<SocketHolder>;
auto CreateServerSocket(u16 port, u32 max_connexions) -> Result<SocketHolder>;
} // namespace pr::net::impl

// =============================================================================
//  Impl - Linux
// =============================================================================
#ifdef __linux__
void impl::SocketHolder::Close() {
    if (socket != InvalidSocket) ::close(socket);
}

auto MakeNonBlocking(impl::Socket sock) -> Result<> {
    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) return Error(
        "Failed to set socket to non-blocking: {}",
        std::strerror(errno)
    );
    return {};
}

auto CreateNonBlockingSocket() -> Result<impl::SocketHolder> {
    // Create the socket.
    impl::SocketHolder sock{socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (sock.handle() == impl::InvalidSocket) return Error(
        "Failed to create TCP socket: {}",
        std::strerror(errno)
    );

    // Set the socket to non-blocking.
    Try(MakeNonBlocking(sock.handle()));

    // Set the socket to be reusable.
    int opt = 1;
    if (setsockopt(sock.handle(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) == -1) return Error(
        "Failed to set socket to reusable: {}",
        std::strerror(errno)
    );

    return std::move(sock);
}

auto impl::AcceptConnexion(Socket sock, bool& done) -> std::optional<AcceptedConnexion> {
    Assert(sock != InvalidSocket, "Server socket invalid");

    sockaddr_in sa{};
    socklen_t sa_len = sizeof sa;
    SocketHolder new_sock{accept(sock, reinterpret_cast<sockaddr*>(&sa), &sa_len)};
    if (new_sock.handle() == InvalidSocket) {
        if (errno == EWOULDBLOCK or errno == EAGAIN or errno == ECONNABORTED) {
            done = true;
            return std::nullopt;
        }

        Log("Failed to accept connexion: {}", std::strerror(errno));
        return std::nullopt;
    }

    // Make the connexion non-blocking.
    auto res = MakeNonBlocking(new_sock.handle());
    if (not res) {
        Log("Failed to make connexion non-blocking: {}", res.error());
        return std::nullopt;
    }

    // Get the IP address.
    char ip_str[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &sa.sin_addr, ip_str, sizeof ip_str) == nullptr) {
        Log("Failed to get IP address: {}", std::strerror(errno));
        return std::nullopt;
    }

    return AcceptedConnexion{std::move(new_sock), ip_str};
}

auto impl::ConnectToServer(std::string_view remote_ip, u16 port) -> Result<SocketHolder> {
    auto sock = Try(CreateNonBlockingSocket());

    // Parse the IP address.
    in_addr addr{};
    if (inet_pton(AF_INET, remote_ip.data(), &addr) != 1) return Error(
        "Failed to parse IP address '{}'",
        remote_ip
    );

    // Connect to the server.
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr = addr;
    if (connect(sock.handle(), reinterpret_cast<sockaddr*>(&sa), sizeof sa) == -1) return Error(
        "Failed to connect to {}: {}",
        remote_ip,
        std::strerror(errno)
    );

    // Take care to clear 'fd' so we don't close the socket.
    return std::move(sock);
}

auto impl::CreateServerSocket(u16 port, u32 max_connexions) -> Result<SocketHolder> {
    auto sock = Try(CreateNonBlockingSocket());

    // Bind the socket.
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock.handle(), reinterpret_cast<sockaddr*>(&sa), sizeof sa) == -1) return Error(
        "Failed to bind to port {}: {}",
        port,
        std::strerror(errno)
    );

    // Start listening.
    if (listen(sock.handle(), int(max_connexions)) == -1) return Error(
        "Failed to listen on port {}: {}",
        port,
        std::strerror(errno)
    );

    // Take care to clear 'fd' so we don't close the socket.
    return std::move(sock);
}
#endif

// =============================================================================
//  Impl - Windows
// =============================================================================
#ifdef _WIN32
#endif

// =============================================================================
//  Impl
// =============================================================================
struct TCPConnexion::Impl : impl::SocketHolder {
    bool disconnected = false;
    std::string ip_address;
    std::vector<std::byte> buffer;

    explicit Impl(SocketHolder socket, std::string ip_address)
        : SocketHolder(std::move(socket)),
          ip_address(std::move(ip_address)) {}

    void Disconnect();
};

struct TCPServer::Impl : impl::SocketHolder {
    std::vector<TCPConnexion> all_connexions;
    const u16 port;
    TCPCallbacks* tcp_callbacks;

    explicit Impl(SocketHolder socket, u16 port)
        : SocketHolder(std::move(socket)),
          port(port) {}

    void CloseConnexionAfterError(TCPConnexion& conn);
    void UpdateConnexions();
    void ReceiveAll();
    void SetCallbacks(TCPCallbacks& callbacks);
};

// =============================================================================
//  Impl - Connexion
// =============================================================================
void TCPConnexion::Impl::Disconnect() {
    if (disconnected) return;
    disconnected = true;

    // TODO: Send message.
}

// =============================================================================
//  Impl - Server
// =============================================================================
void TCPServer::Impl::CloseConnexionAfterError(TCPConnexion& conn) {
    if (errno == ECONNRESET) Log("Connexion {} reset by client", conn.address());
    else Log("Error while processing connexion {}: {}", conn.address(), std::strerror(errno));
    conn.disconnect();
}

void TCPServer::Impl::ReceiveAll() {
    Assert(tcp_callbacks, "Callbacks not set");
    for (auto& conn : all_connexions) {
        constexpr usz ReceivePerTick = 65536;
        if (conn.impl->disconnected) continue;
        auto& buf = conn.impl->buffer;

        // Allocate more space in the buffer.
        auto old_sz = buf.size();
        buf.resize(old_sz + ReceivePerTick);

        // Receive data.
        auto sz = recv(conn.impl->handle(), buf.data() + old_sz, ReceivePerTick, 0);
        if (sz == -1) {
            if (errno == EWOULDBLOCK or errno == EAGAIN) continue;
            CloseConnexionAfterError(conn);
            continue;
        }

        // If we receive 0, the connexion was closed.
        if (sz == 0) {
            Log("Connexion {} closed by client", conn.address());
            conn.disconnect();
            continue;
        }

        // Truncate the buffer to match what we received.
        buf.resize(old_sz + usz(sz));

        // Dispatch the data to the callback and truncate the buffer
        // to remove everything that was processed by it.
        ReceiveBuffer buffer{buf};
        tcp_callbacks->receive(conn, buffer);
        auto processed = buf.size() - buffer.size();
        buf.erase(buf.begin(), buf.begin() + isz(processed));
    }
}

void TCPServer::Impl::SetCallbacks(TCPCallbacks& callbacks) {
    Assert(not tcp_callbacks, "Callbacks already set");
    tcp_callbacks = &callbacks;
}

void TCPServer::Impl::UpdateConnexions() {
    // Delete stale connexions.
    std::erase_if(all_connexions, [](const TCPConnexion& conn) {
        return conn.impl->disconnected;
    });

    // Accept incoming ones.
    for (bool done = false; not done;) {
        auto conn = impl::AcceptConnexion(handle(), done);
        if (not conn) continue;
        Log("Added connexion from {}", conn->ip_address);
        TCPConnexion c;
        c.impl = std::make_unique<TCPConnexion::Impl>(
            std::move(conn->socket),
            std::move(conn->ip_address)
        );
        all_connexions.push_back(std::move(c));
    }
}

// =============================================================================
//  API
// =============================================================================
LIBBASE_DEFINE_HIDDEN_IMPL(TCPConnexion);
LIBBASE_DEFINE_HIDDEN_IMPL(TCPServer);
auto TCPConnexion::Connect(
    std::string_view remote_ip,
    u16 port
) -> Result<TCPConnexion> {
    TCPConnexion conn;
    auto sock = Try(impl::ConnectToServer(remote_ip, port));
    conn.impl = std::make_unique<Impl>(std::move(sock), std::string{remote_ip});
    return conn;
}

auto TCPServer::Create(u16 port, u32 max_connexions) -> Result<TCPServer> {
    TCPServer server;
    auto sock = Try(impl::CreateServerSocket(port, max_connexions));
    server.impl = std::make_unique<Impl>(std::move(sock), port);
    return server;
}

auto TCPConnexion::address() const -> std::string_view { return impl->ip_address; }
void TCPConnexion::disconnect() { impl->Disconnect(); }

auto TCPServer::connexions() -> std::span<TCPConnexion> { return impl->all_connexions; }
auto TCPServer::port() const -> u16 { return impl->port; }
void TCPServer::receive() { impl->ReceiveAll(); }
void TCPServer::set_callbacks(TCPCallbacks& callbacks) { impl->SetCallbacks(callbacks); }
void TCPServer::update_connexions() { impl->UpdateConnexions(); }
