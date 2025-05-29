#include <Client/Client.hh>
#include <Client/Game.hh>
#include <Client/UI/Animations.hh>

#include <Shared/Validation.hh>

#include <base/Base.hh>

#include <generator>
#include <ranges>

using namespace pr;
using namespace pr::client;

namespace sc = packets::sc;
namespace cs = packets::cs;

// =============================================================================
// Validation
// =============================================================================
struct GameScreen::Validator {
    const Player& cs;
    PlayerId us;

    auto operator[](usz i) const -> CardId { return cs.word->stacks()[i].top.id; }
    bool is_own_word() const { return us == cs.id; }
    auto size() const -> usz { return cs.word->stacks().size(); }
    bool stack_is_locked(usz i) const { return cs.word->stacks()[i].locked; }
    bool stack_is_full(usz i) const { return cs.word->stacks()[i].full; }
};

auto GameScreen::ValidatorFor(Player& p) -> Validator {
    return Validator{p, us.id};
}

// =============================================================================
// Card Preview.
// =============================================================================
CardPreview::CardPreview(Screen* parent, Position p) : Widget(parent), card{this, Position()} {
    pos = p;
    visible = false;
    hoverable = Hoverable::Transparent;
    selectable = Selectable::Transparent;
    card.scale = Card::Preview;
}

void CardPreview::draw() {
    auto _ = PushTransform();
    card.draw();
}

void CardPreview::refresh(bool) {
    // Always refresh this element.
    needs_refresh = true;

    // If there is no selected element, make the card invisible.
    auto& s = static_cast<Screen&>(parent);
    if (not s.hovered_element or not s.hovered_element->is<Card>()) {
        visible = false;
        return;
    }

    // Otherwise, make it visible and set the card id.
    visible = true;
    card.id = s.hovered_element->as<Card>().id;
    if (card.needs_refresh) {
        card.refresh(true);
        UpdateBoundingBox(card.bounding_box.size());
    }
}

// =============================================================================
// Effects
// =============================================================================
// Animation that plays a card in hand.
class GameScreen::PlayCard : public Animation {
    static constexpr auto MoveDuration = 250ms;
    static constexpr auto TotalDuration = MoveDuration + 750ms;
    static constexpr auto StartSize = Card::CardSize[Card::Hand];
    static constexpr auto EndSize = Card::CardSize[Card::Preview];

    GameScreen& g;
    Card card{&g, Position()};
    xy pos, start_pos, end_pos;
    f32 scale{};

public:
    explicit PlayCard(GameScreen& g, Card& c) : Animation(&PlayCard::Tick, TotalDuration), g{g} {
        g.ClearSelection(State::PlayedCard);
        g.our_hand->make_selectable(false);
        c.visible = false;
        card.id = c.id;
        card.scale = Card::Preview;
        card.refresh(true);
        start_pos = c.absolute_position();
        end_pos = Position::VCenter(150).resolve(g.bounding_box, EndSize);
        waiting = true;
        blocking = true;
        prevent_user_input = true;
    }

    void Tick() {
        auto t = timer.dt(MoveDuration);
        pos = lerp_smooth(start_pos, end_pos, t);
        scale = lerp_smooth(StartSize.ht / f32(EndSize.ht), 1.f, t);
    }

    void draw() override {
        card.draw_absolute(pos, scale);
    }

    void on_done() override {
        g.state = State::NoSelection;
        g.ResetHand();
    }
};

// =============================================================================
// Play Confirmation Screen
// =============================================================================
ConfirmPlaySelectedScreen::ConfirmPlaySelectedScreen(GameScreen& p): parent{p} {
    preview = &Create<Card>(Position::Center());
    preview->scale = Card::Preview;

    Create<Label>(
        "Are you sure you want to play this card?",
        FontSize::Large,
        Position::HCenter(-100)
    );

    auto& buttons = Create<Group>(Position::HCenter(100));
    buttons.create<Button>("Yes", Position(), [&] { Yes(); });
    buttons.create<Button>("No", Position(), [&] { No(); });
    buttons.gap = 100;
}

