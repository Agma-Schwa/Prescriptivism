module;
#include <chrono>
#include <SDL3/SDL.h>
#include <thread>
module pr.client;

import pr.utils;
import base.text;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Main Menu Screen
// =============================================================================
MenuScreen::MenuScreen(Client& c)
    : title{c.renderer.make_text("Prescriptivism", FontSize::Huge)} {
    auto& quit = Create<Button>(
        c.renderer.make_text("Quit", FontSize::Large),
        Position::HCenter(150),
        10,
        125
    );

    Create<TextEdit>(
        Position::HCenter(250),
        FontSize::Large,
        10,
        250,
        25
    );

    quit.on_click = [&c] { c.input_system.quit = true; };
}

void MenuScreen::render(Renderer& r) {
    r.clear(Colour{45, 42, 46, 255});
    r.draw_text(title, Position::HCenter(-50).absolute(r.size(), title.size()));
    Screen::render(r);
}

// =============================================================================
//  API
// =============================================================================
Client::Client() : renderer(Renderer(800, 600)) {
    menu_screen = std::make_unique<MenuScreen>(*this);
    current_screen = menu_screen.get();
}

void Client::Run() {
    constexpr chr::milliseconds ClientTickDuration = 16ms;
    while (not input_system.quit) {
        Renderer::Frame _ = renderer.frame();
        auto start_of_tick = chr::system_clock::now();

        // Handle user input.
        input_system.process_events();

        // Refresh screen info.
        current_screen->refresh(renderer.size());

        // Tick the screen.
        current_screen->tick(input_system);

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
