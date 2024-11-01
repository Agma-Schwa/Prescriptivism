module;
#include <utility>
#include <algorithm>
module pr.client.ui;

import pr.client.utils;
import pr.client.render;
import pr.client.render.gl;

using namespace pr;
using namespace pr::client;

constexpr Colour DefaultButtonColour{36, 36, 36, 255};
constexpr Colour HoverButtonColour{23, 23, 23, 255};

auto Position::absolute(Size screen_size, Size object_size) -> xy {
    return relative(vec2(), screen_size, object_size);
}

auto Position::relative(xy parent, Size parent_size, Size object_size) -> xy {
    static auto Clamp = [](i32 val, i32 obj_size, i32 total_size) -> i32 {
        if (val == Centered) return (total_size - obj_size) / 2;
        if (val < 0) return total_size + val - obj_size;
        return val;
    };

    auto [sx, sy] = parent_size;
    auto [obj_wd, obj_ht] = object_size;
    auto x = i32(parent.x) + Clamp(base.x, obj_wd, sx) + xadjust;
    auto y = i32(parent.y) + Clamp(base.y, obj_ht, sy) + yadjust;
    return {x, y};
}

Button::Button(
    ShapedText text,
    Position pos,
    i32 padding,
    i32 min_wd,
    i32 min_ht
) : label{std::move(text)},
    pos{pos} {
    sz.wd = std::max(min_wd, i32(label.width())) + 2 * padding;
    sz.ht = std::max(min_ht, i32(label.height() + label.depth())) + 2 * padding;
}

void Button::draw(Renderer& r) {
    auto bg = pos.absolute(r.size(), sz);
    auto text = Position::Center().voffset(i32(label.depth())).relative(bg, sz, label.size());
    r.draw_rect(bg, sz, hovered ? HoverButtonColour : DefaultButtonColour);
    r.draw_text(label, text);
}

void Button::refresh(Size screen_size) {
    SetBoundingBox(AABB(pos.absolute(screen_size, sz), sz));
}

void Screen::refresh(Size screen_size) {
    for (auto& e: children) e->refresh(screen_size);
}

void Screen::render(Renderer& r) {
    for (auto& e: children) e->draw(r);
}

void Screen::tick(MouseState st) {
    for (auto& e : children) {
        e->hovered = e->bounding_box().contains(st.pos);
        if (e->hovered and st.left) if (st.left) e->clicked();
    }
}
