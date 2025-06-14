#include <Client/Render/Render.hh>

#include <base/FS.hh>
#include <base/Text.hh>
#include <SDL3/SDL.h>
#include <webp/decode.h>

#include <algorithm>
#include <cstring>
#include <hb-ft.h>
#include <hb.h>
#include <memory>
#include <mutex>
#include <ranges>
#include <stop_token>

// clang-format off
// Include order matters here!
#include <Client/UI/UI.hh>


#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>
// clang-format on

using namespace gl;
using namespace pr;
using namespace pr::client;
using namespace base;

// =============================================================================
//  Error handling helpers
// =============================================================================
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

// =============================================================================
//  Renderer implementation
// =============================================================================
/// A renderer that renders to a window.
struct Renderer::Impl {
    LIBBASE_MOVE_ONLY(Impl);

public:
    friend AssetLoader;

    SDLWindowHandle window;
    SDLGLContextStateHandle context;
    ShaderProgram primitive_shader;
    ShaderProgram text_shader;
    ShaderProgram image_shader;
    ShaderProgram throbber_shader;
    ShaderProgram rect_shader;
    FontData font_data;
    std::unordered_map<Cursor, SDL_Cursor*> cursor_cache;
    Cursor active_cursor = Cursor::Default;
    Cursor requested_cursor = Cursor::Default;
    std::vector<mat4> matrix_stack;

    Impl(int initial_wd, int initial_ht);

    void draw_arrow(xy start, xy end, i32 thickness, Colour c);
    void draw_line(xy start, xy end, Colour c);
    void draw_outline_rect(AABB box, Sz thickness, Colour c, i32 border_radius);
    void draw_rect(xy pos, Sz size, Colour c, i32 border_radius);
    void draw_text(const Text& text, xy pos, Colour);
    void draw_texture(const DrawableTexture& tex, xy pos);
    void draw_texture_scaled(const DrawableTexture& tex, xy pos, f32 scale);
    void draw_texture_sized(const DrawableTexture& tex, AABB box);
    void draw_throbber(xy pos, f32 radius, f32 rate);
    auto get_font(FontSize size, TextStyle style ) -> Font&;
    auto frame() -> Frame { return Frame(); }
    void frame_end();
    void frame_start();
    auto push_matrix(xy translate, f32 scale) -> MatrixRAII;
    void reload_shaders();
    auto sdl_window() -> SDL_Window* { return window.get(); }
    void update_cursor();
    void use(ShaderProgram& shader, xy position);
};

namespace {
LateInit<Renderer::Impl> global_renderer;
}

// =============================================================================
//  Assets
// =============================================================================
constexpr char DefaultFontRegular[]{
#embed "Fonts/Regular.ttf"
};

[[maybe_unused]] constexpr char DefaultFontItalic[]{
#embed "Fonts/Italic.ttf"
};

[[maybe_unused]] constexpr char DefaultFontBold[]{
#embed "Fonts/Bold.ttf"
};

[[maybe_unused]] constexpr char DefaultFontBoldItalic[]{
#embed "Fonts/BoldItalic.ttf"
};

struct FontDimensions {
    u32 glyph_count;
    u32 max_width;
    u32 max_height;
};

// These MUST follow the order of the enumerators in 'TextStyle'.
constexpr std::span<const char> Fonts[]{
    DefaultFontRegular,
    DefaultFontBold,
    DefaultFontItalic,
    DefaultFontBoldItalic,
};

// =============================================================================
//  Colours
// =============================================================================
constexpr Colour DefaultBGColour{45, 42, 46, 255};

// =============================================================================
//  Text and Fonts
// =============================================================================
auto DumpHBBuffer(hb_font_t* font, hb_buffer_t* buf) {
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

Font::Font(FT_Face ft_face, FontSize size, TextStyle style)
    : face{ft_face},
      _size{size},
      _style{style} {
    // Set the font size.
    FT_Set_Pixel_Sizes(ft_face, 0, +size);
    f32 em = f32(ft_face->units_per_EM);

    // Compute the interline skip.
    // Note: the interline skip for the font we’re using is absurd
    // if calculated this way, so just do it manually.
    skip = u32(1.2f * +size);
    // skip = u32(ft_face->height / f32(ft_face->units_per_EM) * size);

    // Determine the maximum width and height amongst all glyphs; we
    // need to do this now to ensure that the dimensions of a cell
    // don’t change when we add glyphs; otherwise, we’d be invalidating
    // the uv coordinates of already shaped text.
    //
    // Note: we use the advance width for x and the bounding box for y
    // because that seems to more closely match the data we got from
    // iterating over every glyph in the font and computing the maximum
    // metrics.
    atlas_entry_width = u32(std::ceil(ft_face->max_advance_width / em * +size));
    atlas_entry_height = u32(std::ceil(+size * (ft_face->bbox.yMax - ft_face->bbox.yMin) / em));

    // Create a HarfBuzz font for it.
    auto f = hb_ft_font_create(ft_face, nullptr);
    Assert(f, "Failed to create HarfBuzz font");
    hb_font = f;
    hb_ft_font_set_funcs(hb_font.get());

    // According to the OpenType standard, the typographic ascender
    // and descender should be retrieved from the OS/2 table; other
    // 'ascender' and 'descender' fields may contain garbage.
    auto table = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(ft_face, FT_SFNT_OS2));
    if (table) {
        strut_asc = table->sTypoAscender / em * +size;
        strut_desc = -table->sTypoDescender / em * +size;
    } else {
        strut_asc = ft_face->ascender / em * +size;
        strut_desc = -ft_face->descender / em * +size;
    }
}

