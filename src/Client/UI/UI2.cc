#include <Client/UI/UI2.hh>

using namespace pr;
using namespace pr::client;
using namespace pr::client::ui;

// =============================================================================
//  Screen
// =============================================================================
bool Screen::event_click() {
    // Consume the click.
    return true;
}

void Screen::set_active_element(Element* new_value) {
    // If there is a new focused element, it must be a child of this screen.
    Assert(
        not new_value or &new_value->parent_screen() == this,
        "Cannot focus element on a different screen"
    );

    // If the element was already active, do nothing.
    if (_active_element == new_value) return;

    // Remove focus from the old element.
    if (_active_element) _active_element->event_focus_lost();
    _active_element = new_value;

    // Focus the new element.
    if (_active_element) _active_element->event_focus_gained();
}

// =============================================================================
//  Element
// =============================================================================
void Element::BuildLayout(
    Layout l,
    Axis a,
    i32 total_extent,
    i32 max_static_extent,
    i32 dynamic_els
) {
    Assert(not elements.empty());

    // Apply centering by adjusting the position of the first element,
    // assuming the elements don’t overflow the container.
    //
    // Note that the total size of a group of elements along an axis
    // depends on whether the elements are next to each other or overlap
    // on that axis.
    auto ComputeStartingPosition = [&](i32 extent) {
        if (l.is_centered() and extent < computed_size[a])
            return (computed_size[a] - extent) / 2;
        return 0;
    };

    // Arrange the widgets in the right order; due to the way coordinates in
    // opengl work, the default is left to right or bottom to top; we want the
    // default for the latter to be top to bottom, and we also want to allow the
    // user to reverse the order.
    auto LayOutElements = [&](auto layout_func) {
        bool reverse = l.reverse;
        if (a == Axis::Y) reverse = not reverse;
        if (reverse) {
            for (auto& e : elements | vws::reverse) layout_func(e);
        } else {
            for (auto& e : elements) layout_func(e);
        }
    };

    // Distribute dynamic space first so we can handle centering properly. Also add
    // the gap to the calculation while we’re at it.
    switch (l.policy) {
        // Packed fill elements distribute the remaining space among themselves.
        case Layout::Packed:
        case Layout::PackedCenter: {
            total_extent += (i32(elements.size()) - 1) * l.gap;
            for (auto& e : elements) {
                if (e.style.size.is_dynamic(a)) {
                    Assert(e.style.size[a] == SizePolicy::Fill, "Unknown dynamic layout mode");
                    e.computed_size[a] = (computed_size[a] - total_extent) / dynamic_els;
                }
            }
        } break;

        // Overlapping fill elements simply have maximum size. Don’t apply the gap
        // here since the elements are all in the same place.
        case Layout::Overlap:
        case Layout::OverlapCenter: {
            for (auto& e : elements) {
                if (e.style.size.is_dynamic(a)) {
                    Assert(e.style.size[a] == SizePolicy::Fill, "Unknown dynamic layout mode");
                    e.computed_size[a] = computed_size[a];
                }
            }
        }
    }

    // If there are any dynamic elements, then this takes up the maximum available
    // space. This effectively disables centering. There are easier ways of achieving
    // just that, but we might introduce other dynamic sizing policies in the future,
    // in which case this calculation is best done here rather than in the starting
    // position callback.
    if (dynamic_els != 0) total_extent = max_static_extent = computed_size[a];

    // Finally, apply the layout policy.
    switch (l.policy) {
        // Pack the elements along the layout axis.
        case Layout::Packed:
        case Layout::PackedCenter: {
            auto pos = ComputeStartingPosition(total_extent);
            LayOutElements([&](auto& e) {
                e.computed_pos[a] = pos;
                pos += e.computed_size[a] + l.gap;
            });
        } break;

        // Put all elements in the same place.
        case Layout::Overlap:
        case Layout::OverlapCenter: {
            auto pos = ComputeStartingPosition(max_static_extent);
            LayOutElements([&](auto& e) { e.computed_pos[a] = pos; });
        } break;
    }
}

