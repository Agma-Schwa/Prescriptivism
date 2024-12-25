#ifndef PRESCRIPTIVISM_SHARED_TCP_HH
#define PRESCRIPTIVISM_SHARED_TCP_HH

#include <Shared/Utils.hh>

#include <base/Base.hh>
#include <base/Properties.hh>
#include <base/Serialisation.hh>

#include <bit>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace pr::net {
class TCPServerCallbacks;
class TCPServer;
class TCPConnexion;
class ReceiveBuffer;
class SendBuffer;

// We use little-endian for serialisation rather than network byte order
// because this is what most systems we care about use, which means we don’t
// need to do any conversion on those systems.
constexpr auto Endianness = std::endian::little;
using Reader = ser::Reader<Endianness>;
using Writer = ser::Writer<Endianness>;

constexpr u16 DefaultPort = 33'014;
} // namespace pr::net

/// Buffer for receiving data from a TCP connexion.
class pr::net::ReceiveBuffer {
    LIBBASE_IMMOVABLE(ReceiveBuffer);

    std::span<std::byte> data;

public:
    ReceiveBuffer(std::vector<std::byte>& buffer) : data(buffer) {}

    /// Check if the buffer is empty.
    [[nodiscard]] auto empty() const -> bool { return data.empty(); }

    /// \see read().
    [[nodiscard]] auto peek(usz n) -> std::span<std::byte> {
        if (n > data.size()) return {};
        return data.subspan(0, n);
    }

    /// \see read().
    template <typename T>
    requires std::is_trivially_copyable_v<T> and std::is_default_constructible_v<T>
    auto peek() -> std::optional<T> {
        if (data.size() < sizeof(T)) return std::nullopt;
        auto n = peek(sizeof(T));
        T result;
        std::memcpy(&result, n.data(), sizeof(T));
        return result;
    }

    /// Read data from the buffer and discard it.
    ///
    /// If there are fewer bytes in the buffer than requested,
    /// nothing is read and the remains unchanged. Otherwise,
    /// \p n bytes are removed from the start of the buffer and
    /// returned.
    ///
    /// \param n The number of bytes to read.
    [[nodiscard]] auto read(usz n) -> std::span<std::byte> {
        if (n > data.size()) return {};
        auto result = data.subspan(0, n);
        data = data.subspan(n);
        return result;
    }

    /// As read(), but attempts to read a value of a specific type
    /// instead, returning an empty optional if there are not enough
    /// bytes in the buffer.
    template <typename T>
    [[nodiscard]] auto read() -> std::optional<T> {
        static_assert(
            requires (T t, Reader& r) { r >> t; },
            "Type must be serialisable"
        );

        T res;
        Reader reader{data};
        reader >> res;
        if (not reader) return std::nullopt;
        data = data.subspan(data.size() - reader.size());
        return res;
    }

    /// How many bytes are in the buffer.
    [[nodiscard]] auto size() const -> usz { return data.size(); }
};

/// Class that implements common functionality for TCP server
/// that must be implemented by users.
class pr::net::TCPServerCallbacks {
    LIBBASE_IMMOVABLE(TCPServerCallbacks);

protected:
    TCPServerCallbacks() = default;

public:
    virtual ~TCPServerCallbacks() = default;

    /// Called to query if we should accept a connexion. If this returns
    /// false, the connexion is closed. The implementation of this is free
    /// to send data to the client before returning.
    virtual bool accept(TCPConnexion& connexion) = 0;

    /// Called from TCPServer::receive().
    ///
    /// \param connexion The connexion that sent the data.
    /// \param data A buffer that contains any unprocessed data since
    /// the last call to receive(). The buffer should be updated to
    /// remove any data that has been processed.
    virtual void receive(TCPConnexion& connexion, ReceiveBuffer& data) = 0;
};

/// A reference type that holds a TCP connexion that can be
/// used to communicate with a remote peer. This can be a
/// connexion to a server or to a client.
///
/// The actual state is managed by a shared pointer, so copying
/// this and storing copies of it is safe.
class pr::net::TCPConnexion {
    friend TCPServer; // Server needs to create client connexions.

    struct Impl;
    std::shared_ptr<Impl> impl;

    /// Whether this connexion has been disconnected.
    ComputedReadonly(bool, disconnected);

    /// The address of the remote peer.
    ComputedReadonly(std::string_view, address);

public:
    TCPConnexion();
    ~TCPConnexion();

    /// Connect to a server.
    ///
    /// This takes a string because some platforms require
    /// null-termination of the address.
    static auto Connect(std::string remote_address, u16 port) -> Result<TCPConnexion>;

    /// Close the connexion.
    void disconnect();

    /// Get user data that was set with 'set()'.
    template <typename T>
    auto get() -> T* { return static_cast<T*>(GetImpl()); }

    /// Receive data from the remote peer.
    void receive(std::function<void(ReceiveBuffer&)> callback);

    /// Send data to the remote peer.
    template <typename T>
    void send(const T& t) {
        // Type requires serialisation.
        if constexpr (requires (T t, Writer& s) { s << t; }) {
            send(std::span<const std::byte>{ser::Serialise<Endianness>(t)});
        }

        // Type can be sent as-is.
        else {
            static_assert(
                std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable or serialisable"
            );

            send(std::span{reinterpret_cast<const std::byte*>(&t), sizeof(T)});
        }
    }

    /// Send data to the remote peer.
    void send(std::span<const std::byte> data);

    /// Set user data for this connexion.
    void set(void* data);

    friend auto operator<=>(const TCPConnexion&, const TCPConnexion&) = default;

private:
    auto GetImpl() -> void*;
};

/// A reference type that holds a TCP server that can accept
/// incoming connexions.
class pr::net::TCPServer {
    LIBBASE_DECLARE_HIDDEN_IMPL(TCPServer);

public:
    /// Create a server socket that listens on the given port.
    static auto Create(u16 port, u32 max_connexions) -> Result<TCPServer>;

    /// Get the connexions that we have accepted.
    auto connexions() -> std::span<TCPConnexion>;

    /// Receive data from existing connexions. Calls
    /// TCPEventHandler::Receive() with the data.
    void receive();

    /// Get the server port.
    auto port() const -> u16;

    /// Set the server callback handler.
    void set_callbacks(TCPServerCallbacks& callbacks);

    /// Accept incoming connexions and throw away any that have
    /// gone stale. If there is an error with a connexion, it
    /// is logged and the connexion is thrown away.
    ///
    /// Any references to connexions that have been closed must
    /// be cleaned up before this is called, as this deletes any
    /// closed connexions.
    void update_connexions();
};

#endif // PRESCRIPTIVISM_SHARED_TCP_HH
