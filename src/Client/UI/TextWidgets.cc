module;
#include <algorithm>
#include <base/Macros.hh>
#include <cmath>
#include <pr/UIMacros.hh>
#include <ranges>
#include <SDL3/SDL.h>
module pr.client.ui;

using namespace pr;
using namespace pr::client;

constexpr Colour DefaultButtonColour{36, 36, 36, 255};
constexpr Colour HoverButtonColour{23, 23, 23, 255};

/// Compute the absolute coordinates for positioning
/// text in the center of a box. The box must be in
/// absolute coordinates.
auto CenterTextInBox(
    Renderer& r,
    const ShapedText& text,
    i32 box_height,
    AABB absolute_box
) -> xy {
    f32 ascender = r.font_for_text(text).strut_split().first;
    f32 strut = r.font_for_text(text).strut();
    Size sz{text.width, f32(0)}; // Zero out the height to avoid it messing w/ up the calculation.

    // Bail out if we don’t have enough space.
    if (strut > box_height) return Position::Center().relative(absolute_box, sz);

    // This calculation ‘centers’ text in the box at the baseline.
    //
    // To center text in a box whose size is equal to the font strut
    // (i.e. ascender + descender), one of the following, equivalent
    // conditions must be met:
    //
    //   1. The distance from the top of the box must equal the ascender.
    //   2. The distance from the bottom of the box must equal the descender.
    //
    // Observe that these two are equivalent if the box size matches the
    // font strut exactly (e.g. if the ascender is 7, the descender 3, and
    // the box size thus 10, placing text 7 from the top is equivalent to
    // placing it 3 from the bottom.
    //
    // Since this achieves centering, it follows that any extra space added
    // to the box (i.e. if the box is larger than the font strut) must be
    // distributed equally at the top and bottom of the box in order to
    // maintain the centering.
    //
    // Thus, the top offset is given by the ascender of the font (condition
    // 1 above) plus half the extra space in the box, which is exactly
    // (box_height - strut) / 2.
    //
    // See also:
    //    https://learn.microsoft.com/en-us/typography/opentype/spec/recom#stypoascender-stypodescender-and-stypolinegap
    //    https://web.archive.org/web/20241112215935/https://learn.microsoft.com/en-us/typography/opentype/spec/recom#stypoascender-stypodescender-and-stypolinegap
    f32 top_offs = ascender + (box_height - strut) / 2;
    return Position::HCenter(-i32(top_offs)).relative(absolute_box, sz);
}

void Button::draw(Renderer& r) {
    r.draw_rect(abox(), hovered ? HoverButtonColour : DefaultButtonColour);
    TextBox::draw(r);
}

void Label::draw(Renderer& r) {
    auto& shaped = text.shaped(r);
    auto parent_box = parent->bounding_box;
    xy position;

    if (fixed_height != 0) {
        position = CenterTextInBox(r, shaped, fixed_height, abox());
    } else {
        position = auto{pos}.voffset(i32(shaped.depth)).relative(parent_box, shaped.size());
    }

    r.draw_text(shaped, position, colour);
}

void Label::refresh(Renderer& r) {
    defer {
        auto sz = text.shaped(r).size();
        UpdateBoundingBox(Size{sz.wd, std::max(sz.ht, fixed_height)});
    };

    if (not reflow) return;
    text.reflow(r, std::min(max_width, parent->bounding_box.width()));
}

void Label::set_align(TextAlign new_value) { text.align = new_value; }
void Label::set_font_size(FontSize new_value) { text.font_size = new_value; }

TRIVIAL_CACHING_SETTER(Label, bool, reflow);
TRIVIAL_CACHING_SETTER(Label, i32, max_width);
TRIVIAL_CACHING_SETTER(Label, i32, fixed_height);

TextBox::TextBox(
    Element* parent,
    ShapedText text,
    ShapedText placeholder,
    Position pos,
    i32 padding,
    i32 min_wd,
    i32 min_ht
) : Widget{parent, pos},
    placeholder{std::move(placeholder)},
    padding{padding},
    min_wd{min_wd},
    min_ht{min_ht} {
    UpdateText(std::move(text));
}