void Element::recompute_layout() {
    layout_changed = false;
    if (elements.empty()) return;

    // First, refresh each fixed-sized element and collect the minimum extent
    // of this container. Start with the gap, which is inserted between every
    // pair of elements.
    ByAxis<i32> total_size = {};
    ByAxis<i32> max_size = {};
    ByAxis<i32> dynamic_els = 0;
    for (auto& e : elements) {
        e.refresh();
        for (auto a : Axes) {
            if (e.style.size.is_dynamic(a)) {
                dynamic_els[a]++;
                continue;
            }

            e.computed_pos[a] = {};
            total_size[a] += e.computed_size[a];
            max_size[a] = std::max(max_size[a], e.computed_size[a]);
        }
    }

    // Lay out both axes.
    //
    // FIXME: At this point, we probably want a LayoutBuilder class so we don’t have to pass 20
    // arguments to this function.
    for (auto a : Axes) BuildLayout(
        style.layout[a],
        a,
        total_size[a],
        max_size[a],
        dynamic_els[a]
    );

    // Finally, compute the layout of dynamic elements.
    for (auto& e : elements)
        if (e.style.size.is_partially_dynamic())
            e.recompute_layout();
}

void Element::draw(Renderer& r) {
    // Draw background.
    r.draw_rect(computed_pos, computed_size, style.background);

    // Draw overlay last, after popping the matrix.
    defer { r.draw_rect(computed_pos, computed_size, style.overlay); };

    // Push transform matrix for this element.
    auto _ = r.push_matrix(computed_pos, ui_scale);

    // Draw children.
    auto DrawElements = [&](auto&& elems) {
        for (auto& e : FWD(elems)) e.draw(r);
    };

    // Determine if we need to sort the widgets by z order before drawing. We
    // only need to do this if at least two widgets differ in z order.
    auto v = visible_elements();
    if (rgs::adjacent_find(v, rgs::not_equal_to{}, &Element::z_order) == v.end()) DrawElements(v);
    else { // clang-format off
        std::vector<Element*> sorted_elements = v | utils::addrof | rgs::to<std::vector>();

        // Use a stable sort to avoid messing up the z order of elements with
        // the same z value (they should be drawn in layout order). This places
        // the elements with the largest z value last, which means they are drawn
        // on top of the others.
        rgs::stable_sort(sorted_elements, {}, &Element::z_order);
        DrawElements(sorted_elements | utils::deref);
    } // clang-format on
}

void Element::dump() {
    dump_impl(0);
}

void Element::dump_impl(i32 indent) {
    auto n = name();
    auto i = std::string(indent, ' ');
    auto has_focus = parent_screen().active_element == this;

    Log(
        "{}{}\033[33m{} \033[34m{} \033[31m[\033[35m{}, {}×{}\033[31m]{}\033[m\033[31m{}\033[m",
        i,
        has_focus ? "\033[1m"sv : ""sv,
        n,
        static_cast<void*>(this),
        computed_pos,
        computed_size.wd,
        computed_size.ht,
        focusable ? "#"sv : ""sv,
        children().empty() ? ""sv : " {{"sv
    );

    if (not children().empty()) {
        for (auto& c : children()) c.dump_impl(indent + 2);
        Log("{}\033[31m}}\033[m", i, n);
    }
}

void Element::focus() {
    parent_screen().active_element = this;
}

auto Element::parent_screen() -> Screen& {
    if (auto* s = cast<Screen>()) return *s;
    return utils::last(parents())->as<Screen>();
}

void Element::refresh() {
    // Do not refresh the element if the size hasn’t changed and if the
    // layout doesn’t need to be recomputed.
    if (style.size.is_fixed()) {
        if (Size{style.size.xval, style.size.yval} == computed_size and not layout_changed) return;
    }

    // Apply the fixed size to non-computed dimensions.
    for (auto a : Axes)
        if (style.size.is_fixed(a))
            computed_size[a] = style.size[a];

    // If the size is dynamic, we cannot recompute the layout until the
    // size is known.
    if (style.size.is_partially_dynamic()) return;

    // Recompute the layout.
    recompute_layout();
}

void Element::tick(MouseState& mouse, xy rel_pos) {
    tick_mouse(mouse, rel_pos);
}

