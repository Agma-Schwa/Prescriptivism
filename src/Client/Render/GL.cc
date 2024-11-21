#include <Client/Render/GL.hh>

#include <webp/decode.h>

using namespace gl;
using namespace pr;
using namespace pr::client;

constexpr u8 DefaultTextureData[]{
#embed "default_texture.webp"
};

auto GetDefaultTexture() -> DrawableTexture {
    int wd, ht;
    auto data = WebPDecodeRGBA(DefaultTextureData, sizeof DefaultTextureData, &wd, &ht);
    Assert(data, "Failed to decode embedded image?");
    defer { WebPFree(data); };
    return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE, true);
}

DrawableTexture::DrawableTexture(
    const void* data,
    u32 width,
    u32 height,
    GLenum format,
    GLenum type,
    GLenum target,
    GLenum unit,
    bool tile
) : Texture(data, width, height, format, type, target, unit, tile) {
    vao.add_buffer(create_vertices(Size{width, height}), GL_TRIANGLE_STRIP);
}

auto DrawableTexture::LoadFromFile(fs::PathRef path) -> DrawableTexture {
    auto file = File::Read(path);
    if (not file) {
        Log("{}", file.error());
        return GetDefaultTexture();
    }

    int wd, ht;
    auto data = WebPDecodeRGBA(file.value().data<u8>(), file.value().size(), &wd, &ht);
    if (not data) {
        Log("Could not decode image '{}'", path.string());
        return GetDefaultTexture();
    }

    defer { WebPFree(data); };
    return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE);
}

auto DrawableTexture::MakeVerts(f32 wd, f32 ht, f32 u, f32 v) -> std::array<vec4, 4> {
    return {
        vec4{0, 0, 0, v},
        vec4{wd, 0, u, v},
        vec4{0, ht, 0, 0},
        vec4{wd, ht, u, 0},
    };
}

auto DrawableTexture::create_vertices(Size size) const -> std::array<vec4, 4> {
    return MakeVerts(size.wd, size.ht, f32(size.wd) / width, f32(size.ht) / height);
}

auto DrawableTexture::create_vertices_scaled(f32 scale) const -> std::array<vec4, 4> {
    return MakeVerts(f32(width) * scale, f32(height) * scale, 1, 1);
}

void DrawableTexture::draw_vertices() const {
    bind();
    vao.draw_vertices();
}

struct Shader : Descriptor<glDeleteShader> {
    friend ShaderProgram;
    static auto Compile(GLenum type, std::span<const char> source) -> Result<Shader>;

private:
    Shader() = default;
};

auto Shader::Compile(GLenum type, std::span<const char> source) -> Result<Shader> {
    Shader shader;
    shader.descriptor = glCreateShader(type);
    auto size = GLint(source.size());
    auto data = source.data();
    glShaderSource(shader.descriptor, 1, &data, &size);
    glCompileShader(shader.descriptor);

    GLint success;
    glGetShaderiv(shader.descriptor, GL_COMPILE_STATUS, &success);
    if (not success) {
        // Throw this on the heap since it’s huge.
        auto info_log = std::make_unique<char[]>(+GL_INFO_LOG_LENGTH);
        glGetShaderInfoLog(shader.descriptor, +GL_INFO_LOG_LENGTH, nullptr, info_log.get());
        return Error("Shader compilation failed: {}", info_log.get());
    }

    return std::move(shader);
}

template <typename... T>
void ShaderProgram::SetUniform(ZTermString name, auto callable, T... args) {
    auto u = glGetUniformLocation(descriptor, name.c_str());
    if (u == -1) return;
    callable(u, args...);
}

auto ShaderProgram::Compile(
    std::span<const char> vertex_shader_source,
    std::span<const char> fragment_shader_source
) -> Result<ShaderProgram> {
    auto vertex_shader = Try(Shader::Compile(GL_VERTEX_SHADER, vertex_shader_source));
    auto fragment_shader = Try(Shader::Compile(GL_FRAGMENT_SHADER, fragment_shader_source));

    ShaderProgram program;
    program.descriptor = glCreateProgram();
    glAttachShader(program.descriptor, vertex_shader.descriptor);
    glAttachShader(program.descriptor, fragment_shader.descriptor);
    glLinkProgram(program.descriptor);

    GLint success;
    glGetProgramiv(program.descriptor, GL_LINK_STATUS, &success);
    if (not success) {
        // Throw this on the heap since it’s huge.
        auto info_log = std::make_unique<char[]>(+GL_INFO_LOG_LENGTH);
        glGetProgramInfoLog(program.descriptor, +GL_INFO_LOG_LENGTH, nullptr, info_log.get());
        return Error("Shader program linking failed: {}", info_log.get());
    }

    return std::move(program);
}

