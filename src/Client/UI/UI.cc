#include <Client/gl-headers.hh>
#include <Client/UI/UI.hh>

#include <base/Base.hh>
#include <base/Text.hh>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <ranges>
#include <string_view>
#include <utility>

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Helpers
// =============================================================================
auto Position::resolve(AABB parent_box, Size object_size) -> xy {
    return resolve(parent_box.size(), object_size);
}

auto Position::resolve(Size parent_size, Size object_size) -> xy {
    static auto Clamp = [](i32 val, i32 obj_size, i32 total_size) -> i32 {
        if (val == Centered) return (total_size - obj_size) / 2;
        if (val < 0) return total_size + val - obj_size;
        return val;
    };

    auto [sx, sy] = parent_size;
    auto [obj_wd, obj_ht] = object_size;
    auto x = Clamp(base.x, obj_wd, sx) + xadjust;
    auto y = Clamp(base.y, obj_ht, sy) + yadjust;

    auto Adjust = [&](i32 xa, i32 ya) {
        if (base.x != Centered) x -= xa;
        if (base.y != Centered) y -= ya;
    };

    switch (anchor) {
        case Anchor::North: Adjust(obj_wd / 2, obj_ht); break;
        case Anchor::NorthEast: Adjust(obj_wd, obj_ht); break;
        case Anchor::East: Adjust(obj_wd, obj_ht / 2); break;
        case Anchor::SouthEast: Adjust(obj_wd, 0); break;
        case Anchor::South: Adjust(obj_wd / 2, 0); break;
        case Anchor::SouthWest: break;
        case Anchor::West: Adjust(0, obj_ht / 2); break;
        case Anchor::NorthWest: Adjust(0, obj_ht); break;
        case Anchor::Center: Adjust(obj_wd / 2, obj_ht / 2); break;
    }

    return {x, y};
}

// =============================================================================
//  Element
// =============================================================================
void Element::SetBoundingBox(AABB aabb) {
    LIBBASE_DEBUG(_bb_size_initialised = true;)
    _bounding_box = aabb;
}

void Widget::UpdateBoundingBox(Size size) {
    auto scaled = size * ui_scale;
    SetBoundingBox(AABB{pos.resolve(parent.bounding_box, size), size});
    scaled_bounding_box = AABB{pos.resolve(parent.bounding_box, scaled), scaled};
}

// =============================================================================
//  Widget
// =============================================================================
Widget::Widget(Element* parent, Position pos) : _parent(parent), pos(pos) {
    Assert(parent, "Every widget must have a parent!");
}

bool Widget::has_parent(Element* other) {
    auto p = &parent;
    for (;;) {
        if (p == other) return true;
        auto w = p->cast<Widget>();
        if (not w) return false;
        p = &w->parent;
    }
}

auto Widget::hovered_child(xy) -> HoverResult {
    if (hoverable == Hoverable::Yes) hovered = true;
    return HoverResult::TakeIf(this, hoverable);
}

auto Widget::parent_screen() -> Screen& {
    Element* e = &parent;
    for (;;) {
        if (auto screen = e->cast<Screen>()) return *screen;
        e = &e->as<Widget>().parent;
    }
}

auto Widget::position() -> xy {
    return pos.resolve(parent.bounding_box, bounding_box.size() * ui_scale);
}

auto Widget::PushTransform(Renderer& r) -> Renderer::MatrixRAII {
    return r.push_matrix(scaled_bounding_box.origin(), ui_scale);
}

auto Widget::rbox() -> AABB { return {rpos(), bounding_box.size()}; }
auto Widget::rpos() -> xy {
    DebugAssert(
        _bb_size_initialised,
        "Accessing rpos() before bounding box was set! NEVER do "
        "UpdateBoundingBox(rpos()) or SetBoundingBox(rpos(), ...)!"
    );
    return pos.resolve(parent.bounding_box, bounding_box.size());
}

auto Widget::selected_child(xy) -> SelectResult {
    return SelectResult::TakeIf(this, selectable);
}

void Widget::set_ui_scale(f32 new_value) {
    if (_ui_scale == new_value) return;
    _ui_scale = new_value;
    needs_refresh = true;
}

void Widget::unselect() {
    if (not selected and not is<Group>()) return;
    auto& parent = parent_screen();
    unselect_impl(parent);
}

void Widget::unselect_impl(Screen& parent) {
    // If this is selected, unselect it.
    if (selected) {
        selected = false;
        if (parent.selected_element == this) parent.selected_element = nullptr;
        if (parent.hovered_element == this) parent.hovered_element = nullptr;
    }

    // Otherwise, if this is a group, try to unselect all of our children;
    // this is required if we’re e.g. deleting a group whose child needs to
    // be untagged as the selected element.
    else if (auto g = cast<Group>()) {
        for (auto& ch : g->children()) ch.unselect_impl(parent);
    }
}

