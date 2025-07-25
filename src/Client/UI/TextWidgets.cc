#include <Client/UI/UI.hh>

#include <base/Macros.hh>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ranges>

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Colours
// =============================================================================
constexpr Colour InactiveButtonColour{55, 55, 55, 255};
constexpr Colour DefaultButtonColour{36, 36, 36, 255};
constexpr Colour HoverButtonColour{23, 23, 23, 255};

constexpr Colour ButtonTextColour = Colour::White;
constexpr Colour InactiveButtonTextColour = Colour::Grey;

// =============================================================================
//  Helpers
// =============================================================================
/// Compute the absolute coordinates for positioning
/// text in the center of a box. The box must be in
/// absolute coordinates.
auto CenterTextInBox(
    const Text& text,
    i32 box_height,
    AABB absolute_box
) -> xy {
    f32 ascender = text.font.strut_split().first;
    f32 strut = text.font.strut();
    Sz sz{text.width, f32(0)}; // Zero out the height to avoid it messing w/ up the calculation.

    // We need to add extra space for every line beyond the first.
    //
    // Note: This formula is known to be correct for 1–2 lines; it has not been tested
    // for more than 2 lines, so we might have to amend it at some point.
    strut += ascender * (text.lines - 1);

    // Bail out if we don’t have enough space or if the text is empty.
    if (text.empty or strut > box_height) return Position::Center().resolve(absolute_box, sz);

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
    return Position::HCenter(-i32(top_offs)).resolve(absolute_box, sz);
}

// =============================================================================
//  Button
// =============================================================================
Button::Button(
    Element* parent,
    std::string_view label,
    Position pos,
    Font& font,
    i32 padding,
    i32 min_wd,
    i32 min_ht
) : TextBox( // clang-format off
    parent,
    Text(font, label),
    std::nullopt,
    pos,
    padding,
    min_wd,
    min_ht
) { // clang-format on
    selectable = Selectable::Yes;
}

Button::Button(
    Element* parent,
    std::string_view label,
    Position pos,
    std::function<void()> click_handler
) : Button(parent, label, pos) {
    on_click = std::move(click_handler);
}

void Button::draw() {
    bool active = selectable != Selectable::No;
    auto colour = not active ? InactiveButtonColour
                : hovered    ? HoverButtonColour
                             : DefaultButtonColour;
    Renderer::DrawRect(bounding_box, colour);
    Renderer::DrawOutlineRect(bounding_box, 1, colour.lighten(.1f));

    TextBox::draw(active ? ButtonTextColour : InactiveButtonTextColour);
}

void Button::event_click(InputSystem&) {
    unselect();
    if (on_click) on_click();
}

// =============================================================================
//  Label
// =============================================================================
Label::Label(Element* parent, Text text, Position pos)
    : Widget(parent, pos), _text(std::move(text)) {
    reflow = Reflow::Soft;
}

Label::Label(
    Element* parent,
    std::string_view text,
    FontSize sz,
    Position pos
) : Widget(parent, pos), _text(Renderer::GetText(text, sz)) {
    reflow = Reflow::Soft;
}

