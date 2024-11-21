#ifndef PRESCRIPTIVISM_CLIENT_RENDER_RENDER_HH
#define PRESCRIPTIVISM_CLIENT_RENDER_RENDER_HH

#include <Client/Render/GL.hh>

#include <Shared/Serialisation.hh>
#include <Shared/Utils.hh>

#include <base/Macros.hh>
#include <base/Text.hh>
#include <freetype/freetype.h>
#include <glm/glm.hpp>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <hb.h>
#include <memory>
#include <stop_token>
#include <unordered_map>
#include <vector>

namespace pr::client {
struct TextCluster;
struct Colour;
struct AABB;
struct xy;

class AssetLoader;
class Renderer;
class Font;
class Text;

enum struct FontSize : u32;
enum struct TextAlign : u8;
enum struct TextStyle : u8;
enum struct Cursor : u32;
enum struct Reflow : u8;
} // namespace pr::client

// =============================================================================
//  Utility Types
// =============================================================================
/// To keep font sizes uniform.
enum struct pr::client::FontSize : base::u32 {
    Small = 6,
    Normal = 12,
    Intermediate = 18,
    Medium = 24,
    Large = 36,
    Huge = 48,
    Title = 96,
    Gargantuan = 144,
};

/// Alignment of shaped text.
///
/// This is only relevant for multiline text; for single-line text,
/// you can specify SingleLine to indicate that.
enum struct pr::client::TextAlign : base::u8 {
    Left,
    Center,
    Right,
    SingleLine = Left,
};

/// Font style.
///
/// Do NOT reorder or change the values of these enumerators.
enum struct pr::client::TextStyle : base::u8 {
    Regular = 0,
    Bold = 1,
    Italic = 2,
    BoldItalic = Bold | Italic,
};

/// How text should be broken across lines.
enum struct pr::client::Reflow : base::u8 {
    None, ///< Do not reflow the text.
    Soft, ///< Allow breaking at whitespace only.
    Hard, ///< Break in the middle of the word as soon as the max size is exceeded.
};

enum struct pr::client::Cursor : base::u32 {
    Default = SDL_SYSTEM_CURSOR_DEFAULT,
    IBeam = SDL_SYSTEM_CURSOR_TEXT,
};

namespace pr::client {
LIBBASE_DEFINE_FLAG_ENUM(TextStyle);
}

struct pr::client::Colour {
    PR_SERIALISE(r8, g8, b8, a8);

    u8 r8{};
    u8 g8{};
    u8 b8{};
    u8 a8{255};

    ComputedProperty(f32, r, r8 / 255.0f);
    ComputedProperty(f32, g, g8 / 255.0f);
    ComputedProperty(f32, b, b8 / 255.0f);
    ComputedProperty(f32, a, a8 / 255.0f);

public:
    /// Create a new colour.
    constexpr Colour() = default;
    constexpr Colour(
        std::integral auto r,
        std::integral auto g,
        std::integral auto b,
        std::integral auto a = 255
    ) : r8(u8(r)), g8(u8(g)), b8(u8(b)), a8(u8(a)) {}
    constexpr Colour(
        std::same_as<f32> auto r,
        std::same_as<f32> auto g,
        std::same_as<f32> auto b,
        std::same_as<f32> auto a = 1
    ) : Colour(u8(r * 255), u8(g * 255), u8(b * 255), u8(a * 255)) {}

    constexpr static auto ABGR(u32 abgr) -> Colour {
        if constexpr (std::endian::native == std::endian::big)
            abgr = std::byteswap(abgr);

        return {
            u8(abgr & 0xFF),
            u8(abgr >> 8 & 0xFF),
            u8(abgr >> 16 & 0xFF),
            u8(abgr >> 24 & 0xFF),
        };
    }

    constexpr static auto RGBA(u32 rgba) -> Colour {
        if constexpr (std::endian::native == std::endian::big)
            rgba = std::byteswap(rgba);

        return {
            u8(rgba >> 24 & 0xFF),
            u8(rgba >> 16 & 0xFF),
            u8(rgba >> 8 & 0xFF),
            u8(rgba & 0xFF),
        };
    }

