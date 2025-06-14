#ifndef PRESCRIPTIVISM_CLIENT_RENDER_RENDER_HH
#define PRESCRIPTIVISM_CLIENT_RENDER_RENDER_HH

#include <Client/Render/GL.hh>

#include <Shared/Utils.hh>

#include <base/Macros.hh>
#include <base/Properties.hh>
#include <base/Serialisation.hh>
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
class Font;
class Text;

// =============================================================================
//  Utility Types
// =============================================================================
/// To keep font sizes uniform.
enum struct FontSize : u32 {
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
enum struct TextAlign : u8 {
    Left,
    Center,
    Right,
    SingleLine = Left,
};

/// Font style.
///
/// Do NOT reorder or change the values of these enumerators.
enum struct TextStyle : u8 {
    Regular = 0,
    Bold = 1,
    Italic = 2,
    BoldItalic = Bold | Italic,
};

/// How text should be broken across lines.
enum struct Reflow : u8 {
    None, ///< Do not reflow the text.
    Soft, ///< Allow breaking at whitespace only.
    Hard, ///< Break in the middle of the word as soon as the max size is exceeded.
};

enum struct Cursor : u32 {
    Default = SDL_SYSTEM_CURSOR_DEFAULT,
    IBeam = SDL_SYSTEM_CURSOR_TEXT,
};

LIBBASE_DEFINE_FLAG_ENUM(TextStyle);

struct Colour {
    LIBBASE_SERIALISE(r8, g8, b8, a8);

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

    constexpr auto invert() const -> Colour { return {255 - r8, 255 - g8, 255 - b8, a8}; }

    constexpr auto lighten(f32 amount) const -> Colour {
        auto [h, s, l] = hsl();
        return HSLA(h, s, std::min(1.f, l + amount), a);
    }

    constexpr auto luminosity_invert() const -> Colour {
        auto [h, s, l] = hsl();
        return HSLA(h, s, 1 - l, a);
    }

    constexpr auto vec4() const -> glm::vec4 { return {r, g, b, a}; }

    static constinit const Colour White;
    static constinit const Colour Black;
    static constinit const Colour Grey;
};

constexpr Colour Colour::White = {255, 255, 255, 255};
constexpr Colour Colour::Black = {0, 0, 0, 255};
constexpr Colour Colour::Grey = {128, 128, 128, 255};

// XY position that is destructurable.
//
// FIXME: Rewrite this to and introduce some generic vector types because
// I don’t quite like GLM’s API (e.g. the horrible hack wrt how they are
// made destructurable isn’t necessary if you use properties)...
struct xy {
    LIBBASE_SERIALISE(x, y);

    i32 x{};
    i32 y{};

    constexpr xy() = default;
    constexpr xy(i32 x, i32 y) : x(x), y(y) {}
    constexpr xy(vec2 v) : x(i32(v.x)), y(i32(v.y)) {}
    constexpr xy(f32 x, f32 y) : x(i32(x)), y(i32(y)) {}
    constexpr xy(f64 x, f64 y) : x(i32(x)), y(i32(y)) {}
    constexpr xy(Sz sz) : x(sz.wd), y(sz.ht) {}

