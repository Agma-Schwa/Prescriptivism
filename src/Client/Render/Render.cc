module;
#include <algorithm>
#include <base/Assert.hh>
#include <base/Macros.hh>
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
// clang-format on

module pr.client.render;
import pr.client.utils;
import pr.client.render.gl;

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
#embed PRESCRIPTIVISM_DEFAULT_FONT_PATH
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

Font::Font(FT_Face ft_face, u32 size, u32 skip)
    : size{size}, skip{skip} {
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

auto Font::AllocBuffer() -> hb_buffer_t* {
    auto in_use = hb_buffers_in_use++;
    if (in_use == hb_bufs.size()) hb_bufs.push_back(
        HarfBuzzBufferHandle{hb_buffer_create(), hb_buffer_destroy}
    );
    return hb_bufs[in_use].get();
}

/// Shape text using this font.
///
/// The resulting object is position-independent and can
/// be drawn at any coordinates.
auto Font::shape(
    std::u32string_view text,
    TextAlign align,
    i32 desired_text_width,
    std::vector<ShapedText::Cluster>* clusters,
    bool* multiline
) -> ShapedText {
    if (multiline) *multiline = false;
    if (text.empty()) return ShapedText{};
    auto font = hb_font.get();

    // Free buffers after we’re done.
    defer { hb_buffers_in_use = 0; };

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
        if (start != 0 or end != 0) {
            infos = infos.subspan(start, end - start);
            positions = positions.subspan(start, end - start);
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

        // Enable the following OpenType features: 'liga'.
        hb_feature_t ligatures{};
        ligatures.tag = HB_TAG('l', 'i', 'g', 'a');
        ligatures.value = 1;
        ligatures.start = HB_FEATURE_GLOBAL_START;
        ligatures.end = HB_FEATURE_GLOBAL_END;

        // Shape the text.
        hb_shape(font, buf, &ligatures, 1);

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
        for (auto [i, l] : old_lines | vws::enumerate) {
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
            isz last_cluster = -1;                    // The last cluster we checked for whitespace.
            isz last_ws_pos = -1;                     // The position of the last whitespace character.
            usz first_char_pos = 0;                   // The first character of this subline.
            bool force_reshape = false;               // Whether we must reshape the next subline.
            bool line_buffer_referenced = false;      // If set, we must not reuse the buffer for this line.
            f32 x = 0;                                // The width of the current subline.
            f32 ws_width = 0, ws_advance = 0;         // The width up to (but excluding) the last whitespace character.
            auto source_line = lines_to_shape[i];     // Text of the entire line we’re splitting.
            auto [infos, positions] = GetInfo(l.buf); // Pre-split shaping data for the entire line.

            // Reshape a subline.
            auto AddSubline = [&](bool reshape, usz start, usz end, bool last = false) {
                // Reshape the line, reusing the buffer for this line if it was never referenced.
                if (reshape) {
                    auto buf = last and not line_buffer_referenced ? l.buf : AllocBuffer();
                    auto new_line = ShapeLine(source_line.substr(start, end - start), buf);
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

            // Go through all characters and split this line into sublines.
            for (auto [i, data] : vws::zip(infos, positions) | vws::enumerate) {
                auto& [info, pos] = data;

                // If this is whitespace and safe to break, remember its position.
                f32 adv = pos.x_advance / f32(Scale);
                if (info.cluster != last_cluster and source_line[info.cluster] == U' ') {
                    last_ws_pos = i;
                    ws_width = x;
                    ws_advance = adv;
                }

                // Don’t check the same cluster multiple times.
                last_cluster = info.cluster;

                // If this character would bring us above the width limit, break here.
                x += adv;
                if (x <= desired_width) continue;

                // Handle the degenerate case of us shaping a *really long* word
                // in a *really narrow* line, in which case we may have to break
                // mid-word.
                if (last_ws_pos == -1) {
                    AddSubline(
                        hb_glyph_info_get_glyph_flags(&info) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK,
                        first_char_pos,
                        usz(i)
                    );

                    first_char_pos = usz(i);
                    x = 0;
                }

                // If we’re not in the middle of a word, break at the last whitespace position; in
                // this case, we assume that it is always safe to break.
                else {
                    AddSubline(force_reshape, first_char_pos, usz(last_ws_pos));
                    x -= ws_width + ws_advance;
                    first_char_pos = usz(last_ws_pos + 1);
                }

                // Note: we may end up rescanning the last cluster here, but that’s not really that
                // problematic since we don’t expect the glyphs to get much larger or smaller as a
                // result (strictly speaking, we’d have to reshape the rest of the line here if we
                // broke a ligature, but we don’t need to be *that* precise).
                last_cluster = -1;
                last_ws_pos = -1;
                ws_width = 0;
            }

            // We almost certainly have characters left here, since we only break if the line gets
            // too long, in which case there is more text left in to process. Add the last subline.
            AddSubline(force_reshape, first_char_pos, source_line.size(), true);
        }

        // Finally, once again determine the largest line.
        return rgs::max_element(lines, {}, &Line::width)->width;
    };

    // Add the vertices for a line to the vertex buffer.
    std::vector<vec4> verts;
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
            u32 g = u32(info.codepoint);
            f32 xoffs = pos.x_offset / f32(Scale);
            f32 xadv = pos.x_advance / f32(Scale);
            f32 yoffs = pos.y_offset / f32(Scale);

            // Compute the x and y position using the glyph’s metrics and
            // the shaping data provided by HarfBuzz.
            f32 desc = glyphs[g].size.y - glyphs[g].bearing.y;
            f32 xpos = x + glyphs[g].bearing.x + xoffs;
            f32 ypos = ybase + yoffs - desc;
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
    if (lines_to_shape.empty()) return ShapedText{};
    f32 max_x = ShapeLines(lines_to_shape);
    if (multiline) *multiline = lines.size() > 1;

    // Now, add vertices for each line.
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

    // Upload the vertices.
    VertexArrays vao{VertexLayout::PositionTexture4D};
    auto& vbo = vao.add_buffer();
    vbo.copy_data(verts);
    return ShapedText(std::move(vao), size, max_x, ht, dp);
}

void Text::reflow(Renderer& r, i32 width) {
    // This looks a bit weird, but essentially, if we need to reshape
    // anyway, pass in the width right away; if not, we already know
    // the width, in which case reshape if it’s too large or always
    // reshape if we’re taking up multiple lines in case we just got
    // more space.
    if (dirty) shape(r, width);
    else if (width < text.width() or needs_multiple_lines) shape(r, width);
}

auto Text::shaped(Renderer& r) const -> const ShapedText& {
    if (dirty) shape(r, 0);
    return text;
}

void Text::shape(Renderer& r, i32 desired_width) const {
    dirty = false;
    text = r.make_text(
        content,
        text.font_size(),
        align,
        desired_width,
        nullptr,
        &needs_multiple_lines
    );
}

// =============================================================================
//  Initialisation
// =============================================================================
Renderer::Renderer(int initial_wd, int initial_ht) {
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

    // Load the font.
    ftcall FT_Init_FreeType(&ft);
    ftcall FT_New_Memory_Face(
        ft,
        reinterpret_cast<const FT_Byte*>(DefaultFontRegular),
        sizeof DefaultFontRegular,
        0,
        &ft_face
    );

    fonts_by_size[96] = Font(ft_face, 96);
}

Renderer::~Renderer() {
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft);
}

Renderer::Frame::Frame(Renderer& r)
    : r(r) { r.frame_start(); }
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

    // Initialise the text shader.
    use(text_shader);
    text_shader.uniform("text_colour", colour.vec4());
    text_shader.uniform("position", pos.vec());

    // Bind the font atlas.
    font(+text.font_size()).use();

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
    check SDL_GL_SwapWindow(window);
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
        set_cursor_impl();
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
auto Renderer::font(u32 size) -> Font& {
    auto it = fonts_by_size.find(size);
    if (it != fonts_by_size.end()) return it->second;
    return fonts_by_size[size] = Font(ft_face, size);
}

auto Renderer::frame() -> Frame { return Frame(*this); }

auto Renderer::make_text(
    std::string_view text,
    FontSize size,
    TextAlign align,
    i32 desired_width,
    std::vector<ShapedText::Cluster>* clusters,
    bool* multiline
) -> ShapedText {
    return font(+size).shape(text::ToUTF32(text), align, desired_width, clusters, multiline);
}

auto Renderer::make_text(
    std::u32string_view text,
    FontSize size,
    TextAlign align,
    i32 desired_width,
    std::vector<ShapedText::Cluster>* clusters,
    bool* multiline
) -> ShapedText {
    return font(+size).shape(text, align, desired_width, clusters, multiline);
}

void Renderer::reload_shaders() {
    // TODO: inotify().
    auto Reload = [&](ShaderProgram& program, std::string_view shader_name) {
        auto vert = File::Read(std::format("./assets/Shaders/{}.vert", shader_name)).value();
        auto frag = File::Read(std::format("./assets/Shaders/{}.frag", shader_name)).value();
        program = ShaderProgram{std::span{vert}, std::span{frag}};
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

void Renderer::set_cursor_impl() {
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
    check SDL_GetWindowSize(window, &wd, &ht);
    return {wd, ht};
}

// =============================================================================
//  Utils
// =============================================================================
auto AABB::contains(xy pos) const -> bool {
    return pos.x >= min.x and pos.x <= max.x and pos.y >= min.y and pos.y <= max.y;
}
