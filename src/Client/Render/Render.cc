module;
#include <base/Assert.hh>
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
#include <stb_truetype.h>
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

constexpr char IdentityVertexShaderData[]{
#embed "Shaders/Identity.vert"
};

constexpr char IdentityFragmentShaderData[]{
#embed "Shaders/Identity.frag"
};

// =============================================================================
//  OpenGL Wrappers
// =============================================================================
template <auto deleter>
class Descriptor {
protected:
    GLuint descriptor{};
    Descriptor() {}

public:
    Descriptor(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) : descriptor(std::exchange(other.descriptor, 0)) {}
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor& operator=(Descriptor&& other) {
        std::swap(descriptor, other.descriptor);
        return *this;
    }

    ~Descriptor() {
        if (descriptor) {
            if constexpr (requires { deleter(1, &descriptor); }) deleter(1, &descriptor);
            else deleter(descriptor);
        }
    }
};

enum class VertexLayout {
    Position2D, /// vec2f position
};

class VertexArrays;
class VertexBuffer : Descriptor<glDeleteBuffers> {
    friend VertexArrays;

public:
    VertexBuffer() { glGenBuffers(1, &descriptor); }

    /// Binds the buffer.
    void bind() const { glBindBuffer(GL_ARRAY_BUFFER, descriptor); }

    /// Copies data to the buffer.
    void copy_data(std::span<const f32> data, GLenum usage = GL_STATIC_DRAW) {
        bind();
        glBufferData(GL_ARRAY_BUFFER, data.size_bytes(), data.data(), usage);
    }
};

class VertexArrays : Descriptor<glDeleteVertexArrays> {
    VertexLayout layout;

public:
    VertexArrays(VertexLayout layout) : layout{layout} { glGenVertexArrays(1, &descriptor); }

    /// Creates a new buffer and attaches it to the vertex array.
    ///
    /// This function binds both the buffer and the VAO.
    auto add_buffer() -> VertexBuffer {
        VertexBuffer vbo;
        bind();
        glBindBuffer(GL_ARRAY_BUFFER, vbo.descriptor);
        ApplyLayout();
        return vbo;
    }

    /// Binds the vertex array.
    void bind() const { glBindVertexArray(descriptor); }

    /// Unbinds the vertex array.
    void unbind() const { glBindVertexArray(0); }

private:
    void ApplyLayout() {
        switch (layout) {
            case VertexLayout::Position2D:
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                return;
        }

        Unreachable("Invalid vertex layout");
    }
};

class ShaderProgram : Descriptor<glDeleteProgram> {
    struct Shader : Descriptor<glDeleteShader> {
        friend ShaderProgram;
        Shader(GLenum type, std::span<const char> source) {
            descriptor = glCreateShader(type);
            auto size = GLint(source.size());
            auto data = source.data();
            glShaderSource(descriptor, 1, &data, &size);
            glCompileShader(descriptor);

            GLint success;
            glGetShaderiv(descriptor, GL_COMPILE_STATUS, &success);
            if (not success) {
                // Throw this on the heap since it’s huge.
                auto info_log = std::make_unique<char[]>(+GL_INFO_LOG_LENGTH);
                glGetShaderInfoLog(descriptor, +GL_INFO_LOG_LENGTH, nullptr, info_log.get());
                Log("Shader compilation failed: {}", info_log.get());
            }
        }
    };

public:
    ShaderProgram() = default;
    ShaderProgram(
        std::span<const char> vertex_shader_source,
        std::span<const char> fragment_shader_source
    ) {
        Shader vertex_shader(GL_VERTEX_SHADER, vertex_shader_source);
        Shader fragment_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

        descriptor = glCreateProgram();
        glAttachShader(descriptor, vertex_shader.descriptor);
        glAttachShader(descriptor, fragment_shader.descriptor);
        glLinkProgram(descriptor);

        GLint success;
        glGetProgramiv(descriptor, GL_LINK_STATUS, &success);
        if (not success) {
            // Throw this on the heap since it’s huge.
            auto info_log = std::make_unique<char[]>(+GL_INFO_LOG_LENGTH);
            glGetProgramInfoLog(descriptor, +GL_INFO_LOG_LENGTH, nullptr, info_log.get());
            Log("Shader program linking failed: {}", info_log.get());
        }
    }

    /// Set this as the active shader.
    void use() const { glUseProgram(descriptor); }
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

    ShaderProgram default_shader;

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

    // Load the default shader.
    default_shader = ShaderProgram(
        std::span{IdentityVertexShaderData},
        std::span{IdentityFragmentShaderData}
    );

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

    // Draw a triangle.
    f32 points[] { // clang-format off
        -0.5f, -0.5f,
        0.0f, 0.5f,
        0.5f, -0.5f
    }; // clang-format on

    VertexArrays vao{VertexLayout::Position2D};
    VertexBuffer vbo = vao.add_buffer();
    vbo.copy_data(points);
    r.impl->default_shader.use();
    vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);

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