void ConfirmPlaySelectedScreen::on_entered() {
    Assert(parent.our_selected_card, "No card selected?");
    preview->id = parent.our_selected_card->id;
}

void ConfirmPlaySelectedScreen::Yes() {
    parent.PlayCardWithoutTarget();
    parent.client.pop_screen();
}

void ConfirmPlaySelectedScreen::No() {
    parent.ClearSelection();
    parent.client.pop_screen();
}

// =============================================================================
// Card Choice Challenge Screen
// =============================================================================
CardChoiceChallengeScreen::CardChoiceChallengeScreen(GameScreen& p) : parent{p} {
    message = &Create<Label>("", FontSize::Medium, Position::HCenter(-150));
    cards = &Create<CardStacks>(Position::Center().anchor_to(Anchor::Center));
    cards->scale = Card::Hand;
    cards->gap = -Card::CardSize[Card::Hand].wd / 2;
    confirm_button = &Create<Button>("Confirm", Position::HCenter(150), [&] { Confirm(); });
}

void CardChoiceChallengeScreen::enter(packets::CardChoiceChallenge c) {
    message->update_text(std::format( // clang-format off
        "{} {}{} card{} {}",
        c.mode == Exact     ? "Choose"sv : "You may choose"sv,
        c.mode == Exact     ? ""
      : c.mode == AtLeast   ? "at least "sv
                            : "up to "sv,
        c.count,
        c.count == 1 ? ""sv : "s"sv,
        c.title
    )); // clang-format on

    // Passing/confirming is disallowed if we must select a card.
    confirm_button->selectable = c.mode == AtMost ? Selectable::Yes : Selectable::No;

    // Clear out stale data and copy data from the challenge.
    selected.clear();
    cards->clear();
    count = c.count;
    mode = c.mode;

    // Initialise the cards.
    for (auto id : c.cards) cards->add_stack(id);
    cards->selection_mode = CardStacks::SelectionMode::Card;
    cards->make_selectable();

    // Recreate the preview so it’s drawn last.
    // TODO: Z order in the UI maybe?
    if (preview) remove(*preview);
    preview = &Create<CardPreview>();

    // Enter the screen.
    parent.client.push_screen(*this);
}

void CardChoiceChallengeScreen::tick(InputSystem& input) {
    Screen::tick(input);
    if (not selected_element) return;

    // If the selected element was already selected, unselect it and
    // remove it from the list of selected elements.
    auto c = &selected_element->as<Card>();
    if (std::erase(selected, c)) selected_element->unselect();

    // Otherwise, remember it and clear it in the screen.
    else {
        selected.push_back(c);
        selected_element = nullptr;
    }

    // Update selectability.
    for (auto& s : cards->stacks()) {
        bool selectable = selected.size() < count or s.top.selected;
        s.make_selectable(selectable);
        s.make_active(selectable);
    }

    // Update the confirm button.
    bool valid = validation::ValidateCardChoiceChallenge(mode, count, selected.size());
    confirm_button->selectable = valid ? Selectable::Yes : Selectable::No;
}

void CardChoiceChallengeScreen::Confirm() { // clang-format off
    cs::CardChoiceReply choice = selected
        | vws::transform([&](Card* c) { return cards->index_of(c->parent.as<CardStacks::Stack>()).value(); })
        | rgs::to<std::vector>();
    parent.client.server_connexion.send(choice);
    parent.client.pop_screen();
} // clang-format on

// =============================================================================
// Negation Challenge Screen
// =============================================================================
NegationChallengeScreen::NegationChallengeScreen(GameScreen& p) : parent{p} {
    auto& group = Create<Group>(Position::Center());
    auto& negation = group.create<Card>(Position());
    group.create<Arrow>(Position(), vec2{1, 0}, 200).thickness = 10;
    card = &group.create<Card>(Position());
    group.gap = 50;

    negation.id = CardId::P_Negation;
    negation.scale = Card::Preview;
    card->scale = Card::Preview;

    prompt = &Create<Label>("", FontSize::Large, Position::HCenter(-100));

    auto& buttons = Create<Group>(Position::HCenter(100));
    buttons.create<Button>("Yes", Position(), [&] { Negate(true); });
    buttons.create<Button>("No", Position(), [&] { Negate(false); });
    buttons.gap = 100;
}

