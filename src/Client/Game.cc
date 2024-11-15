module;
#include <base/Assert.hh>
#include <ranges>
module pr.client;

import pr.validation;
import pr.packets;

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Game Screen
// =============================================================================
GameScreen::GameScreen(Client& c) : client(c) {
}

auto GameScreen::PlayerById(PlayerId id) -> Player& {
    if (id == us.id) return us;
    auto p = rgs::find(other_players, id, &Player::get_id);
    Assert(p != other_players.end(), "Player {} not found?", id);
    return *p;
}

auto GameScreen::PlayerForCardInWord(Card* c) -> Player* {
    Assert(c);
    auto stack = c->parent->cast<CardStacks::Stack>();
    if (not stack) return nullptr;
    auto p = player_map.find(&stack->parent);
    if (p == player_map.end()) return nullptr;
    return p->second;
}

void GameScreen::ResetOpponentWords() {
    for (auto& p : other_players) {
        p.word->make_selectable(false);
        p.word->set_display_state(Card::DisplayState::Default);
    }
}

void GameScreen::TickSelection() {
    if (not selected_element) return;

    // We selected one of our own cards. Make it so we can now select
    // other player’s cards. Unselect the card manually in that case
    // since we don’t want to clear the 'selected' property.
    if (selected_element->has_parent(our_hand)) {
        // We selected the same card again; unselect it this time.
        if (selected_element == our_selected_card) {
            ResetOpponentWords();
            our_selected_card = nullptr;
            selected_element->unselect();
            return;
        }

        // Unselect the previously selected card, set the current selected
        // element of the screen to null, and save it as our selected card;
        if (our_selected_card) our_selected_card->unselect();
        our_selected_card = &selected_element->as<Card>();

        // Do not unselect this card as we want to keep it selected.
        selected_element = nullptr;

        // Make other player’s cards selectable if we can play this card on it.
        for (auto& p : other_players) {
            auto cards = p.cards();
            for (auto [i, c] : p.word->stacks() | vws::enumerate) {
                auto v = validation::ValidatePlaySoundCard(our_selected_card->id, cards, i);
                bool valid = v == validation::PlaySoundCardValidationResult::Valid;
                c.make_selectable(valid);
                c.display_state = valid ? Card::DisplayState::Default : Card::DisplayState::Inactive;
            }
        }
        return;
    }

    // Otherwise, we selected another player’s card. We should never get here
    // if we didn’t previously select one of our cards.
    Assert(our_selected_card, "We should have selected one of our cards");
    auto& stack = selected_element->as<CardStacks::Stack>();
    auto owner = player_map.at(&stack.parent);
    Assert(owner, "Selected card without owner?");

    // Make opponents’ cards non-selectable again.
    // TODO: We’ll need to amend this once we allow selecting multiple cards.
    ResetOpponentWords();

    // Tell the server about this.
    auto& our_stack = our_selected_card->parent->as<CardStacks::Stack>();
    auto card_in_hand_index = our_hand->index_of(our_stack);
    auto selected_card_index = owner->word->index_of(stack);
    Assert(card_in_hand_index.has_value(), "Could not find card in hand?");
    Assert(selected_card_index.has_value(), "Could not find card in word?");
    client.server_connexion.send(packets::cs::PlaySoundCard{
        *card_in_hand_index,
        owner->id,
        *selected_card_index,
    });

    // Unselect the opponent’s card and remove the card from our hand. The
    // server will send packets that do the res.
    selected_element->unselect();
    our_hand->remove(our_stack);
    our_selected_card = nullptr;
}

void GameScreen::add_card(PlayerId id, u32 stack_idx, CardId card) {
    auto& player = PlayerById(id);
    player.word->stacks()[stack_idx].push(card);
}

void GameScreen::enter(packets::sc::StartGame sg) {
    DeleteAllChildren();

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
    player_map.clear();
    player_map[us.word] = &us;
    for (auto& p : other_players) player_map[p.word] = &p;

    // The preview must be created at the end so it’s drawn
    // above everything else.
    preview = &Create<Card>(Position::VCenter(-100));
    preview->visible = false;
    preview->hoverable = false;
    preview->scale = Card::Preview;

    // Finally, ‘end’ our turn to reset everything.
    end_turn();
    client.enter_screen(*this);
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
    TickSelection();

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
    our_turn = true;
    for (auto& c : our_hand->top_cards()) {
        // Power cards are always usable for now.
        // TODO: Some power cards may not always have valid targets; check for that.
        if (CardDatabase[+c.id].is_power()) {
            c.display_state = Card::DisplayState::Default;
            c.selectable = true;
            continue;
        }

        // For sound cards, check if there are any sounds we can play them on.
        for (auto& p : other_players) {
            auto w = p.cards();
            for (usz i = 0; i < w.size(); i++) {
                auto v = validation::ValidatePlaySoundCard(c.id, w, i);
                if (v == validation::PlaySoundCardValidationResult::Valid) {
                    c.display_state = Card::DisplayState::Default;
                    c.selectable = true;
                    goto next_card;
                }
            }
        }
    next_card:;
    }
}

void GameScreen::end_turn() {
    our_turn = false;
    our_hand->make_selectable(false);
    our_hand->set_display_state(Card::DisplayState::Inactive);
    ResetOpponentWords();
}