auto Font::atlas_height() const -> i32 {
    return i32(atlas_rows * atlas_entry_height);
}

auto Font::bold() -> Font& {
    if (style & TextStyle::Bold) return *this;
    return Renderer::GetFont(size, style | TextStyle::Bold);
}

auto Font::italic() -> Font& {
    if (style & TextStyle::Italic) return *this;
    return Renderer::GetFont(size, style | TextStyle::Italic);
}
void Font::use() const { atlas.bind(); }

auto Font::strut() const -> i32 { return i32(strut_asc + strut_desc); }
auto Font::strut_split() const -> std::pair<i32, i32> { return {strut_asc, strut_desc}; }

auto Font::AllocBuffer() -> hb_buffer_t* {
    Assert(hb_buffers_in_use <= hb_bufs.size());
    if (hb_buffers_in_use == hb_bufs.size()) {
        hb_buffers_in_use++;
        hb_bufs.emplace_back(hb_buffer_create());
        return hb_bufs.back().get();
    }
    return hb_bufs[hb_buffers_in_use++].get();
}

Text::Text()
    : _align{TextAlign::SingleLine},
      _font{&Renderer::GetFont(FontSize::Normal, TextStyle::Regular)} {}

Text::Text(Font& font, std::u32string content, TextAlign align)
    : _align{align}, _content{std::move(content)}, _font{&font} {}

Text::Text(Font& font, std::string_view content, TextAlign align)
    : _align{align}, _content{text::ToUTF32(content)}, _font{&font} {}

void Text::draw_vertices() const { reshape().vertices->draw_vertices(); }

auto Text::reshape() const -> const Text& {
    if (not vertices.has_value()) font.shape(*this, nullptr);
    return *this;
}

void Text::set_content(std::string_view new_text) {
    content = text::ToUTF32(new_text);
}

void Text::set_desired_width(i32 desired) {
    if (desired == _desired_width) return;
    _desired_width = desired;
    if (not vertices or desired < width or multiline) font.shape(*this, nullptr);
}

void Text::set_align(TextAlign new_value) {
    if (_align == new_value) return;
    _align = new_value;
    vertices = std::nullopt;
}

void Text::set_content(std::u32string new_value) {
    if (new_value == _content) return;
    _content = std::move(new_value);
    vertices = std::nullopt;
}

void Text::set_font_size(FontSize new_size) {
    if (_font->size == new_size) return;
    _font = &Renderer::GetFont(new_size, _font->style);
    vertices = std::nullopt;
}

void Text::set_reflow(Reflow new_value) {
    if (_reflow == new_value) return;
    _reflow = new_value;
    if (desired_width != 0) vertices = std::nullopt;
}

void Text::set_style(TextStyle new_value) {
    if (_font->style == new_value) return;
    _font = &Renderer::GetFont(_font->size, new_value);
    vertices = std::nullopt;
}

