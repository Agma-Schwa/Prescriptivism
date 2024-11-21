#ifndef PRESCRIPTIVISM_CLIENT_RENDER_GL_HH
#define PRESCRIPTIVISM_CLIENT_RENDER_GL_HH

#include <Client/gl-headers.hh>

#include <Shared/Serialisation.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>
#include <base/FS.hh>
#include <glm/gtc/type_ptr.hpp>

#include <utility>

namespace pr::client {
using namespace gl;

struct Size;

class DrawableTexture;
class ShaderProgram;
class Texture;
class VertexArrays;
class VertexBuffer;

using glm::ivec2;
using glm::mat3;
using glm::mat4;
using glm::vec;
using glm::vec1;
using glm::vec2;
using glm::vec3;
using glm::vec4;

enum class Axis : u8;
enum class VertexLayout : u8;

template <glm::length_t size>
using Vertices = std::span<const vec<size, f32>>;

constexpr Axis flip(Axis a);
} // namespace pr::client

template <typename T, std::size_t n>
struct std::formatter<glm::vec<n, T>> : std::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(glm::vec<n, T> vec, FormatContext& ctx) const {
        std::string s{"("};
        s += std::to_string(vec.x);
        if constexpr (n >= 2) s += std::format(", {}", vec.y);
        if constexpr (n >= 3) s += std::format(", {}", vec.z);
        if constexpr (n >= 4) s += std::format(", {}", vec.w);
        s += ")";
        return std::formatter<std::string_view>::format(s, ctx);
    }
};

template <typename T, std::size_t n>
struct pr::ser::Serialiser<glm::vec<n, T>> {
    static void Serialise(Writer& w, const glm::vec<n, T>& vec) {
        for (int i = 0; i < int(n); ++i) w(vec[i]);
    }

    static void Deserialise(Reader& r, glm::vec<n, T>& vec) {
        for (int i = 0; i < int(n); ++i) r(vec[i]);
    }
};

/// Supported vertex layouts.
enum class pr::client::VertexLayout : base::u8 {
    Position2D,        /// vec2f position
    PositionTexture4D, /// vec4f position(xy)+texture(zw)
};

enum class pr::client::Axis : base::u8 {
    X,
    Y,
};

constexpr auto pr::client::flip(Axis a) -> Axis {
    return a == Axis::X ? Axis::Y : Axis::X;
}

struct pr::client::Size {
    PR_SERIALISE(wd, ht);

    i32 wd{};
    i32 ht{};

    constexpr Size() = default;
    constexpr Size(i32 both) : wd(both), ht(both) {}
    constexpr Size(i32 wd, i32 ht) : wd(wd), ht(ht) {}
    constexpr Size(f32 wd, f32 ht) : wd(i32(wd)), ht(i32(ht)) {}
    constexpr Size(u32 wd, u32 ht) : wd(i32(wd)), ht(i32(ht)) {}
    constexpr Size(Axis a, i32 axis_value, i32 other)
        : wd(a == Axis::X ? axis_value : other),
          ht(a == Axis::Y ? axis_value : other) {}

    // Compute the area of this size.
    constexpr auto area() const -> i32 { return wd * ht; }

    // Get this as a vector.
    constexpr auto vec() const -> vec2 { return {wd, ht}; }

private:
    friend constexpr bool operator==(Size, Size) = default;
    friend constexpr auto operator*(Size sz, f32 scale) -> Size {
        return Size(sz.wd * scale, sz.ht * scale);
    }
};

/// Helper to keep track of and delete OpenGL objects.
template <auto deleter>
struct Descriptor {
protected:
    gl::GLuint descriptor{};

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

class pr::client::VertexBuffer : Descriptor<glDeleteBuffers> {
    friend VertexArrays;

    GLenum draw_mode;
    GLsizei size = 0;

    template <typename T>
    VertexBuffer(std::span<const T> data, GLenum draw_mode);

public:
    /// Bind the buffer.
    void bind() const;

    /// Copy data to the buffer.
    void copy_data(Vertices<2> data, GLenum usage = GL_STATIC_DRAW);
    void copy_data(Vertices<3> data, GLenum usage = GL_STATIC_DRAW);
    void copy_data(Vertices<4> data, GLenum usage = GL_STATIC_DRAW);

    /// Draw the buffer.
    void draw() const;

    /// Reserve space in the buffer.
    template <typename T>
    void reserve(std::size_t count, GLenum usage = GL_STATIC_DRAW);

    /// Store data into the buffer. Calling reserve() first is mandatory.
    template <typename T>
    void store(std::span<const T> data);

private:
    template <typename T>
    void CopyImpl(std::span<const T> data, GLenum usage);
};

class pr::client::VertexArrays : Descriptor<glDeleteVertexArrays> {
    VertexLayout layout;
    std::vector<VertexBuffer> buffers;

public:
    VertexArrays(VertexLayout layout);