void Widget::set_needs_refresh(bool new_value) {
    // Do NOT cache this since we need to propagate changes to our
    // parent, and that may not have been done yet because this may
    // have already been set before we were assigned a parent if this
    // is a new object.
    _needs_refresh = new_value;

    // Groups care about this because they need to recompute the
    // positions of their children.
    if (auto g = parent.cast<Group>())
        g->needs_refresh = true;
}

auto WidgetHolder::index_of(Widget& c) -> std::optional<u32> {
    auto it = rgs::find(widgets, &c, &std::unique_ptr<Widget>::get);
    if (it == widgets.end()) return std::nullopt;
    return u32(it - widgets.begin());
}

void WidgetHolder::remove(Widget& w) {
    w.unselect();
    auto erased = std::erase_if(widgets, [&](auto& c) { return c.get() == &w; });
    Assert(erased == 1, "Attempted to remove non-direct child?");
    if (auto g = dynamic_cast<Widget*>(this)) g->needs_refresh = true;
}

// =============================================================================
//  Basic Elements
// =============================================================================
Throbber::Throbber(Element* parent, Position pos)
    : Widget(parent, pos), vao(VertexLayout::Position2D) {
    vec2 verts[]{
        {-R, -R},
        {-R, R},
        {R, -R},
        {R, R}
    };
    vao.add_buffer(verts, gl::GL_TRIANGLE_STRIP);
    UpdateBoundingBox(Size{i32(R), i32(R)});
}

void Throbber::draw(Renderer& r) {
    static constexpr f32 Rate = 3; // Smaller means faster.

    // Uses absolute position because it may not have a parent.
    auto at = pos.resolve(r.size(), {i32(R), i32(R)});
    auto rads = f32(glm::radians(fmod(360 * Rate - SDL_GetTicks(), 360 * Rate) / Rate));
    auto xfrm = glm::identity<mat4>();
    xfrm = translate(xfrm, vec3(R, R, 0));
    xfrm = rotate(xfrm, rads, vec3(0, 0, 1));

    r.use(r.throbber_shader, {});
    r.throbber_shader.uniform("position", at.vec());
    r.throbber_shader.uniform("rotation", xfrm);
    r.throbber_shader.uniform("r", R);

    vao.draw_vertices();
}

void Image::draw(Renderer& r) {
    if (texture) r.draw_texture_sized(*texture, rbox());
}

void Image::UpdateDimensions() {
    if (not texture) {
        UpdateBoundingBox(Size{});
        return;
    }

    auto sz = texture->size;
    if (fixed_size.wd) sz.wd = fixed_size.wd;
    if (fixed_size.ht) sz.ht = fixed_size.ht;
    UpdateBoundingBox(sz);
}

TRIVIAL_CACHING_SETTER(Image, Size, fixed_size, UpdateDimensions());
TRIVIAL_CACHING_SETTER(Image, DrawableTexture*, texture, UpdateDimensions());

// =============================================================================
//  Group
// =============================================================================
auto Group::HoverSelectHelper(
    xy rel_pos,
    auto (Widget::*accessor)(xy)->SelectResult,
    Selectable Widget::* property
) -> SelectResult {
    auto Get = [&]<typename T>(T&& range) -> SelectResult {
        auto rel = rel_pos - bounding_box.origin();
        for (auto& c : std::forward<T>(range)) {
            if (c.bounding_box.contains(rel)) {
                auto res = (c.*accessor)(rel);
                if (not res.keep_searching) return res;
            }
        }

        // The group itself is a proxy widget that cannot be hovered.
        return SelectResult::No(this->*property);
    };

    // If two children overlap, we pick the first in the list, unless
    // the maximum gap is negative (which means that the widgets may
    // overlap with widgets on the right being above), in which case
    // we pick the last one.
    return max_gap < 0 ? Get(children() | vws::reverse) : Get(children());
}

void Group::clear() {
    for (auto& w : widgets) w->unselect();
    widgets.clear();
}

void Group::draw(Renderer& r) {
    auto _ = PushTransform(r);
    for (auto& c : visible_elements()) c.draw(r);
}

auto Group::hovered_child(xy rel_pos) -> SelectResult {
    return HoverSelectHelper(rel_pos, &Widget::hovered_child, &Widget::hoverable);
}

