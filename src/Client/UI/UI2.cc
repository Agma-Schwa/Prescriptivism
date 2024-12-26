#include <Client/UI/UI2.hh>

using namespace pr;
using namespace pr::client;
using namespace pr::client::ui;

void Element::BuildLayout(Layout l, Axis a, i32 total_extent, i32 max_extent) {
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

    // Apply the layout policy.
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
            auto pos = ComputeStartingPosition(max_extent);
            LayOutElements([&](auto& e) { e.computed_pos[a] = pos; });
        } break;
    }
}

void Element::RecomputeLayout() {
    if (elements.empty()) return;

    // First, refresh each fixed-sized element and collect the minimum extent
    // of this container. Start with the gap, which is inserted between every
    // pair of elements.
    Size total_size = (i32(elements.size()) - 1) * style.gap();
    Size max_size = {};
    for (auto& e : elements) {
        if (e.style.size.fixed()) {
            e.refresh();
            e.computed_pos = {};
            total_size += e.computed_size;
            max_size = Size{
                std::max(max_size.wd, e.computed_size.wd),
                std::max(max_size.ht, e.computed_size.ht),
            };
        } else {
            Todo("TODO below");
        }
    }

    // TODO: Dynamic elements.

    // Lay out both axes.
    BuildLayout(style.horizontal, Axis::X, total_size[Axis::X], max_size[Axis::X]);
    BuildLayout(style.vertical, Axis::Y, total_size[Axis::Y], max_size[Axis::Y]);
}

void Element::draw(Renderer& r) {
    // Draw background.
    r.draw_rect(computed_pos, computed_size, style.background);

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

void Element::refresh() {
    if (style.size.fixed()) {
        auto f = style.size.as_fixed();

        // Do nothing if the size hasn’t changed, and we don’t need
        // to recompute the layout for other reasons.
        if (f == computed_size and not layout_changed) return;
        computed_size = f;
        layout_changed = false;
        RecomputeLayout();
        return;
    }

    // Dynamic.
    Todo();
}

/// Setters.
void Element::set_ui_scale(f32 new_value) { _ui_scale = new_value; }

