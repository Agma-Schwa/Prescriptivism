#ifndef PR_CLIENT_UI_ANIMATIONS_HH
#define PR_CLIENT_UI_ANIMATIONS_HH

#include <Client/Render/Render.hh>

#include <base/Base.hh>

#include <coroutine>

namespace pr::client {
class Animation;
}

// An animation that can (in theory) be played on any
// screen; it is up to the screen to decide how to
// deal with it when creating it.
//
// Animations are used to both render moving objects
// and also express game logic that goes along with
// them; animations can manage other animations.
class pr::client::Animation {
    LIBBASE_IMMOVABLE(Animation);

public:
    class Coroutine {
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
    /// This animation should block user interaction with the
    /// screen. Note: ESC to bring up the main menu will still
    /// work.
    bool blocking = false;

    /// This animation should block other animations from progressing.
    bool exclusive = false;

    Animation(Coroutine ticker) : ticker(std::move(ticker)) {}
    virtual ~Animation() = default;

    /// Check if this animation is done.
    [[nodiscard]] auto done() const -> bool { return ticker.done(); }

    /// Tick the animation.
    void tick() { ticker(); }

    /// Render the animation.
    virtual void draw(Renderer& r) = 0;
};

#endif // PR_CLIENT_UI_ANIMATIONS_HH
