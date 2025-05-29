#ifndef PRESCRIPTIVISM_CLIENT_CLIENT_HH
#define PRESCRIPTIVISM_CLIENT_CLIENT_HH

#include <Client/Game.hh>
#include <Client/Render/GL.hh>
#include <Client/Render/Render.hh>
#include <Client/UI/UI.hh>

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Packets.hh>
#include <Shared/TCP.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>

#include <functional>
#include <generator>
#include <memory>
#include <ranges>
#include <thread>
#include <vector>

namespace pr::client {
class Client;
class MenuScreen;
class ErrorScreen;
class ConnexionScreen;
class WaitingScreen;
class WordChoiceScreen;
} // namespace pr::client

// =============================================================================
//  General Screens
// =============================================================================
/// Main menu of the game.
class pr::client::MenuScreen : public Screen {
public:
    MenuScreen(Client& c);
};

/// Screen that displays an error.
class pr::client::ErrorScreen : public Screen {
    /// Text to display.
    Label* msg;

    /// The screen to return to after this one is closed.
    Screen* return_screen = nullptr;

public:
    ErrorScreen(Client& c);

    /// Set the text to display and the screen to return to.
    void enter(Client& c, std::string t, Screen& return_to);
};

// =============================================================================
//  Connexion Phase Screens
// =============================================================================
/// Screen that is displayed while we connect to a server.
///
/// This screen has all the connexion logic.
class pr::client::ConnexionScreen : public Screen {
    enum class State : u32 {
        Entered,    ///< The screen has just been entered.
        Connecting, ///< We are trying to connect in a separate thread.
        Aborted,    ///< The 'Cancel' button was pressed.
    };

    Client& client;
    State st;
    std::string address, username, password;
    Thread<net::TCPConnexion> connexion_thread;

public:
    ConnexionScreen(Client& c);

    void enter(std::string address, std::string username, std::string password);
    void tick(InputSystem& input) override;
    void set_address(std::string addr);

private:
    auto connexion_thread_main(std::string address, std::stop_token st) -> Result<net::TCPConnexion>;
};

class pr::client::WaitingScreen : public Screen {
public:
    WaitingScreen(Client& c);
};

class pr::client::WordChoiceScreen : public Screen {
    Client& client;
    CardStacks* cards;
    constants::Word original_word;
    CardStacks::Stack* selected = nullptr;

    void SendWord();

public:
    WordChoiceScreen(Client& c);

    // This takes an array instead of a span to force a specific word size.
    void enter(const constants::Word& word);
    void on_refresh() override;
    void tick(InputSystem& input) override;
};

// =============================================================================
//  Client
// =============================================================================
class pr::client::Client {
    LIBBASE_IMMOVABLE(Client);

public:
    /// The user input handler.
    InputSystem input_system{};

    /// Screens.
    MenuScreen menu_screen{*this};
    ConnexionScreen connexion_screen{*this};
    ErrorScreen error_screen{*this};
    WaitingScreen waiting_screen{*this};
    WordChoiceScreen word_choice_screen{*this};
    GameScreen game_screen{*this};

    /// Connexion to the game server.
    net::TCPConnexion server_connexion;

    /// Used by --connect.
    bool autoconfirm_word = false;

private:
    /// Screens that are currently open.
    std::vector<Screen*> screen_stack;

    explicit Client();

public:
    /// Run the client for ever.
    static void Run();

    /// Run the client for ever, and immediately connect to a server.
    static void RunAndConnect(
        std::string address,
        std::string username,
        std::string password
    );

    /// Pop the topmost screen off the stack. The screen below it is
    /// *not* reentered.
    void pop_screen();

    /// Push a screen onto the stack. The topmost screen is responsible for
    /// handling user input. Screens below it will still be drawn and refreshed,
    /// but will not be ticked or receive input.
    void push_screen(Screen& s);

    /// Swap out the topmost screen on the screen stack for the given screen.
    void set_screen(Screen& s);

    /// Show an error to the user.
    void show_error(std::string error, Screen& return_to);

#define X(name) void handle(packets::sc::name);
    SC_CONFIG_PACKETS(X)
#undef X

private:
    static void Startup();

    void RunGame();
    void Tick();
    void TickNetworking();
};

#endif // PRESCRIPTIVISM_CLIENT_CLIENT_HH