// =============================================================================
//  Text Shaping
// ============================================================================
// Shape text using this font.
//
// The resulting object is position-independent and can
// be drawn at any coordinates.
//
// This is a complicated multi-step process that works roughly as follows:
//
//   1. Break the input text into lines along hard line breaks ('\n').
//
//   2. Shape each line to determine its width and the glyphs we need.
//
//   3. If we’re reflowing, split any lines that are too long into ‘sublines’,
//      each of which may either have to be reshaped or can reference existing
//      shaping information from step 2.
//
//   4. Determine if we need to add any glyphs to the font atlas and do so.
//
//   5. Convert the shaped physical lines into vertices and upload the vertex data.
void Font::shape(const Text& text, std::vector<TextCluster>* clusters) {
    // Reset text properties in case we end up returning early.
    text.vertices = VertexArrays{VertexLayout::PositionTexture4D};
    text._width = text._height = text._depth = 0;
    text._lines = 0;
    if (text.empty) return;

    // Check that this font has been fully initialised.
    auto font = hb_font.get();
    Assert(font, "Forgot to call finalise()!");

    // Free buffers after we’re done.
    defer { hb_buffers_in_use = 0; };

    // Set the font size.
    FT_Set_Pixel_Sizes(face, 0, +size);

    // Refuse to go below a certain width to save us from major headaches.
    static constexpr i32 MinTextWidth = 40;
    const bool should_reflow = text.reflow != Reflow::None and text.desired_width != 0;
    const f32 desired_width = not should_reflow ? 0 : std::max(text.desired_width, MinTextWidth);

    // Get the glyph information and position from a HarfBuzz buffer.
    auto GetInfo = [](hb_buffer_t* buf, i32 start = 0, i32 end = 0) -> std::pair<std::span<hb_glyph_info_t>, std::span<hb_glyph_position_t>> {
        unsigned count;
        auto info_ptr = hb_buffer_get_glyph_infos(buf, &count);
        auto pos_ptr = hb_buffer_get_glyph_positions(buf, &count);

        // Sanity check.
        if (not info_ptr or not pos_ptr) count = 0;
        auto infos = std::span{info_ptr, count};
        auto positions = std::span{pos_ptr, count};

        // We might only want to reference part of the buffer.
        if (end != 0) {
            infos = infos.subspan(start, std::clamp(end - start, 0, i32(count)));
            positions = positions.subspan(start, std::clamp(end - start, 0, i32(count)));
        }

        return {infos, positions};
    };

    // Shaped data and width of a single physical line.
    struct Line {
        // Buffer that contains the shaped data.
        hb_buffer_t* buf;

        // The width of this line.
        f32 width;

        // If not both 0, this references part of a shaped buffer. Only
        // use the range [start, end) for this line.
        i32 start = 0, end = 0;
    };

    // Shape a single line; we need to do line breaks manually, so
    // we might have to call this multiple times.
    std::vector<Line> lines;
    static constexpr int Scale = 64;
    auto ShapeLine = [&](std::u32string_view line, hb_buffer_t* buf) mutable -> f32 {
        // Add the text and compute properties.
        hb_buffer_clear_contents(buf);
        hb_buffer_set_content_type(buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
        hb_buffer_add_utf32(buf, reinterpret_cast<const u32*>(line.data()), int(line.size()), 0, int(line.size()));
        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
        hb_buffer_set_language(buf, hb_language_from_string("en", -1));
        // hb_buffer_guess_segment_properties(buf);

        // Scale the font; HarfBuzz uses integers for position values,
        // so this is used so we can get fractional values out of it.
        hb_font_set_scale(font, +size * Scale, +size * Scale);

        // Enable an OpenType feature.
        auto Feature = [](auto tag) {
            return hb_feature_t{
                .tag = tag,
                .value = 1,
                .start = HB_FEATURE_GLOBAL_START,
                .end = HB_FEATURE_GLOBAL_END,
            };
        };

        // OpenType feature list.
        std::array features{
            Feature(HB_TAG('l', 'i', 'g', 'a')),
            Feature(HB_TAG('s', 's', '1', '3')),
        };

        // Shape the text.
        hb_shape(font, buf, features.data(), features.size());

        // Compute the width of this line.
        f32 x = 0;
        auto [_, positions] = GetInfo(buf);
        for (const auto& pos : positions) x += pos.x_advance / f32(Scale);
        return x;
    };

    // Shape multiple lines; this performs reflowing if need be.
    auto ShapeLines = [&](std::span<std::u32string_view> lines_to_shape) -> f32 {
        Assert(not lines_to_shape.empty(), "Should have returned earlier");

        // First, shape all lines in any case.
        for (auto l : lines_to_shape) {
            auto buf = AllocBuffer();
            lines.emplace_back(buf, ShapeLine(l, buf));
        }

        // If we don’t need to reflow, we’re done.
        auto max_x = rgs::max_element(lines, {}, &Line::width)->width;
        if (not should_reflow or max_x <= f32(desired_width)) return max_x;

        // Reflow each line that is too wide.
        auto old_lines = std::exchange(lines, {});
        for (auto [lineno, l] : old_lines | vws::enumerate) {
            // Line is narrow enough; retain it as-is.
            if (l.width <= desired_width) {
                lines.push_back(l);
                continue;
            }

            // This is the actual hart part: reflowing the line; fortunately,
            // we may not have to reshape the line since HarfBuzz keeps track
            // of positions that are ‘safe to break’, i.e. where we can split
            // the line w/o having to reshape.
            //
            // The text we’ve been passed might be very long, so start scanning
            // from the front of the line.
            isz last_ws_cluster_index = -1;            // The index in the cluster array of the last whitespace cluster.
            usz start_cluster_index = 0;               // The index in the cluster array of the first cluster of this subline.
            bool force_reshape = false;                // Whether we must reshape the next subline.
            bool line_buffer_referenced = false;       // If set, we must not reuse the buffer for this line.
            f32 x = 0;                                 // The width of the current subline.
            f32 ws_width = 0, ws_advance = 0;          // The width up to (but excluding) the last whitespace character.
            auto source_line = lines_to_shape[lineno]; // Text of the entire line we’re splitting.
            auto [infos, positions] = GetInfo(l.buf);  // Pre-split shaping data for the entire line.

            // Reshape a subline.
            auto AddSubline = [&](bool reshape, usz start, usz end, bool last = false) {
                // Reshape the line, reusing the buffer for this line if it was never referenced.
                if (reshape) {
                    auto si = infos[start].cluster;
                    auto ei = infos[end - 1].cluster;
                    auto buf = last and not line_buffer_referenced ? l.buf : AllocBuffer();
                    auto new_line = ShapeLine(source_line.substr(si, ei - si), buf);
                    lines.emplace_back(buf, new_line);
                    force_reshape = true;
                }

                // Reference the existing shaping data.
                else {
                    line_buffer_referenced = true;
                    lines.emplace_back(l.buf, last ? x : ws_width, i32(start), i32(end));
                    force_reshape = false;
                }
            };

            // Go through all clusters and split this line into sublines.
            for (auto [cluster_index, data] : vws::zip(infos, positions) | vws::enumerate) {
                auto& [info, pos] = data;

                // If this is whitespace and safe to break, remember its position; we
                // check the cluster index directly because whitespace we care about
                // shouldn’t be merged with anything else.
                f32 adv = pos.x_advance / f32(Scale);
                if (source_line[info.cluster] == U' ') {
                    last_ws_cluster_index = cluster_index;
                    ws_width = x;
                    ws_advance = adv;
                }

                // If this character would bring us above the width limit, break here.
                x += adv;
                if (x <= desired_width) continue;

                // Handle the degenerate case of us shaping a *really long* word
                // in a *really narrow* line, in which case we may have to break
                // mid-word. This is only allowed if hard reflow is enabled.
                if (last_ws_cluster_index == -1) {
                    if (text.reflow == Reflow::Soft) continue;
                    ws_width = x - adv; // Referenced by AddSubline().
                    AddSubline(
                        hb_glyph_info_get_glyph_flags(&info) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK,
                        start_cluster_index,
                        usz(cluster_index)
                    );

                    start_cluster_index = cluster_index;
                    x = 0;
                }

                // If we’re not in the middle of a word, break at the last whitespace position; in
                // this case, we assume that it is always safe to break.
                else {
                    AddSubline(force_reshape, start_cluster_index, usz(last_ws_cluster_index));
                    x -= ws_width + ws_advance;
                    start_cluster_index = usz(last_ws_cluster_index + 1);
                }

                last_ws_cluster_index = -1;
                ws_width = 0;
            }

            // We almost certainly have characters left here, since we only break if the line gets
            // too long, in which case there is more text left in to process. Add the last subline.
            AddSubline(force_reshape, start_cluster_index, positions.size(), true);
        }

        // Finally, once again determine the largest line.
        return rgs::max_element(lines, {}, &Line::width)->width;
    };

    // Rebuild the font atlas, adding any new glyphs we need.
    //
    // We do this this way since precomputing the atlas for the
    // entire font is rather expensive in terms of memory usage
    // (over 100MB for a 96pt font), and time (it takes about 5
    // second to build the atlas...).
    auto RebuildAtlas = [&] {
        // Collect all glyphs that are not part of the atlas.
        for (auto& l : lines) {
            auto [infos, _] = GetInfo(l.buf, l.start, l.end);
            for (auto& i : infos) {
                // If the glyph is already in the atlas, we don’t need to do anything here.
                auto g = i.codepoint;
                if (auto it = glyphs.find(i.codepoint); it != glyphs.end()) continue;

                // Load the glyph’s metrics.
                glyphs_ordered.push_back(g);
                if (FT_Load_Glyph(face, g, FT_LOAD_BITMAP_METRICS_ONLY) != 0) {
                    Log("Failed to load glyph #{}", g);
                    glyphs[g] = {};
                    continue;
                }

                glyphs[g] = {
                    .atlas_index = u32(glyphs_ordered.size() - 1),
                    .size = {face->glyph->bitmap.width, face->glyph->bitmap.rows},
                    .bearing = {face->glyph->bitmap_left, face->glyph->bitmap_top},
                };
            }
        }

        // At this point, we know what glyphs we need.
        if (atlas_entries == glyphs_ordered.size()) return;
        defer { atlas_entries = u32(glyphs_ordered.size()); };

        // Determine how many characters we can fit in a single row since an
        // entire font tends to exceed OpenGL’s texture size limits in terms
        // of width.
        auto max_columns = Texture::MaxSize() / atlas_entry_width;
        atlas_rows = u32(std::ceil(f64(glyphs_ordered.size()) / max_columns));
        auto texture_height = atlas_height();
        auto atlas_columns = atlas_width / atlas_entry_width;

        // Allocate memory for the texture.
        atlas_buffer.resize(usz(atlas_width * texture_height));

        // Add new glyphs to the atlas.
        //
        // Old glyphs don’t need to be updated since they don’t move because we
        // keep the atlas *width* constant.
        for (auto [i, glyph_index] : glyphs_ordered | vws::drop(atlas_entries) | vws::enumerate) {
            // This *should* never fail because we're loading glyphs and
            // not codepoints, but prefer not to crash if it does fail.
            if (FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER) != 0) {
                Log("Failed to load glyph #{}", glyph_index);
                continue;
            }

            // Index should start at the first new entry.
            i += atlas_entries;
            u32 row = u32(i) / atlas_columns;
            u32 col = u32(i) % atlas_columns;

            // Copy the glyph’s bitmap data into the atlas.
            // TODO: Optimisation: If the bitmap is empty (i.e. every cell is 0), then
            //       we should not create any vertices for this glyph below.
            for (usz r = 0; r < face->glyph->bitmap.rows; r++) {
                std::memcpy(
                    atlas_buffer.data() + (row * atlas_entry_height + r) * u32(atlas_width) + col * atlas_entry_width,
                    face->glyph->bitmap.buffer + r * face->glyph->bitmap.width,
                    face->glyph->bitmap.width
                );
            }
        }

        // Rebuild the texture.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        atlas = Texture(
            atlas_buffer.data(),
            atlas_width,
            texture_height,
            GL_RED,
            GL_UNSIGNED_BYTE
        );
    };

    // Add the vertices for a line to the vertex buffer.
    std::vector<vec4> verts;
    auto atlas_columns = atlas_width / atlas_entry_width;
    auto AddVertices = [&](const Line& l, f32 xbase, f32 ybase) -> std::pair<f32, f32> {
        auto [infos, positions] = GetInfo(l.buf, l.start, l.end);
        f32 x = xbase;
        f32 line_ht = 0, line_dp = 0;

        // Compute the vertices for each glyph.
        if (clusters) clusters->clear();
        for (auto [info, pos] : vws::zip(infos, positions)) {
            if (clusters) clusters->emplace_back(info.cluster, i32(x));

            // Note: 'codepoint' here is actually a glyph index in the
            // font after shaping, and not a codepoint.
            auto& g = glyphs.at(u32(info.codepoint));
            f32 xoffs = pos.x_offset / f32(Scale);
            f32 xadv = pos.x_advance / f32(Scale);
            f32 yoffs = pos.y_offset / f32(Scale);

            // Compute the x and y position using the glyph’s metrics and
            // the shaping data provided by HarfBuzz.
            f32 desc = g.size.y - g.bearing.y;
            f32 xpos = x + g.bearing.x + xoffs;
            f32 ypos = ybase + yoffs - desc;
            f32 w = g.size.x;
            f32 h = g.size.y;

            // Compute the offset of the glyph in the atlas.
            f64 tx = f64(g.atlas_index % atlas_columns) * atlas_entry_width;
            f64 ty = f64(g.atlas_index / atlas_columns) * atlas_entry_height;

            // Compute the uv coordinates of the glyph; note that the glyph
            // is likely smaller than the width of an atlas cell, so perform
            // this calculation in pixels.
            //
            // The atlas width is constant, so we can factor it into the U
            // coordinate’s calculation here and now. On the other hand, the
            // V coordinate may have to change since the atlas *height* may
            // change; we deal with this by computing the actual V coordinate
            // in the vertex shader by passing the current atlas height as
            // a uniform, for which reason we encode absolute V coordinates
            // here.
            f32 u0 = f32(f64(tx) / atlas_width);
            f32 u1 = f32(f64(tx + w) / atlas_width);
            f32 v0 = f32(ty);
            f32 v1 = f32(ty + h);

            // Advance past the glyph.
            x += xadv;
            line_ht = std::max(line_ht, yoffs - desc + h);
            line_dp = std::max(line_dp, desc);

            // Build vertices for the glyph’s position and texture coordinates.
            verts.push_back({xpos, ypos + h, u0, v0});
            verts.push_back({xpos, ypos, u0, v1});
            verts.push_back({xpos + w, ypos, u1, v1});
            verts.push_back({xpos, ypos + h, u0, v0});
            verts.push_back({xpos + w, ypos, u1, v1});
            verts.push_back({xpos + w, ypos + h, u1, v0});
        }

        return {line_ht, line_dp};
    };

    // Shape (and if need be reflow) the lines.
    auto lines_to_shape = u32stream{text.content}.lines() | vws::transform(&u32stream::text) | rgs::to<std::vector>();
    if (lines_to_shape.empty()) return;
    f32 max_x = ShapeLines(lines_to_shape);
    text._lines = i32(lines.size());

    // Rebuild the texture atlas.
    RebuildAtlas();

    // Finally, add vertices for each line.
    f32 ybase = 0;
    f32 ht = 0, dp = 0;
    f32 last_line_skip = 0;
    for (const auto& line : lines) {
        // Determine the starting X position for this line.
        f32 xbase = [&] -> f32 {
            switch (text.align) {
                case TextAlign::Left: return 0;
                case TextAlign::Center: return (max_x - line.width) / 2;
                case TextAlign::Right: return max_x - line.width;
            }
            Unreachable();
        }();

        // Build the vertices for this line.
        auto [line_ht, line_dp] = AddVertices(line, xbase, ybase);

        // Update the total height and width.
        if (ybase == 0) {
            ht = line_ht;
            dp = line_dp;
        } else {
            dp += line_ht + line_dp + last_line_skip;
        }

        // Add interline skip before the next line.
        f32 skip_amount = std::max(line_ht + line_dp, f32(skip));
        last_line_skip = skip_amount - line_ht + line_dp;
        ybase -= skip_amount;
    }

    // And upload the vertices.
    text.vertices->add_buffer().copy_data(verts);
    text._width = max_x;
    text._height = ht;
    text._depth = dp;
}

