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
import pr.packets;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Error Screen
// =============================================================================
ErrorScreen::ErrorScreen(Client& c) {
    msg = &Create<Label>(FontSize::Large, TextAlign::Center, Position::Center());

    auto& back = Create<Button>(
        c.renderer.make_text("Back", FontSize::Medium),
        Position::HCenter(150),
        10,
        125
    );

    back.on_click = [&] { c.enter_screen(*return_screen); };
}

void ErrorScreen::enter(Client& c, std::string t, Screen& return_to) {
    msg->update_text(std::move(t));
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

    auto& address = Create<TextEdit>(
        Position::HCenter(350),
        FontSize::Medium,
        c.renderer.make_text("Server Address", FontSize::Medium),
        10,
        false,
        250,
        25
    );

    auto& username = Create<TextEdit>(
        Position::HCenter(287),
        FontSize::Medium,
        c.renderer.make_text("Your Name", FontSize::Medium),
        10,
        false,
        250,
        25
    );

    auto& password = Create<TextEdit>(
        Position::HCenter(225),
        FontSize::Medium,
        c.renderer.make_text("Password", FontSize::Medium),
        10,
        true,
        250,
        25
    );

    // FIXME: Testing only. Remove these later.
    address.value(U"localhost");
    username.value(U"testuser");
    password.value(U"password");

    quit.on_click = [&] { c.input_system.quit = true; };
    connect.on_click = [&] {
        c.connexion_screen.enter(
            address.value(),
            username.value(),
            password.value()
        );
    };
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

void ConnexionScreen::enter(std::string addr, std::string name, std::string pass) {
    st = State::Entered;
    address = std::move(addr);
    username = std::move(name);
    password = std::move(pass);
    client.enter_screen(*this);
}

void ConnexionScreen::tick(InputSystem& input) {
    Screen::tick(input);
    switch (st) {
        case State::Aborted:
            connexion_thread.stop_and_release();
            client.enter_screen(client.menu_screen);
            break;

        case State::Connecting: {
            if (connexion_thread.running()) return;

            // Connexion thread has exited. Check if we have a connexion.
            auto conn = connexion_thread.value();
            if (not conn) {
                client.show_error(
                    std::format("Connexion failed ffffffffffffffffffffffff: {}", conn.error()),
                    client.menu_screen
                );
                return;
            }

            // We do! Tell the server who we are and switch to game screen.
            conn.value().send(packets::cs::Login(std::move(username), std::move(password)));
            client.game_screen.enter(std::move(conn.value()));
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
//  Game Screen
// =============================================================================
GameScreen::GameScreen(Client& c) : client(c) {
    auto& card = Create<Card>(
        c.renderer,
        Position::Center(),
        "P3M5",
        "Voiced\nvelar\nstop",
        "g",
        "-> ɣ\n-> w",
        10
    );

    auto& small = Create<Button>(
        c.renderer.make_text("Small", FontSize::Medium),
        Position{30, 30},
        10,
        125
    );

    auto& medium = Create<Button>(
        c.renderer.make_text("Medium", FontSize::Medium),
        Position::HCenter(30),
        10,
        125
    );

    auto& large = Create<Button>(
        c.renderer.make_text("Large", FontSize::Medium),
        Position{-30, 30},
        10,
        125
    );

    small.on_click = [&] { card.set_scale(Card::OtherPlayer); };
    medium.on_click = [&] { card.set_scale(Card::Field); };
    large.on_click = [&] { card.set_scale(Card::Large); };
}

void GameScreen::enter(net::TCPConnexion conn) {
    server_connexion = std::move(conn);
    client.enter_screen(*this);
}

void GameScreen::tick(InputSystem& input) {
    // Server has gone away.
    if (server_connexion->disconnected()) {
        server_connexion.reset();
        client.show_error("Disconnected: Server has gone away", client.menu_screen);
        return;
    }

    // Receive data from the server.
    tick_networking();
    Screen::tick(input);
}

void GameScreen::tick_networking() {
    server_connexion->receive([&](net::ReceiveBuffer& buf) {
        while (not server_connexion->disconnected() and not buf.empty()) {
            auto res = packets::HandleClientSidePacket(*this, buf);

            // If there was an error, close the connexion.
            if (not res) {
                server_connexion->disconnect();
                client.show_error(res.error(), client.menu_screen);
            }

            // And stop if the packet was incomplete.
            if (not res.value()) break;
        }
    });
}

// =============================================================================
//  Game Screen - Packet Handlers
// =============================================================================
namespace sc = packets::sc;
namespace cs = packets::cs;

void GameScreen::handle(sc::Disconnect packet) {
    server_connexion->disconnect();
    auto reason = [&] -> std::string_view {
        switch (packet.reason) {
            using Reason = packets::sc::Disconnect::Reason;
            case Reason::Unspecified: return "Disconnected";
            case Reason::ServerFull: return "Disconnected: Server full";
            case Reason::InvalidPacket: return "Disconnected: Client sent invalid packet";
            case Reason::UsernameInUse: return "Disconnected: User name already in use";
            default: return "Disconnected: <<<Invalid>>>";
        }
    }();
    client.show_error(std::string{reason}, client.menu_screen);
}

void GameScreen::handle(sc::HeartbeatRequest req) {
    server_connexion->send(cs::HeartbeatResponse{req.seq_no});
}

// =============================================================================
//  API
// =============================================================================
Client::Client() : renderer(Renderer(800, 600)) {
    enter_screen(menu_screen);
}

void Client::enter_screen(Screen& s) {
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
        current_screen->refresh(renderer);

        // Tick the screen.
        current_screen->tick(input_system);

        // Draw it.
        current_screen->draw(renderer);

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

void Client::show_error(std::string error, Screen& return_to) {
    error_screen.enter(
        *this,
        std::move(error),
        return_to
    );
}
