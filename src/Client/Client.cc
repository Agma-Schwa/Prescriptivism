module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <chrono>
#include <functional>
#include <SDL3/SDL.h>
#include <thread>
module pr.client;

import pr.utils;
import base.text;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Error Screen
// =============================================================================
ErrorScreen::ErrorScreen(Client& c) {
    msg = &Create<Label>(Position::Center());

    auto& back = Create<Button>(
        c.renderer.make_text("Back", FontSize::Medium),
        Position::HCenter(150),
        10,
        125
    );

    back.on_click = [&] { c.enter_screen(*return_screen); };
}

void ErrorScreen::enter(Client& c, std::string_view t, Screen& return_to) {
    msg->update_text(c.renderer.make_text(t, FontSize::Large));
    return_screen = &return_to;
    c.enter_screen(*this);
}

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
ConnexionScreen::ConnexionScreen(Client& c) : client{c} {
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

    Create<Throbber>(Position::Center());

    abort.on_click = [&] { st = State::Aborted; };
}

auto ConnexionScreen::connexion_thread_main(
    std::string address,
    std::stop_token st
) -> Result<net::TCPConnexion> {
    auto sock = net::TCPConnexion::Connect(address, net::DefaultPort);
    if (st.stop_requested()) return Error("Stop requested");
    return sock;
}

void ConnexionScreen::on_entered() {
    st = State::Entered;
}

void ConnexionScreen::tick(InputSystem& input) {
    Screen::tick(input);
    switch (st) {
        case State::Aborted:
            connexion_thread.stop_and_release();
            client.enter_screen(*client.menu_screen);
            break;

        case State::Connecting: {
            if (connexion_thread.running()) return;

            // Connexion thread has exited. Check if we have a connexion.
            auto conn = connexion_thread.value();
            if (not conn) {
                client.show_error(
                    std::format("Connexion failed: {}", conn.error()),
                    *client.menu_screen
                );
                return;
            }

            // We do! TODO: Switch to game screen.
            client.enter_screen(*client.menu_screen);
            return;
        }

        case State::Entered:
            if (connexion_thread.running()) return;

            // Restart it.
            st = State::Connecting;
            connexion_thread.start(&ConnexionScreen::connexion_thread_main, this, std::move(address));
            break;
    }
}

void ConnexionScreen::set_address(std::string addr) {
    address = std::move(addr);
}

// =============================================================================
//  API
// =============================================================================
Client::Client() : renderer(Renderer(800, 600)) {
    menu_screen = std::make_unique<MenuScreen>(*this);
    connexion_screen = std::make_unique<ConnexionScreen>(*this);
    error_screen = std::make_unique<ErrorScreen>(*this);
    enter_screen(*menu_screen);
}

void Client::connect_to_server(std::string_view ip_address) {
    Log("Connecting to server at {}", ip_address);
    connexion_screen->set_address(std::string{ip_address});
    enter_screen(*connexion_screen);
}

void Client::enter_screen(Screen& s) {
    Assert(&s, "Forgot to initialise screen");
    current_screen = &s;
    s.on_entered();
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

void Client::show_error(std::string_view error, Screen& return_to) {
    error_screen->enter(
        *this,
        error,
        return_to
    );
}