    constexpr static auto HSLA(f32 h, f32 s, f32 l, f32 alpha) -> Colour {
        f32 a = s * std::min(l, 1 - l);
        auto F = [&](f32 n) {
            f32 k = std::fmod(n + h / 30.f, 12.f);
            return l - a * std::max(-1.f, std::min<f32>({k - 3, 9 - k, 1}));
        };
        return Colour{F(0), F(8), F(4), alpha};
    }

    constexpr auto darken(f32 amount) const -> Colour {
        auto [h, s, l] = hsl();
        return HSLA(h, s, std::max(0.f, l - amount), a);
    }

    constexpr auto hsl() const -> std::tuple<f32, f32, f32> {
        f32 xmin = std::min({r, g, b});
        f32 v = std::max({r, g, b});
        f32 c = v - xmin;
        f32 l = (v + xmin) / 2;
        f32 h = 0;
        f32 s = 0;
        if (c != 0) {
            s = l == 0 or l == 1 ? 0 : (v - l) / std::min(l, 1 - l);
            if (v == r) h = 60 * std::fmod((g - b) / c, 6.f);
            if (v == g) h = 60 * ((b - r) / c + 2);
            if (v == b) h = 60 * ((r - g) / c + 4);
        }
        return {h, s, l};
    }

    constexpr auto lighten(f32 amount) const -> Colour {
        auto [h, s, l] = hsl();
        return HSLA(h, s, std::min(1.f, l + amount), a);
    }

    constexpr auto vec4() const -> glm::vec4 { return {r, g, b, a}; }

    static constinit const Colour White;
    static constinit const Colour Black;
    static constinit const Colour Grey;
};

constexpr pr::client::Colour pr::client::Colour::White = {255, 255, 255, 255};
constexpr pr::client::Colour pr::client::Colour::Black = {0, 0, 0, 255};
constexpr pr::client::Colour pr::client::Colour::Grey = {128, 128, 128, 255};

// XY position that is destructurable.
struct pr::client::xy {
    PR_SERIALISE(x, y);

    i32 x{};
    i32 y{};

    constexpr xy() = default;
    constexpr xy(i32 x, i32 y) : x(x), y(y) {}
    constexpr xy(vec2 v) : x(i32(v.x)), y(i32(v.y)) {}
    constexpr xy(f32 x, f32 y) : x(i32(x)), y(i32(y)) {}
    constexpr xy(f64 x, f64 y) : x(i32(x)), y(i32(y)) {}
    constexpr xy(Size sz) : x(sz.wd), y(sz.ht) {}