void TextBox::UpdateText(ShapedText new_text) {
    label = std::move(new_text);
    needs_refresh = true;
}

auto TextBox::TextPos(Renderer& r, const ShapedText& text) -> xy {
    return CenterTextInBox(r, text, bounding_box.height(), abox());
}

void TextBox::draw(Renderer& r) {
    auto& text = label.empty() ? placeholder : label;
    auto pos = TextPos(r, text);
    r.draw_text(text, pos, label.empty() ? Colour::Grey : Colour::White);
    if (cursor_offs != -1) {
        auto [asc, desc] = r.font_for_text(label).strut_split();
        r.draw_line(
            xy(i32(pos.x) + cursor_offs, pos.y - i32(desc)),
            xy(i32(pos.x) + cursor_offs, pos.y + i32(asc)),
            Colour::White
        );
    }
}

void TextBox::refresh(Renderer& r) {
    auto strut = r.font_for_text(label).strut();
    Size sz{
        std::max(min_wd, i32(label.width)) + 2 * padding,
        std::max({min_ht, i32(label.height + label.depth), strut}) + 2 * padding,
    };

    SetBoundingBox(apos(), sz);
}

void TextEdit::draw(Renderer& r) {
    if (dirty) {
        dirty = false;
        auto shaped = r.make_text(
            hide_text ? std::u32string(text.size(), U'•') : text,
            size,
            style,
            TextAlign::SingleLine,
            0,
            &clusters
        );

        UpdateText(std::move(shaped));
    }

    // Use HarfBuzz cluster information to position the cursor: if the cursor
    // position corresponds to an entry in the clusters array (e.g. the cursor
    // is at position 3, and the array has an entry with value 3), position the
    // cursor right before the character that starts at that cluster (i.e. the
    // n-th character, where n is the index of the entry with value 3).
    //
    // If there is no entry, find the smallest cluster value 's' closest to the
    // cursor position, take the difference between it and the next cluster, and
    // lerp the cursor in the middle of the character at 's'.
    //
    // Concretely, there are 3 possible cases here:
    //
    //   1. The cursor is at the start/end of the text, in which case it has
    //      index 0/n (where n is the number of characters in the text); draw
    //      it before the first / after the last character.
    //
    //   2. The cursor corresponds to the index of a cluster; draw it before
    //      that cluster.
    //
    //   3. The cursor corresponds to an index that is in between two clusters;
    //      interpolate between them to position the cluster in the middle
    //      somewhere.
    if (no_blink_ticks) no_blink_ticks--;
    if (selected and not clusters.empty() and (no_blink_ticks or r.blink_cursor())) {
        cursor_offs = [&] -> i32 {
            // Cursor is at the start/end of the text.
            if (cursor == 0) return 0;
            if (cursor == i32(text.size())) return i32(label.width);

            // Find the smallest cluster with an index greater than or equal
            // to the cursor position. We interpolate the cursor’s position
            // between it and the previous cluster.
            //
            // Note that there *must* always be a cluster with an index *smaller*
            // than the cursor, since there will always be a cluster with index
            // 0, and the cursor index cannot be zero (because we checked for that
            // above).
            auto it = rgs::lower_bound(clusters, cursor, {}, &ShapedText::Cluster::index);
            auto prev = std::prev(it);
            i32 x1 = prev->xoffs;
            i32 i1 = prev->index;
            i32 x2, i2;

            // If we get here, the cursor must not be at the very start or end
            // of the text; despite that, there may not be a cluster with an
            // index larger than the cursor’s.
            //
            // This can happen e.g. if we have a ligature at the end of the text.
            // For instance, if the text is 'fl', which is converted to a single
            // ligature, the clusters array contains a single cluster with index 0.
            //
            // In this case, cursor index 1—despite being larger than the index of
            // any cluster—is *not* at the end of the text. However, we can resolve
            // this by pretending there is an additional cluster at the end of the
            // array whose index is the size of the text, and then interpolate between
            // that and the last actual cluster.
            if (it != clusters.end()) {
                // Cursor is right before a character.
                if (it->index == cursor) return i32(it->xoffs);

                // Cursor is between two clusters.
                x2 = it->xoffs;
                i2 = it->index;
            }

            // Interpolate between the last cluster and the end of the text.
            else {
                x2 = i32(label.width);
                i2 = i32(text.size());
            }

            return i32(std::lerp(x1, x2, f32(cursor - i1) / f32(i2 - i1)));
        }();
    } else {
        cursor_offs = -1;
    }

    if (hovered) r.set_cursor(Cursor::IBeam);

    r.draw_rect(abox(), selected ? HoverButtonColour : DefaultButtonColour);
    TextBox::draw(r);
}

