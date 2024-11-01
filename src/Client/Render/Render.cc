module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <glbinding-aux/types_to_string.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <hb-ot.h>
#include <hb.h>
#include <libassert/assert.hpp>
#include <memory>
#include <SDL3/SDL.h>

// clang-format off
// Include order matters here!
#include <ft2build.h>
#include FT_FREETYPE_H

#include <glbinding/AbstractFunction.h>
#include <glbinding/FunctionCall.h>
// clang-format on
module pr.client.render;

using namespace gl;
using namespace pr;
using namespace pr::client;

using glm::ivec2;
using glm::mat4;
using glm::vec;
using glm::vec1;
using glm::vec2;
using glm::vec3;
using glm::vec4;

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

#define ftcall FTCallImpl{}->*
struct FTCallImpl {
    void operator->*(FT_Error err) {
        Assert(err == 0, "FreeType call failed: {}", err);
    }
};

constexpr char IdentityVertexShaderData[]{
#embed "Shaders/Identity.vert"
};

constexpr char IdentityFragmentShaderData[]{
#embed "Shaders/Identity.frag"
};

constexpr char TextVertexShaderData[]{
#embed "Shaders/Text.vert"
};

constexpr char TextFragmentShaderData[]{
#embed "Shaders/Text.frag"
};

constexpr char DefaultFontRegular[]{
#embed "NotoSans-Medium.ttf"
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
    Position2D,        /// vec2f position
    PositionTexture4D, /// vec4f position(xy)+texture(zw)
};

template <glm::length_t size>
using Vertices = std::span<const vec<size, f32>>;

class VertexArrays;
class VertexBuffer : Descriptor<glDeleteBuffers> {
    friend VertexArrays;

    GLenum draw_mode;
    GLsizei size = 0;

    VertexBuffer(Vertices<2> data, GLenum draw_mode = GL_TRIANGLES) : draw_mode{draw_mode} {
        glGenBuffers(1, &descriptor);
        copy_data(data);
    }

public:
    /// Bind the buffer.
    void bind() const { glBindBuffer(GL_ARRAY_BUFFER, descriptor); }

    /// Copy data to the buffer.
    void copy_data(Vertices<2> data, GLenum usage = GL_STATIC_DRAW) {
        CopyImpl(data, usage);
    }

    /// Draw the buffer.
    void draw() const {
        bind();
        glDrawArrays(draw_mode, 0, size);
    }

    /// Reserve space in the buffer.
    template <typename T>
    void reserve(std::size_t count, GLenum usage = GL_STATIC_DRAW) {
        bind();
        glBufferData(GL_ARRAY_BUFFER, count * sizeof(T), nullptr, usage);
        size = GLsizei(count);
    }

    /// Store data into the buffer. Calling reserve() first is mandatory.
    template <typename T>
    void store(std::span<const T> data) {
        Assert(data.size() == size, "Data size mismatch");
        bind();
        glBufferSubData(GL_ARRAY_BUFFER, 0, data.size_bytes(), data.data());
    }

private:
    template <typename T>
    void CopyImpl(std::span<const T> data, GLenum usage) {
        bind();
        glBufferData(GL_ARRAY_BUFFER, data.size_bytes(), data.data(), usage);
        size = GLsizei(data.size());
    }
};

class VertexArrays : Descriptor<glDeleteVertexArrays> {
    VertexLayout layout;
    std::vector<VertexBuffer> buffers;

public:
    VertexArrays(VertexLayout layout)
        : layout{layout} { glGenVertexArrays(1, &descriptor); }

    /// Creates a new buffer and attaches it to the vertex array.
    auto add_buffer(Vertices<2> data, GLenum draw_mode = GL_TRIANGLES) -> VertexBuffer& {
        buffers.push_back(VertexBuffer{data, draw_mode});
        auto& vbo = buffers.back();
        bind();
        glBindBuffer(GL_ARRAY_BUFFER, vbo.descriptor);
        ApplyLayout();
        return vbo;
    }

    /// Creates a new buffer and attaches it to the vertex array.
    auto add_buffer(GLenum draw_mode = GL_TRIANGLES) -> VertexBuffer& {
        return add_buffer(Vertices<2>{}, draw_mode);
    }

    /// Binds the vertex array.
    void bind() const { glBindVertexArray(descriptor); }

    /// Draw the vertex array.
    void draw() const {
        bind();
        for (const auto& vbo : buffers) vbo.draw();
    }

