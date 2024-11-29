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

auto client::lerp_smooth(Position a, Position b, f32 t) -> Position {
    static constexpr auto C = Position::Centered;
    Position pos = a;

    // Interpolate X and Y, but center if either is centered.
    if (a.base.x == C or b.base.x == C) pos.base.x = C;
    else pos.base.x = lerp_smooth(a.base.x, b.base.x, t);
    if (a.base.y == C or b.base.y == C) pos.base.y = C;
    else pos.base.y = lerp_smooth(a.base.y, b.base.y, t);

    // Interpolate adjustments.
    pos.xadjust = lerp_smooth(a.xadjust, b.xadjust, t);
    pos.yadjust = lerp_smooth(a.yadjust, b.yadjust, t);
    return pos;
}

// =============================================================================
//  Element
// =============================================================================
void Element::SetBoundingBox(AABB aabb) {
    _bounding_box = aabb;
}

void Widget::RefreshBoundingBox() {
    UpdateBoundingBox(bounding_box.size());
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

auto Widget::absolute_position() -> xy {
    auto p = scaled_bounding_box.origin();
    auto e = &parent;
    for (;;) {
        if (auto w = e->cast<Widget>()) {
            p += w->scaled_bounding_box.origin();
            e = &w->parent;
        } else {
            return p;
        }
    }
}

void Widget::draw_absolute(Renderer& r, xy a, f32 scale) {
    auto _ = r.push_matrix(a - scaled_bounding_box.origin(), scale);
    draw(r);
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

auto Widget::PushTransform(Renderer& r) -> Renderer::MatrixRAII {
    return r.push_matrix(scaled_bounding_box.origin(), ui_scale);
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
    // ALWAYS check these since we may be about to delete this widget.
    if (parent.hovered_element == this) parent.hovered_element = nullptr;
    if (parent.selected_element == this) parent.selected_element = nullptr;

    // If this is selected, unselect it.
    if (selected) selected = false;

    // Otherwise, if this is a group, try to unselect all of our children;
    // this is required if we’re e.g. deleting a group whose child needs to
    // be untagged as the selected element.
    if (auto g = cast<Group>()) {
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

void WidgetHolder::DrawVisibleElements(Renderer& r) {
    for (auto& e : visible_elements()) e.draw(r);
}

void WidgetHolder::RefreshElement(Renderer& r, Widget& w) {
    // Always clear out the refresh flag before refreshing the element
    // since it may decide to set it back to true immediately.
    bool requested = w.needs_refresh;
    w.needs_refresh = false;
    w.refresh(r, requested);
}

auto WidgetHolder::index_of(Widget& c) -> std::optional<usz> {
    return widgets.index_of(c);
}

void WidgetHolder::remove(Widget& w) {
    w.unselect();
    auto erased = widgets.erase(w);
    Assert(erased, "Attempted to remove non-direct child?");
    if (auto g = dynamic_cast<Widget*>(this)) g->needs_refresh = true;
}

void WidgetHolder::remove(usz idx) {
    Assert(idx < widgets.size(), "Index out of bounds!");
    remove(widgets[idx]);
}

// =============================================================================
//  Basic Elements
// =============================================================================
Arrow::Arrow(Element* parent, Position pos, vec2 direction, i32 length)
    : Widget(parent, pos), _direction(glm::normalize(direction)), length(length) {
    UpdateBoundingBox(Size{length, thickness});
}

void Arrow::draw(Renderer& r) {
    auto _ = PushTransform(r);
    r.draw_arrow(xy(), xy(direction) * length, thickness, colour);
}

void Arrow::set_direction(vec2 new_value) {
    _direction = glm::normalize(new_value);
}

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
    if (texture) r.draw_texture_sized(*texture, bounding_box);
}

void Image::refresh(Renderer&, bool full) {
    if (not full) return RefreshBoundingBox();
    if (not texture) {
        UpdateBoundingBox(Size{});
        return;
    }

    auto sz = texture->size;
    if (fixed_size.wd) sz.wd = fixed_size.wd;
    if (fixed_size.ht) sz.ht = fixed_size.ht;
    UpdateBoundingBox(sz);
}

TRIVIAL_CACHING_SETTER(Image, Size, fixed_size, );
TRIVIAL_CACHING_SETTER(Image, DrawableTexture*, texture);

// =============================================================================
//  Group
// =============================================================================
Group::InterpolateGroupPositions::InterpolateGroupPositions(Group& g, Token)
    : Animation(&InterpolateGroupPositions::tick, Duration), g{g} {
    blocking = true;
    prevent_user_input = true;

    // Save the current positions.
    for (auto& w : g.widgets) positions[&w].start = w.pos;

    // Compute where everything should be and save those positions too.
    ComputeEndPositions();
    g.animation = this;
}

void Group::InterpolateGroupPositions::ComputeEndPositions() {
    g.ComputeDefaultLayout(g.parent_screen().renderer);
    for (auto& w : g.widgets) positions[&w].end = w.pos;
    g.FinishLayout(g.parent_screen().renderer); // Recompute BB.
}

void Group::InterpolateGroupPositions::on_done() {
    g.needs_refresh = true;
    g.animation = nullptr;
}

void Group::InterpolateGroupPositions::tick() {
    /// Another widget was added or removed.
    if (g.needs_refresh) {
        // Add the start positions of elements that were added.
        for (auto& w : g.widgets)
            if (not positions.contains(&w))
                positions[&w].start = w.pos;

        // And recompute the final layout.
        ComputeEndPositions();
    }

    // Interpolate the elements’ positions.
    auto t = timer.dt();
    for (auto& w : g.widgets) {
        auto pos = positions.get(&w);
        if (not pos) continue;
        w.pos = lerp_smooth(pos->start, pos->end, t);
    }

    // Refresh our children.
    for (auto& c : g.widgets) g.RefreshElement(g.parent_screen().renderer, c);
}

void Group::ComputeDefaultLayout(Renderer& r) {
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
    for (auto& c : widgets) {
        RefreshElement(r, c);
        total_extent += c.bounding_box.extent(a);

        // If the gap is *negative*, i.e. we’re supposed to overlap
        // elements, factor it into the calculation.
        if (gap < 0) total_extent += gap;
    }

    // Compute gap size.
    auto parent_extent = parent.bounding_box.extent(a);
    i32 g = 0;
    if (total_extent < parent_extent and widgets.size() > 1) {
        g = std::min(
            gap,
            i32((parent_extent - total_extent) / (widgets.size() - 1))
        );
    } else if (gap < 0) {
        g = gap;
    }

    // Position the children.
    i32 offset = 0;
    for (auto& c : widgets) {
        c.pos = Position(flip(a), alignment, offset);
        offset += c.bounding_box.extent(a) + g;
    }
}

void Group::FinishLayout(Renderer& r) {
    Assert(not widgets.empty());

    // Refresh the children once so the extent calculations below are correct.
    for (auto& c : widgets) RefreshElement(r, c);

    // Compute the combined extent along the layout axis.
    i32 extent{};
    Axis a = vertical ? Axis::Y : Axis::X;
    if (widgets.size() == 1) {
        extent = widgets.front().bounding_box.size().extent(a);
    } else {
        // Get the leftmost origin and rightmost end point.
        auto min = rgs::min(widgets | vws::transform([&](auto& w) { return w.bounding_box.origin().extent(a); }));
        auto max = rgs::max(widgets | vws::transform([&](auto& w) { return w.bounding_box.end(a); }));
        extent = max - min;
    }

    // Compute the maximum extent along the secondary axis.
    auto max = rgs::max(widgets | vws::transform([&](auto& w) { return w.bounding_box.extent(flip(a)); }));

    // Update our bounding box.
    auto sz = Size{a, extent, max};
    UpdateBoundingBox(sz);

    // And refresh the children again now that we know where everything is.
    for (auto& c : widgets) RefreshElement(r, c);
}

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
    return gap < 0 ? Get(children() | vws::reverse) : Get(children());
}

void Group::OnRemove() {
    if (not animate or animation) return;
    parent_screen().Queue(std::make_unique<InterpolateGroupPositions>(*this));
}

void Group::RecomputeLayout(Renderer& r) {
    ComputeDefaultLayout(r);
    FinishLayout(r);

    // Refreshing this group’s elements might have triggered the refresh
    // flag. Do not refresh again after we’re done here.
    needs_refresh = false;
}

void Group::clear() {
    for (auto& w : widgets) w.unselect();
    widgets.clear();
}

void Group::draw(Renderer& r) {
    auto _ = PushTransform(r);
    DrawVisibleElements(r);
}

auto Group::hovered_child(xy rel_pos) -> SelectResult {
    return HoverSelectHelper(rel_pos, &Widget::hovered_child, &Widget::hoverable);
}

void Group::refresh(Renderer& r, bool full) {
    if (widgets.empty()) return;

    // If we’re in an animation, then our layout is controlled
    // by it; don’t do anything here in that case.
    if (not animation) RecomputeLayout(r);
}

auto Group::selected_child(xy rel_pos) -> SelectResult {
    return HoverSelectHelper(rel_pos, &Widget::selected_child, &Widget::selectable);
}

void Group::remove(u32 idx) {
    WidgetHolder::remove(idx);
    OnRemove();
}

void Group::remove(Widget& s) {
    WidgetHolder::remove(s);
    OnRemove();
}

void Group::swap(Widget* a, Widget* b) {
    widgets.swap_indices(widgets.index_of(*a).value(), widgets.index_of(*b).value());
    needs_refresh = true;
}

void Group::make_selectable(Selectable new_value) {
    for (auto& c : widgets) {
        if (auto g = c.cast<Group>()) g->make_selectable(new_value);
        c.selectable = new_value;
    }
}

TRIVIAL_CACHING_SETTER(Group, i32, gap);
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
    DrawVisibleElements(r);
    for (auto& e : effects) {
        if (auto a = dynamic_cast<Animation*>(&e)) a->draw(r);
        if (e.blocking) break;
    }
}

void Screen::refresh(Renderer& r) {
    SetBoundingBox(AABB({0, 0}, r.size()));
    on_refresh(r);

    // Size hasn’t changed. Still update any elements that
    // requested a refresh. Also ignore visibility here.
    if (prev_size == r.size()) {
        for (auto& e : widgets)
            if (e.needs_refresh)
                RefreshElement(r, e);
        return;
    }

    // Refresh every visible element, and every element that
    // requested a refresh.
    prev_size = r.size();
    for (auto& e : widgets)
        if (e.visible or e.needs_refresh)
            RefreshElement(r, e);
}

void Screen::tick(InputSystem& input) {
    // Tick animations. Note that this may create even more effects,
    // so take care not to fall victim to iterator invalidation here.
    bool prevent_user_input = false;
    for (usz i = 0; i < effects.size(); ++i) {
        effects[i].tick();
        if (not effects[i].done()) {
            if (effects[i].prevent_user_input) prevent_user_input = true;
            if (effects[i].blocking) break;
        } else {
            effects[i].on_done();
        }
    }

    // If any effects were ticked, refresh the screen again. Not doing
    // this needs to weird in-between flickering for a single frame if
    // an effect happens to modify UI state in a way that requires a
    // refresh.
    if (not effects.empty()) refresh(input.renderer);

    // Remove any that are done.
    effects.erase_if(&Effect::done);

    // Do not do anything if we’re blocking user interaction.
    if (prevent_user_input) return;

    // Reset the currently hovered element since we always recompute
    // this; we also need to make sure to clear its hovered flag here.
    if (hovered_element) {
        hovered_element->hovered = false;
        hovered_element = nullptr;
    }

    // Deselect the currently selected element if there was a click.
    if (input.mouse.left and selected_element) selected_element->unselect();

    // Whether we need to keep searching for the hovered element; this
    // needs to be a separate flag since we may want to continue searching
    // even if we’ve already found a hovered element, and vice versa.
    bool check_hover = true;

    // Then, find the hovered/selected element.
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

void Screen::QueueImpl(std::unique_ptr<Effect> effect, bool flush_queue) {
    // Signal to all effects that they can stop waiting.
    if (flush_queue)
        for (auto& e : effects)
            e.waiting = false;

    // And add this one at the end.
    effects.push_back(std::move(effect));
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