void NegationChallengeScreen::Negate(bool negate) {
    parent.client.server_connexion.send(cs::PromptNegationReply{negate});
    parent.client.pop_screen();
}

void NegationChallengeScreen::enter(sc::PromptNegation p) {
    card->id = p.card_id;
    parent.client.push_screen(*this);
    prompt->update_text(std::format("Use Negation to protect yourself from {}?", CardDatabase[+p.card_id].name));
}

// =============================================================================
// Helpers
// =============================================================================
GameScreen::GameScreen(Client& c) : client(c) {
    // UI is set up in enter().
}

void GameScreen::ClearSelection(State new_state) {
    ResetWords();
    SetPlayerNamesSelectable();
    state = new_state;
    if (selected_element) selected_element->unselect();
    if (our_selected_card) our_selected_card->unselect();
    our_selected_card = nullptr;
}

void GameScreen::ClosePreview() {
    preview->visible = false;
    hovered_element = nullptr;
}

void GameScreen::Discard(base::u32 amount) {
    // 0 means discard the entire hand.
    if (amount == 0) our_hand->clear();
    else Log("TODO: Implement discarding {} cards", amount);
}

void GameScreen::Discard(CardStacks::Stack& stack) {
    ClearSelection();
    our_hand->remove(stack);
}

auto GameScreen::GetStackInHand(Card& card) -> std::pair<CardStacks::Stack&, u32> {
    auto& our_stack = card.parent.as<CardStacks::Stack>();
    auto idx = our_hand->index_of(our_stack);
    Assert(idx.has_value(), "Card not in hand?");
    return {our_stack, *idx};
}

auto GameScreen::PlayerById(PlayerId id) -> Player& {
    auto p = rgs::find(all_players, id, &Player::get_id);
    Assert(p != all_players.end(), "Player {} not found?", id);
    return **p;
}

auto GameScreen::PlayerForCardInWord(Card* c) -> Player* {
    Assert(c);
    auto stack = c->parent.cast<CardStacks::Stack>();
    if (not stack) return nullptr;
    return stack->parent.owner;
}

void GameScreen::ResetHand() {
    for (auto& c : our_hand->top_cards()) {
        if (
            state != State::NotOurTurn and
            (not utils::Empty(Targets(c)) or validation::AlwaysPlayable(c.id))
        ) {
            c.overlay = Card::Overlay::Default;
            c.selectable = Selectable::Yes;
        } else {
            c.overlay = Card::Overlay::Inactive;
            c.selectable = Selectable::No;
        }
    }
}

void GameScreen::ResetWords(
    Selectable s,
    Card::Overlay o
) {
    for (auto p : all_players) {
        p->word->make_selectable(s);
        p->word->set_overlay(o);
    }
}

auto GameScreen::SelectedPlayer() -> Player& {
    Assert(selected_element, "No element selected");
    auto& l = selected_element->as<Label>();
    return *rgs::find(other_players, &l, &Player::name_widget);
}

void GameScreen::SetPlayerNamesSelectable(Selectable s) {
    for (auto& p : other_players) p.name_widget->selectable = s;
}

void GameScreen::SwapSelectedCard() {
    Assert(our_selected_card, "No card selected?");
    Assert(selected_element, "No card to swap to?");
    SetPlayerNamesSelectable();
    if (selected_element == our_selected_card) return ClearSelection();
    our_selected_card->unselect();
    our_selected_card = nullptr;
    return TickNoSelection();
}

