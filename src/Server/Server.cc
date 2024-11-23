#include <Server/Server.hh>

#include <Shared/Validation.hh>

#include <base/Base.hh>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <ranges>
#include <thread>
#include <vector>

using namespace pr;
using namespace pr::server;
namespace sc = packets::sc;
namespace cs = packets::cs;

// ============================================================================
// Constants
// ============================================================================
using enum DisconnectReason;
namespace {
constexpr usz PlayersNeeded = pr::constants::PlayersPerGame;
constexpr usz HandSize = 7;
constexpr u32 PacketsPerTick = 10;
constexpr u32 MaxReceiveBufferSize = 40 * 1'024;
constexpr u32 MaxPacketSize = 10 * 1'024;
}

// ============================================================================
// Helpers
// ============================================================================
struct Server::Validator {
    const Player& p;
    PlayerId acting_player;

    auto operator[](usz i) const -> CardId { return p.word.stacks[i].top; }
    bool is_own_word() const { return acting_player == p.id; }
    auto size() const -> usz { return p.word.stacks.size(); }
    bool stack_is_locked(usz i) const { return p.word.stacks[i].locked; }
    bool stack_is_full(usz i) const { return p.word.stacks[i].full; }
};

auto BuildWordArray(const Word& w) {
    return w.stacks | vws::transform(&Stack::get_top);
}

auto Server::ValidatorFor(Player& p) -> Validator {
    return Validator{p, current_player};
}

// =============================================================================
//  Networking
// =============================================================================
void Server::Kick(net::TCPConnexion& client, DisconnectReason reason, std::source_location sloc) {
    Log(
        "Kicking client {} for reason {} (at {}:{}:{})",
        client.address,
        +reason,
        sloc.file_name(),
        sloc.line(),
        sloc.column()
    );

    client.send(sc::Disconnect{reason});
    client.disconnect();
}

void Server::Tick() {
    // Receive incoming data.
    server.receive();

    // For any pending connexions, disconnect any that haven’t sent
    // a login packet within the timeout.
    auto now = chr::steady_clock::now();
    for (auto& [conn, established] : pending_connexions) {
        if (now - established > 30s) {
            // Don’t even bother sending a packet here; if they didn’t
            // respond within the time frame, they’re likely not actually
            // a game client, but rather some random other connexion.
            Log("Client {} took too long to send a login packet", conn.address);
            conn.disconnect();
        }
    }

    // Clear out pending connexions that may have gone away.
    std::erase_if(pending_connexions, [](const auto& c) { return c.conn.disconnected; });

    // Clear out player connexions of players that are disconnected.
    std::erase_if(player_map, [](const auto& x) { return x.first.disconnected; });

    // As the last step, accept new connexions and close stale ones.
    server.update_connexions();

    // Start the game if we have enough connected players.
    // Start the game iff all players are connected and all words have been received.
    if (
        state == State::WaitingForWords and
        AllPlayersConnected() and
        AllWordsSubmitted()
    ) {
        state = State::Running;

        // Send each player’s word to every player and tell the first
        // player to start their turn.
        for (auto& p : players) SendGameState(*p);
        player().send(sc::StartTurn{});
    }
}

bool Server::accept(net::TCPConnexion& connexion) {
    // Make sure we’re not full yet.
    auto connected_players = rgs::distance(players | vws::filter([](auto& p) { return p->connected; }));
    if (connected_players + pending_connexions.size() == PlayersNeeded) {
        connexion.send(packets::sc::Disconnect{ServerFull});
        return false;
    }

    pending_connexions.emplace_back(connexion, chr::steady_clock::now());
    return true;
}

void Server::receive(net::TCPConnexion& client, net::ReceiveBuffer& buf) {
    // If there’s too much data, kick the client.
    if (buf.size() > MaxReceiveBufferSize) return Kick(client, BufferFull);

    // Limit how many packets we’re willing to process per tick per connexion.
    u32 count = 0;
    while (not client.disconnected and not buf.empty() and count++ < PacketsPerTick) {
        auto res = packets::HandleServerSidePacket(*this, client, buf);

        // If there was an error, close the connexion.
        if (not res) {
            Log("Packet error while processing {}: {}", client.address, res.error());
            return Kick(client, InvalidPacket);
        }

        // The packet was incomplete.
        if (not res.value()) {
            // Conversely, any data left is a *single* incomplete packet, so
            // kick the client if it is too large.
            if (buf.size() > MaxPacketSize) Kick(client, PacketTooLarge);

            // Stop processing packets until we have more data.
            return;
        }
    }
}

