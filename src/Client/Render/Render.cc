module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <glbinding-aux/types_to_string.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <hb-ft.h>
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

import base.text;

using namespace gl;
using namespace pr;
using namespace pr::client;
using namespace base;

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
#embed PRESCRIPTIVISM_DEFAULT_FONT_PATH
};

// =============================================================================
//  OpenGL Wrappers
// =============================================================================
template <auto deleter>
struct Descriptor {
protected:
    GLuint descriptor{};

    Descriptor() = default;

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
    void copy_data(Vertices<2> data, GLenum usage = GL_STATIC_DRAW) { CopyImpl(data, usage); }
    void copy_data(Vertices<3> data, GLenum usage = GL_STATIC_DRAW) { CopyImpl(data, usage); }
    void copy_data(Vertices<4> data, GLenum usage = GL_STATIC_DRAW) { CopyImpl(data, usage); }

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
    void uniform(ZTermString name, vec2 v) {
        return SetUniform(name, glUniform2f, v.x, v.y);
    }

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

class Texture : Descriptor<glDeleteTextures> {
    GLenum target{};
    GLenum unit{};
    GLenum format{};
    GLenum type{};

public:
    Texture() = default;

    /// Allocate a texture with the given width and height.
    Texture(
        u32 width,
        u32 height,
        GLenum format,
        GLenum type,
        GLenum target = GL_TEXTURE_2D,
        GLenum unit = GL_TEXTURE0
    ) : target{target},
        unit{unit},
        format{format},
        type{type} {

        glGenTextures(1, &descriptor);
        bind();
        glTexImage2D(
            target,
            0,
            format,
            width,
            height,
            0,
            format,
            type,
            nullptr
        );

        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    /// Get the maximum texture size.
    static auto MaxSize() -> GLint {
        GLint max_texture_size;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        return max_texture_size;
    }

    /// Bind this texture and make its texture unit active.
    void bind() const {
        glActiveTexture(unit);
        glBindTexture(target, descriptor);
    }

    /// Write data into the texture at a given offset.
    void write(u32 x, u32 y, u32 width, u32 height, const void* data) {
        glTexSubImage2D(
            target,
            0,
            x,
            y,
            width,
            height,
            format,
            type,
            data
        );
    }
};

// =============================================================================
//  Text and Fonts
// =============================================================================
class Font;

/// Text to be rendered.
class ShapedText {
    friend Renderer;
    friend Font;

    VertexArrays vao;
    explicit ShapedText(VertexArrays vao) : vao{std::move(vao)} {}

public:
    /// Debugging function that dumps the contents of a HarfBuzz buffer;
    /// must be called after shaping.
    static auto DumpHBBuffer(hb_font_t* font, hb_buffer_t* buf) {
        std::string debug;
        debug.resize(10'000);
        hb_buffer_serialize_glyphs(
            buf,
            0,
            hb_buffer_get_length(buf),
            debug.data(),
            u32(debug.size()),
            nullptr,
            font,
            HB_BUFFER_SERIALIZE_FORMAT_TEXT,
            HB_BUFFER_SERIALIZE_FLAG_DEFAULT
        );
        Log("Buffer: {}", debug);
    }

    /// Get the vertices for this text.
    auto verts() const -> const VertexArrays& { return vao; }
};

/// A fixed-sized font, combined with a HarfBuzz shaper and texture atlas.
class Font {
    using HarfBuzzFontHandle = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
    using HarfBuzzBufferHandle = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>;
    struct Metrics {
        vec2 size;
        vec2 bearing;
    };

    /// HarfBuzz font to use for shaping.
    HarfBuzzFontHandle hb_font = {nullptr, hb_font_destroy};
    HarfBuzzBufferHandle hb_buf = {hb_buffer_create(), hb_buffer_destroy};

    /// Metrics for all glyphs in the font.
    std::vector<Metrics> glyphs{};

    /// The width and height of a cell in the texture atlas.
    u32 atlas_entry_width{};
    u32 atlas_entry_height{};

    /// The number of columns and rows in the texture atlas.
    u32 atlas_columns{};
    u32 atlas_rows{};

    /// The font size.
    u32 size{};

