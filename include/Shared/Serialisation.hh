#ifndef PRESCRIPTIVISM_SHARED_SERIALISATION_HH
#define PRESCRIPTIVISM_SHARED_SERIALISATION_HH

#include <Shared/Utils.hh>

#include <base/Base.hh>

#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

/// Serialisation module.
///
/// This defines a Reader and Writer pair for serialising and deserialising
/// objects. The simplest way of using this for your own types is to use the
/// PR_SERIALISE() macro defined in 'pr/Utils.hh':
///
/// class Foo {
///     PR_SERIALISE(field1, field2, field3, ...);
///     ...
/// };
///
/// If this is not possible, perhaps because the type in question is defined
/// in another library, you can implement specialisations of Serialiser<T>
/// for your type:
///
/// template <>
/// struct pr::Serialiser<Foo> {
///     static void serialise(Writer& w, const Foo& f) { ... }
///     static void deserialise(Reader& r, const Foo& f) { ... }
/// };
namespace pr::ser {
class Reader;
class Writer;

template <usz n>
class Magic;

template <typename T>
class Blob;

template <typename T>
struct Serialiser;

struct InputSpan : std::span<const std::byte> {
    using std::span<const std::byte>::span;

    explicit InputSpan(const char* data, usz size)
        : std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size) {}

    explicit InputSpan(const u8* data, usz size)
        : std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size) {}

    InputSpan(std::span<const std::byte> data) : std::span<const std::byte>(data) {}
    InputSpan(std::span<const char> data) : InputSpan(data.data(), data.size()) {}
    InputSpan(std::span<const u8> data) : InputSpan(data.data(), data.size()) {}

    template <usz n>
    explicit InputSpan(const char (&str)[n])
        : std::span<const std::byte>(reinterpret_cast<const std::byte*>(str), n) {}
};

template <typename T>
auto Deserialise(InputSpan data) -> Result<T>;

template <typename T>
auto Serialise(const T& t) -> std::vector<std::byte>;
} // namespace pr::ser

/// Helper class to (partially) deserialise objects.
class pr::ser::Reader {
    InputSpan data;

public:
    Result<> result;

    explicit Reader(InputSpan data) : data(data) {}

    /// Read several fields from this packet.
    template <typename... Fields>
    void operator()(Fields&&... fields) {
        (void) ((*this >> std::forward<Fields>(fields), result.has_value()) and ...);
    }

    /// Read a number from this packet.
    template <std::integral T>
    T read() {
        T t;
        *this >> t;
        return t;
    }

    template <std::integral T>
    auto operator>>(T& t) -> Reader& {
        Copy(&t, sizeof(T));
        if constexpr (std::endian::native != std::endian::little) t = std::byteswap(t);
        return *this;
    }

    template <std::floating_point T>
    auto operator>>(T& t) -> Reader& {
        Copy(&t, sizeof(T));
        return *this;
    }

    template <typename T>
    requires std::is_enum_v<T>
    auto operator>>(T& t) -> Reader& {
        std::underlying_type_t<T> u;
        *this >> u;
        t = static_cast<T>(u);
        return *this;
    }

    auto operator>>(std::span<std::byte> data) -> Reader& {
        Copy(data.data(), data.size());
        return *this;
    }

    // Accept any reference for some of these since we may be
    // serialising proxy objects.
    template <typename T>
    requires requires (T&& t, Reader& r) { std::forward<T>(t).deserialise(r); }
    auto operator>>(T&& t) -> Reader& {
        std::forward<T>(t).deserialise(*this);
        return *this;
    }

    template <typename T>
    requires requires (T&& t, Reader& r) { Serialiser<std::remove_reference_t<T>>::Deserialise(r, std::forward<T>(t)); }
    auto operator>>(T&& t) -> Reader& {
        Serialiser<std::remove_reference_t<T>>::Deserialise(*this, std::forward<T>(t));
        return *this;
    }

    /// Read a string from this packet.
    auto operator>>(std::string& s) -> Reader& {
        s.resize_and_overwrite(
            read<usz>(),
            [&](char* ptr, usz count) { return Copy(ptr, count); }
        );
        return *this;
    }

    template <typename T, usz n>
    auto operator>>(std::array<T, n>& a) -> Reader& {
        for (auto& elem : a) *this >> elem;
        return *this;
    }

    template <typename T>
    auto operator>>(std::vector<T>& s) -> Reader& {
        auto sz = read<usz>();
        if (sz > s.max_size()) {
            result = Error("Input size {} exceeds maximum size {} of std::vector<>", sz, s.max_size());
            return *this;
        }

        s.resize(sz);
        for (auto& elem : s) *this >> elem;
        return *this;
    }

    /// Check if we could read the entire thing.
    [[nodiscard]] explicit operator bool() { return result.has_value(); }

    /// Mark that serialisation has failed for whatever reason.
    void fail(std::string_view err) {
        if (result) result = Error("{}", err);
    }

