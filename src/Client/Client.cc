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
MenuScreen::MenuScreen(Client& c) {
    Create<Label>(
        c.renderer.make_text("Prescriptivism", FontSize::Title),
        Position::HCenter(-50)
    );

    auto& quit = Create<Button>(
        c.renderer.make_text("Quit", FontSize::Medium),
        Position::HCenter(75),
        10,
        125
    );

    auto& connect = Create<Button>(
        c.renderer.make_text("Connect", FontSize::Medium),
        Position::HCenter(150),
        10,
        125
    );

    auto& text_edit = Create<TextEdit>(
        Position::HCenter(250),
        FontSize::Medium,
        10,
        250,
        25
    );

    quit.on_click = [&] { c.input_system.quit = true; };
    connect.on_click = [&] { c.connect_to_server(text_edit.value()); };
}

// =============================================================================
//  Connexion Screen
// =============================================================================
ConnexionScreen::ConnexionScreen(Client& c): client{c} {
    Create<Label>(
        c.renderer.make_text("Connecting to server...", FontSize::Large),
        Position::HCenter(-100)
    );

    auto& abort = Create<Button>(
        c.renderer.make_text("Abort", FontSize::Medium),
        Position::HCenter(150),
        10,
        125
    );

    abort.on_click = [&] { st = State::Aborted; };
}

void ConnexionScreen::enter() {
    st = State::Entered;
}

void ConnexionScreen::tick(InputSystem& input) {
    Screen::tick(input);
    switch (st) {
        case State::Aborted:
            Log("Aborted connexion attempt.");
            client.enter_screen(*client.menu_screen);
            break;

        case State::Connecting:
            break;

        case State::Entered:
            Log("TODO: Connect");
            st = State::Connecting;
            break;
    }
}

// =============================================================================
//  API
// =============================================================================
Client::Client() : renderer(Renderer(800, 600)) {
    menu_screen = std::make_unique<MenuScreen>(*this);
    connexion_screen = std::make_unique<ConnexionScreen>(*this);
    enter_screen(*menu_screen);
}

void Client::connect_to_server(std::string_view ip_address) {
    Log("Connecting to server at {}", ip_address);
    enter_screen(*connexion_screen);
}

void Client::enter_screen(Screen& s) {
    current_screen = &s;
    s.enter();
}

void Client::run() {
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