    /// Unbinds the vertex array.
    void unbind() const { glBindVertexArray(0); }

private:
    void ApplyLayout() {
        switch (layout) {
            case VertexLayout::Position2D:
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                return;
            case VertexLayout::PositionTexture4D:
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
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

    /// Set a uniform.
    void uniform(ZTermString name, vec4 v) {
        return SetUniform(name, glUniform4f, v.x, v.y, v.z, v.w);
    }

    void uniform(ZTermString name, mat4 m) {
        return SetUniform(name, glUniformMatrix4fv, 1, GL_FALSE, &m[0][0]);
    }

    /// Set this as the active shader.
    void use() const { glUseProgram(descriptor); }

private:
    template <typename... T>
    void SetUniform(ZTermString name, auto callable, T... args) {
        auto u = glGetUniformLocation(descriptor, name.c_str());
        if (u == -1) return;
        callable(u, args...);
    }
};

/// Text to be rendered.
static constexpr u32 FontSize = 96;
static constexpr u32 Chars = 128;
/*class ShapedText {
    friend Renderer;

    VertexArrays vao{VertexLayout::PositionTexture4D};

public:
    struct Char {
        char32_t codepoint;
        f32 x0;
        f32 x1;
    };

    std::vector<Char> characters;

    ShapedText(hb_font_t* font, std::string_view text, i32 font_size) {
        auto buf = hb_buffer_create();
        if (not buf) {
            Log("Failed to create HarfBuzz buffer");
            return;
        }

        // Add the text and compute properties.
        defer { hb_buffer_destroy(buf); };
        hb_buffer_add_utf8(buf, text.data(), int(text.size()), 0, int(text.size()));
        hb_buffer_guess_segment_properties(buf);
        // hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        // hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
        // hb_buffer_set_language(buf, hb_language_from_string("en", -1));

        // Scale the font; HarfBuzz uses integers for position values,
        // so this is used so we can get fractional values out of it.
        static constexpr int Scale = 64;
        hb_font_set_scale(font, font_size * Scale, font_size * Scale);

        // Shape it.
        hb_shape(font, buf, nullptr, 0);
        unsigned count;
        auto info_ptr = hb_buffer_get_glyph_infos(buf, &count);
        auto pos_ptr = hb_buffer_get_glyph_positions(buf, &count);
        auto infos = std::span{info_ptr, count};
        auto positions = std::span{pos_ptr, count};

        // Record character data for later.
        f32 x = 0, y = 0;
        for (unsigned i = 0; i < count; ++i) {
            [[maybe_unused]] auto& info = infos[i];
            auto& pos = positions[i];
            auto& c = characters.emplace_back();
            c.codepoint = char32_t(info.codepoint);
            c.x0 = x + pos.x_offset / f32(Scale);
            x = c.x1 = x + pos.x_advance / f32(Scale);
        }

        // Create VAO.
        auto& vbo = vao.add_buffer();
        vbo.reserve<vec4>(6, GL_DYNAMIC_DRAW);
    }
};*/

// =============================================================================
//  Impl
// =============================================================================
struct Character {
    GLuint texture;
    vec2 size;
    vec2 bearing;
    i32 advance;
};

using HarfBuzzFont = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
using HarfBuzzFace = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>;
struct Renderer::Impl {
    LIBBASE_IMMOVABLE(Impl);

    SDL_Window* window;
    SDL_GLContextState* context;

    ShaderProgram default_shader;
    ShaderProgram text_shader;
    HarfBuzzFace default_face{nullptr, hb_face_destroy};
    HarfBuzzFont default_font{nullptr, hb_font_destroy};
    FT_Library ft;
    FT_Face ft_face;
    std::array<Character, Chars> characters{};

    Impl(int initial_wd, int initial_ht);
    ~Impl();

    // TODO: Colour and position (matrix) as uniforms.
    void draw_text(
        std::string_view,
        i32 x,
        i32 y,
        Colour c
    );

    auto size() -> ivec2 {
        int wd, ht;
        check SDL_GetWindowSize(window, &wd, &ht);
        return {wd, ht};
    }
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

    // After every gl function call (except 'glGetError'), log
    // if there was an error.
    setCallbackMaskExcept(glbinding::CallbackMask::After | glbinding::CallbackMask::Parameters, {"glGetError"});
    glbinding::setAfterCallback([](const glbinding::FunctionCall& call) {
        auto err = glGetError();
        if (err != GL_NO_ERROR) {
            auto msg = [&] {
                switch (err) {
                    case GL_NO_ERROR: return "GL_NO_ERROR";
                    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
                    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
                    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
                    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
                    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
                    default: return "Unknown Error";
                }
            }();

            std::stringstream ss{};
            for (auto& p : call.parameters) {
                if (not ss.view().empty()) ss << ", ";
                ss << p.get();
            }

            Log(
                "OpenGL error {} in {}({}): {}",
                +err,
                call.function->name(),
                ss.str(),
                msg
            );
        }
    });

    // Enable VSync.
    check SDL_GL_SetSwapInterval(1);

    // Load shaders.
    default_shader = ShaderProgram(
        std::span{IdentityVertexShaderData},
        std::span{IdentityFragmentShaderData}
    );

    text_shader = ShaderProgram(
        std::span{TextVertexShaderData},
        std::span{TextFragmentShaderData}
    );

    // Enable blending.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Create a HarfBuzz shaper using the default font.
    auto blob = hb_blob_create(
        DefaultFontRegular,
        sizeof DefaultFontRegular,
        HB_MEMORY_MODE_READONLY,
        nullptr,
        nullptr
    );

    defer { hb_blob_destroy(blob); };
    default_face.reset(hb_face_create(blob, 0));
    default_font.reset(hb_font_create(default_face.get()));
    hb_ot_font_set_funcs(default_font.get());
    Assert(default_face and default_font, "Failed to create HarfBuzz font");

    // Load the font itself.
    ftcall FT_Init_FreeType(&ft);
    ftcall FT_New_Memory_Face(
        ft,
        reinterpret_cast<const FT_Byte*>(DefaultFontRegular),
        sizeof DefaultFontRegular,
        0,
        &ft_face
    );

    // Set the font size.
    FT_Set_Pixel_Sizes(ft_face, 0, FontSize);

    // Generate the font textures.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (u32 c = 0; c < Chars; c++) {
        // Not all chars need to exist in the font.
        if (FT_Load_Char(ft_face, c, FT_LOAD_RENDER) != 0) continue;
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            ft_face->glyph->bitmap.width,
            ft_face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            ft_face->glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        characters[c] = {
            texture,
            {ft_face->glyph->bitmap.width, ft_face->glyph->bitmap.rows},
            {ft_face->glyph->bitmap_left, ft_face->glyph->bitmap_top},
            i32(ft_face->glyph->advance.x)
        };
    }
}

Renderer::Impl::~Impl() {
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft);
}