void Element::tick_mouse(MouseState& mouse, xy rel_pos) {
    // We’re inside this element.
    if (box().contains(rel_pos)) {
        // Apply hover state.
        if (not under_mouse) {
            under_mouse = true;
            event_mouse_enter();
        }

        // Tick children.
        for (auto& e : children())
            e.tick_mouse(mouse, rel_pos - box().origin());

        // Check if we can focus or click on this element.
        if (mouse.left) {
            // Fire click events.
            if (event_click()) mouse.left = false;

            // Focus this element if possible. Consume the click so we don’t
            // unfocus it again.
            if (focusable) {
                mouse.left = false;
                focus();
            }

            // If the click was consumed, clear the active element if it’s not
            // this one.
            if (not mouse.left and parent_screen().active_element != this)
                parent_screen().active_element = nullptr;
        }
    }

    // We aren’t; fire the leave event if we were inside before.
    else if (under_mouse) {
        under_mouse = false;
        event_mouse_leave();
    }
}

void Element::set_ui_scale(f32 new_value) { _ui_scale = new_value; }

// =============================================================================
//  Text Widgets
// =============================================================================
/// Compute the offset of text from the bottom left corner of the box
/// that achieves horizontal and vertical centering.
///
/// This is simple to calculate but hard to get right, so do not change
/// this function unless you understand what you’re doing.
static auto CenterTextInBox(const Text& text, Size box_size) -> xy {
    // Compute the x offset based on the text width; this is the easy part.
    f32 x = text.width > box_size.wd ? 0 : (box_size.wd - text.width) / 2.f;

    // We need to add extra space for every line beyond the first.
    //
    // Note: This formula is known to be correct for 1–2 lines; it has not
    // been tested for more than 2 lines, so we might have to amend it at
    // some point.
    f32 ascender = text.font.strut_split().first;
    f32 strut = text.font.strut();
    strut += ascender * (text.lines - 1);

    // Bail out if we don’t have enough space or if the text is empty.
    if (text.empty or strut > box_size.ht) return xy{x, 0.f};

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
    f32 top_offs = ascender + (box_size.ht - strut) / 2;
    return xy{x, box_size.ht - top_offs};
}

TextEdit::TextEdit(Element* parent, FontSize sz, TextStyle text_style)
    : TextElement(parent, "", sz, text_style),
      placeholder{parent_screen().renderer.font(sz, text_style), "Placeholder"} {
    focusable = true;
    style.background = InactiveButtonColour;
}

TextElement::TextElement(Element* parent, std::string_view contents, FontSize sz, TextStyle text_style)
    : Element(parent),
      text{parent_screen().renderer.font(sz, text_style), contents} {
    style.size = SizePolicy::ComputedSize();
}

void TextEdit::draw(Renderer& r) {
    TextElement::draw(r);
    if (text.empty) DrawCenteredText(r, placeholder, style.text_colour.darken(.2f));
}

bool TextEdit::event_click() {
    // TODO: Clicking into the middle of the text.
    return false;
}

void TextEdit::event_focus_gained() {
    placeholder.content = U"Focused!";
    style.background = DefaultButtonColour;
}

void TextEdit::event_focus_lost() {
    placeholder.content = U"Not focused";
    style.background = InactiveButtonColour;
}

void TextEdit::refresh() {
    RefreshImpl(text.empty ? placeholder : text);
}

void TextElement::DrawCenteredText(Renderer& r, const Text& text, Colour colour) {
    auto pos = computed_pos + CenterTextInBox(text, computed_size);
    r.draw_text(text, pos, colour);
}

void TextElement::RefreshImpl(const Text& text) {
    // Compute the size based on the text contents.
    if (style.size.is_partially_computed()) {
        computed_size = {
            i32(text.width),
            std::max(i32(text.height), text.font.strut()),
        };
    }

    // Call into the parent to apply fixed sizes.
    Element::refresh();
}

void TextElement::draw(Renderer& r) {
    Element::draw(r);
    DrawCenteredText(r, text, style.text_colour);
}

void TextElement::refresh() {
    RefreshImpl(text);
}