// =============================================================================
//  General Packet Handlers
// =============================================================================
void Server::handle(net::TCPConnexion& client, cs::Disconnect) {
    Log("Client {} disconnected", client.address);
    client.disconnect();
}

// =============================================================================
//  Login and Setup Packet Handlers
// =============================================================================
void Server::handle(net::TCPConnexion& client, sc::WordChoice wc) {
    // Player has already submitted a word.
    if (not player_map.contains(client) or player_map[client]->submitted_word) {
        Kick(client, UnexpectedPacket);
        return;
    }

    // Word is invalid.
    constants::Word original;
    auto& p = player_map[client];
    for (auto [i, s] : p->word.stacks | vws::enumerate) original[i] = s.top;
    if (validation::ValidateInitialWord(wc.word, original) != validation::InitialWordValidationResult::Valid) {
        Kick(client, InvalidPacket);
        return;
    }

    // Word is valid. Mark it as submitted.
    for (auto [i, c] : wc.word | vws::enumerate) p->word.stacks[i].cards[0].id = c;
    p->submitted_word = true;
    Log("Client gave back word");
}

void Server::handle(net::TCPConnexion& client, cs::HeartbeatResponse res) {
    Log("Received heartbeat response from client {}", res.seq_no);
}

void Server::handle(net::TCPConnexion& client, cs::Login login) {
    Log("Login: name = {}, password = {}", login.name, login.password);

    // Mark this as no longer pending and also check whether it was
    // pending in the first place. Clients that are already connected
    // to a player are not supposed to send a login packet.
    auto erased = std::erase_if(pending_connexions, [&](auto& x) {
        return client == x.conn;
    });
    if (erased != 1) return Kick(client, InvalidPacket);

    // Check that the password matches.
    if (login.password != password) return Kick(client, WrongPassword);

    // Try to match this connexion to an existing player.
    for (auto& p : players) {
        if (p->name == login.name) {
            // Someone is trying to connect to a player that is
            // already connected.
            if (p->connected) {
                Log("{} is already connected", login.name);
                return Kick(client, UsernameInUse);
            }

            Log("Player {} logging back in", login.name);
            p->client_connexion = client;
            player_map[client] = p.get();

            // Get the player up to date with the current game state.
            switch (state) {
                case State::WaitingForPlayerRegistration: break;
                case State::WaitingForWords:
                    if (not p->submitted_word) p->send(sc::WordChoice{p->word.ids()});
                    break;

                case State::Running:
                    SendGameState(*p);
                    if (current_player == p->id) {
                        p->send(sc::StartTurn{});
                        p->challenge.visit(utils::Overloaded{
                            [&](std::monostate) {},
                            [&](const challenge::CardChoice& c) { p->send(sc::CardChoice(c.data)); }
                        });
                    }
                    break;
            }
            return;
        }
    }

    // Create a new player. This is also the only place where we can
    // reach the player limit for the first time, so perform game
    // initialisation here if we have enough players.
    players.push_back(std::make_unique<Player>(client, std::move(login.name)));
    player_map[client] = players.back().get();
    if (players.size() == PlayersNeeded) SetUpGame();
}

// =============================================================================
//  Game Packet Handlers
// =============================================================================
void Server::handle(net::TCPConnexion& client, cs::Pass pass) {
    auto res = CanPlayCard(client, pass.card_index);
    if (not res) return;
    auto [p, card, _] = res;
    RemoveCard(*p, *card);
    NextPlayer();
}

