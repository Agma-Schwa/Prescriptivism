#include <Client/UI/UI2.hh>

using namespace pr;
using namespace pr::client;
using namespace pr::client::ui;

void Element::BuildPackedLayout() {
    // First, refresh each fixed-sized element and collect the minimum extent
    // of this container.
    Size total_fixed_size = 0;
    for (auto& e : elements) {
        if (e.style.size.fixed()) {
            e.refresh();
            total_fixed_size += e.computed_size;
        } else {
            Todo("TODO below");
        }
    }

    // TODO: Dynamic elements.

    // Stack the elements along the layout axis.
    auto layout_axis = style.vertical ? Axis::Y : Axis::X;
    xy pos = {0, 0};
    for (auto& e : elements) {
        e.computed_pos = pos;
        pos[layout_axis] += e.computed_size[layout_axis];
    }
}


void Element::RecomputeLayout() {
    switch (style.layout) {
        case LayoutPolicy::Packed: BuildPackedLayout(); break;
    }
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

