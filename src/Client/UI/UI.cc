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
auto Position::resolve(AABB parent_box, Sz object_size) -> xy {
    return resolve(parent_box.size(), object_size);
}

auto Position::resolve(Sz parent_size, Sz object_size) -> xy {
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

void Widget::UpdateBoundingBox(Sz size) {
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
    return rgs::fold_left(parents<Widget>(), scaled_bounding_box.origin(), [](auto acc, auto w) {
        return acc + w->scaled_bounding_box.origin();
    });
}

void Widget::draw_absolute(xy a, f32 scale) {
    auto _ = Renderer::PushMatrix(a - scaled_bounding_box.origin(), scale);
    draw();
}

bool Widget::has_parent(Element* other) {
    return rgs::contains(parents(), other);
}

auto Widget::hovered_child(xy) -> HoverResult {
    if (hoverable == Hoverable::Yes) hovered = true;
    return HoverResult::TakeIf(this, hoverable);
}

auto Widget::parent_screen() -> Screen& {
    return utils::last(parents())->as<Screen>();
}

auto Widget::PushTransform() -> MatrixRAII {
    return Renderer::PushMatrix(scaled_bounding_box.origin(), ui_scale);
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

void WidgetHolder::DrawVisibleElements() {
    for (auto& e : visible_elements()) e.draw();
}

void WidgetHolder::RefreshElement(Widget& w) {
    // Always clear out the refresh flag before refreshing the element
    // since it may decide to set it back to true immediately.
    bool requested = w.needs_refresh;
    w.needs_refresh = false;
    w.refresh(requested);
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
    UpdateBoundingBox(Sz{length, thickness});
}

void Arrow::draw() {
    auto _ = PushTransform();
    Renderer::DrawArrow(xy(), xy(direction) * length, thickness, colour);
}

void Arrow::set_direction(vec2 new_value) {
    _direction = glm::normalize(new_value);
}

Throbber::Throbber(Element* parent, Position pos) : Widget(parent, pos)  {
    UpdateBoundingBox(Sz{i32(R), i32(R)});
}

void Throbber::draw() {
    static constexpr f32 Rate = 3; // Smaller means faster.

    // Uses absolute position because it may not have a parent.
    auto at = pos.resolve(Renderer::GetWindowSize(), {i32(R), i32(R)});
    Renderer::DrawThrobber(at, R, Rate);
}

void Image::draw() {
    if (texture) Renderer::DrawTextureSized(*texture, bounding_box);
}

void Image::refresh(bool full) {
    if (not full) return RefreshBoundingBox();
    if (not texture) {
        UpdateBoundingBox(Sz{});
        return;
    }

    auto sz = texture->size;
    if (fixed_size.wd) sz.wd = fixed_size.wd;
    if (fixed_size.ht) sz.ht = fixed_size.ht;
    UpdateBoundingBox(sz);
}

TRIVIAL_CACHING_SETTER(Image, Sz, fixed_size, );
TRIVIAL_CACHING_SETTER(Image, DrawableTexture*, texture);

// =============================================================================
//  Group
// =============================================================================
void Group::ComputeDefaultLayout() {
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
        RefreshElement(c);
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

void Group::FinishLayout() {
    Assert(not widgets.empty());

    // Refresh the children once so the extent calculations below are correct.
    for (auto& c : widgets) RefreshElement(c);

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
    auto sz = Sz{a, extent, max};
    UpdateBoundingBox(sz);

    // And refresh the children again now that we know where everything is.
    for (auto& c : widgets) RefreshElement(c);

    // Refreshing this group’s elements might have triggered the refresh
    // flag. Do not refresh again after we’re done here.
    needs_refresh = false;
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

void Group::RecomputeLayout() {
    ComputeDefaultLayout();
    FinishLayout();
}

void Group::clear() {
    for (auto& w : widgets) w.unselect();
    widgets.clear();
}

void Group::draw() {
    auto _ = PushTransform();
    DrawVisibleElements();
}

auto Group::hovered_child(xy rel_pos) -> SelectResult {
    return HoverSelectHelper(rel_pos, &Widget::hovered_child, &Widget::hoverable);
}

void Group::refresh(bool full) {
    if (widgets.empty()) return;
    RecomputeLayout();
}

auto Group::selected_child(xy rel_pos) -> SelectResult {
    return HoverSelectHelper(rel_pos, &Widget::selected_child, &Widget::selectable);
}

void Group::remove(u32 idx) {
    WidgetHolder::remove(idx);
}

void Group::remove(Widget& s) {
    WidgetHolder::remove(s);
}

void Group::swap(Widget* a, Widget* b) {
    widgets.swap_indices(widgets.index_of(*a).value(), widgets.index_of(*b).value());
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
    mouse.pos = {x, Renderer::GetWindowSize().ht - y};

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
                if (event.key.key == SDLK_F12) Renderer::ReloadAllShaders();
                kb_events.emplace_back(event.key.key, event.key.mod);
                break;

            case SDL_EVENT_TEXT_INPUT:
                text_input += text::ToUTF32(event.text.text);
                break;
        }
    }
}

// =============================================================================
//  Screen
// =============================================================================
void Screen::DeleteAllChildren() {
    selected_element = nullptr;
    hovered_element = nullptr;
    widgets.clear();
}

void Screen::draw() {
    Renderer::SetActiveCursor(Cursor::Default);
    DrawVisibleElements();
    for (auto& e : effects) {
        if (auto a = dynamic_cast<Animation*>(&e)) a->draw();
        if (e.blocking) break;
    }
}

void Screen::refresh() {
    SetBoundingBox(AABB({0, 0}, Renderer::GetWindowSize()));
    on_refresh();

    // Size hasn’t changed. Still update any elements that
    // requested a refresh. Also ignore visibility here.
    if (prev_size == Renderer::GetWindowSize()) {
        for (auto& e : widgets)
            if (e.needs_refresh)
                RefreshElement(e);
        return;
    }

    // Refresh every visible element, and every element that
    // requested a refresh.
    prev_size = Renderer::GetWindowSize();
    for (auto& e : widgets)
        if (e.visible or e.needs_refresh)
            RefreshElement(e);
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
    if (not effects.empty()) refresh();

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