    /// The atlas texture.
    Texture atlas;

public:
    Font() = default;
    Font(FT_Face ft_face, u32 size) : size{size} {
        // Set the font size.
        FT_Set_Pixel_Sizes(ft_face, 0, size);

        // Create a HarfBuzz font for it.
        hb_font.reset(hb_ft_font_create(ft_face, nullptr));
        hb_ft_font_set_funcs(hb_font.get());
        Assert(hb_font, "Failed to create HarfBuzz font");

        // Determine the maximum width and height amongst all glyphs.
        for (FT_UInt g = 0; g < FT_UInt(ft_face->num_glyphs); g++) {
            if (FT_Load_Glyph(ft_face, g, FT_LOAD_BITMAP_METRICS_ONLY) != 0) {
                Log("Failed to load glyph #{}", g);
                continue;
            }

            atlas_entry_width = std::max(atlas_entry_width, ft_face->glyph->bitmap.width);
            atlas_entry_height = std::max(atlas_entry_height, ft_face->glyph->bitmap.rows);
        }

        // Determine how many characters we can fit in a single row since an
        // entire font tends to exceed OpenGL’s texture size limits in terms
        // of width.
        atlas_columns = Texture::MaxSize() / atlas_entry_width;
        atlas_rows = u32(std::ceil(f64(ft_face->num_glyphs) / atlas_columns));
        u32 texture_width = atlas_columns * atlas_entry_width;
        u32 texture_height = atlas_rows * atlas_entry_height;

        // Allocate the atlas.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        atlas = Texture(texture_width, texture_height, GL_RED, GL_UNSIGNED_BYTE);
        glyphs.resize(ft_face->num_glyphs);

        // And copy each glyph.
        for (FT_UInt g = 0; g < FT_UInt(ft_face->num_glyphs); g++) {
            // This *should* never fail because we're loading glyphs and
            // not codepoints, but prefer not to crash if it does fail.
            if (FT_Load_Glyph(ft_face, g, FT_LOAD_RENDER) != 0) {
                Log("Failed to load glyph #{}", g);
                continue;
            }

            u32 x = g % atlas_columns;
            u32 y = g / atlas_columns;

            atlas.write(
                x * atlas_entry_width,
                y * atlas_entry_height,
                ft_face->glyph->bitmap.width,
                ft_face->glyph->bitmap.rows,
                ft_face->glyph->bitmap.buffer
            );

            glyphs[g] = {
                {ft_face->glyph->bitmap.width, ft_face->glyph->bitmap.rows},
                {ft_face->glyph->bitmap_left, ft_face->glyph->bitmap_top},
            };
        }
    }

