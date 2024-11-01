module;
#include <algorithm>
#include <SDL3/SDL.h>
#include <string_view>
#include <utility>
module pr.client.ui;

import pr.client.utils;
import pr.client.render;
import pr.client.render.gl;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Constants
// =============================================================================
constexpr Colour DefaultButtonColour{36, 36, 36, 255};
constexpr Colour HoverButtonColour{23, 23, 23, 255};

// =============================================================================
//  Helpers
// =============================================================================
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

// =============================================================================
//  Elements
// =============================================================================
void Button::draw(Renderer& r) {
    auto bg = pos.absolute(r.size(), sz);
    r.draw_rect(bg, sz, hovered ? HoverButtonColour : DefaultButtonColour);
    TextBox::draw(r);
}

TextBox::TextBox(
    ShapedText text,
    Position pos,
    i32 padding,
    i32 min_wd,
    i32 min_ht
) : pos{pos},
    padding{padding},
    min_wd{min_wd},
    min_ht{min_ht} {
    UpdateText(std::move(text));
}

void TextBox::UpdateText(ShapedText new_text) {
    label = std::move(new_text);
    sz.wd = std::max(min_wd, i32(label.width())) + 2 * padding;
    sz.ht = std::max(min_ht, i32(label.height() + label.depth())) + 2 * padding;
}

void TextBox::draw(Renderer& r) {
    auto bg = pos.absolute(r.size(), sz);
    auto text = Position::Center().voffset(i32(label.depth())).relative(bg, sz, label.size());
    r.draw_text(label, text);
}

void TextBox::refresh(Size screen_size) {
    SetBoundingBox(AABB(pos.absolute(screen_size, sz), sz));
}

void TextEdit::draw(Renderer& r) {
    if (dirty) {
        dirty = false;
        UpdateText(r.make_text(text, size));
    }

    auto bg = pos.absolute(r.size(), sz);
    r.draw_rect(bg, sz, selected ? HoverButtonColour : DefaultButtonColour);
    TextBox::draw(r);
}

void TextEdit::event_text_input(std::string_view input) {
    text += input;
    dirty = true;
}

// =============================================================================
//  Input Handler.
// =============================================================================
void InputSystem::process_events() {
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

            case SDL_EVENT_TEXT_INPUT:
                text_input += event.text.text;
                Log("Text: {}", text_input);
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
void Screen::refresh(Size screen_size) {
    for (auto& e : children) e->refresh(screen_size);
}

void Screen::render(Renderer& r) {
    for (auto& e : children) e->draw(r);
}

void Screen::tick(InputSystem& input) {
    // Deselect the currently selected element if there was a click.
    if (input.mouse.left) selected = nullptr;

    // Tick each child.
    for (auto& e : children) {
        // First, reset all of the child’s properties so we can
        // recompute them.
        e->reset_properties();

        // If the cursor is within the element’s bounds, mark it as hovered.
        e->hovered = e->bounding_box().contains(input.mouse.pos);

        // If, additionally, we had a click, select the element and fire the
        // event handler.
        if (e->hovered and input.mouse.left) {
            if (e->selectable) selected = e.get();
            e->event_click();
        }
    }

    // Mark the selected element as selected once more.
    if (selected) {
        selected->selected = true;
        if (not input.text_input.empty()) selected->event_text_input(input.text_input);
    }

    // In any case, tell the input system whether we have a
    // selected element.
    input.update_selection(selected != nullptr);
}