void ShaderProgram::uniform(ZTermString name, vec2 v) {
    return SetUniform(name, glUniform2f, v.x, v.y);
}

void ShaderProgram::uniform(ZTermString name, vec4 v) {
    return SetUniform(name, glUniform4f, v.x, v.y, v.z, v.w);
}

void ShaderProgram::uniform(ZTermString name, mat3 m) {
    return SetUniform(name, glUniformMatrix3fv, 1, GL_FALSE, value_ptr(m));
}

void ShaderProgram::uniform(ZTermString name, mat4 m) {
    return SetUniform(name, glUniformMatrix4fv, 1, GL_FALSE, value_ptr(m));
}

void ShaderProgram::uniform(ZTermString name, f32 f) {
    return SetUniform(name, glUniform1f, f);
}

Texture::Texture(
    const void* data,
    u32 width,
    u32 height,
    GLenum format,
    GLenum type,
    GLenum target,
    GLenum unit,
    bool tile
) : target{target},
    unit{unit},
    format{format},
    type{type},
    _width{width},
    _height{height} {
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
        data
    );

    auto param = tile ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(target, GL_TEXTURE_WRAP_S, param);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, param);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

auto Texture::MaxSize() -> GLint {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

void Texture::bind() const {
    glActiveTexture(unit);
    glBindTexture(target, descriptor);
}

void Texture::write(u32 x, u32 y, u32 width, u32 height, const void* data) {
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

template <typename T>
VertexBuffer::VertexBuffer(std::span<const T> data, GLenum draw_mode) : draw_mode{draw_mode} {
    glGenBuffers(1, &descriptor);
    copy_data(data);
}

template <typename T> void
VertexBuffer::reserve(std::size_t count, GLenum usage) {
    bind();
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(T), nullptr, usage);
    size = GLsizei(count);
}

template <typename T>
void VertexBuffer::store(std::span<const T> data) {
    Assert(data.size() == size, "Data size mismatch");
    bind();
    glBufferSubData(GL_ARRAY_BUFFER, 0, data.size_bytes(), data.data());
}

template <typename T>
void VertexBuffer::CopyImpl(std::span<const T> data, GLenum usage) {
    bind();
    glBufferData(GL_ARRAY_BUFFER, data.size_bytes(), data.data(), usage);
    size = GLsizei(data.size());
}

void VertexBuffer::bind() const { glBindBuffer(GL_ARRAY_BUFFER, descriptor); }

void VertexBuffer::copy_data(Vertices<2> data, GLenum usage) { CopyImpl(data, usage); }
void VertexBuffer::copy_data(Vertices<3> data, GLenum usage) { CopyImpl(data, usage); }
void VertexBuffer::copy_data(Vertices<4> data, GLenum usage) { CopyImpl(data, usage); }

void VertexBuffer::draw() const {
    bind();
    glDrawArrays(draw_mode, 0, size);
}

template <typename T>
auto VertexArrays::AddBufferImpl(std::span<const T> verts, GLenum draw_mode) -> VertexBuffer& {
    buffers.push_back(VertexBuffer{verts, draw_mode});
    auto& vbo = buffers.back();
    bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo.descriptor);
    ApplyLayout();
    return vbo;
}

VertexArrays::VertexArrays(VertexLayout layout) : layout{layout} {
    glGenVertexArrays(1, &descriptor);
}

auto VertexArrays::add_buffer(Vertices<2> data, GLenum draw_mode) -> VertexBuffer& {
    return AddBufferImpl(data, draw_mode);
}

auto VertexArrays::add_buffer(Vertices<3> data, GLenum draw_mode) -> VertexBuffer& {
    return AddBufferImpl(data, draw_mode);
}

auto VertexArrays::add_buffer(Vertices<4> data, GLenum draw_mode) -> VertexBuffer& {
    return AddBufferImpl(data, draw_mode);
}

auto VertexArrays::add_buffer(GLenum draw_mode) -> VertexBuffer& {
    return add_buffer(Vertices<2>{}, draw_mode);
}

void VertexArrays::bind() const { glBindVertexArray(descriptor); }

void VertexArrays::draw_vertices() const {
    bind();
    for (const auto& vbo : buffers) vbo.draw();
}

void VertexArrays::unbind() const { glBindVertexArray(0); }

void VertexArrays::ApplyLayout() {
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