void Server::handle(net::TCPConnexion& client, packets::cs::PlayNoTarget packet) {
    auto res = CanPlayCard(client, packet.card_index);
    if (not res) return;
    auto [p, card, _] = res;
    switch (card->id.value) {
        default:
            Log("Sorry, playing {} is not implemented yet", CardDatabase[+card->id].name);
            Kick(client, InvalidPacket);
            return;

        // Always playable.
        case CardId::P_Babel: {
            Broadcast(sc::DiscardAll{});
            for (auto& player : players) {
                rgs::move(player->hand, std::back_inserter(discard));
                player->hand.clear();
                Draw(*player, HandSize);
            }

            // Do NOT remove the card here since it’s already been discarded.
        } break;

        // Always playable.
        case CardId::P_Whorf: {
            Word new_word;
            usz deck_size = deck.size();
            for (auto& s : p->word.stacks) {
                // Empty stacks can simply be ignored here.
                if (s.cards.empty()) continue;

                // Keep the topmost card.
                new_word.add_stack(std::move(s.cards.back()));

                // All cards except the topmost one are placed at
                // the bottom of the deck.
                for (auto& c : s.cards | vws::take(s.cards.size() - 1))
                    deck.push_back(std::move(c));
            }

            // All cards so added are shuffled.
            rgs::shuffle(deck.begin() + deck_size, deck.end(), rng);

            // Replace the word and broadcast the change.
            p->word = std::move(new_word);
            std::vector<std::vector<CardId>> ids;
            for (auto& s : p->word.stacks) {
                auto& v = ids.emplace_back();
                for (auto& c : s.cards) v.push_back(c.id);
            }

            Broadcast(sc::WordChanged{p->id, ids});
            RemoveCard(*p, *card);
        } break;
    }

    NextPlayer();
}

void Server::handle(net::TCPConnexion& client, packets::cs::PlayPlayerTarget packet) {
    auto res = CanPlayCard(client, packet.card_index, packet.player);
    if (not res) return;
    auto [p, card, target_player] = res;
    switch (card->id.value) {
        default:
            Log("Sorry, playing {} is not implemented yet", CardDatabase[+card->id].name);
            Kick(client, InvalidPacket);
            return;

        case CardId::P_Superstratum: {
            // We can’t target ourselves with this.
            if (p == target_player) return Kick(client, InvalidPacket);

            // Show the target player’s hand to the player.
            p->challenge = challenge::CardChoice{
                .target_player = target_player->id,
                .data = {
                    .title = std::format("from {}’s hand", target_player->name),
                    .cards = target_player->hand | vws::transform(&Card::id) | rgs::to<std::vector>(),
                    .count = 1,
                    .mode = packets::CardChoiceChallenge::Mode::AtMost,
                }
            };

            p->send(sc::CardChoice{p->challenge.get<challenge::CardChoice>().data});
        } break;
    }

    RemoveCard(*p, *card);
}

void Server::handle(net::TCPConnexion& client, cs::PlaySingleTarget packet) {
    auto res = CanPlayCard(client, packet.card_index, packet.player);
    if (not res) return;
    auto [p, card, target_player] = res;

    // Check that the target card index is valid.
    if (packet.target_stack_index >= target_player->word.stacks.size())
        return Kick(client, InvalidPacket);

    // Now that we have checked that all indices are valid and that it’s
    // this player’s turn, validate the action itself. Any code below this
    // MUST be handled in the validation namespace to make sure the client
    // can perform the exact same checks as the server so we don’t allow a
    // player to take an invalid action.
    //
    // The card is a sound card.
    if (card->id.is_sound()) return HandlePlaySoundCard(
        client,
        *card,
        *target_player,
        packet.target_stack_index
    );

    // The card is a power card.
    switch (card->id.value) {
        default:
            Log("Sorry, playing {} is not implemented yet", CardDatabase[+card->id].name);
            Kick(client, InvalidPacket);
            return;

        case CardIdValue::P_Descriptivism: {
            if (not validation::ValidateP_Descriptivism(ValidatorFor(*target_player), packet.target_stack_index))
                return Kick(client, InvalidPacket);

            // Unlock the stack.
            target_player->word.stacks[packet.target_stack_index].locked = false;
            Broadcast(sc::StackLockChanged{target_player->id, packet.target_stack_index, false});
        } break;

        case CardIdValue::P_SpellingReform: {
            if (not validation::ValidateP_SpellingReform(ValidatorFor(*target_player), packet.target_stack_index))
                return Kick(client, InvalidPacket);

            // Lock the stack.
            //
            // TODO: The Spelling Reform *is* the lock, so it should only go
            //       back into the discard pile when the lock is removed.
            target_player->word.stacks[packet.target_stack_index].locked = true;
            Broadcast(sc::StackLockChanged{target_player->id, packet.target_stack_index, true});
        } break;
    }

    RemoveCard(*p, *card);
    NextPlayer();
}

