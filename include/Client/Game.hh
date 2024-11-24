#ifndef PRESCRIPTIVISM_CLIENT_GAME_HH
#define PRESCRIPTIVISM_CLIENT_GAME_HH

#include <Client/Render/GL.hh>
#include <Client/Render/Render.hh>
#include <Client/UI/UI.hh>

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Packets.hh>
#include <Shared/TCP.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>

#include <ranges>
#include <vector>

namespace pr::client {
class Client;
class ConfirmPlaySelectedScreen;
class CardChoiceChallengeScreen;
class NegationChallengeScreen;
class GameScreen;
class Player;
class CardPreview;
}

class pr::client::Player {
    LIBBASE_MOVE_ONLY(Player);

    /// The server-side player id.
    Readonly(u8, id);

    /// The player name.
    Readonly(std::string, name);

public:
    /// The current word of this player.
    CardStacks* word{};

    /// The name widget of this player. This is null if the player is us.
    Label* name_widget{};

    explicit Player() = default;
    explicit Player(std::string name, u8 id) : _id{id}, _name{std::move(name)} {}
};

/// Widget that shows the hovered card of the parent screen.
class pr::client::CardPreview : public Widget {
    Card card;

public:
    CardPreview(Screen* parent, Position p = Position::VCenter(-100));
    void refresh(Renderer &r) override;
    void draw(Renderer &r) override;
};

/// This screen is used to confirm whether the user actually wants
/// to play a card they selected.
class pr::client::ConfirmPlaySelectedScreen : public Screen {
    GameScreen& parent;
    Card* preview;

public:
    ConfirmPlaySelectedScreen(GameScreen& p);

    void on_entered() override;

private:
    void Yes();
    void No();
};

/// This screen is used to display a set of cards from which
/// the player can/must choose a certain number.
class pr::client::CardChoiceChallengeScreen : public Screen {
    using enum packets::CardChoiceChallenge::Mode;
    GameScreen& parent;
    Label* message;
    CardStacks* cards;
    Button* confirm_button;
    CardPreview* preview{};
    std::vector<Card*> selected{};
    packets::CardChoiceChallenge::Mode mode;
    u32 count;

public:
    CardChoiceChallengeScreen(GameScreen& p);

    void enter(packets::CardChoiceChallenge c);
    void tick(InputSystem &input) override;

private:
    void Confirm();
};

/// This screen is used to handle the 'Negation' card.
class pr::client::NegationChallengeScreen : public Screen {
    GameScreen& parent;
    Label* prompt;
    Card* card;

public:
    NegationChallengeScreen(GameScreen& p);

    void enter(packets::sc::PromptNegation p);

private:
    void Negate(bool negate);
};

/// This screen renders the actual game.
class pr::client::GameScreen : public Screen {
    struct Validator;
    friend Validator;
    friend ConfirmPlaySelectedScreen;
    friend CardChoiceChallengeScreen;
    friend NegationChallengeScreen;

    enum struct State {
        /// The starting state. Nothing is selected.
        NoSelection,

        /// It is not our turn. User interaction is passed.
        NotOurTurn,

        /// A card in hand is selected, and we are waiting for
        /// the user to select a target for it.
        ///
        /// 'our_selected_card' holds the selected sound card.
        SingleTarget,

        /// Same as 'SingleTarget', but we need to select a player
        /// instead.
        PlayerTarget,

        /// We pressed the pass button; prompt the user to select
        /// a card to discard.
        Passing,

        /// Another screen (which is drawn on top of this one) is
        /// currently handling user input; do nothing.
        InAuxiliaryScreen,
    };

    /// A targeted card in someone’s word.
    struct Target {
        CardStacks::Stack* stack;
        std::optional<u32> card_idx;

        explicit Target(CardStacks::Stack& s) : stack{&s} {}
    };

    Client& client;
    ConfirmPlaySelectedScreen confirm_play_selected_screen{*this};
    CardChoiceChallengeScreen card_choice_challenge_screen{*this};
    NegationChallengeScreen negation_challenge_screen{*this};

    /// The end turn / pass / cancel button in the lower
    /// right corner of the screen.
    Button* end_turn_button;

    /// The other players in the game.
    std::vector<Player> other_players;

    /// All players, including us.
    std::vector<Player*> all_players;

    /// Our player object.
    Player us;

    /// The cards in our hand.
    CardStacks* our_hand{};

    /// The words and names of other players.
    Group* other_words{};

    /// The card widget used to preview a card.
    CardPreview* preview{};

    /// The last card that was selected by the player
    Card* our_selected_card{};

    /// The current game state.
    State state = State::NotOurTurn;

public:
    explicit GameScreen(Client& c);
    void enter(packets::sc::StartGame sg);
    void on_refresh(Renderer& r) override;
    void tick(InputSystem& input) override;

#define X(name) void handle(packets::sc::name);
    SC_PLAY_PACKETS(X)
#undef X

private:
    void ClearSelection(State new_state = State::NoSelection);
    void ClosePreview();
    void Discard(u32 amount);
    void Discard(CardStacks::Stack& stack);
    void EndTurn();
    auto GetStackInHand(Card& card) -> std::pair<CardStacks::Stack&, u32>;
    void SetPlayerNamesSelectable(Selectable s = Selectable::No);
    void Pass();
    auto PlayerById(PlayerId id) -> Player&;
    auto PlayerForCardInWord(Card* c) -> Player*; /// Return the player that owns this card in their word.
    void PlayCardWithoutTarget();
    void ResetHand();
    void ResetWords(Selectable s = Selectable::No, Card::Overlay o = Card::Overlay::Default);
    auto SelectedPlayer() -> Player&;
    void SwapSelectedCard();

    /// Get all valid targets for the card at this index, assuming it is in our hand.
    auto Targets(Card& c) -> std::generator<Target>;

    void TickNoSelection();
    void TickNotOurTurn();
    void TickPassing();
    void TickPlayerTarget();
    void TickSingleTarget();
    auto ValidatorFor(Player& p) -> Validator;
};

#endif // PRESCRIPTIVISM_CLIENT_GAME_HH
