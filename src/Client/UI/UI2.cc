#include <Client/UI/UI2.hh>

using namespace pr;
using namespace pr::client;
using namespace pr::client::ui;

// =============================================================================
//  Input Handler.
// =============================================================================
extern void DumpActiveScreen();

bool InputSystem::has_input() {
    return not text_input.empty() or not kb_events.empty();
}

void InputSystem::game_loop(std::function<void()> tick) {
    constexpr auto ClientTickDuration = 16ms;
    while (not quit) {
        auto start_of_tick = chr::system_clock::now();

        // Handle user input.
        process_events();

        tick();

        const auto end_of_tick = chr::system_clock::now();
        const auto tick_duration = chr::duration_cast<chr::milliseconds>(end_of_tick - start_of_tick);
        if (tick_duration < ClientTickDuration) {
            SDL_WaitEventTimeout(
                nullptr,
                i32((ClientTickDuration - tick_duration).count())
            );
        } else {
#ifndef PRESCRIPTIVISM_ENABLE_SANITISERS
            Log("Client tick took too long: {}ms", tick_duration.count());
#endif
        }
    }
}

void InputSystem::process_events() {
    kb_events.clear();
    text_input.clear();

    // Get mouse state.
    mouse = {};
    f32 x, y;
    SDL_GetMouseState(&x, &y);
    mouse.pos = {x, renderer.size().ht - y};

    // Process events.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            default: break;
            case SDL_EVENT_QUIT:
                quit = true;
                break;

            // Record the button presses instead of acting on them immediately; this
            // has the effect of debouncing clicks within a single tick.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) mouse.left = true;
                if (event.button.button == SDL_BUTTON_RIGHT) mouse.right = true;
                if (event.button.button == SDL_BUTTON_MIDDLE) mouse.middle = true;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_F12) renderer.reload_shaders();
                if (event.key.key == SDLK_F11) DumpActiveScreen();
                kb_events.emplace_back(event.key.key, event.key.mod);
                break;

            case SDL_EVENT_TEXT_INPUT:
                text_input += text::ToUTF32(event.text.text);
                break;
        }
    }
}

void InputSystem::set_accept_text_input(bool new_value) {
    if (_accept_text_input == new_value) return;
    _accept_text_input = new_value;
    if (new_value) SDL_StartTextInput(renderer.sdl_window());
    else SDL_StopTextInput(renderer.sdl_window());
}

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

void Screen::tick(InputSystem& input) {
    tick(input.mouse, input.mouse.pos);

    // If there is an active element, set it to receive text input.
    input.accept_text_input = active_element != nullptr;

    // If there is input, send it to the active element.
    if (input.has_input() and active_element)
        active_element->event_input(input);
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

void Element::set_layout_changed(bool new_value) {
    if (_layout_changed == new_value) return;
    _layout_changed = new_value;

    // Propagate layout changes upward.
    if (_layout_changed and parent) parent->layout_changed = true;
}

void Element::tick(MouseState& mouse, xy rel_pos) {
    tick_mouse(mouse, rel_pos);
}

void Element::tick_mouse(MouseState& mouse, xy rel_pos) {
    bool inside = box().contains(rel_pos);

    // Tick the mouse position if we’re inside the element or if we
    // just left it.
    if (inside or under_mouse) {
        // Apply hover state.
        if (inside != under_mouse) {
            under_mouse = inside;
            if (inside) event_mouse_enter();
            else event_mouse_leave();
        }

        // Tick children.
        for (auto& e : children())
            e.tick_mouse(mouse, rel_pos - box().origin());
    }

    // Check if we can focus or click on this element.
    if (inside and mouse.left) {
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
      label{parent_screen().renderer.font(sz, text_style), contents} {
    style.size = SizePolicy::ComputedSize();
}

void TextEdit::RecomputeCursorOffset() {
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
    if (
        parent_screen().active_element == this and
        not text.empty() and
        not clusters.empty() and
        (no_blink_ticks or parent_screen().renderer.blink_cursor())
    ) {
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
}

void TextEdit::draw(Renderer& r) {
    RecomputeCursorOffset();
    TextElement::draw(r);
    if (text.empty()) DrawText(r, placeholder, style.text_colour.darken(.2f));
}

bool TextEdit::event_click() {
    // TODO: Clicking into the middle of the text.
    return false;
}

void TextEdit::event_focus_gained() {
    style.background = DefaultButtonColour;
}

void TextEdit::event_focus_lost() {
    cursor_offs = -1;
    style.background = InactiveButtonColour;
}

void TextEdit::event_input(InputSystem& input) {
    if (not input.text_input.empty()) {
        text_changed = true;
        no_blink_ticks = 20;
        text.insert(cursor, input.text_input);
        cursor += i32(input.text_input.size());
    }

    auto Paste = [&] {
        if (SDL_HasClipboardText()) {
            text += text::ToUTF32(SDL_GetClipboardText());
            text_changed = true;
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
                    text_changed = true;
                } else if (cursor != 0) {
                    --cursor;
                    text.erase(cursor, 1);
                    text_changed = true;
                }
                break;
            case SDLK_DELETE:
                if (cursor != i32(text.size())) {
                    text.erase(cursor, 1);
                    text_changed = true;
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

    // Adding text may require to adjust the size of the text box, so
    // mark us for a layout recomputation.
    if (text_changed) layout_changed = true;
}

void TextEdit::event_mouse_enter() {
    parent_screen().renderer.set_cursor(Cursor::IBeam);
}

void TextEdit::event_mouse_leave() {
    parent_screen().renderer.set_cursor(Cursor::Default);
}

void TextEdit::refresh() {
    if (text_changed) {
        text_changed = false;
        label.content = hide_text ? std::u32string(text.size(), U'•') : text;
        label.font.shape(label, &clusters);
    }

    RefreshImpl(text.empty() ? placeholder : label);
}

void TextElement::DrawText(Renderer& r, const Text& text, Colour colour) {
    auto pos = computed_pos + CenterTextInBox(text, computed_size);
    r.draw_text(text, pos, colour);
    if (cursor_offs != -1) {
        auto [asc, desc] = label.font.strut_split();
        r.draw_line(
            xy(i32(pos.x) + cursor_offs, pos.y - i32(desc)),
            xy(i32(pos.x) + cursor_offs, pos.y + i32(asc)),
            Colour::White
        );
    }
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
    DrawText(r, label, style.text_colour);
}

void TextElement::refresh() {
    RefreshImpl(label);
}