void Group::refresh(Renderer& r) {
    auto ch = children();
    if (ch.empty()) return;

    // Reset our bounding box to our parent’s before refreshing the
    // children; otherwise, nested groups can get stuck at a smaller
    // size: the child group will base its width around the parent’s
    // which in turn is based on the width of the children; what should
    // happen instead is that groups propagate the parent size downward
    // and adjust to their actual size after the children have been
    // positioned.
    SetBoundingBox(parent.bounding_box);

    // Refresh each element to make sure their sizes are up-to-date
    // and compute the total width of all elements.
    Axis a = vertical ? Axis::Y : Axis::X;
    i32 total_extent = 0;
    for (auto& c : ch) {
        c.refresh(r);
        total_extent += c.bounding_box.extent(a);

        // If the gap is *negative*, i.e. we’re supposed to overlap
        // elements, factor it into the calculation.
        if (max_gap < 0) total_extent += max_gap;
    }

    // Compute gap size.
    auto parent_extent = parent.bounding_box.extent(a);
    i32 gap = 0;
    if (total_extent < parent_extent and ch.size() > 1) {
        gap = std::min(
            max_gap,
            i32((parent_extent - total_extent) / (ch.size() - 1))
        );
    } else if (max_gap < 0) {
        gap = max_gap;
    }

    // Position the children.
    i32 offset = 0;
    for (auto& c : ch) {
        c.pos = Position(flip(a), alignment, offset);
        offset += c.bounding_box.extent(a) + gap;
    }

    // Update our bounding box.
    auto max = rgs::max(widgets | vws::transform([&](auto& w) { return w->bounding_box.extent(flip(a)); }));
    auto sz = Size{a, offset - gap, max};
    UpdateBoundingBox(sz);

    // And refresh the children again now that we know where everything is.
    for (auto& c : ch) c.refresh(r);
}

auto Group::selected_child(xy rel_pos) -> SelectResult {
    return HoverSelectHelper(rel_pos, &Widget::selected_child, &Widget::selectable);
}

void Group::swap(Widget* a, Widget* b) {
    auto ita = rgs::find_if(widgets, [&](auto& w) { return w.get() == a; });
    auto itb = rgs::find_if(widgets, [&](auto& w) { return w.get() == b; });
    Assert(ita != widgets.end() and itb != widgets.end(), "Widget not found in group?");
    std::iter_swap(ita, itb);
    needs_refresh = true;
}

void Group::make_selectable(Selectable new_value) {
    for (auto& c : children()) {
        if (auto g = c.cast<Group>()) g->make_selectable(new_value);
        c.selectable = new_value;
    }
}

TRIVIAL_CACHING_SETTER(Group, i32, max_gap);
TRIVIAL_CACHING_SETTER(Group, bool, vertical);
TRIVIAL_CACHING_SETTER(Group, i32, alignment);

// =============================================================================
//  Input Handler.
// =============================================================================
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
                kb_events.emplace_back(event.key.key, event.key.mod);
                break;

            case SDL_EVENT_TEXT_INPUT:
                text_input += text::ToUTF32(event.text.text);
                break;
        }
    }
}

void InputSystem::update_selection(bool is_element_selected) {
    if (was_selected == is_element_selected) return;
    was_selected = is_element_selected;
    if (is_element_selected) SDL_StartTextInput(renderer.sdl_window());
    else SDL_StopTextInput(renderer.sdl_window());
}

// =============================================================================
//  Screen
// =============================================================================
void Screen::DeleteAllChildren() {
    selected_element = nullptr;
    hovered_element = nullptr;
    widgets.clear();
}

void Screen::draw(Renderer& r) {
    r.set_cursor(Cursor::Default);
    for (auto& e : visible_elements()) e.draw(r);
}

void Screen::refresh(Renderer& r) {
    SetBoundingBox(AABB({0, 0}, r.size()));
    on_refresh(r);

    // Size hasn’t changed. Still update any elements that
    // requested a refresh. Also ignore visibility here.
    if (prev_size == r.size()) {
        for (auto& e : children()) {
            if (e.needs_refresh) {
                e.needs_refresh = false;
                e.refresh(r);
            }
        }

        return;
    }

    // Refresh every visible element, and every element that
    // requested a refresh.
    prev_size = r.size();
    for (auto& e : children()) {
        if (e.visible or e.needs_refresh) {
            e.needs_refresh = false;
            e.refresh(r);
        }
    }
}

void Screen::tick(InputSystem& input) {
    hovered_element = nullptr;

    // Deselect the currently selected element if there was a click.
    if (input.mouse.left and selected_element) selected_element->unselect();

    // First, reset all children’s properties.
    for (auto& e : visible_elements()) e.hovered = false;

    // Then, find the hovered/selected element.
    bool check_hover = true;
    for (auto& e : visible_elements()) {
        if (not check_hover and not input.mouse.left) break;
        if (not e.bounding_box.contains(input.mouse.pos)) continue;

        // Check if this element is being hovered over.
        if (check_hover) {
            auto res = e.hovered_child(input.mouse.pos);
            hovered_element = res.widget;
            check_hover = res.keep_searching;
        }

        // If there was a click, attempt to select an element.
        if (input.mouse.left) {
            auto res = e.selected_child(input.mouse.pos);
            selected_element = res.widget;
            defer { input.mouse.left = res.keep_searching; };

            // If we could select an element, mark it as selected and set
            // it as the target for the click instead.
            if (selected_element) {
                selected_element->selected = true;
                selected_element->event_click(input);
            }
        }
    }

    // Send any input to the selected element if there is one.
    if (selected_element) selected_element->event_input(input);

    // In any case, tell the input system whether we have a
    // selected element.
    input.update_selection(selected_element != nullptr);
}

// =============================================================================
//  Game Loop.
// =============================================================================
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