// =============================================================================
//  Initialisation
// =============================================================================
Renderer::Impl::Impl(int initial_wd, int initial_ht) {
    check SDL_Init(SDL_INIT_VIDEO);
    std::atexit(SDL_Quit);

    // OpenGL 3.3, core profile.
    check SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    check SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    check SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Enable double buffering, 24-bit depth buffer, and 8-bit stencil buffer.
    check SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    check SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    check SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Enable multisampling.
    check SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);

    // We want to set the sample count to the maximum value the GPU supports,
    // but to query the value, we need to create a window first! In conclusion,
    // we need to create the window and context twice here.
    auto CreateWindowAndContext = [&](bool hidden) {
        auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
        if (hidden) flags |= SDL_WINDOW_HIDDEN;

        // Create the window.
        window = check SDL_CreateWindow(
            "Prescriptivism, the Game",
            initial_wd,
            initial_ht,
            flags
        );

        // Create the OpenGL context.
        context = check SDL_GL_CreateContext(*window);

        // Initialise OpenGL.
        check SDL_GL_MakeCurrent(*window, *context);
    };

    // Create a dummy window and context to query the sample count.
    CreateWindowAndContext(true);
    glbinding::useCurrentContext();
    glbinding::initialize(SDL_GL_GetProcAddress);
    GLint max_samples{};
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);

    // Now that we have the sample count, create the actual window and context.
    Log("Using {}x multisampling", max_samples);
    check SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, max_samples);
    CreateWindowAndContext(false);

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
    reload_shaders();

    // Enable blending, smooth lines, and multisampling.
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_MULTISAMPLE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Push initial matrix.
    matrix_stack.push_back(mat4(1.f));
}