auto GameScreen::Targets(Card& c) -> std::generator<Target> {
    auto YieldStacksFromAll = [&](auto pred) -> std::generator<Target> {
        for (auto p : all_players) {
            auto v = ValidatorFor(*p);
            for (auto [i, s] : p->word->stacks() | vws::enumerate)
                if (pred(v, i))
                    co_yield Target{s};
        }
    };

#define YieldStacksFromAll(...) co_yield rgs::elements_of(YieldStacksFromAll([&](auto& v, isz i) __VA_ARGS__))

    if (c.id.is_sound()) {
        YieldStacksFromAll({
            // TODO: Handle evolutions that require an extra card.
            auto res = validation::ValidatePlaySoundCard(c.id, v, i);
            return res == validation::PlaySoundCardValidationResult::Valid;
        });
        co_return;
    }

    switch (c.id.value) {
        default: break;
        case CardIdValue::P_Descriptivism:
            YieldStacksFromAll({ return validation::ValidateP_Descriptivism(v, i); });
            break;

        case CardIdValue::P_SpellingReform: {
            auto v = ValidatorFor(us);
            for (auto [i, s] : us.word->stacks() | vws::enumerate)
                if (validation::ValidateP_SpellingReform(v, i))
                    co_yield Target{s};
        } break;
    }

#undef YieldStacksFromAll
}

// =============================================================================
// Packet Handlers
// =============================================================================
#define X(name)                                                                               \
    void GameScreen::handle(packets::sc::name packet) {                                       \
        if (effect_queue_empty()) {                                                           \
            HandleImpl(std::move(packet));                                                    \
            return;                                                                           \
        }                                                                                     \
                                                                                              \
        Queue([this, packet = std::move(packet)] mutable { HandleImpl(std::move(packet)); }); \
    }
SC_PLAY_PACKETS(X)
#undef X

void GameScreen::HandleImpl(sc::AddSoundToStack add) {
    auto& player = PlayerById(add.player);
    player.word->stacks()[add.stack_index].push(add.card);
}

void GameScreen::HandleImpl(sc::CardChoice c) {
    packets::CardChoiceChallenge c1 = std::move(c.challenge);
    card_choice_challenge_screen.enter(std::move(c1));
}

void GameScreen::HandleImpl(sc::Draw dr) {
    our_hand->add_stack(dr.card);
    ResetHand();
}

void GameScreen::HandleImpl(sc::DiscardAll) {
    Discard(0);
}

void GameScreen::HandleImpl(sc::EndTurn) {
    EndTurn();
}

void GameScreen::HandleImpl(sc::RemoveCard r) {
    // TODO: Show message to the user. This is only used when a card
    //       is removed via some effect, so show e.g. ‘One of your cards has
    //       been stolen!’ on the screen or sth like that.
    //
    //       In general, we need some API for flashing a message on the screen
    //       above everything else (including every open screen). Probably put
    //       that in the Client class.
    our_hand->remove(r.card_index);
}

void GameScreen::HandleImpl(sc::StackLockChanged lock) {
    auto& p = PlayerById(lock.player);
    p.word->stacks()[lock.stack_index].locked = lock.locked;
}

void GameScreen::HandleImpl(sc::StartTurn) {
    state = State::NoSelection;
    end_turn_button->selectable = Selectable::Yes;
    ResetHand();
    // TODO: Automatically go into passing mode if we cannot play anything in our hand.
}

void GameScreen::HandleImpl(sc::WordChanged wc) {
    auto& p = PlayerById(wc.player);
    p.word->clear();
    for (auto& s : wc.new_word) {
        auto& stack = p.word->add_stack();
        for (auto c : s) stack.push(c);
    }
}

void GameScreen::HandleImpl(sc::PromptNegation p) {
    negation_challenge_screen.enter(p);
}

// =============================================================================
//  Game Logic
// =============================================================================
void GameScreen::EndTurn() {
    state = State::NotOurTurn;
    end_turn_button->selectable = Selectable::No;
    our_hand->make_selectable(Selectable::No);
    our_hand->set_overlay(Card::Overlay::Inactive);
    ClearSelection();
}

