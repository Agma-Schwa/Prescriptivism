module;
#include <base/Assert.hh>
#include <generator>
#include <ranges>
module pr.client;

import pr.validation;
import pr.packets;
import pr.cards;

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
// Helpers
// =============================================================================
GameScreen::GameScreen(Client& c) : client(c) {
    // UI is set up in enter().
}

auto GameScreen::PlayerById(PlayerId id) -> Player& {
    auto p = rgs::find(all_players, id, &Player::get_id);
    Assert(p != all_players.end(), "Player {} not found?", id);
    return **p;
}

auto GameScreen::PlayerForCardInWord(Card* c) -> Player* {
    Assert(c);
    auto stack = c->parent->cast<CardStacks::Stack>();
    if (not stack) return nullptr;
    auto p = player_map.find(&stack->parent);
    if (p == player_map.end()) return nullptr;
    return p->second;
}

void GameScreen::ResetWords(
    Selectable s,
    Card::Overlay o
) {
    for (auto p : all_players) {
        p->word->make_selectable(s);
        p->word->set_display_state(o);
    }
}

auto GameScreen::Targets(Card& c) -> std::generator<Target> {
    if (c.id.is_sound()) {
        for (auto p : all_players) {
            for (auto [i, s] : p->word->stacks() | vws::enumerate) {
                // TODO: Handle evolutions that require an extra card.
                auto res = validation::ValidatePlaySoundCard(c.id, ValidatorFor(*p), i);
                bool valid = res == validation::PlaySoundCardValidationResult::Valid;
                if (valid) co_yield Target{s, s.cards()[i]};
            }
        }
        co_return;
    }

    switch (c.id.value) {
        default: break;
        case CardIdValue::P_SpellingReform: {
            auto v = ValidatorFor(us);
            for (auto [i, s] : us.word->stacks() | vws::enumerate)
                if (validation::ValidateSpellingReform(v, i))
                    co_yield Target{s, s.top};
        } break;
    }
}

// =============================================================================
//  Game Logic
// =============================================================================
void GameScreen::ClearSelection() {
    ResetWords();
    state = State::NoSelection;
    if (selected_element) selected_element->unselect();
    if (our_selected_card) our_selected_card->unselect();
    our_selected_card = nullptr;
}

void GameScreen::TickNoSelection() {
    if (not selected_element) return;
    Assert(not our_selected_card, "Should not be here if a card was previously selected");
    Assert(selected_element->has_parent(our_hand), "How did we select someone else’s card here?");
    our_selected_card = &selected_element->as<Card>();

    // Clear the selected element pointer of the screen so we can select a new card.
    selected_element = nullptr;

    // TODO: Depending on what card is selected, we may want to change
    // what we can select here.
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

void GameScreen::TickSingleTarget() {
    if (not selected_element) return;
    Assert(our_selected_card, "We should have selected one of our cards");

    auto PlaySingleTarget = [&] {
        auto& stack = selected_element->as<CardStacks::Stack>();
        auto owner = player_map.at(&stack.parent);
        Assert(owner, "Selected card without owner?");

        // Tell the server about this.
        auto& our_stack = our_selected_card->parent->as<CardStacks::Stack>();
        auto card_in_hand_index = our_hand->index_of(our_stack);
        auto selected_card_index = owner->word->index_of(stack);
        client.server_connexion.send(packets::cs::PlaySingleTarget{
            *card_in_hand_index,
            owner->id,
            *selected_card_index,
        });

        // Remove the card we played.
        ClearSelection();
        our_hand->remove(our_stack);
    };

    // We selected one of our own cards while we already had one selected.
    if (selected_element->has_parent(our_hand)) {
        if (selected_element == our_selected_card) return ClearSelection();
        our_selected_card->unselect();
        our_selected_card = nullptr;
        return TickNoSelection();
    }

    // We’re playing a sound card on a sound card.
    if (our_selected_card->id.is_sound()) {
        PlaySingleTarget();
        return;
    }

    // We’re playing a power card.
    if (our_selected_card->id.is_power()) {
        switch (our_selected_card->id.value) {
            case CardIdValue::P_SpellingReform: PlaySingleTarget(); return;
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

void GameScreen::enter(packets::sc::StartGame sg) {
    DeleteAllChildren();

    pass = &Create<Button>(client.renderer.make_text("Pass", FontSize::Medium), Position(-50, 50));
    pass->on_click = [&] { Log("TODO: Pass turn"); };

    other_players.clear();
    other_words = &Create<Group>(Position());
    for (auto [i, p] : sg.player_data | vws::enumerate) {
        if (i == sg.player_id) {
            us = Player("You", sg.player_id);
            us.word = &Create<CardStacks>(Position(), p.word);
            our_hand = &Create<CardStacks>(Position(), sg.hand);
            our_hand->scale = Card::Hand;
            our_hand->max_gap = -Card::CardSize[Card::Hand].wd / 2;
            our_hand->selection_mode = CardStacks::SelectionMode::Card;
            us.word->alignment = -5;
            continue;
        }

        auto& op = other_players.emplace_back(std::move(p.name), u8(i));
        op.word = &other_words->create<CardStacks>(Position(), p.word);
        op.word->scale = Card::OtherPlayer;
        op.word->alignment = -5;
    }

    // Build the player map *after* creating all the players, since they
    // might move while in the loop above.
    all_players.clear();
    player_map.clear();
    player_map[us.word] = &us;
    all_players.push_back(&us);
    for (auto& p : other_players) {
        player_map[p.word] = &p;
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
    client.enter_screen(*this);
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
    other_words->max_gap = 100;
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
        case State::SingleTarget: TickSingleTarget(); break;
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
    pass->selectable = Selectable::Yes;
    for (auto& c : our_hand->top_cards()) {
        if (not Empty(Targets(c))) {
            c.overlay = Card::Overlay::Default;
            c.selectable = Selectable::Yes;
        }
    }
}

void GameScreen::end_turn() {
    state = State::NotOurTurn;
    pass->selectable = Selectable::No;
    our_hand->make_selectable(Selectable::No);
    our_hand->set_display_state(Card::Overlay::Inactive);
    ResetWords();
}
