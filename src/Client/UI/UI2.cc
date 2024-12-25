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

    // Apply the layout policy.
    switch (l.policy) {
        // Pack the elements along the layout axis.
        case Layout::Packed:
        case Layout::PackedCenter: {
            auto pos = ComputeStartingPosition(total_extent);
            for (auto& e : elements) {
                e.computed_pos[a] = pos;
                pos += e.computed_size[a] + l.gap;
            }
        } break;

        // Put all elements in the same place.
        case Layout::Overlap:
        case Layout::OverlapCenter: {
            auto pos = ComputeStartingPosition(max_extent);
            for (auto& e : elements) e.computed_pos[a] = pos;
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

    // Draw children.
    for (auto& e : visible_elements()) e.draw(r);
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