void GameScreen::Pass() {
    ClearSelection(state == State::Passing ? State::NoSelection : State::Passing);

    // Update the button to cancel the passing action if pressed again.
    // TODO: In addition to changing the button’s state, also display a permanent
    //       message to the user (either above their hand or their word) along the
    //       lines of ‘Select a card in your hand to discard’.
    end_turn_button->update_text(state == State::Passing ? "Cancel"sv : "Pass"sv);

    // Prepare to select a card to discard.
    if (state == State::Passing) {
        our_hand->make_selectable();
        our_hand->set_overlay(Card::Overlay::Default);
    } else {
        ResetHand();
    }
}

void GameScreen::PlayCardWithoutTarget() {
    Assert(our_selected_card, "No card selected?");
    auto [stack, idx] = GetStackInHand(*our_selected_card);
    client.server_connexion.send(cs::PlayNoTarget{idx});
    Queue<PlayCard>(*our_selected_card);
}

void GameScreen::TickNoSelection() {
    if (not selected_element) return;
    Assert(not our_selected_card, "Should not be here if a card was previously selected");
    Assert(selected_element->has_parent(our_hand), "How did we select someone else’s card here?");
    our_selected_card = &selected_element->as<Card>();

    // Clear the selected element pointer of the screen so we can select a new card.
    selected_element = nullptr;

    switch (our_selected_card->id.value) {
        default: break;

        // Some cards can be played without a target.
        case CardId::P_Babel:
        case CardId::P_Whorf: {
            state = State::InAuxiliaryScreen;
            ResetWords();
            ClosePreview();
            client.push_screen(confirm_play_selected_screen);
            return;
        }

        // Some cards target a player.
        case CardId::P_Superstratum: {
            state = State::PlayerTarget;
            ResetWords();
            SetPlayerNamesSelectable(Selectable::Yes);
            return;
        }
    }

    // Others require exactly one target.
    state = State::SingleTarget;

    // If this is a sound card, make other player’s cards selectable if we can
    // play this card on it.
    ResetWords(Selectable::No, Card::Overlay::Inactive);
    for (auto [s, _] : Targets(*our_selected_card)) {
        s->make_selectable();
        s->overlay = Card::Overlay::Default;
    }
}

void GameScreen::TickNotOurTurn() {}

void GameScreen::TickPassing() {
    if (not selected_element) return;
    auto [stack, idx] = GetStackInHand(selected_element->as<Card>());
    client.server_connexion.send(cs::Pass{idx});
    end_turn_button->update_text("Pass");

    /// Make sure the user can’t press the pass button again (and the server
    /// is going to send an end turn packet anyway), so end the turn now.
    EndTurn();
}

void GameScreen::TickPlayerTarget() {
    if (not selected_element) return;
    Assert(our_selected_card, "We should have selected one of our cards");

    // We selected a different card in hand.
    if (selected_element->has_parent(our_hand)) return SwapSelectedCard();

    // At this point, the only thing that is selectable should be a player.
    auto& p = SelectedPlayer();
    switch (our_selected_card->id.value) {
        default:
            Log("TODO: Implement {}", CardDatabase[+our_selected_card->id].name);
            ClearSelection();
            break;

        case CardIdValue::P_Superstratum: {
            auto [stack, idx] = GetStackInHand(*our_selected_card);
            client.server_connexion.send(cs::PlayPlayerTarget{idx, p.id});
            Queue<PlayCard>(*our_selected_card);
        } break;
    }
}

