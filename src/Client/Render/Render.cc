module;
#include <algorithm>
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <cstring>
#include <hb-ft.h>
#include <hb.h>
#include <libassert/assert.hpp>
#include <memory>
#include <pr/gl-headers.hh>
#include <ranges>
#include <SDL3/SDL.h>
#include <webp/decode.h>

// clang-format off
// Include order matters here!
#include <ft2build.h>
#include FT_FREETYPE_H

#include <mutex>
#include <stop_token>
// clang-format on

module pr.client.render;
import pr.client.utils;
import pr.client.render.gl;
import pr.serialisation;

import base.text;
import base.fs;

using namespace gl;
using namespace pr;
using namespace pr::client;
using namespace base;

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

constexpr Colour DefaultBGColour{45, 42, 46, 255};

constexpr char PrimitiveVertexShaderData[]{
#embed "Shaders/Primitive.vert"
};

constexpr char PrimitiveFragmentShaderData[]{
#embed "Shaders/Primitive.frag"
};

constexpr char TextVertexShaderData[]{
#embed "Shaders/Text.vert"
};

constexpr char TextFragmentShaderData[]{
#embed "Shaders/Text.frag"
};

constexpr char ImageVertexShaderData[]{
#embed "Shaders/Image.vert"
};

constexpr char ImageFragmentShaderData[]{
#embed "Shaders/Image.frag"
};

constexpr char ThrobberVertexShaderData[]{
#embed "Shaders/Throbber.vert"
};