    /// Creates a new buffer and attaches it to the vertex array.
    auto add_buffer(Vertices<2> data, GLenum draw_mode = GL_TRIANGLES) -> VertexBuffer&;
    auto add_buffer(Vertices<3> data, GLenum draw_mode = GL_TRIANGLES) -> VertexBuffer&;
    auto add_buffer(Vertices<4> data, GLenum draw_mode = GL_TRIANGLES) -> VertexBuffer&;
    auto add_buffer(GLenum draw_mode = GL_TRIANGLES) -> VertexBuffer&;

    /// Binds the vertex array.
    void bind() const;

    /// Draw the vertex array.
    void draw_vertices() const;

    /// Check if this contains no buffers.
    auto empty() const -> bool { return buffers.empty(); }

    /// Unbinds the vertex array.
    void unbind() const;

private:
    template <typename T>
    auto AddBufferImpl(std::span<const T> verts, GLenum draw_mode) -> VertexBuffer&;
    void ApplyLayout();
};

class pr::client::ShaderProgram : Descriptor<glDeleteProgram> {
public:
    ShaderProgram() = default;

    /// Compile a shader.
    static auto Compile(
        std::span<const char> vertex_shader_source,
        std::span<const char> fragment_shader_source
    ) -> Result<ShaderProgram>;

    /// Set a uniform.
    void uniform(ZTermString name, vec2 v);
    void uniform(ZTermString name, vec4 v);
    void uniform(ZTermString name, mat3 m);
    void uniform(ZTermString name, mat4 m);
    void uniform(ZTermString name, f32 f);

    /// Set this as the active shader.
    ///
    /// Prefer to call Renderer::use() instead.
    void use_shader_program_dont_call_this_directly() const { glUseProgram(descriptor); }

private:
    template <typename... T>
    void SetUniform(ZTermString name, auto callable, T... args);
};

/// This is an internal handle to texture data. You probably
/// wand DrawableTexture instead.
class pr::client::Texture : Descriptor<glDeleteTextures> {
    GLenum target{};
    GLenum unit{};
    GLenum format{};
    GLenum type{};
    Readonly(u32, width);
    Readonly(u32, height);
    ComputedReadonly(Size, size, Size(width, height));

public:
    Texture() = default;

    /// Allocate a texture with the given width and height.
    ///
    /// \see DrawableTexture for loading textures from a file.
    Texture(
        const void* data,
        u32 width,
        u32 height,
        GLenum format,
        GLenum type,
        GLenum target = GL_TEXTURE_2D,
        GLenum unit = GL_TEXTURE0,
        bool tile = false
    );

    Texture(
        u32 width,
        u32 height,
        GLenum format,
        GLenum type,
        GLenum target = GL_TEXTURE_2D,
        GLenum unit = GL_TEXTURE0
    ) : Texture(nullptr, width, height, format, type, target, unit) {}

    /// Get the maximum texture size.
    static auto MaxSize() -> GLint;

    /// Bind this texture and make its texture unit active.
    void bind() const;

    /// Write data into the texture at a given offset.
    void write(u32 x, u32 y, u32 width, u32 height, const void* data);
};

class pr::client::DrawableTexture : public Texture {
    VertexArrays vao{VertexLayout::PositionTexture4D};

public:
    DrawableTexture(
        const void* data,
        u32 width,
        u32 height,
        GLenum format,
        GLenum type,
        GLenum target = GL_TEXTURE_2D,
        GLenum unit = GL_TEXTURE0,
        bool tile = false
    );

    DrawableTexture(
        const void* data,
        u32 width,
        u32 height,
        GLenum format,
        GLenum type,
        bool tile
    ) : DrawableTexture(data, width, height, format, type, GL_TEXTURE_2D, GL_TEXTURE0, tile) {}

    /// Create triangle strip texture vertices for a given size.
    auto create_vertices(Size size) const -> std::array<vec4, 4>;

    /// Create triangle strip texture vertices for a given size.
    auto create_vertices_scaled(f32 scale) const -> std::array<vec4, 4>;

    /// Load a texture from a file.
    ///
    /// If the texture cannot be loaded, e.g. because it doesn’t exist
    /// or because it is not a valid image, this returns a builtin default
    /// texture.
    static auto LoadFromFile(fs::PathRef path) -> DrawableTexture;

    /// Draw the texture.
    ///
    /// Prefer to call Renderer::draw_texture() instead.
    void draw_vertices() const;

private:
    static auto MakeVerts(f32 wd, f32 ht, f32 u, f32 v) -> std::array<vec4, 4>;
};

#endif // PRESCRIPTIVISM_CLIENT_RENDER_GL_HH
