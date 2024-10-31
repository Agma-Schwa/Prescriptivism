module;
#include <base/Macros.hh>
#include <glbinding-aux/debug.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <libassert/assert.hpp>
#include <memory>
#include <SDL3/SDL.h>
module pr.client.render;

using namespace gl;
using namespace pr;
using namespace pr::client;

#define check SDLCallImpl{}->*
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

// ============================================================================
//  Type Converters
// ============================================================================
auto Convert(Colour c) -> ImVec4 {
    ImVec4 res;
    res.x = c.r / 255.f;
    res.y = c.g / 255.f;
    res.z = c.b / 255.f;
    res.w = c.a / 255.f;
    return res;
}

// =============================================================================
//  Impl
// =============================================================================
struct Renderer::Impl {
    LIBBASE_IMMOVABLE(Impl);

    SDL_Window* window;
    SDL_GLContextState* context;
    ImGuiContext* imgui;

    Impl(int initial_wd, int initial_ht);
    ~Impl();
};

Renderer::Impl::Impl(int initial_wd, int initial_ht) {
    check SDL_Init(SDL_INIT_VIDEO);

    // OpenGL 3.3, core profile.
    check SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    check SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    check SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Enable double buffering, 24-bit depth buffer, and 8-bit stencil buffer.
    check SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    check SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    check SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Create the window.
    window = check SDL_CreateWindow(
        "Prescriptivism, the Game",
        initial_wd,
        initial_ht,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    // Create the OpenGL context.
    context = check SDL_GL_CreateContext(window);

    // Initialise OpenGL.
    check SDL_GL_MakeCurrent(window, context);
    glbinding::useCurrentContext();
    glbinding::initialize(SDL_GL_GetProcAddress);
    glbinding::aux::enableGetErrorCallback();

    // Enable VSync.
    check SDL_GL_SetSwapInterval(1);

    // Initialise ImGui.
    IMGUI_CHECKVERSION();
    imgui = check ImGui::CreateContext();
    ImGui::StyleColorsDark();
    check ImGui_ImplSDL3_InitForOpenGL(window, context);
    check ImGui_ImplOpenGL3_Init();

    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
}

Renderer::Impl::~Impl() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imgui);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

Renderer::Frame::Frame(Renderer& r) : r(r) {
    r.clear();

    // Tell ImGui to start a new frame.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Mouse capture causes problems if we’re running in a debugger because
    // it means we can’t click on anything else; ImGui likes to think it’s
    // clever and captures the mouse without asking whether we want that, so
    // turn this off again every frame after ImGui has had a chance to do its
    // thing.
    if (libassert::is_debugger_present()) {
        check SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
        check SDL_CaptureMouse(false);
    }

    // Window for background text.
    int wd, ht;
    check SDL_GetWindowSize(r.impl->window, &wd, &ht);
    ImGui::SetNextWindowPos(ImVec2{0, 0});
    ImGui::SetNextWindowSize(ImVec2{float(wd), float(ht)});
    ImGui::Begin(
        "Debug Text",
        nullptr,
        ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus
    );
}

Renderer::Frame::~Frame() {
    // Close background text window.
    ImGui::End();

    // Render ImGui data.
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Swap buffers.
    check SDL_GL_SwapWindow(r.impl->window);
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
    check SDL_GetWindowSize(impl->window, &wd, &ht);
    glViewport(0, 0, wd, ht);
    glClearColor(c.red(), c.green(), c.blue(), c.alpha());
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw_text(std::string_view text, int x, int y, u32 font_size, Colour c) {
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(),
        ImGui::GetFontSize(),
        ImVec2{float(x), float(y)},
        ImGui::GetColorU32(Convert(c)),
        text.data(),
        text.data() + text.size()
    );
}

auto Renderer::frame() -> Frame {
    return Frame(*this);
}