constexpr char ThrobberFragmentShaderData[]{
#embed "Shaders/Throbber.frag"
};

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
//  Text and Fonts
// =============================================================================
auto ShapedText::DumpHBBuffer(hb_font_t* font, hb_buffer_t* buf) {
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

Font::Font(FT_Face ft_face, u32 size, TextStyle style)
    : face{ft_face},
      size{size},
      style{style} {
    // Set the font size.
    FT_Set_Pixel_Sizes(ft_face, 0, size);
    f32 em = f32(ft_face->units_per_EM);

    // Compute the font’s strut.
    strut_asc = size * ft_face->ascender / em;
    strut_desc = size * -ft_face->descender / em;

    // Compute the interline skip.
    skip = u32(ft_face->height / f32(ft_face->units_per_EM) * size);

    // Determine the maximum width and height amongst all glyphs; we
    // need to do this now to ensure that the dimensions of a cell
    // don’t change when we add glyphs; otherwise, we’d be invalidating
    // the uv coordinates of already shaped text.
    //
    // Note: we use the advance width for x and the bounding box for y
    // because that seems to more closely match the data we got from
    // iterating over every glyph in the font and computing the maximum
    // metrics.
    atlas_entry_width = u32(std::ceil(ft_face->max_advance_width / em * size));
    atlas_entry_height = u32(std::ceil(size * (ft_face->bbox.yMax - ft_face->bbox.yMin) / em));

    // Create a HarfBuzz font for it.
    auto f = hb_ft_font_create(ft_face, nullptr);
    Assert(f, "Failed to create HarfBuzz font");
    hb_font = f;
    hb_ft_font_set_funcs(hb_font.get());
}

auto Font::AllocBuffer() -> hb_buffer_t* {
    Assert(hb_buffers_in_use <= hb_bufs.size());
    if (hb_buffers_in_use == hb_bufs.size()) {
        hb_buffers_in_use++;
        hb_bufs.emplace_back(hb_buffer_create());
        return hb_bufs.back().get();
    }
    return hb_bufs[hb_buffers_in_use++].get();
}

FontSize Text::get_font_size() const {
    return text.font_size;
}

void Text::reflow(Renderer& r, i32 width) {
    if (dirty or width < text.width or needs_multiple_lines) shape(r, width);
}

void Text::set_align(TextAlign a) {
    _align = a;
    dirty = true;
}

void Text::set_font_size(FontSize new_value) {
    text = ShapedText(new_value);
    dirty = true;
}

void Text::set_style(TextStyle s) {
    _style = s;
    dirty = true;
}

auto Text::shaped(Renderer& r) const -> const ShapedText& {
    if (dirty) shape(r, 0);
    return text;
}

void Text::shape(Renderer& r, i32 desired_width) const {
    dirty = false;
    text = r.make_text(
        content,
        text.font_size,
        style,
        align,
        desired_width,
        nullptr,
        &needs_multiple_lines
    );
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
auto Font::shape(
    std::u32string_view text,
    TextAlign align,
    i32 desired_text_width,
    std::vector<ShapedText::Cluster>* clusters,
    bool* multiline
) -> ShapedText {
    if (multiline) *multiline = false;
    if (text.empty()) return ShapedText{FontSize(size), style};
    auto font = hb_font.get();
    Assert(font, "Forgot to call finalise()!");

    // Free buffers after we’re done.
    defer { hb_buffers_in_use = 0; };

    // Set the font size.
    FT_Set_Pixel_Sizes(face, 0, size);

    // Refuse to go below a certain width to save us from major headaches.
    static constexpr i32 MinTextWidth = 40;
    const bool reflow = desired_text_width != 0;
    const f32 desired_width = desired_text_width == 0 ? 0 : std::max(desired_text_width, MinTextWidth);

    // Convert to UTF-32 in canonical decomposition form; this helps
    // with fonts that may not have certain precombined characters, but
    // which *do* support the individual components.
    //
    // Normalisation can fail if the input was nonsense; just render an
    // error string in that case.
    auto norm = Normalise(text, text::NormalisationForm::NFD).value_or(U"<ERROR>");

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
        hb_font_set_scale(font, size * Scale, size * Scale);

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
        if (not reflow or max_x <= f32(desired_width)) return max_x;

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
                // mid-word.
                if (last_ws_cluster_index == -1) {
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
            line_ht = std::max(line_ht, ypos + h);
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
    auto lines_to_shape = u32stream{norm}.lines() | vws::transform(&u32stream::text) | rgs::to<std::vector>();
    if (lines_to_shape.empty()) return ShapedText{FontSize(size), style};
    f32 max_x = ShapeLines(lines_to_shape);
    if (multiline) *multiline = lines.size() > 1;

    // Rebuild the texture atlas.
    RebuildAtlas();

    // Finally, add vertices for each line.
    f32 ybase = 0;
    f32 ht = 0, dp = 0;
    f32 last_line_skip = 0;
    for (const auto& line : lines) {
        // Determine the starting X position for this line.
        f32 xbase = [&] -> f32 {
            switch (align) {
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
    VertexArrays vao{VertexLayout::PositionTexture4D};
    auto& vbo = vao.add_buffer();
    vbo.copy_data(verts);
    return ShapedText(std::move(vao), FontSize(size), style, max_x, ht, dp);
}

// =============================================================================
//  Initialisation
// =============================================================================
Renderer::Renderer(int initial_wd, int initial_ht) {
    static std::once_flag GlobalInit;
    std::call_once(GlobalInit, [] {
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
    });

    // Create the window.
    window = check SDL_CreateWindow(
        "Prescriptivism, the Game",
        initial_wd,
        initial_ht,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    // Create the OpenGL context.
    context = check SDL_GL_CreateContext(*window);

    // Initialise OpenGL.
    check SDL_GL_MakeCurrent(*window, *context);
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
    primitive_shader = ShaderProgram(
        std::span{PrimitiveVertexShaderData},
        std::span{PrimitiveFragmentShaderData}
    );

    text_shader = ShaderProgram(
        std::span{TextVertexShaderData},
        std::span{TextFragmentShaderData}
    );

    image_shader = ShaderProgram(
        std::span{ImageVertexShaderData},
        std::span{ImageFragmentShaderData}
    );

    throbber_shader = ShaderProgram(
        std::span{ThrobberVertexShaderData},
        std::span{ThrobberFragmentShaderData}
    );

    // Enable blending.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

Renderer::Frame::Frame(Renderer& r) : r(r) { r.frame_start(); }
Renderer::Frame::~Frame() { r.frame_end(); }

// =============================================================================
//  Drawing
// =============================================================================
void Renderer::clear(Colour c) {
    auto [sx, sy] = size();
    glViewport(0, 0, sx, sy);
    glClearColor(c.red(), c.green(), c.blue(), c.alpha());
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw_line(xy start, xy end, Colour c) {
    use(primitive_shader);
    primitive_shader.uniform("in_colour", c.vec4());
    VertexArrays vao{VertexLayout::Position2D};
    vec2 verts[]{start.vec(), end.vec()};
    vao.add_buffer(verts, GL_LINES);
    vao.draw_vertices();
}

void Renderer::draw_outline_rect(xy pos, Size size, i32 thickness, Colour c) {
    use(primitive_shader);
    primitive_shader.uniform("in_colour", c.vec4());
    auto [x, y] = pos;
    auto [wd, ht] = size;
    VertexArrays vao{VertexLayout::Position2D};

    // Draw four rectangles around the original rectangle.
    vec2 verts[] {
        // Left.
        {x, y},
        {x - thickness, y},
        {x, y + ht},
        {x, y + ht},
        {x - thickness, y},
        {x - thickness, y + ht},

        // Right.
        {x + wd , y},
        {x + wd + thickness, y},
        {x + wd , y + ht},
        {x + wd , y + ht},
        {x + wd + thickness, y},
        {x + wd + thickness, y + ht},

        // Top.
        {x - thickness, y + ht},
        {x - thickness, y + ht + thickness},
        {x + wd + thickness, y + ht},
        {x + wd + thickness, y + ht},
        {x - thickness, y + ht + thickness},
        {x + wd + thickness, y + ht + thickness},

        // Bottom.
        {x - thickness, y},
        {x - thickness, y - thickness},
        {x + wd + thickness, y},
        {x + wd + thickness, y},
        {x - thickness, y - thickness},
        {x + wd + thickness, y - thickness},

    };

    vao.add_buffer(verts, GL_TRIANGLES);
    vao.draw_vertices();
}

void Renderer::draw_rect(xy pos, Size size, Colour c) {
    use(primitive_shader);
    primitive_shader.uniform("in_colour", c.vec4());
    auto [x, y] = pos;
    VertexArrays vao{VertexLayout::Position2D};
    vec2 verts[]{
        {x, y},
        {x + size.wd, y},
        {x, y + size.ht},
        {x + size.wd, y + size.ht}
    };
    vao.add_buffer(verts, GL_TRIANGLE_STRIP);
    vao.draw_vertices();
}

void Renderer::draw_text(
    const ShapedText& text,
    xy pos,
    Colour colour
) {
    if (text.empty()) return;
    auto& f = font(+text.font_size, text.style);

    // Initialise the text shader.
    use(text_shader);
    text_shader.uniform("text_colour", colour.vec4());
    text_shader.uniform("position", pos.vec());
    text_shader.uniform("atlas_height", f.atlas_height());

    // Bind the font atlas.
    f.use();

    // Dew it.
    text.vao.draw_vertices();
}

void Renderer::draw_text(const Text& text, xy pos, Colour c) {
    draw_text(text.shaped(*this), pos, c);
}

void Renderer::draw_texture(
    const DrawableTexture& tex,
    xy pos
) {
    use(image_shader);
    image_shader.uniform("position", pos.vec());
    tex.draw_vertices();
}

void Renderer::frame_end() {
#if 0 // FOR TESTING
    // Draw an image.
    static auto image = [&] {
        auto file = File::Read<std::vector<char>>("./assets/test.webp").value();
        int wd, ht;
        auto data = WebPDecodeRGBA(reinterpret_cast<const u8*>(file.data()), file.size(), &wd, &ht);
        Assert(data, "Decoding error");
        defer { WebPFree(data); };
        return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE);
    }();

    draw_texture(image, 0, 0);

    // Draw a triangle.
    vec2 points[]{// clang-format off
        {0, 0},
        {50, 0},
        {0, 50}
    }; // clang-format on

    auto sz = size();
    primitive_shader.use();
    primitive_shader.uniform("projection", glm::ortho<f32>(0, sz.x, 0, sz.y));
    VertexArrays vao{VertexLayout::Position2D};
    vao.add_buffer(points);
    vao.draw();

    // Draw text.
    constexpr std::string_view InputText = "EÉÉẸ̣eééé́ẹ́ẹ́ʒffifl";
    static ShapedText text = default_font.shape(InputText);
    draw_text(text, 20, size().y - 200, Colour{255, 255, 255, 255});
    draw_text(text, 20, size().y - 400, Colour{200, 120, 120, 255});
#endif

    // Swap buffers.
    check SDL_GL_SwapWindow(*window);
}

void Renderer::frame_start() {
    clear(DefaultBGColour);

    // Disable mouse capture if the debugger is running.
    if (libassert::is_debugger_present()) {
        check SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
        check SDL_CaptureMouse(false);
    }

    // Set the active cursor if it has changed.
    if (requested_cursor != active_cursor) {
        active_cursor = requested_cursor;
        SetCursorImpl();
    }
}

void Renderer::use(ShaderProgram& shader) {
    auto [sx, sy] = size();
    shader.use_shader_program_dont_call_this_directly();
    shader.uniform("projection", glm::ortho<f32>(0, sx, 0, sy));
}

// =============================================================================
//  Creating Objects
// =============================================================================
auto Renderer::font(u32 size, TextStyle style) -> Font& {
    // Make sure the font exists.
    auto it = font_data.fonts.find({size, style});
    Assert(
        it != font_data.fonts.end(),
        "Font with size {} and style {} has not been built yet. Make sure to "
        "load this font size in the asset loader (AssetLoader::load()) if you "
        "want to use it",
        size,
        +style
    );

    return it->second;
}

auto Renderer::font_for_text(const ShapedText& r) -> Font& {
    return font(+r.font_size, r.style);
}

auto Renderer::frame() -> Frame { return Frame(*this); }

auto Renderer::make_text(
    std::u32string_view text,
    FontSize size,
    TextStyle style,
    TextAlign align,
    i32 desired_width,
    std::vector<ShapedText::Cluster>* clusters,
    bool* multiline
) -> ShapedText {
    return font(+size, style).shape(text, align, desired_width, clusters, multiline);
}

void Renderer::reload_shaders() {
    // TODO: inotify().
    auto Reload = [&](ShaderProgram& program, std::string_view shader_name) {
        auto vert = File::Read(std::format("./assets/Shaders/{}.vert", shader_name)).value();
        auto frag = File::Read(std::format("./assets/Shaders/{}.frag", shader_name)).value();
        program = ShaderProgram{vert.view(), frag.view()};
    };

    Reload(primitive_shader, "Primitive");
    Reload(text_shader, "Text");
    Reload(image_shader, "Image");
    Reload(throbber_shader, "Throbber");
}

void Renderer::set_cursor(Cursor c) {
    // Rather than actually setting the cursor, we register the
    // change and set it at the start of the next frame; this allows
    // us to avoid flicker in case a cursor change is requested multiple
    // times per frame and thus also makes that possible as a pattern.
    requested_cursor = c;
}

void Renderer::SetCursorImpl() {
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
bool Renderer::blink_cursor() {
    return SDL_GetTicks() % 1'500 < 750;
}

auto Renderer::size() -> Size {
    i32 wd, ht;
    check SDL_GetWindowSize(*window, &wd, &ht);
    return {wd, ht};
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
auto AssetLoader::Create(Renderer&) -> Thread<AssetLoader> {
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
            FontSize::Text,
            FontSize::Medium,
            FontSize::Large,
            FontSize::Huge,
            FontSize::Title,
        }
    ) {
        for (auto s : {Regular, Italic, Bold, BoldItalic})
            font_data.fonts[{+f, s}] = Font{*font_data.ft_face[+s], +f, s};
    }
}

/// Finish loading assets.
void AssetLoader::finalise(Renderer& r) {
    // Move resources.
    r.font_data = std::move(font_data);

    // Build font textures.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto& [_, f] : r.font_data.fonts)
        f.atlas_width = Texture::MaxSize();
}
