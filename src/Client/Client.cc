module;
#include <chrono>
#include <SDL3/SDL.h>
#include <thread>
module pr.client;

import pr.utils;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Main Menu Screen
// =============================================================================
MenuScreen::MenuScreen(Renderer& r)
    : title{r.make_text("Prescriptivism", FontSize::Huge)} {
    auto& quit = Create<Button>(
        r.make_text("Quit", FontSize::Large),
        Position::HCenter(150),
        10,
        125
    );

    Create<TextEdit>(
        Position::HCenter(250),
        10,
        250,
        25
    );

    quit.on_click = [] {
        Client::quit = true;
    };
}

void MenuScreen::render(Renderer& r) {
    r.clear(Colour{45, 42, 46, 255});
    r.draw_text(title, Position::HCenter(-50).absolute(r.size(), title.size()));
    Screen::render(r);
}

// =============================================================================
//  API
// =============================================================================
MouseState Client::mouse;
bool Client::quit = false;

Client::Client() : renderer(Renderer(800, 600)) {
    menu_screen = std::make_unique<MenuScreen>(renderer);
    current_screen = menu_screen.get();
}

void Client::Run() {
    constexpr chr::milliseconds ClientTickDuration = 16ms;
    while (not quit) {
        Renderer::Frame _ = renderer.frame();
        auto start_of_tick = chr::system_clock::now();

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
            }
        }

        // Refresh screen info.
        current_screen->refresh(renderer.size());

        // Tick the screen.
        current_screen->tick(mouse);

        // Draw it.
        current_screen->render(renderer);

        const auto end_of_tick = chr::system_clock::now();
        const auto tick_duration = chr::duration_cast<chr::milliseconds>(end_of_tick - start_of_tick);
        if (tick_duration < ClientTickDuration) {
            SDL_WaitEventTimeout(
                nullptr,
                i32((ClientTickDuration - tick_duration).count())
            );
        } else {
            Log("Client tick took too long: {}ms", tick_duration.count());
        }
    }
}