Renderer::Frame::Frame(Renderer& r) : r(r) {
    r.clear();

    // Mouse capture causes problems if we’re running in a debugger because
    // it means we can’t click on anything else; ImGui likes to think it’s
    // clever and captures the mouse without asking whether we want that, so
    // turn this off again every frame after ImGui has had a chance to do its
    // thing.
    if (libassert::is_debugger_present()) {
        check SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
        check SDL_CaptureMouse(false);
    }
}

Renderer::Frame::~Frame() {
    // Draw a triangle.
    vec2 points[]{// clang-format off
        {-0.5f, -0.5f},
        {0.0f, 0.5f},
        {0.5f, -0.5f}
    }; // clang-format on

    VertexArrays vao{VertexLayout::Position2D};
    vao.add_buffer(points);
    // vao.draw();

    // Draw text.
    constexpr std::string_view InputText = "Elersbd"; // "eééé́ẹ́ẹ́ʒ";
    // ShapedText text(r.impl->default_font.get(), InputText, 96);
    r.impl->draw_text(InputText, 200, 200, Colour{255, 255, 255, 255});

    // Swap buffers.
    check SDL_GL_SwapWindow(r.impl->window);
}

void Renderer::Impl::draw_text(
    std::string_view text,
    i32 x,
    i32 y,
    Colour colour
) {
    // Activate the text shader.
    auto sz = size();
    text_shader.use();
    text_shader.uniform("text_colour", colour.vec4());
    text_shader.uniform("projection", glm::ortho<f32>(0, sz.x, 0, sz.y));
    glActiveTexture(GL_TEXTURE0);

    // Create the VAO for the text.
    VertexArrays vao{VertexLayout::PositionTexture4D};
    auto& vbo = vao.add_buffer();
    vbo.reserve<vec4>(6, GL_DYNAMIC_DRAW);
    vao.bind();

    // Draw each character.
    for (auto c : text) {
        if (usz(c) > characters.size() or characters[c].texture == 0) c = '?';
        f32 xpos = x + characters[c].bearing.x;
        f32 ypos = y - (characters[c].size.y - characters[c].bearing.y);
        f32 w = characters[c].size.x;
        f32 h = characters[c].size.y;

        vec4 verts[]{
            {xpos, ypos + h, 0, 0},
            {xpos, ypos, 0, 1},
            {xpos + w, ypos, 1, 1},
            {xpos, ypos + h, 0, 0},
            {xpos + w, ypos, 1, 1},
            {xpos + w, ypos + h, 1, 0}
        };

        static bool logged = false;
        if (not logged) {
            logged = true;
            Log("V1: {}", verts[0]);
            Log("V2: {}", verts[1]);
            Log("V3: {}", verts[2]);
            Log("V4: {}", verts[3]);
            Log("V5: {}", verts[4]);
            Log("V6: {}", verts[5]);
        }

        // Bind the character's texture.
        glBindTexture(GL_TEXTURE_2D, characters[c].texture);

        // Upload the vertices.
        vbo.store(std::span<const vec4>{verts});
        vao.draw();
        x += characters[c].advance / 64;
    }
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
    auto sz = impl->size();
    glViewport(0, 0, sz.x, sz.y);
    glClearColor(c.red(), c.green(), c.blue(), c.alpha());
    glClear(GL_COLOR_BUFFER_BIT);
}

auto Renderer::frame() -> Frame {
    return Frame(*this);
}
