#ifndef PRESCRIPTIVISM_SHARED_UTILS_HH
#define PRESCRIPTIVISM_SHARED_UTILS_HH

#include <base/Base.hh>
#include <base/Properties.hh>

#include <chrono>
#include <cstdarg>
#include <format>
#include <functional>
#include <print>
#include <string>
#include <thread>

#define FWD(x) std::forward<decltype(x)>(x)

#define ComputedAccessor(type, name, ...)                                                                              \
public:                                                                                                                \
    [[nodiscard]] type get_##name() __VA_OPT__({ return) LIBBASE_VA_FIRST(__VA_ARGS__ __VA_OPT__(, );) __VA_OPT__(; }) \
        __declspec(property(get = get_##name)) type name;                                                              \
private:

namespace pr {
using namespace base;

struct Profile;
struct ZTermString;

template <typename T>
struct Debug;

template <typename T, auto deleter>
class Handle;

template <typename T>
class LateInit;

template <typename ResultType>
class Thread;

void CloseLoggingThread();

struct SilenceLog {
    LIBBASE_IMMOVABLE(SilenceLog);
    SilenceLog();
    ~SilenceLog();
};

class Timer {
    Readonly(chr::milliseconds, duration);
    Readonly(chr::steady_clock::time_point, start);

public:
    /// Create a new timer with the given duration and start it.
    explicit Timer(chr::milliseconds duration)
        : _duration(duration), _start(chr::steady_clock::now()) {}

    /// Get the elapsed time normalised between 0 and 1.
    [[nodiscard]] auto dt() const -> f32 { return dt(duration); }
    [[nodiscard]] auto dt(chr::milliseconds duration) const -> f32 {
        return f32(elapsed().count()) / duration.count();
    }

    /// Get the time that has elapsed since the animation started.
    [[nodiscard]] auto elapsed() const -> chr::milliseconds {
        return chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - start);
    }

    /// Check if the timer has expired.
    [[nodiscard]] auto expired() const -> bool {
        return elapsed() >= duration;
    }

    /// Extend the duration of the current iteration of the timer.
    void extend(chr::milliseconds extra) { _start += extra; }

    /// Restart the timer.
    void restart() { _start = chr::steady_clock::now(); }
};

template <typename... Args>
void Log(std::format_string<Args...> fmt, Args&&... args);
} // namespace pr

namespace pr {
void LogImpl(std::string);
}

namespace base::utils {
/// Return the last element in a range; this has undefined behaviour
/// if the range is empty.
template <typename Range>
requires (not std::is_reference_v<rgs::range_value_t<Range>>)
auto last(Range&& r) -> rgs::range_value_t<Range> {
    auto t = rgs::begin(r);
    auto e = rgs::end(r);
    Assert(t != e, "Cannot get the last element of an empty range!");
    if constexpr (requires { *rgs::prev(e); }) return *rgs::prev(e);
    else {
        auto last = *t;
        while (++t != e) last = *t;
        return last;
    }
}
} // namespace base::utils

template <typename T>
struct pr::Debug {
    T* value;
    Debug(T* t) : value(t) {}
    Debug(T& t) : value(&t) {}
    Debug(T&& t) : value(&t) {}
};

template <typename T, auto deleter>
class pr::Handle {
    T value{};

public:
    Handle() = default;
    Handle(T value) : value(std::move(value)) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { Delete(); }

    Handle(Handle&& other) noexcept : value(std::exchange(other.value, T{})) {}
    auto operator=(Handle&& other) noexcept -> Handle& {
        if (this != &other) {
            Delete();
            value = std::exchange(other.value, T{});
        }
        return *this;
    }

    [[nodiscard]] auto operator->() -> T* { return &value; }
    [[nodiscard]] auto operator*() -> T& { return value; }
    [[nodiscard]] auto get() const -> const T& { return value; }

private:
    void Delete() {
        if (value) deleter(value);
    }
};

template <typename T>
class pr::LateInit {
    LIBBASE_IMMOVABLE(LateInit);
    alignas(T) mutable std::byte storage[sizeof(T)];
    mutable bool initialised = false;

public:
    LateInit() = default;
    ~LateInit() {
        if (initialised) Ptr()->~T();
    }

    void operator=(T&& value)
    requires std::is_move_assignable_v<T>
    {
        if (not initialised) init(std::move(value));
        else *Ptr() = std::move(value);
    }

    template <typename... Args>
    auto init(Args&&... args) const -> T* {
        if (initialised) Ptr()->~T();
        initialised = true;
        new (storage) T(std::forward<Args>(args)...);
        return Ptr();
    }

    void reset() {
        if (initialised) Ptr()->~T();
        initialised = false;
    }

    [[nodiscard]] auto operator->() const -> T* {
        Assert(initialised, "LateInit not initialised!");
        return Ptr();
    }

    [[nodiscard]] auto operator*() const -> T& {
        Assert(initialised, "LateInit not initialised!");
        return *Ptr();
    }

private:
    auto Ptr() const -> T* { return reinterpret_cast<T*>(std::launder(storage)); }
};

struct pr::Profile {
    std::string name;
    chr::system_clock::time_point start = chr::system_clock::now();
    Profile(std::string name) : name(name) {}
    ~Profile() {
        auto end = chr::system_clock::now();
        auto duration = chr::duration_cast<chr::milliseconds>(end - start);
        std::println("Profile ({}): {}ms", name, duration.count());
    }
};

/// Thread that can be restarted, aborted, and which may return a value.
template <typename ResultType>
class pr::Thread {
    LIBBASE_IMMOVABLE(Thread);
    std::atomic_bool run_flag = false;
    Result<ResultType> result = Error("Thread was aborted");

    // The thread MUST be the last member of this class, otherwise, we
    // might try to write to the result or run flag after they have been
    // destroyed if the thread is joined in the destructor.
    std::jthread thread;

public:
    explicit Thread() = default;

    template <typename Callable, typename... Args>
    requires (not std::is_same_v<Callable, Thread>)
    explicit Thread(Callable&& c, Args&&... args) {
        start(std::forward<Callable>(c), std::forward<Args>(args)...);
    }

    /// Check if the thread is running.
    [[nodiscard]] bool running() const { return run_flag.load(std::memory_order::acquire); }

    /// Start the thread.
    template <typename Callable, typename... Args>
    void start(Callable&& c, Args&&... args) { // clang-format off
        Assert(not running(), "Thread already started!");
        result = Error("Thread was aborted");
        thread = std::jthread{[
            this,
            ...args = std::forward<Args>(args),
            callable = std::forward<Callable>(c)
        ](
            std::stop_token stop
        ) {
            // Stop token goes at the end because the first argument
            // might be 'this' if the callable is a member function.
            result = std::invoke(callable, args..., stop);
            run_flag.store(false, std::memory_order::release);
        }};
        run_flag.store(true, std::memory_order::release);
    } // clang-format on

    /// Stop the thread and unbind it from the current instance.
    void stop_and_release() {
        if (thread.request_stop()) thread.detach();
    }

    /// Get the result value of the thread.
    [[nodiscard]] auto value() -> Result<ResultType>
    requires (not std::is_void_v<ResultType>)
    {
        Assert(not running(), "Thread is still running!");
        return std::move(result);
    }
};

/// Wrapper around a null-terminated string; this is non-owning
/// and should only be used in function parameters.
struct pr::ZTermString {
private:
    std::string_view data;

public:
    constexpr ZTermString() : data("") {}
    constexpr ZTermString(std::string& str) : data(str) {}

    template <usz n>
    constexpr ZTermString(const char (&str)[n]) : data(str, n - 1) {}

    auto c_str() const -> const char* { return data.data(); }
};

template <typename... Args>
void pr::Log(std::format_string<Args...> fmt, Args&&... args) {
    pr::LogImpl(std::format(fmt, std::forward<Args>(args)...));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
template <typename T>
struct std::formatter<pr::Debug<T>> : std::formatter<std::string> {
    template <typename FormatContext>
    auto format(pr::Debug<T> t, FormatContext& ctx) const {
        std::string s;

        auto Formatter = [&](const char* fmt, ...) {
            va_list ap;
            va_start(ap, fmt);
            auto sz = std::vsnprintf(nullptr, 0, fmt, ap);
            va_end(ap);

            s.resize(s.size() + sz + 1);
            va_start(ap, fmt);
            std::vsnprintf(s.data() + s.size() - sz - 1, sz + 1, fmt, ap);
            va_end(ap);
        };

        __builtin_dump_struct(t.value, Formatter);
        return std::formatter<std::string>::format(s, ctx);
    }
};
#pragma clang diagnostic pop

#endif // PRESCRIPTIVISM_SHARED_UTILS_HH