void GameScreen::TickSingleTarget() {
    if (not selected_element) return;
    Assert(our_selected_card, "We should have selected one of our cards");

    auto PlaySingleTarget = [&] {
        auto& stack = selected_element->as<CardStacks::Stack>();
        auto owner = stack.parent.owner;
        Assert(owner, "Selected card without owner?");

        // Tell the server about this.
        auto& our_stack = our_selected_card->parent.as<CardStacks::Stack>();
        auto card_in_hand_index = our_hand->index_of(our_stack);
        auto selected_card_index = owner->word->index_of(stack);
        client.server_connexion.send(cs::PlaySingleTarget{
            card_in_hand_index.value(),
            owner->id,
            selected_card_index.value(),
        });

        Queue<PlayCard>(*our_selected_card);
    };

    // We selected a different card in hand.
    if (selected_element->has_parent(our_hand)) return SwapSelectedCard();

    // We’re playing a sound card on a sound card.
    if (our_selected_card->id.is_sound()) {
        PlaySingleTarget();
        return;
    }

    // We’re playing a power card.
    if (our_selected_card->id.is_power()) {
        switch (our_selected_card->id.value) {
            case CardIdValue::P_Descriptivism:
            case CardIdValue::P_SpellingReform:
                PlaySingleTarget();
                return;
            default:
                Log("TODO: Implement {}", CardDatabase[+our_selected_card->id].name);
                ClearSelection();
                break;
        }
    }
}

void GameScreen::enter(sc::StartGame sg) {
    DeleteAllChildren();

    end_turn_button = &Create<Button>("Pass", Position(-50, 50), [&] { Pass(); });
    other_players.clear();
    other_words = &Create<Group>(Position());
    for (auto [i, p] : sg.player_data | vws::enumerate) {
        if (i == sg.player_id) {
            us = Player("You", sg.player_id);
            us.word = &Create<CardStacks>(Position(), p.word);
            our_hand = &Create<CardStacks>(Position(), sg.hand);
            our_hand->scale = Card::Hand;
            our_hand->gap = -Card::CardSize[Card::Hand].wd / 2;
            our_hand->selection_mode = CardStacks::SelectionMode::Card;
            our_hand->animate = true;
            us.word->alignment = -5;
            continue;
        }

        auto& op = other_players.emplace_back(std::move(p.name), u8(i));
        auto& word_and_name = other_words->create<Group>(Position());
        word_and_name.vertical = true;
        op.word = &word_and_name.create<CardStacks>(Position(), p.word);
        op.word->scale = Card::OtherPlayer;
        op.word->alignment = -5;
        op.name_widget = &word_and_name.create<Label>(op.name, FontSize::Medium, Position());
    }

    // Build the player map *after* creating all the players, since they
    // might move while in the loop above.
    all_players.clear();
    us.word->owner = &us;
    all_players.push_back(&us);
    for (auto& p : other_players) {
        p.word->owner = &p;
        all_players.push_back(&p);
    }

    // The preview must be created at the end so it’s drawn
    // above everything else.
    preview = &Create<CardPreview>();

    // Put our hand at the bottom, with the cards slightly out of the screen.
    our_hand->pos = Position::HCenter(50).anchor_to(Anchor::Center);

    // Put our word in the center, but offset it upward to counteract the anchor,
    // which is there because we want additional cards to ‘hang’ from the top of
    // the word (i.e. cards added to a stack should go below the ‘baseline’).
    us.word->pos = Position::HCenter(Renderer::GetWindowSize().ht / 2 + Card::CardSize[us.word->scale].wd)
        .anchor_to(Anchor::North);

    // Position the other players’ words at the top of the screen.
    other_words->pos = Position::HCenter(-100);
    other_words->gap = 100;

    // Finally, ‘end’ our turn to reset everything.
    EndTurn();
    client.set_screen(*this);
}

void GameScreen::tick(InputSystem& input) {
    if (client.server_connexion.disconnected) {
        client.show_error("Disconnected: Server has gone away", client.menu_screen);
        return;
    }

    // Handle user input.
    Screen::tick(input);

    // Handle the game state.
    switch (state) {
        case State::NoSelection: TickNoSelection(); break;
        case State::NotOurTurn: TickNotOurTurn(); break;
        case State::Passing: TickPassing(); break;
        case State::SingleTarget: TickSingleTarget(); break;
        case State::PlayerTarget: TickPlayerTarget(); break;
        case State::PlayedCard: break;
        case State::InAuxiliaryScreen: Unreachable("Should never get here if in auxiliary screen");
    }
}
