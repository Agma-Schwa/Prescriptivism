module;
#include <base/Macros.hh>
#include <glbinding-aux/debug.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <memory>
#include <SDL3/SDL.h>
module pr.client.render;

using namespace gl;
using namespace pr;
using namespace pr::client;

#define sdlcall SDLCallImpl{}->*
struct SDLCallImpl {
    void operator->*(bool cond) {
        if (not cond) Log("SDL call failed: {}", SDL_GetError());
    }

    template <typename T>
    auto operator->*(T* ptr) -> T* {
        if (not ptr) Log("SDL call failed: {}", SDL_GetError());
        return ptr;
    }
};

// =============================================================================
//  Impl
// =============================================================================
struct Renderer::Impl {
    LIBBASE_IMMOVABLE(Impl);

    SDL_Window* window;
    SDL_GLContextState* context;

    Impl(int initial_wd, int initial_ht);
    ~Impl();
};

Renderer::Impl::Impl(int initial_wd, int initial_ht) {
    sdlcall SDL_Init(SDL_INIT_VIDEO);

    // OpenGL 3.3, core profile.
    sdlcall SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    sdlcall SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    sdlcall SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Enable double buffering.
    sdlcall SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Create the window.
    window = sdlcall SDL_CreateWindow(
        "Prescriptivism, the Game",
        initial_wd,
        initial_ht,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    // Create the OpenGL context.
    context = sdlcall SDL_GL_CreateContext(window);

    // Initialise OpenGL.
    sdlcall SDL_GL_MakeCurrent(window, context);
    glbinding::useCurrentContext();
    glbinding::initialize(SDL_GL_GetProcAddress);
    glbinding::aux::enableGetErrorCallback();

    // Enable VSync.
    sdlcall SDL_GL_SetSwapInterval(1);
}

Renderer::Impl::~Impl() {
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

Renderer::Frame::~Frame() {
    SDL_GL_SwapWindow(r.impl->window);
}

// =============================================================================
//  API
// =============================================================================
LIBBASE_DEFINE_HIDDEN_IMPL(Renderer);
auto Renderer::Create(int initial_wd, int initial_ht) -> Renderer {
    Renderer r;
    r.impl = std::make_unique<Impl>(initial_wd, initial_ht);
    return r;
}

void Renderer::clear(Colour c) {
    int wd, ht;
    sdlcall SDL_GetWindowSize(impl->window, &wd, &ht);
    glViewport(0, 0, wd, ht);
    glClearColor(c.red(), c.green(), c.blue(), c.alpha());
    glClear(GL_COLOR_BUFFER_BIT);
}

auto Renderer::frame() -> Frame {
    clear();
    return Frame(*this);
}