    /// Check how many bytes are left in the buffer.
    [[nodiscard]] auto size() const -> usz { return data.size(); }

private:
    usz Copy(void* ptr, usz count) {
        if (data.size() < count or not result) {
            result = Error("Not enough data to read {} bytes ({} bytes left)", count, data.size());
            return 0;
        }

        std::memcpy(ptr, data.data(), count);
        data = data.subspan(count);
        return count;
    }
};

/// Helper for serialising data into a buffer.
class pr::ser::Writer {
public:
    std::vector<std::byte> data;

    Writer() = default;

    /// Write several fields to this packet.
    template <typename... Fields>
    void operator()(Fields&&... fields) {
        (*this << ... << std::forward<Fields>(fields));
    }

    /// Write a type to this packet.
    template <typename T>
    auto operator<<(const T& t) -> Writer& {
        write(t);
        return *this;
    }

    template <std::integral T>
    void write(T t) {
        if constexpr (std::endian::native != std::endian::little) t = std::byteswap(t);
        Append(&t, sizeof(T));
    }

    template <std::floating_point T>
    void write(T t) {
        Append(&t, sizeof(T));
    }

    template <typename T>
    requires std::is_enum_v<T>
    void write(T t) {
        write(std::to_underlying(t));
    }

    void write(InputSpan span) {
        Append(span.data(), span.size());
    }

    template <typename T>
    requires requires (const T& t, Writer& w) { t.serialise(w); }
    void write(const T& t) {
        t.serialise(*this);
    }

    template <typename T>
    requires requires (const T& t, Writer& w) { Serialiser<std::remove_reference_t<T>>::Serialise(w, t); }
    void write(const T& t) {
        Serialiser<std::remove_reference_t<T>>::Serialise(*this, t);
    }

    void write(const std::string& s) {
        write(s.size());
        Append(s.data(), s.size());
    }

    template <typename T>
    void write(const std::vector<T>& v) {
        write(v.size());
        for (const auto& elem : v) write(elem);
    }

    template <typename T, usz n>
    void write(const std::array<T, n>& a) {
        for (const auto& elem : a) write(elem);
    }

private:
    void Append(const void* ptr, usz count) {
        auto* p = static_cast<const std::byte*>(ptr);
        data.insert(data.end(), p, p + count);
    }
};

/// Magic number to check the serialised data is valid.
template <base::usz n>
class pr::ser::Magic {
    char magic[n]{};

public:
    template <usz sz>
    consteval explicit Magic(const char (&m)[sz]) { std::copy_n(m, n, magic); }

    template <utils::is<char, u8, std::byte>... Vals>
    consteval explicit Magic(Vals... vals) : magic{char(vals)...} {}

    void deserialise(Reader& r) const {
        std::byte buf[n]{};
        r >> std::span{buf};
        if (not r) return;
        if (std::memcmp(buf, magic, n) != 0) {
            std::string msg = "Magic number mismatch! Got '";
            for (auto b : buf) msg += std::format("{:02x}", u8(b));
            msg += "', expected '";
            for (auto b : magic) msg += std::format("{:02x}", u8(b));
            msg += "'";
            r.fail(msg);
        }
    }

    void serialise(Writer& w) const {
        w.write(InputSpan{magic});
    }
};

/// A blob of data with the size supplied externally.
template <typename T>
class pr::ser::Blob {
    std::unique_ptr<T[]>& data;
    usz size;

public:
    // The const_cast here is ugly, but with how this is actually used,
    // this should never be a problem.
    Blob(std::unique_ptr<T[]>& data, usz size) : data(data), size(size) {}
    Blob(const std::unique_ptr<T[]>& data, usz size)
        : data(const_cast<std::unique_ptr<T[]>&>(data)),
          size(size) {}

    void deserialise(Reader& r) {
        r >> size;
        data = std::make_unique<T[]>(size);
        if constexpr (utils::is<T, char, u8, std::byte>) {
            r >> std::span(reinterpret_cast<std::byte*>(data.get()), size);
        } else {
            for (usz i = 0; i < size; ++i) r >> data[i];
        }
    }

    void serialise(Writer& w) const {
        w << size;
        if constexpr (utils::is<T, char, u8, std::byte>) {
            w.write(std::span(reinterpret_cast<const std::byte*>(data.get()), size));
        } else {
            for (usz i = 0; i < size; ++i) w << data[i];
        }
    }
};

namespace pr::ser {
template <usz n>
Magic(const char (&)[n]) -> Magic<n - 1>;

template <typename... args>
Magic(args...) -> Magic<sizeof...(args)>;

template <typename T>
Blob(std::unique_ptr<T[]>&, usz) -> Blob<T>;

template <typename T>
Blob(const std::unique_ptr<T[]>&, usz) -> Blob<T>;
}

template <typename T>
auto pr::ser::Deserialise(InputSpan data) -> pr::Result<T> {
    Result<T> t{T{}};
    Reader r{data};
    r >> t.value();
    Try(r.result);
    return t;
}

template <typename T>
auto pr::ser::Serialise(const T& t) -> std::vector<std::byte> {
    Writer w;
    w << t;
    return std::move(w.data);
}

#endif // PRESCRIPTIVISM_SHARED_SERIALISATION_HH