void Label::draw() {
    if (fixed_height != 0) {
        auto _ = PushTransform();
        xy position = CenterTextInBox(text, fixed_height, bounding_box);
        Renderer::DrawText(text, position, colour);
    } else {
        xy position = auto{pos}.voffset(i32(text.depth)).resolve(parent.bounding_box, text.text_size);
        Renderer::DrawText(text, position, colour);
    }

    if (selectable == Selectable::Yes) Renderer::DrawOutlineRect(
        bounding_box.grow(5),
        3,
        Colour::RGBA(0xa4dc'a0ff)
    );
}

void Label::refresh(bool) {
    defer {
        auto sz = text.text_size;
        UpdateBoundingBox(Sz{sz.wd, std::max(sz.ht, fixed_height)});
    };

    if (reflow == Reflow::None) return;
    _text.desired_width = std::min(max_width, parent.bounding_box.width());
}

void Label::update_text(std::string_view new_text) {
    _text.content = new_text;
    needs_refresh = true;
}

void Label::set_reflow(Reflow new_value) {
    if (text.reflow == new_value) return;
    _text.reflow = new_value;
    needs_refresh = true;
}

TRIVIAL_CACHING_SETTER(Label, i32, max_width);
TRIVIAL_CACHING_SETTER(Label, i32, fixed_height);
CACHING_SETTER(Label, TextAlign, align, _text.align);
CACHING_SETTER(Label, FontSize, font_size, _text.font_size);

// =============================================================================
//  TextBox
// =============================================================================
TextBox::TextBox(
    Element* parent,
    Text text,
    std::optional<Text> placeholder,
    Position pos,
    i32 padding,
    i32 min_wd,
    i32 min_ht
) : Widget{parent, pos},
    label{std::move(text)},
    placeholder{std::move(placeholder)},
    padding{padding},
    min_wd{min_wd},
    min_ht{min_ht} {}

void TextBox::update_text(std::string_view new_text) {
    label.content = new_text;
    needs_refresh = true;
}

void TextBox::update_text(Text new_text) {
    label = std::move(new_text);
    needs_refresh = true;
}

auto TextBox::TextPos(const Text& text) -> xy {
    return CenterTextInBox(text, bounding_box.height(), bounding_box);
}

void TextBox::draw() {
    draw(label.empty ? Colour::Grey : Colour::White);
}

void TextBox::draw(Colour text_colour) {
    auto& text = label.empty and placeholder.has_value() ? *placeholder : label;
    auto _ = PushTransform();
    auto pos = TextPos(text);
    Renderer::DrawText(text, pos, text_colour);
    if (cursor_offs != -1) {
        auto [asc, desc] = text.font.strut_split();
        Renderer::DrawLine(
            xy(i32(pos.x) + cursor_offs, pos.y - i32(desc)),
            xy(i32(pos.x) + cursor_offs, pos.y + i32(asc)),
            Colour::White
        );
    }
}

void TextBox::refresh(bool full) {
    if (not full) return RefreshBoundingBox();
    auto strut = label.font.strut();
    Sz sz{
        std::max(min_wd, i32(label.width)) + 2 * padding,
        std::max({min_ht, i32(label.height + label.depth), strut}) + 2 * padding,
    };

    UpdateBoundingBox(sz);
}

// =============================================================================
//  TextEdit
// =============================================================================
TextEdit::TextEdit(
    Element* parent,
    Position pos,
    std::string_view placeholder,
    Font& font,
    i32 padding,
    bool hide_text,
    i32 min_wd,
    i32 min_ht
) : TextBox( // clang-format off
    parent,
    Text(font, ""),
    Text(font, placeholder),
    pos,
    padding,
    min_wd,
    min_ht
), hide_text{hide_text} {
    selectable = Selectable::Yes;
} // clang-format on

void TextEdit::draw() {
    if (dirty) {
        dirty = false;
        label.content = hide_text ? std::u32string(text.size(), U'•') : text;
        label.font.shape(label, &clusters);
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
    if (selected and not clusters.empty() and (no_blink_ticks or Renderer::ShouldBlinkCursor())) {
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
            auto it = rgs::lower_bound(clusters, cursor, {}, &TextCluster::index);
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

    if (hovered) Renderer::SetActiveCursor(Cursor::IBeam);

    auto colour = hovered ? HoverButtonColour : DefaultButtonColour;
    Renderer::DrawRect(bounding_box, colour);
    Renderer::DrawOutlineRect(bounding_box, 1, colour.lighten(.1f));
    TextBox::draw();
}

void TextEdit::event_click(InputSystem& input) {
    // Figure out where we clicked and set the cursor accordingly;
    // we do this by iterating over all clusters; as soon as we find
    // one whose offset brings us further away from the click position,
    // we stop and go back to the one before it.
    no_blink_ticks = 20;
    i32 mx = input.mouse.pos.x - bounding_box.origin().x;
    i32 x0 = TextPos(label).x;
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
        TextCluster* prev = nullptr;
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

void TextEdit::set_hide_text(bool hide) {
    hide_text = hide;
    dirty = true;
}

auto TextEdit::value() -> std::string { return text::ToUTF8(text); }
void TextEdit::value(std::u32string new_text) {
    text = std::move(new_text);
    dirty = true;
}