void TextEdit::event_click(InputSystem& input) {
    // Figure out where we clicked and set the cursor accordingly;
    // we do this by iterating over all clusters; as soon as we find
    // one whose offset brings us further away from the click position,
    // we stop and go back to the one before it.
    no_blink_ticks = 20;
    i32 mx = input.mouse.pos.x;
    i32 x0 = TextPos(input.renderer, label).x;
    i32 x1 = x0 + i32(label.width);
    if (mx < x0) cursor = 0;
    else if (mx > x1) cursor = i32(text.size());
    else if (clusters.size() < 2) cursor = 0;
    else {
        cursor = 0;
        i32 x = x0;
        i32 d = std::abs(x - mx);
        auto it = clusters.begin();

        // A cluster might correspond to multiple glyphs, in which case
        // we need to interpolate into it.
        ShapedText::Cluster* prev = nullptr;
        while (cursor < i32(text.size()) and it != clusters.end()) {
            auto xoffs = [&] {
                // Cluster matches cursor index; we can use the x
                // offset exactly.
                if (cursor == it->index) return it->xoffs;

                // Cluster index is too large; interpolate between the
                // previous index and this one.
                auto prev_x = prev ? prev->xoffs : 0;
                auto prev_i = prev ? prev->index : 0;
                return i32(std::lerp(prev_x, it->xoffs, f32(cursor - prev_i) / f32(it->index - prev_i)));
            }();

            auto nd = std::abs(x + xoffs - mx);
            if (nd > d) {
                cursor--;
                break;
            }

            d = nd;
            prev = &*it;
            cursor++;
            if (cursor > it->index) ++it;
        }

        cursor = std::clamp(cursor, 0, i32(text.size()));
    }
}

void TextEdit::event_input(InputSystem& input) {
    // Copy text into the buffer.
    if (not input.text_input.empty()) {
        no_blink_ticks = 20;
        dirty = true;
        text.insert(cursor, input.text_input);
        cursor += i32(input.text_input.size());
    }

    auto Paste = [&] {
        if (SDL_HasClipboardText()) {
            text += text::ToUTF32(SDL_GetClipboardText());
            dirty = true;
        }
    };

    for (auto [key, mod] : input.kb_events) {
        no_blink_ticks = 20;
        switch (key) {
            default: break;
            case SDLK_BACKSPACE:
                if (mod & SDL_KMOD_CTRL) {
                    static constexpr std::u32string_view ws = U" \t\n\r\v\f";
                    u32stream segment{text.data(), usz(cursor)};
                    auto pos = segment.trim_back().drop_back_until_any(ws).size();
                    text.erase(pos, cursor - pos);
                    cursor = i32(pos);
                    dirty = true;
                } else if (cursor != 0) {
                    text.erase(--cursor, 1);
                    dirty = true;
                }
                break;
            case SDLK_DELETE:
                if (cursor != i32(text.size())) {
                    text.erase(cursor, 1);
                    dirty = true;
                }
                break;
            case SDLK_LEFT: cursor = std::max(0, cursor - 1); break;
            case SDLK_RIGHT: cursor = std::min(i32(text.size()), cursor + 1); break;
            case SDLK_HOME: cursor = 0; break;
            case SDLK_END: cursor = i32(text.size()); break;
            case SDLK_V:
                if (mod & SDL_KMOD_CTRL) Paste();
                break;
            case SDLK_INSERT:
                if (mod & SDL_KMOD_SHIFT) Paste();
                break;
        }
    }
}