    constexpr auto extent(Axis a) const -> i32 { return a == Axis::X ? x : y; }
    constexpr auto vec() const -> vec2 { return {f32(x), f32(y)}; }

private:
    friend constexpr bool operator==(xy, xy) = default;
    friend constexpr xy operator-(xy a) { return {-a.x, -a.y}; }
    friend constexpr xy operator+(xy a, xy b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr xy operator-(xy a, xy b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr xy operator*(xy a, f32 b) { return {i32(a.x * b), i32(a.y * b)}; }
    friend constexpr xy operator/(xy a, f32 b) { return {i32(a.x / b), i32(a.y / b)}; }
    friend constexpr xy& operator+=(xy& a, xy b) { return a = a + b; }
    friend constexpr xy& operator-=(xy& a, xy b) { return a = a - b; }
    friend constexpr xy& operator*=(xy& a, f32 b) { return a = a * b; }
    friend constexpr xy& operator/=(xy& a, f32 b) { return a = a / b; }
};

/// Axis-aligned bounding box.
struct AABB {
    LIBBASE_SERIALISE(min, max);

    xy min;
    xy max;

    constexpr AABB() = default;
    constexpr AABB(i32 x, i32 y, i32 wd, i32 ht) : min(x, y), max(x + wd, y + ht) {}
    constexpr AABB(xy min, xy max) : min(min), max(max) {}
    constexpr AABB(xy pos, Sz sz) : min(pos), max(pos.x + sz.wd, pos.y + sz.ht) {}

    /// Check if this box contains a point.
    [[nodiscard]] auto contains(xy) const -> bool;

    /// Get the end value along an axis.
    [[nodiscard]] constexpr auto end(Axis a) const -> i32 { return a == Axis::X ? max.x : max.y; }

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
    [[nodiscard]] constexpr auto size() const -> Sz { return {width(), height()}; }

    /// Get the width of this box.
    [[nodiscard]] constexpr auto width() const -> i32 { return max.x - min.x; }

    constexpr bool friend operator==(const AABB&, const AABB&) = default;
};

template <typename T>
auto lerp_smooth(T a, T b, f32 t) -> T {
    t = std::clamp(t, 0.f, 1.f);
    t = t * t * (3 - 2 * t);
    return T(a * (1 - t) + b * t);
}

// =============================================================================
//  Text
// =============================================================================
/// A fixed-sized font, combined with a HarfBuzz shaper and texture atlas.
class Font {
public:
    friend AssetLoader;

private:
    using HarfBuzzFontHandle = Handle<hb_font_t*, hb_font_destroy>;
    using HarfBuzzBufferHandle = Handle<hb_buffer_t*, hb_buffer_destroy>;
    struct Metrics {
        LIBBASE_SERIALISE(atlas_index, size, bearing);

        u32 atlas_index;
        vec2 size;
        vec2 bearing;
    };

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
struct TextCluster {
    i32 index; ///< Cluster index.
    i32 xoffs; ///< X position right before the cluster.

    bool operator<(const TextCluster& rhs) const { return index < rhs.index; }
    bool operator==(const TextCluster& rhs) const { return index == rhs.index; }
};

/// A text object that caches the shaping output and vertices needed
/// to render the text.
class Text {
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
    ComputedReadonly(Sz, text_size, Sz(i32(width), i32(height + depth)));

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
using FTLibraryHandle = Handle<FT_Library, FT_Done_FreeType>;
using FTFaceHandle = Handle<FT_Face, FT_Done_Face>;
using SDLWindowHandle = Handle<SDL_Window*, SDL_DestroyWindow>;
using SDLGLContextStateHandle = Handle<SDL_GLContextState*, SDL_GL_DestroyContext>;
using FontEntry = std::pair<u32, TextStyle>;
} // namespace pr::client

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

class AssetLoader {
    FontData font_data;

    AssetLoader() = default;

public:
    /// Load assets; this is an expensive operation and happens
    /// in a separate thread. This does *NOT* create any OpenGL
    /// objects.
    static auto Create() -> Thread<AssetLoader>;

    /// Finish loading assets.
    void finalise();

private:
    static auto Load(std::stop_token stop) -> AssetLoader;
    void load(std::stop_token stop);
};

namespace Renderer {
struct Impl;
}

class [[nodiscard]] MatrixRAII {
    LIBBASE_IMMOVABLE(MatrixRAII);
    friend Renderer::Impl;
    explicit MatrixRAII() {}
public:
    ~MatrixRAII();
};

class [[nodiscard]] Frame {
    LIBBASE_IMMOVABLE(Frame);
    friend Renderer::Impl;
    explicit Frame();
public:
    ~Frame();
};

namespace Renderer {
/// Clear the screen.
void Clear(Colour c = Colour::White);

/// Draw an arrow from one point to another.
void DrawArrow(xy start, xy end, i32 thickness = 2, Colour c = Colour::White);

/// Draw a line between two points.
void DrawLine(xy start, xy end, Colour c = Colour::White);

/// \see draw_outline_rect() below.
void DrawOutlineRect(
    AABB box,
    Sz thickness = 1,
    Colour c = Colour::White,
    i32 border_radius = 0
);

/// Draw an outline around a rectangle.
///
/// \param pos The position of the bottom left corner of the rectangle.
/// \param size The size of the rectangle.
/// \param thickness The thickness of the outline; can be different for x and y.
/// \param c The colour of the outline.
inline void DrawOutlineRect(
    xy pos,
    Sz size,
    Sz thickness = 1,
    Colour c = Colour::White,
    i32 border_radius = 0
) {
    DrawOutlineRect(
        AABB{pos, size},
        thickness,
        c,
        border_radius
    );
}

/// Draw a rectangle at a position in world coordinates.
void DrawRect(xy pos, Sz size, Colour c = Colour::White, i32 border_radius = 0);
inline void DrawRect(AABB box, Colour c = Colour::White, i32 border_radius = 0) {
    DrawRect(box.origin(), box.size(), c, border_radius);
}

/// Draw text at a position in world coordinates.
void DrawText(const Text& text, xy pos, Colour c = Colour::White);

/// Draw a texture at a position in world coordinates.
///
/// \see draw_texture_scaled(), draw_texture_sized()
void DrawTexture(const DrawableTexture& tex, xy pos);

/// Draw a texture at a position in world coordinates.
///
/// If the texture is smaller than the requested size, the texture
/// will be scaled down. If the texture is larger than the requested
/// size, it will be stretched to fill the space instead.
///
/// \see draw_texture(), draw_texture_sized()
void DrawTextureScaled(const DrawableTexture& tex, xy pos, f32 scale);

/// Draw a texture at a position in world coordinates.
///
/// If the texture is smaller than the requested size, the texture
/// will be clamped or tiled, depending on the texture’s settings.
///
/// If the texture is larger than the requested size, only part of
/// the texture will be drawn.
///
/// \see draw_texture(), draw_texture_scaled()
void DrawTextureSized(const DrawableTexture& tex, AABB box);

/// Draw a throbber.
void DrawThrobber(xy pos, f32 radius, f32 rate);

/// Get a font of a given size.
auto GetFont(FontSize size, TextStyle style = TextStyle::Regular) -> Font&;

/// As GetText() below, but takes a UTF-32 string.
auto GetText(
    std::u32string value,
    FontSize size = FontSize::Normal,
    TextStyle style = TextStyle::Regular,
    TextAlign align = TextAlign::Left,
    std::vector<TextCluster>* clusters = nullptr
) -> Text;

/// Create a text object that can be drawn.
///
/// \param value The text to be shaped.
/// \param size The font size to use.
/// \param align The alignment of the text.
/// \param style The style options of the text (regular, bold, italic).
/// \param clusters Out parameter for clusters that allow mapping shaped text to original text.
/// \return The shaped text object.
inline auto GetText(
    std::string_view value = "",
    FontSize size = FontSize::Normal,
    TextStyle style = TextStyle::Regular,
    TextAlign align = TextAlign::Left,
    std::vector<TextCluster>* clusters = nullptr
) -> Text { return GetText(text::ToUTF32(value), size, style, align, clusters); }


/// Get the size of the window.
auto GetWindowSize() -> Sz;

/// Create a new window and renderer.
void Initialise(int initial_wd, int initial_ht);

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
auto PushMatrix(xy translate, f32 scale = 1) -> MatrixRAII;

/// Reload all shaders.
void ReloadAllShaders();

/// Set the active cursor.
void SetActiveCursor(Cursor cursor);

/// Whether to render blinking cursors.
///
/// \return True to render the cursor, false to hide it.
bool ShouldBlinkCursor();

/// Check if we should do any rendering this frame.
auto ShouldRender() -> bool;

/// Free rendering resources.
void ShutdownRenderer();

/// Start a new frame.
auto StartFrame() -> Frame;
} // namespace Renderer
} // namespace pr::client

template <>
struct std::formatter<pr::client::Sz> : std::formatter<std::string> {
    template <typename FormatContext>
    auto format(pr::client::Sz p, FormatContext& ctx) const {
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

#endif // PRESCRIPTIVISM_CLIENT_RENDER_RENDER_HH
