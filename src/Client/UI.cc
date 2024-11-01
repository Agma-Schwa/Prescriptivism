module;
#include <algorithm>
#include <cmath>
#include <ranges>
#include <SDL3/SDL.h>
#include <string_view>
#include <utility>
module pr.client.ui;

import base.text;
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
constexpr char32_t Backspace = U'\b';
constexpr char32_t BackspaceWord = U'\x18';
constexpr char32_t Delete = U'\x7F';

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

auto TextBox::TextPos(Renderer& r) -> xy {
    auto bg = pos.absolute(r.size(), sz);
    return Position::Center().voffset(i32(label.depth())).relative(bg, sz, label.size());
}


void TextBox::draw(Renderer& r) {
    auto text = TextPos(r);
    r.draw_text(label, text);
    if (cursor_offs != -1) {
        r.draw_line(
            xy(i32(text.x) + cursor_offs, text.y - i32(label.depth())),
            xy(i32(text.x) + cursor_offs, text.y + i32(label.height())),
            Colour::White
        );
    }
}

void TextBox::refresh(Size screen_size) {
    SetBoundingBox(AABB(pos.absolute(screen_size, sz), sz));
}

void TextEdit::draw(Renderer& r) {
    if (dirty) {
        dirty = false;
        UpdateText(r.make_text(text, size, &clusters));
    }

    // Use HarfBuzz cluster information to position the cursor: if the cursor
    // position corresponds to an entry in the clusters array (e.g. the cursor
    // is at position 3, and the array has an entry with value 3), position the
    // cursor right before the character that starts at that cluster (i.e. the
    // n-th character, where n is the index of the entry with value 3).
    //
    // If there is no entry, find the smallest cluster value 's' closest to the
    // cursor position, take the difference between it and the next cluster, and
    // lerp the cursor in the middle of the character at 's'.
    if (no_blink_ticks) no_blink_ticks--;
    if (selected and not clusters.empty() and (no_blink_ticks or r.blink_cursor())) {
        cursor_offs = [&] -> i32 {
            // Cursor is at the end of the text.
            if (cursor == i32(text.size())) return i32(label.width());

            // Cursor is too far right. Put it at the very end.
            auto it = rgs::lower_bound(clusters, cursor, {}, &ShapedText::Cluster::index);
            if (it == clusters.end()) it = clusters.begin() + clusters.size() - 1;

            // Cursor is right before a character.
            if (it->index == cursor) return i32(it->xoffs);

            // Cursor is in the middle of a character; interpolate into it.
            auto next = std::next(it);
            auto x1 = it->xoffs;
            auto x2 = next == clusters.end() ? label.width() : next->xoffs;
            auto ni = next == clusters.end() ? text.size() : next->index;
            auto x = i32(std::lerp(x1, x2, f32(cursor - it->index) / f32(ni - it->index)));
            return x;
        }();
    } else {
        cursor_offs = -1;
    }

    if (hovered) r.set_cursor(Cursor::IBeam);

    auto bg = pos.absolute(r.size(), sz);
    r.draw_rect(bg, sz, selected ? HoverButtonColour : DefaultButtonColour);
    TextBox::draw(r);
}

void TextEdit::event_click(InputSystem& input) {
    // Figure out where we clicked and set the cursor accordingly;
    // we do this by iterating over all clusters; as soon as we find
    // one whose offset brings us further away from the click position,
    // we stop and go back to the one before it.
    auto pos = TextPos(input.renderer);
    i32 mx = input.mouse.pos.x;
    i32 x0 = pos.x;
    i32 x1 = x0 + i32(label.width());
    if (mx < x0) cursor = 0;
    else if (mx > x1) cursor = i32(text.size());
    else if (clusters.size() < 2) cursor = 0;
    else {
        i32 x = x0;
        i32 d = std::abs(x - mx);
        for (ShapedText::Cluster* prev = nullptr; auto& c : clusters) {
            auto nd = std::abs(x + c.xoffs - mx);
            if (nd > d) {
                if (not prev) cursor = 0;
                else cursor = prev->index;
                return;
            }
            d = nd;
            prev = &c;
        }
        cursor = i32(text.size());
    }
}


void TextEdit::event_input(InputSystem& input) {
    // Copy text into the buffer.
    if (not input.text_input.empty()) {
        no_blink_ticks = 10;
        dirty = true;
        for (auto c : input.text_input) {
            switch (c) {
                default: text.insert(cursor++, 1, c); break;
                case Backspace:
                    if (cursor != 0) text.erase(--cursor, 1);
                    break;
                case Delete:
                    if (cursor != i32(text.size())) text.erase(cursor, 1);
                    break;
                case BackspaceWord: {
                    static constexpr std::u32string_view ws = U" \t\n\r\v\f";
                    u32stream segment{text.data(), usz(cursor)};
                    auto pos = segment.trim_back().drop_back_until_any(ws).size();
                    text.erase(pos, cursor - pos);
                    cursor = i32(pos);
                } break;
            }
        }
    }

    if (input.mov.left) cursor = std::max(0, cursor - 1);
    if (input.mov.right) cursor = std::min(i32(text.size()), cursor + 1);
    if (input.mov.home) cursor = 0;
    if (input.mov.end) cursor = i32(text.size());
    if (input.keyboard_input) no_blink_ticks = 10;
}

// =============================================================================
//  Input Handler.
// =============================================================================
void InputSystem::process_events() {
    text_input.clear();
    keyboard_input = false;
    mov = {};

    // Get mouse state.
    mouse = {};
    f32 x, y;
    SDL_GetMouseState(&x, &y);
    mouse.pos = {x, renderer.size().ht - y};

    auto Paste = [&] {
        if (SDL_HasClipboardText()) text_input += text::ToUTF32(SDL_GetClipboardText());
    };

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
                keyboard_input = true;
                switch (event.key.key) {
                    default: break;
                    case SDLK_BACKSPACE:
                        if (event.key.mod & SDL_KMOD_CTRL) text_input += BackspaceWord;
                        else text_input += Backspace;
                        break;
                    case SDLK_DELETE:
                        // if (event.key.mod & SDL_KMOD_CTRL) text_input += DeleteWord;
                        // else
                        text_input += Delete;
                        break;
                    case SDLK_LEFT: mov.left = true; break;
                    case SDLK_RIGHT: mov.right = true; break;
                    case SDLK_UP: mov.up = true; break;
                    case SDLK_DOWN: mov.down = true; break;
                    case SDLK_HOME: mov.home = true; break;
                    case SDLK_END: mov.end = true; break;
                    case SDLK_V:
                        if (event.key.mod & SDL_KMOD_CTRL) Paste();
                        break;
                    case SDLK_INSERT:
                        if (event.key.mod & SDL_KMOD_SHIFT) Paste();
                        break;
                }
                break;

            case SDL_EVENT_TEXT_INPUT:
                keyboard_input = true;
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
void Screen::refresh(Size screen_size) {
    for (auto& e : children) e->refresh(screen_size);
}

void Screen::render(Renderer& r) {
    r.set_cursor(Cursor::Default);
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
            e->event_click(input);
        }
    }

    // Mark the selected element as selected once more.
    if (selected) {
        selected->selected = true;
        selected->event_input(input);
    }

    // In any case, tell the input system whether we have a
    // selected element.
    input.update_selection(selected != nullptr);
}
