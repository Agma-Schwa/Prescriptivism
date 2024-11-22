#include <Client/Client.hh>
#include <Client/Game.hh>

#include <Shared/Validation.hh>

#include <base/Base.hh>

#include <generator>
#include <ranges>

using namespace pr;
using namespace pr::client;

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
// Play Confirmation Screen
// =============================================================================
ConfirmPlaySelectedScreen::ConfirmPlaySelectedScreen(pr::client::GameScreen& p) : parent{p} {
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
    cards->autoscale = true;

    auto& buttons = Create<Group>(Position::HCenter(150));
    buttons.create<Button>("Confirm", Position(), [&] { Confirm(); });
    pass_button = &buttons.create<Button>("Pass", Position(), [&] { Pass(); });
    buttons.gap = 100;
}

void CardChoiceChallengeScreen::enter(packets::CardChoiceChallenge c) {
    message->update_text(std::format(
        "{} {}{} card{} {}",
        c.mode == Exact ? "Choose"sv : "You may choose"sv,
        c.mode == Exact     ? ""
        : c.mode == AtLeast ? "at least "sv
                            : "up to "sv,
        c.count,
        c.count == 1 ? ""sv : "s"sv,
        c.title
    ));

    pass_button->selectable = c.mode == Exact ? Selectable::No : Selectable::Yes;
    count = c.count;
    mode = c.mode;
    cards->clear();
    for (auto id : c.cards) cards->add_stack(id);
    parent.client.push_screen(*this);
}

void CardChoiceChallengeScreen::Confirm() {
    Log("TODO: Confirm");
}

void CardChoiceChallengeScreen::Pass() {
    Log("TODO: Pass");
}

// =============================================================================
// Helpers
// =============================================================================
GameScreen::GameScreen(Client& c) : client(c) {
    // UI is set up in enter().
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
        if (not Empty(Targets(c)) or validation::AlwaysPlayable(c.id)) {
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
    for (auto& p : other_players)
        if (p.name_widget == &l)
            return p;
    Unreachable();
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
//  Game Logic
// =============================================================================
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

    // Also required if this is called before we tick the preview.
    hovered_element = nullptr;
}

void GameScreen::Discard(CardStacks::Stack& stack) {
    ClearSelection();
    our_hand->remove(stack);
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
    client.server_connexion.send(packets::cs::PlayNoTarget{idx});
    Discard(stack);
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
    client.server_connexion.send(packets::cs::Pass{idx});
    Discard(stack);
    end_turn_button->update_text("Pass");

    /// Make sure the user can’t press the pass button again (and the server
    /// is going to send an end turn packet anyway), so end the turn now.
    end_turn();
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
            client.server_connexion.send(packets::cs::PlayPlayerTarget{idx, p.id});
            Discard(stack);
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
        client.server_connexion.send(packets::cs::PlaySingleTarget{
            card_in_hand_index.value(),
            owner->id,
            selected_card_index.value(),
        });

        // Remove the card we played.
        Discard(our_stack);
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

// =============================================================================
//  API
// =============================================================================
void GameScreen::add_card(PlayerId id, u32 stack_idx, CardId card) {
    auto& player = PlayerById(id);
    player.word->stacks()[stack_idx].push(card);
}

void GameScreen::add_card_to_hand(CardId id) {
    our_hand->add_stack(id);
}

void GameScreen::discard(base::u32 amount) {
    // 0 means discard the entire hand.
    if (amount == 0) our_hand->clear();
    else Log("TODO: Implement discarding {} cards", amount);
}

void GameScreen::end_turn() {
    state = State::NotOurTurn;
    end_turn_button->selectable = Selectable::No;
    our_hand->make_selectable(Selectable::No);
    our_hand->set_overlay(Card::Overlay::Inactive);
    ClearSelection();
}

void GameScreen::enter(packets::sc::StartGame sg) {
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
    preview = &Create<Card>(Position::VCenter(-100));
    preview->visible = false;
    preview->hoverable = Hoverable::Transparent;
    preview->scale = Card::Preview;

    // Finally, ‘end’ our turn to reset everything.
    end_turn();
    client.set_screen(*this);
}

void GameScreen::handle_challenge(packets::CardChoiceChallenge c) {
    card_choice_challenge_screen.enter(std::move(c));
}

void GameScreen::lock_changed(PlayerId player, u32 stack_index, bool locked) {
    auto& p = PlayerById(player);
    p.word->stacks()[stack_index].locked = locked;
}

void GameScreen::on_refresh(Renderer& r) {
    // Put our hand at the bottom, with the cards slightly out of the screen.
    our_hand->pos = Position::HCenter(50).anchor_to(Anchor::Center);

    // Put our word in the center, but offset it upward to counteract the anchor,
    // which is there because we want additional cards to ‘hang’ from the top of
    // the word (i.e. cards added to a stack should go below the ‘baseline’).
    us.word->pos = Position::HCenter(r.size().ht / 2 + Card::CardSize[us.word->scale].wd).anchor_to(Anchor::North);

    // Position the other players’ words at the top of the screen.
    other_words->pos = Position::HCenter(-100);
    other_words->gap = 100;
}

void GameScreen::tick(InputSystem& input) {
    if (client.server_connexion.disconnected) {
        client.show_error("Disconnected: Server has gone away", client.menu_screen);
        return;
    }

    Screen::tick(input);
    switch (state) {
        case State::NoSelection: TickNoSelection(); break;
        case State::NotOurTurn: TickNotOurTurn(); break;
        case State::Passing: TickPassing(); break;
        case State::SingleTarget: TickSingleTarget(); break;
        case State::PlayerTarget: TickPlayerTarget(); break;
        case State::InAuxiliaryScreen: Unreachable("Should never get here if in auxiliary screen");
    }

    // Preview any card that the user is hovering over.
    auto c = dynamic_cast<Card*>(hovered_element);
    if (c) {
        preview->visible = true;
        preview->id = c->id;

        // Refresh the card now to prevent weird rendering artefacts.
        if (preview->needs_refresh) preview->refresh(client.renderer);
    } else {
        preview->visible = false;
    }
}

void GameScreen::start_turn() {
    state = State::NoSelection;
    end_turn_button->selectable = Selectable::Yes;
    ResetHand();
    // TODO: Automatically go into passing mode if we cannot play anything in our hand.
}

void GameScreen::update_word(
    pr::PlayerId player,
    std::span<const std::vector<CardId>> new_word
) {
    auto& p = PlayerById(player);
    p.word->clear();
    for (auto& s : new_word) {
        auto& stack = p.word->add_stack();
        for (auto c : s) stack.push(c);
    }
}
