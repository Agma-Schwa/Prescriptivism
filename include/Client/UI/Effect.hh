#ifndef PR_CLIENT_UI_EFFECT_HH
#define PR_CLIENT_UI_EFFECT_HH

#include <Client/Render/Render.hh>

#include <base/Base.hh>

#include <coroutine>

namespace pr::client {
class Animation;
class Effect;
}

/// An effect is something that either 1. takes time to execute
/// and thus has to be stored somewhere to preserve its state, or
/// 2. something that has to happen after another effect is done,
/// which means we need to store it for execution later.
///
/// Concretely, we might e.g. start an animation, and while it is
/// in progress receive a packet from the server that should trigger
/// another animation after the first one is done; both need to be
/// stored in a queue, the former because it takes place over some
/// amount of time, the latter because it has to wait for the former.
///
/// Effects can overlap or block effects later on in the queue from
/// executing, and effects can also block user input entirely.
class pr::client::Effect {
    LIBBASE_IMMOVABLE(Effect);

protected:
    class [[clang::coro_return_type, clang::coro_lifetimebound]] Coroutine {
    public:
        struct promise_type;

    private:
        using handle_type = std::coroutine_handle<promise_type>;
        struct Yield {}; // 'co_yield;' is invalid, so we do 'co_yield {};'.

    public:
        struct promise_type {
            auto get_return_object() noexcept -> Coroutine { return handle_type::from_promise(*this); }

            auto initial_suspend() noexcept -> std::suspend_always { return {}; }
            auto final_suspend() noexcept -> std::suspend_always { return {}; }

            auto yield_value(Yield) noexcept -> std::suspend_always { return {}; }
            void return_void() noexcept {}

            [[noreturn]] void unhandled_exception() noexcept { Unreachable(); }
        };

    private:
        handle_type handle;
        Coroutine(handle_type h) noexcept : handle(h) {}

    public:
        Coroutine(Coroutine const&) = delete;
        Coroutine(Coroutine&& other) noexcept : handle(std::exchange(other.handle, {})) {}
        Coroutine& operator=(Coroutine const&) = delete;
        Coroutine& operator=(Coroutine&& other) noexcept {
            handle = std::exchange(other.handle, {});
            return *this;
        }

        ~Coroutine() noexcept {
            if (handle) handle.destroy();
        }

        /// Check if this is done.
        [[nodiscard]] auto done() const noexcept -> bool { return handle.done(); }

        /// Resume the animation. Returns whether the animation is done.
        bool operator()() noexcept {
            handle.resume();
            return handle.done();
        }
    };

private:
    Coroutine ticker;

public:
    /// This effect should block user interaction with the
    /// screen. Note: ESC to bring up the main menu will still
    /// work.
    bool prevent_user_input = false;

    /// This effect should block other effects from progressing
    /// and rendering. This only blocks effects that come later
    /// on in the effect queue.
    bool blocking = false;

    /// This animation is waiting for some other effect before it
    /// will stop running.
    bool waiting = false;

public:
    Effect(Coroutine ticker) : ticker(std::move(ticker)) {}

    template <typename Callable>
    explicit Effect(Callable c) : ticker([](auto c) -> Coroutine { c();  co_return; }(std::move(c))) {}

public:
    virtual ~Effect() = default;

    /// Check if this animation is done.
    [[nodiscard]] auto done() const -> bool {
        return not waiting and ticker.done();
    }

    /// Tick the animation.
    void tick() {
        if (not ticker.done()) ticker();
    }

    /// Run code when this is removed.
    virtual void on_done() {}
};

/// An effect that also requires drawing something on the screen.
class pr::client::Animation : public Effect {
    chr::milliseconds duration;
    chr::steady_clock::time_point start;

protected:
    Animation(Coroutine ticker, chr::milliseconds duration)
        : Effect{MakeTicker(std::move(ticker))}, duration{duration} {
        start = chr::steady_clock::now();
    }

    template <std::derived_from<Animation> T>
    Animation(void (T::*f)(), chr::milliseconds duration)
        : Animation{Animation::InfiniteLoop(this, f), duration} {}

public:
    /// Render the animation.
    virtual void draw(Renderer&) {}

    /// Get the elapsed time normalised between 0 and 1.
    [[nodiscard]] auto dt() const -> f32 { return dt(duration); }
    [[nodiscard]] auto dt(chr::milliseconds duration) const -> f32 {
        return f32(elapsed().count()) / duration.count();
    }

    /// Get the time that has elapsed since the animation started.
    [[nodiscard]] auto elapsed() const -> chr::milliseconds {
        return chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - start);
    }

private:
    /// This is static because it, unlike MakeTicker(), is called before the Animation
    /// object is fully constructed (because of the delegated constructor call), so a
    /// non-static member function call would technically be UB.
    template <std::derived_from<Animation> T>
    static auto InfiniteLoop(Animation* self, void (T::*f)()) -> Coroutine {
        for (;;) {
            // The downcast needs to happen in here for the same reason.
            (static_cast<T*>(self)->*f)();
            co_yield {};
        }
    }

    auto MakeTicker(Coroutine c) -> Coroutine {
        while (elapsed() <= duration) {
            if (not c.done()) c();
            co_yield {};
        }
    }
};

#endif // PR_CLIENT_UI_EFFECT_HH