// =============================================================================
//  Challenge Packet Handlers
// =============================================================================
void Server::handle(net::TCPConnexion& client, packets::cs::CardChoiceReply packet) {
    auto& p = player_map[client];
    auto c = p->challenge.get_if<challenge::CardChoice>();
    if (not c) return Kick(client, UnexpectedPacket);

    // All indices must be in bounds and unique.
    rgs::sort(packet.card_indices);
    if (
        rgs::any_of(packet.card_indices, [&](auto i) { return i >= c->data.cards.size(); }) or
        rgs::adjacent_find(packet.card_indices) != packet.card_indices.end()
    ) return Kick(client, InvalidPacket);

    // Check constraints on the count and mode.
    auto ok = [&] {
        using Mode = packets::CardChoiceChallenge::Mode;
        switch (c->data.mode) {
            case Mode::Exact: return packet.card_indices.size() == c->data.count;
            case Mode::AtLeast: return packet.card_indices.size() >= c->data.count;
            case Mode::AtMost: return packet.card_indices.size() <= c->data.count;
        }
        Unreachable();
    }();
    if (not ok) return Kick(client, InvalidPacket);

    // Add the selected cards to the player’s hand.
    for (auto i : packet.card_indices) {
        p->hand.push_back(Card{c->data.cards[i]});
        p->send(sc::Draw{c->data.cards[i]});
    }

    // And delete them from the target player’s hand.
    //
    // Note: The indices are sorted, so deleting them in reverse order
    // does the expected thing here.
    auto& target = players[c->target_player];
    for (auto i : packet.card_indices | vws::reverse)
        RemoveCard(*target, target->hand[i]);

    // Finally, clear the challenge.
    p->challenge = std::monostate{};
}

// =============================================================================
//  Playing Cards
// =============================================================================
void Server::HandlePlaySoundCard(
    net::TCPConnexion& client,
    Card& card,
    Player& target_player,
    u32 target_index
) {
    auto& p = player_map[client];

    // Perform validation.
    if (
        validation::ValidatePlaySoundCard(card.id, ValidatorFor(target_player), target_index) !=
        validation::PlaySoundCardValidationResult::Valid
    ) return Kick(client, InvalidPacket);

    // Add the sound to the player’s word.
    // TODO: Special effects when playing a sound.
    // TODO: Check for locks.
    // TODO: The cursed i+2*j -> j change.
    target_player.word.stacks[target_index].push(Card{card.id});
    Broadcast(sc::AddSoundToStack{target_player.id, target_index, card.id});

    // Remove the card from the player’s hand and end their turn.
    RemoveCard(*p, card);
    NextPlayer();
}

// =============================================================================
//  General Game Logic
// =============================================================================
auto Server::CanPlayCard(
    net::TCPConnexion& client,
    std::optional<u32> card_index,
    std::optional<PlayerId> target_player
) -> CanPlayCardResult {
    // Check that the player is the current player.
    auto& p = player_map[client];
    if (p->id != current_player) {
        Kick(client, UnexpectedPacket);
        return {};
    }

    /// Check that the player does not have a pending challenge.
    if (not p->challenge.is<std::monostate>()) {
        Kick(client, UnexpectedPacket);
        return {};
    }

    /// Check that the card index is valid.
    if (card_index and *card_index >= p->hand.size()) {
        Kick(client, InvalidPacket);
        return {};
    }

    /// Check that the player index is valid.
    if (target_player and *target_player >= players.size()) {
        Kick(client, InvalidPacket);
        return {};
    }

    return {
        p,
        card_index ? &p->hand[*card_index] : nullptr,
        target_player ? players[*target_player].get() : nullptr
    };
}

void Server::Draw(Player& p, usz count) {
    for (usz i = 0; i < count; ++i) {
        if (deck.empty()) {
            // TODO: Shuffle discard pile back into deck.
            return;
        }

        p.hand.push_back(std::move(deck.back()));
        deck.pop_back();
        p.send(sc::Draw{p.hand.back().id});
    }
}

void Server::NextPlayer() {
    // TODO: Can a player somehow have more than 7 cards in hand?
    if (player().hand.size() < HandSize) Draw(player(), HandSize - player().hand.size());
    player().send(sc::EndTurn{});
    current_player = (current_player + 1) % players.size();
    player().send(sc::StartTurn{});

    // If this player’s hand is empty, move on to the next player. Do this
    // *after* drawing so this code only fires if the deck is empty.
    if (player().hand.empty()) {
        // If *all* players’ hands are empty, we need to end the game. This
        // *shouldn’t* happen, but you never know...
        if (rgs::all_of(players, [](auto& p) { return p->hand.empty(); })) {
            Broadcast(sc::Disconnect{Unspecified});
            Log("No more plays can be made. The game is a draw.");
            std::exit(27);
        }

        NextPlayer();
    }
}