void Renderer::Initialise(int initial_wd, int initial_ht) {
    global_renderer.init(initial_wd, initial_ht);
}

Frame::Frame() { global_renderer->frame_start(); }
Frame::~Frame() { global_renderer->frame_end(); }

MatrixRAII::~MatrixRAII() {
    global_renderer->matrix_stack.pop_back();
}

// =============================================================================
//  Drawing
// =============================================================================
void Renderer::Clear(Colour c) {
    auto [sx, sy] = GetWindowSize();
    glViewport(0, 0, sx, sy);
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::DrawArrow(xy start, xy end, i32 thickness, Colour c) {
    global_renderer->draw_arrow(start, end, thickness, c);
}

void Renderer::Impl::draw_arrow(xy start_pos, xy end_pos, i32 thickness, Colour c) {
    use(primitive_shader, {});
    primitive_shader.uniform("in_colour", c.vec4());

    // A thickness of 1 doesn’t work w/ our algorithm that extrudes
    // halfway to either side, so clamp it to at least 2.
    thickness = std::max(thickness, 2);

    // Compute the points for the arrow head, taking into account
    // the thickness of the line. For this, we compute perpendicular
    // vectors, extrude outwards, and then back towards the start
    // position.
    //
    // Furthermore, the tip of the arrow has to extrude outwards
    // from the actual arrow head since it tapers off, so move the
    // actual end of the arrow back a bit.
    static constexpr f32 HeadSz = 2.f;
    auto start = start_pos.vec();
    auto end = end_pos.vec();
    auto dir = glm::normalize(end - start);
    auto head_end = end;
    auto n1 = vec2(-dir.y, dir.x);
    auto n2 = vec2(dir.y, -dir.x);
    auto h1 = end + n1 * (thickness * HeadSz) - dir * (thickness * HeadSz);
    auto h2 = end + n2 * (thickness * HeadSz) - dir * (thickness * HeadSz);
    end -= dir * (thickness * HeadSz);

    // We use a rectangle instead of a GL line for the shaft because
    // this makes drawing the arrow head easier.
    auto a1 = start + n1 * (thickness / 2.f);
    auto a2 = start + n2 * (thickness / 2.f);
    auto a3 = end + n2 * (thickness / 2.f);
    auto a4 = end + n1 * (thickness / 2.f);
    VertexArrays vao{VertexLayout::Position2D};
    vec2 verts[]{
        // start -> end
        a1,
        a2,
        a3,
        a3,
        a4,
        a1,
        // head
        head_end,
        h1,
        h2,
    };

    vao.add_buffer(verts, GL_TRIANGLES);
    vao.draw_vertices();
}

void Renderer::DrawLine(xy start, xy end, Colour c) {
    global_renderer->draw_line(start, end, c);
}

void Renderer::Impl::draw_line(xy start, xy end, Colour c) {
    glLineWidth(1);
    use(primitive_shader, {});
    primitive_shader.uniform("in_colour", c.vec4());
    VertexArrays vao{VertexLayout::Position2D};
    vec2 verts[]{start.vec(), end.vec()};
    vao.add_buffer(verts, GL_LINES);
    vao.draw_vertices();
}

void Renderer::DrawOutlineRect(AABB box, Sz thickness, Colour c, i32 border_radius) {
    return global_renderer->draw_outline_rect(box, thickness, c, border_radius);
}

void Renderer::Impl::draw_outline_rect(
    AABB box,
    Sz thickness,
    Colour c,
    i32 border_radius
) {
    box = box.grow(thickness.wd, thickness.ht);
    auto pos = box.origin();
    auto size = box.size();
    auto [wd, ht] = size;
    auto [tx, ty] = thickness;
    use(rect_shader, pos);
    rect_shader.uniform("in_colour", c.vec4());
    rect_shader.uniform("size", size.vec());
    rect_shader.uniform("radius", border_radius);
    VertexArrays vao{VertexLayout::Position2D};

    // Draw four rectangles around the original rectangle.
    //
    // These vertices actually draw a rectangle *inside* 'box', but
    // we grow 'box' by the thickness of the outline, so the result
    // is an outline around what the user passed in.
    //
    // We do it this way because the rectangle shader can only draw
    // the inside of a rectangle.
    vec2 verts[]{
        // Left, inner.
        {0, ty},
        {tx, ty},
        {tx, ht - ty},
        {tx, ht - ty},
        {0, ty},
        {0, ht - ty},

        // Right, inner.
        {wd - tx, 0},
        {wd, 0},
        {wd - tx, ht},
        {wd - tx, ht},
        {wd, 0},
        {wd, ht},

        // Top, outer.
        {0, ht - ty},
        {0, ht},
        {wd, ht - ty},
        {wd, ht - ty},
        {0, ht},
        {wd, ht},

        // Bottom, outer.
        {0, 0},
        {0, ty},
        {wd, ty},
        {wd, ty},
        {0, 0},
        {wd, 0},
    };

    vao.add_buffer(verts, GL_TRIANGLES);
    vao.draw_vertices();
}

void Renderer::DrawRect(xy pos, Sz size, Colour c, i32 border_radius) {
    return global_renderer->draw_rect(pos, size, c, border_radius);
}

void Renderer::Impl::draw_rect(xy pos, Sz size, Colour c, i32 border_radius) {
    use(rect_shader, pos);
    rect_shader.uniform("in_colour", c.vec4());
    rect_shader.uniform("size", size.vec());
    rect_shader.uniform("radius", border_radius);
    VertexArrays vao{VertexLayout::Position2D};
    vec2 verts[]{
        {0, 0},
        {size.wd, 0},
        {0, size.ht},
        {size.wd, size.ht}
    };
    vao.add_buffer(verts, GL_TRIANGLE_STRIP);
    vao.draw_vertices();
}

void Renderer::DrawText(const Text& text, xy pos, Colour c) {
    global_renderer->draw_text(text, pos, c);
}

void Renderer::Impl::draw_text(
    const Text& text,
    xy pos,
    Colour colour
) {
    if (text.empty) return;

    // Initialise the text shader.
    use(text_shader, pos);
    text_shader.uniform("text_colour", colour.vec4());
    text_shader.uniform("atlas_height", text.font.atlas_height());

    // Bind the font atlas.
    text.font.use();

    // Dew it.
    text.draw_vertices();
}

void Renderer::DrawTexture(const DrawableTexture& tex, xy pos) {
    global_renderer->draw_texture(tex, pos);
}

void Renderer::Impl::draw_texture(
    const DrawableTexture& tex,
    xy pos
) {
    use(image_shader, pos);
    tex.draw_vertices();
}

void Renderer::DrawTextureScaled(const DrawableTexture& tex, xy pos, f32 scale) {
    global_renderer->draw_texture_scaled(tex, pos, scale);
}

void Renderer::Impl::draw_texture_scaled(const DrawableTexture& tex, xy pos, f32 scale) {
    use(image_shader, pos);
    tex.bind();
    VertexArrays vao{VertexLayout::PositionTexture4D};
    vao.add_buffer(tex.create_vertices_scaled(scale), GL_TRIANGLE_STRIP);
    vao.draw_vertices();
}

void Renderer::DrawTextureSized(const DrawableTexture& tex, AABB box) {
    global_renderer->draw_texture_sized(tex, box);
}

void Renderer::Impl::draw_texture_sized(const DrawableTexture& tex, AABB box) {
    use(image_shader, box.origin());
    tex.bind();
    VertexArrays vao{VertexLayout::PositionTexture4D};
    vao.add_buffer(tex.create_vertices(box.size()), GL_TRIANGLE_STRIP);
    vao.draw_vertices();
}

void Renderer::DrawThrobber(xy pos, f32 radius, f32 rate) {
    global_renderer->draw_throbber(pos, radius, rate);
}

void Renderer::Impl::draw_throbber(xy pos, f32 radius, f32 rate) {
    VertexArrays vao(VertexLayout::Position2D);
    vec2 verts[]{
        {-radius, -radius},
        {-radius, radius},
        {radius, -radius},
        {radius, radius}
    };
    vao.add_buffer(verts, GL_TRIANGLE_STRIP);

    auto rads = f32(glm::radians(fmod(360 * rate - SDL_GetTicks(), 360 * rate) / rate));
    auto xfrm = glm::identity<mat4>();
    xfrm = translate(xfrm, vec3(radius, radius, 0));
    xfrm = rotate(xfrm, rads, vec3(0, 0, 1));

    use(throbber_shader, {});
    throbber_shader.uniform("position", pos.vec());
    throbber_shader.uniform("rotation", xfrm);
    throbber_shader.uniform("r", radius);

    vao.draw_vertices();
}


void Renderer::Impl::frame_end() {
    // Swap buffers.
    check SDL_GL_SwapWindow(*window);
}

void Renderer::Impl::frame_start() {
    Clear(DefaultBGColour);

    // Disable mouse capture if the debugger is running.
    if (libassert::is_debugger_present()) {
        check SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
        check SDL_CaptureMouse(false);
    }

    // Set the active cursor if it has changed.
    if (requested_cursor != active_cursor) {
        active_cursor = requested_cursor;
        update_cursor();
    }
}

void Renderer::Impl::use(ShaderProgram& shader, xy position) {
    auto [sx, sy] = GetWindowSize();
    shader.use_shader_program_dont_call_this_directly();

    auto m = matrix_stack.back();
    m = glm::translate(m, {position.x, position.y, 0});
    m = glm::ortho<f32>(0, sx, 0, sy) * m;

    shader.uniform("transform", m);
}

// =============================================================================
//  Creating Objects
// =============================================================================
auto Renderer::GetFont(FontSize size, TextStyle style) -> Font& {
    return global_renderer->get_font(size, style);
}

auto Renderer::Impl::get_font(FontSize size, TextStyle style) -> Font& {
    // Make sure the font exists.
    auto it = font_data.fonts.find({+size, style});
    Assert(
        it != font_data.fonts.end(),
        "Font with size {} and style {} has not been built yet. Make sure to "
        "load this font size in the asset loader (AssetLoader::load()) if you "
        "want to use it",
        +size,
        +style
    );

    return it->second;
}

auto Renderer::PushMatrix(xy translate, f32 scale) -> MatrixRAII {
    return global_renderer->push_matrix(translate, scale);
}

auto Renderer::Impl::push_matrix(xy translate, f32 scale) -> MatrixRAII {
    auto m = matrix_stack.back();
    m = glm::translate(m, {translate.x, translate.y, 0});
    m = glm::scale(m, {scale, scale, 1});
    matrix_stack.push_back(m);
    return MatrixRAII{};
}

void Renderer::ReloadAllShaders() {
    global_renderer->reload_shaders();
}

void Renderer::Impl::reload_shaders() {
    auto ReloadImpl = [&](ShaderProgram& program, std::string_view shader_name) -> Result<> {
        auto vert = Try(File::Read(std::format("./assets/Shaders/{}.vert", shader_name)));
        auto frag = Try(File::Read(std::format("./assets/Shaders/{}.frag", shader_name)));
        program = Try(ShaderProgram::Compile(vert.view(), frag.view()));
        return {};
    };

    auto Reload = [&](ShaderProgram& program, std::string_view shader_name) {
        auto res = ReloadImpl(program, shader_name);
        if (not res) Log("Error loading shader '{}': {}", shader_name, res.error());
    };

    Log("Loading shaders...");
    Reload(primitive_shader, "Primitive");
    Reload(text_shader, "Text");
    Reload(image_shader, "Image");
    Reload(throbber_shader, "Throbber");
    Reload(rect_shader, "Rectangle");
}

void Renderer::SetActiveCursor(Cursor c) {
    // Rather than actually setting the cursor, we register the
    // change and set it at the start of the next frame; this allows
    // us to avoid flicker in case a cursor change is requested multiple
    // times per frame and thus also makes that possible as a pattern.
    global_renderer->requested_cursor = c;
}

bool Renderer::ShouldRender() {
    return not(SDL_GetWindowFlags(global_renderer->window.get()) & SDL_WINDOW_MINIMIZED);
}

void Renderer::ShutdownRenderer() {
    global_renderer.reset();
}

auto Renderer::StartFrame() -> Frame {
    return global_renderer->frame();
}

auto Renderer::GetText(
    std::u32string value,
    FontSize size,
    TextStyle style,
    TextAlign align,
    std::vector<TextCluster>* clusters
) -> Text {
    Text t{GetFont(size, style), std::move(value), align};
    t.font.shape(t, clusters);
    return t;
}

void Renderer::Impl::update_cursor() {
    auto it = cursor_cache.find(active_cursor);
    if (it != cursor_cache.end()) {
        check SDL_SetCursor(it->second);
        return;
    }

    SDL_Cursor* cursor = check SDL_CreateSystemCursor(SDL_SystemCursor(active_cursor));
    cursor_cache[active_cursor] = cursor;
    check SDL_SetCursor(cursor);
}

// =============================================================================
//  Querying State
// =============================================================================
auto Renderer::GetWindowSize() -> Sz {
    i32 wd, ht;
    check SDL_GetWindowSize(*global_renderer->window, &wd, &ht);
    return {wd, ht};
}

bool Renderer::ShouldBlinkCursor() {
    return SDL_GetTicks() % 1'500 < 750;
}

// =============================================================================
//  Utils
// =============================================================================
auto AABB::contains(xy pos) const -> bool {
    return pos.x >= min.x and pos.x <= max.x and pos.y >= min.y and pos.y <= max.y;
}

// =============================================================================
//  Startup
// =============================================================================
// The renderer parameter is unused and is only passed in
// to ensure that we create the renderer before the asset
// loader.
auto AssetLoader::Create() -> Thread<AssetLoader> {
    return Thread<AssetLoader>{&Load};
}

auto AssetLoader::Load(std::stop_token stop) -> AssetLoader {
    AssetLoader loader;
    loader.load(stop);
    return loader;
}

void AssetLoader::load(std::stop_token stop) {
    using enum TextStyle;

    // Load the font faces.
    for (auto f : {Regular, Italic, Bold, BoldItalic}) {
        if (stop.stop_requested()) return;
        ftcall FT_Init_FreeType(&*font_data.ft[+f]);
        ftcall FT_New_Memory_Face(
            *font_data.ft[+f],
            reinterpret_cast<const FT_Byte*>(Fonts[+f].data()),
            Fonts[+f].size(),
            0,
            &*font_data.ft_face[+f]
        );
    }

    // Load each predefined font.
    for (
        auto f : {
            FontSize::Small,
            FontSize::Normal,
            FontSize::Intermediate,
            FontSize::Medium,
            FontSize::Large,
            FontSize::Huge,
            FontSize::Title,
            FontSize::Gargantuan,
        }
    ) {
        for (auto s : {Regular, Italic, Bold, BoldItalic})
            font_data.fonts[{+f, s}] = Font{*font_data.ft_face[+s], f, s};
    }
}

/// Finish loading assets.
void AssetLoader::finalise() {
    // Move resources.
    global_renderer->font_data = std::move(font_data);

    // Build font textures.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto& f : global_renderer->font_data.fonts | vws::values)
        f.atlas_width = Texture::MaxSize();
}

void InputSystem::update_selection(bool is_element_selected) {
    if (was_selected == is_element_selected) return;
    was_selected = is_element_selected;
    if (is_element_selected) SDL_StartTextInput(global_renderer->sdl_window());
    else SDL_StopTextInput(global_renderer->sdl_window());
}