    constexpr auto vec() const -> vec2 { return {f32(x), f32(y)}; }

private:
    friend constexpr bool operator==(xy, xy) = default;
    friend constexpr xy operator+(xy a, xy b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr xy operator-(xy a, xy b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr xy operator*(xy a, f32 b) { return {i32(a.x * b), i32(a.y * b)}; }
};

/// Axis-aligned bounding box.
struct pr::client::AABB {
    PR_SERIALISE(min, max);

    xy min;
    xy max;

    constexpr AABB() = default;
    constexpr AABB(i32 x, i32 y, i32 wd, i32 ht) : min(x, y), max(x + wd, y + ht) {}
    constexpr AABB(xy min, xy max) : min(min), max(max) {}
    constexpr AABB(xy pos, Size sz) : min(pos), max(pos.x + sz.wd, pos.y + sz.ht) {}

    /// Check if this box contains a point.
    [[nodiscard]] auto contains(xy) const -> bool;

    /// Get the extent along an axis.
    [[nodiscard]] constexpr auto extent(Axis a) const -> i32 {
        return a == Axis::X ? width() : height();
    }

    /// Grow the box.
    [[nodiscard]] constexpr auto grow(i32 amount) const -> AABB { return grow(amount, amount); }
    [[nodiscard]] constexpr auto grow(i32 x, i32 y) const -> AABB {
        return {min - xy(x, y), max + xy(x, y)};
    }

    /// Get the height of this box.
    [[nodiscard]] constexpr auto height() const -> i32 { return max.y - min.y; }

    /// Get the origin point of this box.
    [[nodiscard]] constexpr auto origin() const -> xy { return min; }

    /// Scale this box.
    [[nodiscard]] constexpr auto scale(f32 amount) const -> AABB {
        return {min, min + xy((max - min) * amount)};
    }

    /// Shrink the box by a given amount.
    [[nodiscard]] constexpr auto shrink(i32 amount) const -> AABB { return shrink(amount, amount); }
    [[nodiscard]] constexpr auto shrink(i32 x, i32 y) const -> AABB {
        return {min + xy(x, y), max - xy(x, y)};
    }

    /// Get the size of this box.
    [[nodiscard]] constexpr auto size() const -> Size { return {width(), height()}; }

    /// Get the width of this box.
    [[nodiscard]] constexpr auto width() const -> i32 { return max.x - min.x; }

    constexpr bool friend operator==(const AABB&, const AABB&) = default;
};

template <>
struct std::formatter<pr::client::Size> : std::formatter<std::string> {
    template <typename FormatContext>
    auto format(pr::client::Size p, FormatContext& ctx) const {
        return std::formatter<std::string>::format(std::format("({}, {})", p.wd, p.ht), ctx);
    }
};

template <>
struct std::formatter<pr::client::xy> : std::formatter<std::string> {
    template <typename FormatContext>
    auto format(pr::client::xy p, FormatContext& ctx) const {
        return std::formatter<std::string>::format(std::format("({}, {})", p.x, p.y), ctx);
    }
};

template <>
struct std::formatter<pr::client::AABB> : std::formatter<std::string> {
    template <typename FormatContext>
    auto format(pr::client::AABB p, FormatContext& ctx) const {
        return std::formatter<std::string>::format(
            std::format("{} -> {}", p.min, p.max),
            ctx
        );
    }
};

// =============================================================================
//  Text
// =============================================================================
/// A fixed-sized font, combined with a HarfBuzz shaper and texture atlas.
class pr::client::Font {
public:
    friend AssetLoader;

private:
    using HarfBuzzFontHandle = Handle<hb_font_t*, hb_font_destroy>;
    using HarfBuzzBufferHandle = Handle<hb_buffer_t*, hb_buffer_destroy>;
    struct Metrics {
        PR_SERIALISE(atlas_index, size, bearing);

        u32 atlas_index;
        vec2 size;
        vec2 bearing;
    };

    /// The renderer that owns this font.
    Readonly(Renderer&, renderer, nullptr);

    /// HarfBuzz font to use for shaping.
    HarfBuzzFontHandle hb_font;

    /// Font to pull characters from.
    FT_Face face;

    /// Cached HarfBuzz buffers to use for shaping.
    std::vector<HarfBuzzBufferHandle> hb_bufs;
    usz hb_buffers_in_use = 0;

    /// Metrics for all glyphs in the font.
    std::unordered_map<FT_UInt, Metrics> glyphs{};

    /// List of glyphs, in order, that have been added to the atlas; this
    /// has the same contents as the map, except it’s ordered.
    std::vector<FT_UInt> glyphs_ordered{};

    /// Memory buffer for texture atlas allocation.
    std::vector<std::byte> atlas_buffer;

    /// Whether the atlas needs to be rebuilt.
    bool dirty = false;

    /// The width and height of a cell in the texture atlas.
    u32 atlas_entry_width{};
    u32 atlas_entry_height{};

    /// The width of the atlas; to prevent glyphs that are already in the
    /// atlas from moving, we always set the atlas width to the maximum row
    /// size.
    u32 atlas_width{};

    /// The number of rows in the texture atlas.
    u32 atlas_rows{};

    /// The number of entries in the atlas.
    u32 atlas_entries{};

    /// The font size.
    Readonly(FontSize, size);

    /// The font style.
    Readonly(TextStyle, style);

    /// Interline skip.
    ///
    /// In multiline text, if the height+depth of a line is less than
    /// this value it will be padded to this height.
    u32 skip{};

    /// Maximum ascender and descender.
    f32 strut_asc{}, strut_desc{};

    /// The atlas texture.
    Texture atlas;

public:
    i32 x_height{};

private:
    Font(FT_Face ft_face, FontSize size, TextStyle style);

public:
    Font() = default;

    /// Get the size of the font texture.
    auto atlas_height() const -> i32;

    /// Get the bold variant of this font.
    auto bold() -> Font&;

    /// Get the italic variant of this font.
    auto italic() -> Font&;

    /// Activate the font for rendering.
    void use() const;

    /// Shape text using this font.
    ///
    /// The resulting object is position-independent and can
    /// be drawn at any coordinates.
    ///
    /// This always reshapes the text and should only be called by code
    /// that manages caching of the shaped text, as well as any code that
    /// cares about properties computed during shaping, such as cluster
    /// information.
    void shape(const Text& text, std::vector<TextCluster>* clusters);

    /// Get the font’s strut height.
    auto strut() const -> i32;
    auto strut_split() const -> std::pair<i32, i32>;

private:
    auto AllocBuffer() -> hb_buffer_t*;
};

/// Information about a segment of shaped text.
struct pr::client::TextCluster {
    i32 index; ///< Cluster index.
    i32 xoffs; ///< X position right before the cluster.

    bool operator<(const TextCluster& rhs) const { return index < rhs.index; }
    bool operator==(const TextCluster& rhs) const { return index == rhs.index; }
};

/// A text object that caches the shaping output and vertices needed
/// to render the text.
class pr::client::Text {
    friend Font;

    /// The alignment of the text.
    Property(TextAlign, align);

    /// The text content as UTF-32.
    Property(std::u32string, content);

    /// The desired width of this text object.
    ///
    /// If this is 0, the text will not be wrapped.
    Property(i32, desired_width, 0);

    /// The font of the text.
    Readonly(Font&, font);

    /// How this text should be broken across lines.
    Property(Reflow, reflow, Reflow::None);

    /// The number of lines in this text.
    ComputedReadonly(i32, lines, _lines);

    /// Whether this text spans multiple lines.
    ComputedReadonly(bool, multiline, _lines > 1);

    /// The style of the text (regular, bold, italic).
    ComputedProperty(TextStyle, style, font.style);

    /// The font size of the text.
    ComputedProperty(FontSize, font_size, font.size);

    /// Check if this is empty.
    ComputedReadonly(bool, empty, content.empty());

    /// The horizontal width of the text.
    ComputedReadonly(f32, width, reshape()._width);

    /// The vertical height of the text above the baseline.
    ComputedReadonly(f32, height, reshape()._height);

    /// The vertical depth of the text below the baseline. This
    /// value is positive.
    ComputedReadonly(f32, depth, reshape()._depth);

    /// The total size of the text, including depth.
    ComputedReadonly(Size, text_size, Size(i32(width), i32(height + depth)));

    /// Internal state cache.
    mutable std::optional<VertexArrays> vertices;
    mutable f32 _width{}, _height{}, _depth{};
    mutable i32 _lines{};

public:
    /// Use Renderer::text() instead if you want the text to be shaped
    /// immediately. These constructors are lazy and only shape the text
    /// when it is drawn or when the text size is queried.
    explicit Text();
    explicit Text(Font& font, std::u32string content, TextAlign align = TextAlign::SingleLine);

    explicit Text(Font& font, std::string_view content, TextAlign align = TextAlign::SingleLine);

    /// Draw the vertices of this text.
    void draw_vertices() const;

    /// Update the text.
    ///
    /// Assign to 'content' instead of calling this directly.
    void set_content(std::string_view new_text);

private:
    auto reshape() const -> const Text&;
};

// =============================================================================
//  Renderer
// =============================================================================
namespace pr::client {
using FTLibraryHandle = Handle<FT_Library, FT_Done_FreeType>;
using FTFaceHandle = Handle<FT_Face, FT_Done_Face>;
using SDLWindowHandle = Handle<SDL_Window*, SDL_DestroyWindow>;
using SDLGLContextStateHandle = Handle<SDL_GLContextState*, SDL_GL_DestroyContext>;
using FontEntry = std::pair<u32, TextStyle>;
}

template <>
struct std::hash<pr::client::FontEntry> {
    auto operator()(pr::client::FontEntry f) const noexcept -> std::size_t {
        using namespace pr;
        return std::hash<u64>{}(u64(f.first) << 32 | u64(f.second));
    }
};

namespace pr::client {
struct FontData {
    std::array<FTLibraryHandle, 4> ft{};
    std::array<FTFaceHandle, 4> ft_face{};
    std::unordered_map<FontEntry, Font> fonts{};
};
} // namespace pr::client

class pr::client::AssetLoader {
    FontData font_data;

    AssetLoader() = default;

public:
    /// Load assets; this is an expensive operation and happens
    /// in a separate thread. This does *NOT* create any OpenGL
    /// objects.
    static auto Create() -> Thread<AssetLoader>;

    /// Finish loading assets.
    void finalise(Renderer& r);

private:
    static auto Load(std::stop_token stop) -> AssetLoader;
    void load(std::stop_token stop);
};

/// A renderer that renders to a window.
class pr::client::Renderer {
    LIBBASE_MOVE_ONLY(Renderer);

    friend AssetLoader;

public:
    class [[nodiscard]] MatrixRAII {
        LIBBASE_IMMOVABLE(MatrixRAII);
        friend Renderer;
        Renderer& r;
        explicit MatrixRAII(Renderer& r) : r(r) {}

    public:
        ~MatrixRAII() { r.matrix_stack.pop_back(); }
    };

private:
    SDLWindowHandle window;
    SDLGLContextStateHandle context;

public:
    ShaderProgram primitive_shader;
    ShaderProgram text_shader;
    ShaderProgram image_shader;
    ShaderProgram throbber_shader;
    ShaderProgram rect_shader;

private:
    FontData font_data;
    std::unordered_map<Cursor, SDL_Cursor*> cursor_cache;
    Cursor active_cursor = Cursor::Default;
    Cursor requested_cursor = Cursor::Default;
    std::vector<mat4> matrix_stack;

public:
    class Frame {
        LIBBASE_IMMOVABLE(Frame);
        Renderer& r;
        friend Renderer;
        explicit Frame(Renderer& r);
    public:
        ~Frame();
    };

    /// Create a new window and renderer.
    Renderer(int initial_wd, int initial_ht, bool set_active = true);

    /// Get the current renderer.
    static auto current() -> Renderer&;

    /// Set the renderer for the current thread.
    static void SetThreadRenderer(Renderer& r);

    /// Whether to render blinking cursors.
    ///
    /// \return True to render the cursor, false to hide it.
    bool blink_cursor();

    /// Clear the screen.
    void clear(Colour c = Colour::White);

    /// Draw a line between two points.
    void draw_line(xy start, xy end, Colour c = Colour::White);

    /// Draw an outline around a rectangle.
    ///
    /// \param pos The position of the bottom left corner of the rectangle.
    /// \param size The size of the rectangle.
    /// \param thickness The thickness of the outline; can be different for x and y.
    /// \param c The colour of the outline.
    void draw_outline_rect(
        xy pos,
        Size size,
        Size thickness = 1,
        Colour c = Colour::White,
        i32 border_radius = 0
    ) {
        draw_outline_rect(
            AABB{pos, size},
            thickness,
            c,
            border_radius
        );
    }

    /// \see draw_outline_rect() above.
    void draw_outline_rect(
        AABB box,
        Size thickness = 1,
        Colour c = Colour::White,
        i32 border_radius = 0
    );

    /// Draw a rectangle at a position in world coordinates.
    void draw_rect(xy pos, Size size, Colour c = Colour::White, i32 border_radius = 0);
    void draw_rect(AABB box, Colour c = Colour::White, i32 border_radius = 0) {
        draw_rect(box.origin(), box.size(), c, border_radius);
    }

    /// Draw text at a position in world coordinates.
    void draw_text(const Text& text, xy pos, Colour c = Colour::White);

    /// Draw a texture at a position in world coordinates.
    ///
    /// \see draw_texture_scaled(), draw_texture_sized()
    void draw_texture(const DrawableTexture& tex, xy pos);

    /// Draw a texture at a position in world coordinates.
    ///
    /// If the texture is smaller than the requested size, the texture
    /// will be scaled down. If the texture is larger than the requested
    /// size, it will be stretched to fill the space instead.
    ///
    /// \see draw_texture(), draw_texture_sized()
    void draw_texture_scaled(const DrawableTexture& tex, xy pos, f32 scale);

    /// Draw a texture at a position in world coordinates.
    ///
    /// If the texture is smaller than the requested size, the texture
    /// will be clamped or tiled, depending on the texture’s settings.
    ///
    /// If the texture is larger than the requested size, only part of
    /// the texture will be drawn.
    ///
    /// \see draw_texture(), draw_texture_scaled()
    void draw_texture_sized(const DrawableTexture& tex, AABB box);

    /// Get a font of a given size.
    auto font(FontSize size, TextStyle style = TextStyle::Regular) -> Font&;

    /// Start a new frame.
    auto frame() -> Frame;

    /// Push a transform matrix.
    ///
    /// For UI elements, prefer calling Widget::push_transform() instead.
    ///
    /// This makes it so that the translation and scaling passed here
    /// are applied to everything drawn until the matrix is popped. The
    /// return value is an RAII object that pops the matrix when it goes
    /// out of scope.
    ///
    /// The transformation will be applied relative to the previous matrix
    /// (i.e. pushing two translation matrices will result in the sum of the
    /// two translations).
    ///
    /// The bottom-most element of the matrix stack is the identity matrix.
    ///
    /// E.g. assuming an initially empty stack, if we push a matrix, draw
    /// a rectangle, then push another matrix, and draw another rectangle,
    ///
    ///     auto _ = renderer.push_matrix({100, 100});
    ///     renderer.draw_rect({0, 0}, {20, 20});
    ///
    ///     auto _ = renderer.push_matrix({50, 50});
    ///     renderer.draw_rect({0, 0}, {20, 20});
    ///
    /// we get two rectangles at (100, 100) and (150, 150) respectively.
    auto push_matrix(xy translate, f32 scale = 1) -> MatrixRAII;

    /// Reload all shaders.
    void reload_shaders();

    /// Set the active cursor.
    void set_cursor(Cursor);

    /// Get the SDL window.
    auto sdl_window() -> SDL_Window* { return window.get(); }

    /// Get the size of the window.
    auto size() -> Size;

    /// Create a text object that can be drawn.
    ///
    /// \param value The text to be shaped.
    /// \param size The font size to use.
    /// \param align The alignment of the text.
    /// \param style The style options of the text (regular, bold, italic).
    /// \param clusters Out parameter for clusters that allow mapping shaped text to original text.
    /// \return The shaped text object.
    auto text(
        std::string_view value = "",
        FontSize size = FontSize::Normal,
        TextStyle style = TextStyle::Regular,
        TextAlign align = TextAlign::Left,
        std::vector<TextCluster>* clusters = nullptr
    ) -> Text { return text(text::ToUTF32(value), size, style, align, clusters); }

    /// As text(), but takes a UTF-32 string.
    auto text(
        std::u32string value,
        FontSize size = FontSize::Normal,
        TextStyle style = TextStyle::Regular,
        TextAlign align = TextAlign::Left,
        std::vector<TextCluster>* clusters = nullptr
    ) -> Text;

    /// Set the active shader.
    void use(ShaderProgram& shader, xy position);

private:
    /// Start/end a frame.
    void frame_end();
    void frame_start();

    /// Set the current cursor.
    void SetCursorImpl();
};

#endif // PRESCRIPTIVISM_CLIENT_RENDER_RENDER_HH