void Server::SendGameState(Player& p) {
    // Yes, we recompute this every time this packet is sent, but
    // it’s sent so rarely (once at the start of the game and once
    // every time someone rejoins), that it’s not really worth moving
    // this into a separate function.
    //
    // TODO: Send *entire* stacks, not just the topmost card in each stack.
    std::array<sc::StartGame::PlayerInfo, constants::PlayersPerGame> player_infos;
    for (auto [i, p] : players | vws::enumerate) {
        player_infos[i].name = p->name;
        for (auto [j, c] : p->word.stacks | vws::enumerate)
            player_infos[i].word[j] = c.top;
    }

    // Send each player their hand and id.
    p.send(sc::StartGame{
        player_infos,
        p.hand | vws::transform(&Card::id) | rgs::to<std::vector>(),
        u8(p.id),
    });
}

void Server::SetUpGame() {
    Assert(state == State::WaitingForPlayerRegistration);
    state = State::WaitingForWords;

    // Consonants.
    auto AddCards = [&](std::span<const CardData> s) {
        for (auto& c : s) {
            for (u8 i = 0; i < c.count_in_deck; ++i) {
                deck.emplace_back(c.id);
            }
        }
    };

    AddCards(CardDatabaseConsonants);
    auto num_consonants = deck.size();

    // Vowels.
    AddCards(CardDatabaseVowels);

    // Draw the cards for reach player’s word. Alternate vowels
    // and consonants so the player always gets a valid word to
    // begin with.
    rgs::shuffle(deck.begin(), deck.begin() + num_consonants, rng);
    rgs::shuffle(deck.begin() + num_consonants, deck.end(), rng);
    for (auto& p : players) {
        for (u8 i = 0; i < 3; ++i) {
            // Add consonant.
            p->word.add_stack(std::move(deck.front()));
            deck.erase(deck.begin());

            // Add vowel.
            p->word.add_stack(std::move(deck.back()));
            deck.pop_back();
        }

        // Send the player their word.
        p->send(sc::WordChoice{BuildWordArray(p->word)});
    }

    // Special cards.
    AddCards(CardDatabasePowers);

    // Draw each player’s hand, but don’t send the cards yet.
    rgs::shuffle(deck, rng);
    for (auto& p : players) {
        Assert(deck.size() > HandSize, "Somehow out of cards?");
        p->hand.insert(
            p->hand.end(),
            std::make_move_iterator(deck.end() - HandSize),
            std::make_move_iterator(deck.end())
        );
        deck.erase(deck.end() - HandSize, deck.end());

        // FIXME: TESTING ONLY. REMOVE THIS LATER: Hallucinate whatever
        // power card we’re currently testing into the player’s hand.
        p->hand.emplace_back(CardId::P_Superstratum);
    }
    rgs::shuffle(players, rng);

    // FIXME: FOR TESTING ONLY. COMMENT THIS OUT IN PRODUCTION.
    auto it = rgs::find_if(players, [](auto& p) { return p->name == "debugger" or p->name == "console"; });
    if (it != players.end()) {
        Log("Debugger or console found.");
        std::iter_swap(it, players.begin());
    }

    // Initialise player IDs.
    for (auto [i, p] : players | vws::enumerate) p->id = u8(i);
}

// =============================================================================
//  API
// =============================================================================
Server::Server(u16 port, std::string password)
    : server(net::TCPServer::Create(port, 200).value()),
      password(std::move(password)) {
    server.set_callbacks(*this);
}

void Server::Run() {
    constexpr chr::milliseconds ServerTickDuration = 33ms;
    Log("Server listening on port {}", server.port());
    for (;;) {
        const auto start_of_tick = chr::system_clock::now();

        Tick();

        // Sleep for a bit.
        const auto end_of_tick = chr::system_clock::now();
        const auto tick_duration = chr::duration_cast<chr::milliseconds>(end_of_tick - start_of_tick);
        if (tick_duration < ServerTickDuration) {
            std::this_thread::sleep_for(ServerTickDuration - tick_duration);
        } else {
#ifndef PRESCRIPTIVISM_ENABLE_SANITISERS
            Log("Server tick took too long: {}ms", tick_duration.count());
#endif
        }
    }
}