    /// Shape text using this font.
    ///
    /// The resulting object is position-independent and can
    /// be drawn at any coordinates.
    auto shape(std::string_view text) -> ShapedText {
        auto buf = hb_buf.get();
        auto font = hb_font.get();

        // Convert to UTF-32 in canonical decomposition form; this helps
        // with fonts that may not have certain precombined characters, but
        // which *do* support the individual components.
        //
        // Normalisation can fail if the input was nonsense; just render an
        // error string in that case.
        auto norm = Normalise(text::ToUTF32(text), text::NormalisationForm::NFD).value_or(U"<ERROR>");

        // Add the text and compute properties.
        hb_buffer_clear_contents(buf);
        hb_buffer_set_content_type(buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
        hb_buffer_add_utf32(buf, reinterpret_cast<const u32*>(norm.data()), int(norm.size()), 0, int(norm.size()));
        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
        hb_buffer_set_language(buf, hb_language_from_string("en", -1));
        // hb_buffer_guess_segment_properties(buf);

        // Scale the font; HarfBuzz uses integers for position values,
        // so this is used so we can get fractional values out of it.
        static constexpr int Scale = 64;
        hb_font_set_scale(font, size * Scale, size * Scale);

        // Enable the following OpenType features: 'liga'.
        hb_feature_t ligatures{};
        ligatures.tag = HB_TAG('l', 'i', 'g', 'a');
        ligatures.value = 1;
        ligatures.start = HB_FEATURE_GLOBAL_START;
        ligatures.end = HB_FEATURE_GLOBAL_END;

        // Shape the text.
        hb_shape(font, buf, &ligatures, 1);
        unsigned count;
        auto info_ptr = hb_buffer_get_glyph_infos(buf, &count);
        auto pos_ptr = hb_buffer_get_glyph_positions(buf, &count);
        if (not info_ptr or not pos_ptr) count = 0;
        auto infos = std::span{info_ptr, count};
        auto positions = std::span{pos_ptr, count};

        // Compute the vertices for each glyph.
        std::vector<vec4> verts;
        f32 x = 0;
        for (unsigned i = 0; i < count; ++i) {
            auto& info = infos[i];
            auto& pos = positions[i];

            // Note: 'codepoint' here is actually a glyph index in the
            // font after shaping, and not a codepoint.
            auto g = char32_t(info.codepoint);
            auto xoffs = pos.x_offset / f32(Scale);
            auto xadv = pos.x_advance / f32(Scale);
            auto yoffs = pos.y_offset / f32(Scale);

            // Compute the x and y position using the glyph’s metrics and
            // the shaping data provided by HarfBuzz.
            f32 xpos = x + glyphs[g].bearing.x + xoffs;
            f32 ypos = - (glyphs[g].size.y - glyphs[g].bearing.y) + yoffs;
            f32 w = glyphs[g].size.x;
            f32 h = glyphs[g].size.y;

            // Compute the offset of the glyph in the atlas.
            f64 tx = f64(g % atlas_columns) * atlas_entry_width;
            f64 ty = f64(g / atlas_columns) * atlas_entry_height;
            f64 atlas_width = f64(atlas_columns * atlas_entry_width);
            f64 atlas_height = f64(atlas_rows * atlas_entry_height);

            // Compute the uv coordinates of the glyph; note that the glyph
            // is likely smaller than the width of an atlas cell, so perform
            // this calculation in pixels.
            f32 u0 = f32(f64(tx) / atlas_width);
            f32 u1 = f32(f64(tx + w) / atlas_width);
            f32 v0 = f32(f64(ty) / atlas_height);
            f32 v1 = f32(f64(ty + h) / atlas_height);

            // Advance past the glyph.
            x += xadv;

            // Build vertices for the glyph’s position and texture coordinates.
            verts.push_back({xpos, ypos + h, u0, v0});
            verts.push_back({xpos, ypos, u0, v1});
            verts.push_back({xpos + w, ypos, u1, v1});
            verts.push_back({xpos, ypos + h, u0, v0});
            verts.push_back({xpos + w, ypos, u1, v1});
            verts.push_back({xpos + w, ypos + h, u1, v0});
        }

        // Upload the vertices.
        VertexArrays vao{VertexLayout::PositionTexture4D};
        auto& vbo = vao.add_buffer();
        vbo.copy_data(verts);
        vao.draw();
        return ShapedText(std::move(vao));
    }

    /// Activate the font for rendering.
    void use() const { atlas.bind(); }

    /// Get the height and width of a cell in the atlas.
    auto cell_height() const -> u32 { return atlas_entry_height; }
    auto cell_width() const -> u32 { return atlas_entry_width; }

    /// Get the number of rows and columns in the atlas.
    auto columns() const -> u32 { return atlas_columns; }
    auto rows() const -> u32 { return atlas_rows; }

    /// Get the total width and height of the atlas.
    auto height() const -> u32 { return atlas_rows * atlas_entry_height; }
    auto width() const -> u32 { return atlas_columns * atlas_entry_width; }
};

// =============================================================================
//  Impl
// =============================================================================
struct Renderer::Impl {
    LIBBASE_IMMOVABLE(Impl);

    SDL_Window* window;
    SDL_GLContextState* context;

    ShaderProgram default_shader;
    ShaderProgram text_shader;
    FT_Library ft;
    FT_Face ft_face;
    Font default_font;

    Impl(int initial_wd, int initial_ht);
    ~Impl();

    // TODO: Colour as uniform.
    void draw_text(
        const ShapedText& text,
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

    // Load the font.
    ftcall FT_Init_FreeType(&ft);
    ftcall FT_New_Memory_Face(
        ft,
        reinterpret_cast<const FT_Byte*>(DefaultFontRegular),
        sizeof DefaultFontRegular,
        0,
        &ft_face
    );

    default_font = Font(ft_face, 96);
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
    constexpr std::string_view InputText = "EÉÉẸ̣eééé́ẹ́ẹ́ʒffifl";
    static ShapedText text = r.impl->default_font.shape(InputText);
    r.impl->draw_text(text, 20, r.impl->size().y - 200, Colour{255, 255, 255, 255});
    r.impl->draw_text(text, 20, r.impl->size().y - 400, Colour{255, 255, 255, 255});

    // Swap buffers.
    check SDL_GL_SwapWindow(r.impl->window);
}

void Renderer::Impl::draw_text(
    const ShapedText& text,
    i32 x,
    i32 y,
    Colour colour
) {
    // Initialise the text shader.
    auto sz = size();
    text_shader.use();
    text_shader.uniform("text_colour", colour.vec4());
    text_shader.uniform("projection", glm::ortho<f32>(0, sz.x, 0, sz.y));
    text_shader.uniform("position", vec2(x, y));

    // Bind the font atlas.
    default_font.use();

    // Dew it.
    text.vao.draw();
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
