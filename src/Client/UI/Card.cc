#include <Client/UI/UI.hh>

#include <Shared/Packets.hh>

#include <base/Base.hh>

#include <format>
#include <numeric>
#include <ranges>

using namespace pr;
using namespace pr::client;

// =============================================================================
//  Constants
// =============================================================================
constexpr auto ConsonantColour = Colour::RGBA(0xfea3'aaff);
constexpr auto VowelColour = Colour::RGBA(0xfad3'84ff);
constexpr auto PowerColour = Colour::RGBA(0xb2ce'feff);
constexpr auto UniquePowerColour = Colour::RGBA(0xd0bc'f3ff);

// The inverted versions of the power card colours happen to be pretty, so
// use them for extra consonants and vowels.
//
// FIXME: Not constexpr because __builtin_fmodf is apparently not constexpr yet.
const auto ExtraConsonantColour = PowerColour.invert().luminosity_invert();
const auto ExtraVowelColour = UniquePowerColour.invert().luminosity_invert();

// =============================================================================
//  Card Data
// =============================================================================
struct PowerCardData {
    /// Basic rules text.
    std::string_view rules;

    /// Extended tooltip with the complete rules text; this
    /// is shown if the user holds down some key (e.g. shift).
    std::string_view extended_rules;

    /// Path to the image.
    std::string_view image_path;

    /// Cached image texture.
    LateInit<DrawableTexture> image{};

    explicit PowerCardData(
        std::string_view image_path,
        std::string_view rules,
        std::string_view extended_rules
    ) : rules(rules), extended_rules(extended_rules), image_path(image_path) {}
};

namespace pr::client::power_card_database {
using enum CardIdValue;

#define Entry(x, ...) [+P_##x - +$$PowersStart] = PowerCardData(#x ".webp", __VA_ARGS__)

const PowerCardData Database[+$$PowersEnd - +$$PowersStart + 1]{
    Entry(
        Assimilation,
        "",
        ""
    ),

    Entry(
        Babel,
        "Each player discards their hand, then draws 7 cards.",
        ""
    ),

    Entry(
        Brasil,
        "",
        ""
    ),

    Entry(
        Campbell,
        "",
        ""
    ),

    Entry(
        Chomsky,
        "",
        ""
    ),

    Entry(
        Darija,
        "",
        ""
    ),

    Entry(
        Descriptivism,
        "Break a Lock on a sound.",
        ""
    ),

    Entry(
        Dissimilation,
        "",
        ""
    ),

    Entry(
        Elision,
        "",
        ""
    ),

    Entry(
        Epenthesis,
        "",
        ""
    ),

    Entry(
        GVS,
        "",
        ""
    ),

    Entry(
        Grimm,
        "",
        ""
    ),

    Entry(
        Gvprtskvni,
        "",
        ""
    ),

    Entry(
        Heffer,
        "",
        ""
    ),

    Entry(
        LinguaFranca,
        "",
        ""
    ),

    Entry(
        Negation,
        "Protect yourself from the effects of a card. This card can "
        "be played at any time, even if it’s not your turn.",
        ""
    ),

    Entry(
        Owl,
        "",
        ""
    ),

    Entry(
        Pinker,
        "",
        ""
    ),

    Entry(
        ProtoWorld,
        "",
        ""
    ),

    Entry(
        REA,
        "",
        ""
    ),

    Entry(
        Reconstruction,
        "",
        ""
    ),

    Entry(
        Regression,
        "",
        ""
    ),

    Entry(
        Revival,
        "",
        ""
    ),

    Entry(
        Rosetta,
        "",
        ""
    ),

    Entry(
        Schleicher,
        "",
        ""
    ),

    Entry(
        Schleyer,
        "",
        ""
    ),

    Entry(
        SpellingReform,
        "Lock one of your sounds, or combine with a sound card to break "
        "a lock on an adjacent sound.",
        ""
    ),

    Entry(
        Substratum,
        "",
        ""
    ),

    Entry(
        Superstratum,
        "Choose a player and look at their hand; you may choose a card "
        "from and add it to your hand.",
        ""
    ),

    Entry(
        Urheimat,
        "",
        ""
    ),

    Entry(
        Vajda,
        "",
        ""
    ),

    Entry(
        Vernacular,
        "",
        ""
    ),

    Entry(
        Whorf,
        "The current state of your word becomes your original word; all "
        "cards except the top-most sound cards are removed from your word.",
        ""
    ),

    Entry(
        Zamnenhoff,
        "",
        ""
    ),
};

struct DatabaseImpl {
    auto operator[](CardId id) -> const PowerCardData& { return Database[+id - +$$PowersStart]; }
    auto begin() { return std::begin(Database); }
    auto end() { return std::end(Database); }
} PowerCardDatabase;

#undef Entry
} // namespace pr::client::power_card_database

namespace pr::client {
using power_card_database::PowerCardDatabase;
}

/// The texture used to indicate that a stack is locked.
LateInit<DrawableTexture> LockedTexture;

/// The card shadow texture.
LateInit<DrawableTexture> CardShadow;

void client::InitialiseUI() {
    LockedTexture.init(DrawableTexture::LoadFromFile("assets/locked.webp"));
    CardShadow.init(DrawableTexture::LoadFromFile("assets/shadow.webp"));
    SilenceLog _;
    for (auto& p : PowerCardDatabase) {
        p.image.init(DrawableTexture::LoadFromFile(fs::Path{"assets/Cards"} / p.image_path));
    }
}

// =============================================================================
//  Card
// =============================================================================
Card::Card(
    Element* parent,
    Position pos
) : Widget{parent, pos},
    code{this, Text(), Position()},
    name{this, Text(), Position()},
    middle{this, Text(), Position::Center()},
    description{this, Text(), Position()},
    image{this, Position()} {
    code.colour = Colour::Black;
    name.colour = Colour::Black;
    middle.colour = Colour::Black;
    description.colour = Colour::Black;
}

void Card::draw() {
    auto _ = PushTransform();

    // Draw a drop shadow before anything else; scale it to the card size
    // since we only have a single drop shadow texture.
    {
        auto _ = Renderer::PushMatrix({}, CardSize[scale].wd / f32(CardSize[Preview].wd));
        Renderer::DrawTexture(*CardShadow, {-20, -20});
    }

    auto colour = variant == Variant::Regular ? outline_colour
                : variant == Variant::Added   ? alternate_colour
                : variant == Variant::Ghost   ? Colour{222, 222, 222, 255}
                                              : outline_colour.darken(.2f);

    AABB rect{{0, 0}, CardSize[scale]};
    Renderer::DrawRect(rect, colour.lighten(.1f), BorderRadius[scale]);
    if (selected) Renderer::DrawOutlineRect(
        rect,
        CardStacks::CardGaps[scale] / 2,
        Colour{50, 50, 200, 255},
        BorderRadius[scale]
    );

    Renderer::DrawOutlineRect(
        rect.shrink(Border[scale].wd, Border[scale].ht),
        Sz{Border[scale]},
        colour,
        BorderRadius[scale]
    );

    DrawChildren();

    // Draw the border *after* the children since it must be drawn
    // on top of the image.
    auto b = InnerBorder[scale];
    Renderer::DrawOutlineRect(
        rect.shrink(Border[scale].wd + b, Border[scale].ht + b),
        b,
        colour.darken(.1f),
        b
    );

    if (id.is_power()) Renderer::DrawOutlineRect(
        image.bounding_box.shrink(b),
        b,
        colour.darken(.1f),
        b
    );

    // Draw a white rectangle on top of this card if it is inactive.
    if (overlay == Overlay::Inactive) Renderer::DrawRect(
        rect,
        Colour{255, 255, 255, 200},
        BorderRadius[scale]
    );

    // TODO: Sounds that have been deleted or added to the word
    //       should be greyed out / orange (or a plus in the corner),
    //       respectively.
}

void Card::DrawChildren() {
    code.draw();
    image.draw();
    middle.draw();
    description.draw();

    // Do not draw the name if this is a small sound card.
    if (scale > OtherPlayer or id.is_power())
        name.draw();

    if (id.is_sound()) {
        auto offs = Padding[scale];
        for (int i = 0; i < count; ++i) Renderer::DrawRect(
            Position{-3 * offs, -(2 * offs + 2 * i * offs)}
                .hoffset(-Border[scale].ht)
                .voffset(-Border[scale].wd)
                .resolve(bounding_box, {5 * offs, offs}),
            {5 * offs, offs},
            Colour::Black
        );
    }
}

void Card::refresh(bool full) {
    // If the window was resized, we don’t need to update the
    // font size etc. every time. Crucially, this behaviour is
    // also used to manually override the card bounding box during
    // animations without it being reset on a partial refresh.
    if (not full) {
        RefreshBoundingBox();
        return;
    }

    bool power = id.is_power();
    UpdateBoundingBox(CardSize[scale]);
    code.font_size = CodeSizes[scale];
    name.font_size = NameSizes[scale];
    middle.font_size = MiddleSizes[scale];
    description.font_size = (power ? PowerDescriptionSizes : SoundDescriptionSizes)[scale];
    description.max_width = CardSize[scale].wd - 2 * Padding[scale] - 2 * Border[scale].wd;

    // Center the middle text.
    middle.fixed_height = CardSize[scale].ht;

    // Handle power-card-specific formatting.
    if (power) {
        // Set name position.
        //
        // For power cards, we want to make sure the image always stays
        // in the same place, even if we need multiple lines for the name;
        // set the height of the name field to a fixed value based on the
        // font’s strut (which *should* work for any font size), and center
        // the name vertically in that field.
        auto name_height = i32(1.75 * name.text.font.strut());
        name.pos = Position::HCenter(i32(-Border[scale].ht));
        name.max_width = description.max_width;
        name.fixed_height = name_height;

        // Position the image right below the name. Since the name field ends
        // up being larger than the height of the nam text, we don’t need to
        // add any extra vertical padding here.
        auto wd = CardSize[scale].wd - 2 * Border[scale].wd;
        auto ht = wd / 4 * 3; // Arbitrary aspect ratio.
        image.fixed_size = Sz{wd, ht};
        image.pos = Position{Border[scale].wd, -Border[scale].ht}.voffset(-name_height);

        // The description is below the image.
        description.pos = auto{image.pos}
                              .voffset(-ht - Padding[scale])
                              .hoffset(Padding[scale]);
    } else {
        code.pos = Position{Border[scale].wd + Padding[scale], -Border[scale].ht - Padding[scale]};
        name.pos = auto{code.pos}.voffset(i32(-code.text.height - 2 * Padding[scale]));
        name.max_width = CardSize[scale].wd / 3;
        name.fixed_height = 0;
        description.pos = Position::HCenter(Border[scale].ht + 3 * Padding[scale]);
    }

    // Finally, refresh our children.
    code.refresh(full);
    name.refresh(full);
    middle.refresh(full);
    description.refresh(full);
    image.refresh(full);
}

void Card::set_id(CardId ct) {
    if (ct == CardId::$$Count or _id == ct) return;
    _id = ct;
    auto& data = CardDatabase[+ct];

    // Common properties.
    count = std::saturate_cast<u8>(data.count_in_deck);
    name.update_text(std::string{data.name});

    // Sound card properties.
    if (ct.type() == CardType::SoundCard) {
        outline_colour = ct.is_consonant() ? ConsonantColour : VowelColour;
        alternate_colour = ct.is_consonant() ? ExtraConsonantColour : ExtraVowelColour;
        code.update_text(std::format( //
            "{}{}{}{}",
            ct.is_consonant() ? 'P' : 'F',
            data.place_or_frontness,
            ct.is_consonant() ? 'M' : 'H',
            data.manner_or_height
        ));

        middle.update_text(std::string{data.center});
        description.update_text( // clang-format off
            utils::join(data.converts_to, "\n", "{}", [](const auto& vec) {
                return std::format("→ {}", utils::join(vec, ", ", "{}", [](CardId id) {
                    return CardDatabase[+id].center;
                }));
            })
        ); // clang-format on
        description.reflow = Reflow::None;
        name.align = TextAlign::Left;
        image.texture = nullptr;
    }

    // Power card properties.
    else {
        auto& power = PowerCardDatabase[ct];
        outline_colour = data.count_in_deck == 1 ? UniquePowerColour : PowerColour;
        name.update_text(std::string{data.name});
        code.update_text("");
        middle.update_text("");
        description.update_text(std::string{power.rules});
        description.reflow = Reflow::Soft;
        name.align = TextAlign::Center;
        image.texture = &*power.image;
    }

    needs_refresh = true;
}

TRIVIAL_CACHING_SETTER(Card, Scale, scale)

// =============================================================================
//  Stack
// =============================================================================
void CardStacks::Stack::draw() {
    Group::draw();
    if (selected) {
        Assert(not widgets.empty(), "Selected empty stack?");
        auto& c = cards().front();
        Renderer::DrawOutlineRect(
            bounding_box,
            CardGaps[c.scale] / 2,
            Colour{50, 50, 200, 255},
            Card::BorderRadius[c.scale]
        );
    }

    if (locked) {
        auto _ = PushTransform();
        auto cs = Card::CardSize[scale];
        auto b = Card::Border[scale];
        auto p = Card::Padding[scale];
        auto sz = LockedTexture->size * Card::IconScale[scale];
        Renderer::DrawTextureScaled(
            *LockedTexture,
            Position{b.wd + p, -cs.ht + 2 * (b.ht + p)}.resolve(bounding_box, sz),
            Card::IconScale[scale]
        );
    }
}

void CardStacks::Stack::make_active(bool active) {
    overlay = active ? Card::Overlay::Default : Card::Overlay::Inactive;
}

void CardStacks::Stack::push(CardId card) {
    auto& c = create<Card>(Position());
    c.id = card;
    c.scale = scale;
    if (full) c.variant = Card::Variant::FullStackTop;
}

void CardStacks::Stack::refresh(bool full) {
    if (full) {
        for (auto& c : cards()) c.scale = scale;
        gap = -Card::CardSize[scale].ht + 2 * Card::Border[scale].ht;
    }

    Group::refresh(full);
}

TRIVIAL_CACHING_SETTER(
    CardStacks::Stack,
    Scale,
    scale
)

void CardStacks::Stack::set_overlay(Card::Overlay new_value) {
    for (auto& c : cards()) c.overlay = new_value;
}

// =============================================================================
//  CardStacks
// =============================================================================
auto CardStacks::add_stack() -> Stack& {
    return create<Stack>();
}

void CardStacks::add_stack(CardId c) {
    auto& card = add_stack().create<Card>(Position());
    card.id = c;

    // TODO: This starting position is currently arbitrary; actually make
    // it so this lines up w/ the deck if we’re drawing a card and so on.
    card.parent.as<Widget>().pos.yadjust = 600;
}

auto CardStacks::selected_child(xy rel_pos) -> SelectResult {
    auto res = Group::selected_child(rel_pos);
    if (res.widget) res.widget = [&] -> Widget* {
        switch (selection_mode) {
            case SelectionMode::Stack: return &res.widget->parent.as<Stack>();
            case SelectionMode::Card: return res.widget;
            case SelectionMode::Top: return &res.widget->parent.as<Stack>().top;
        }
        Unreachable();
    }();
    return res;
}

void CardStacks::refresh(bool full) {
    auto ch = stacks();
    if (ch.empty()) return;

    // If we’re allowed to scale up, determine the maximum scale that works (see
    // Group::ComputeDefaultLayout() for why we use our parent’s bounding box here).
    if (autoscale) {
        i32 width = max_width != 0 ? max_width : parent.bounding_box.size().wd;
        auto s = Scale(Scale::NumScales - 1);
        while (s != scale) {
            auto wd = i32(ch.size() * Card::CardSize[s].wd + (ch.size() - 1) * CardGaps[s]);
            if (wd < width) break;
            s = Scale(s - 1);
        }

        for (auto& c : ch) c.scale = s;
    }

    // Otherwise, set the scale if this is a full refresh.
    else if (full) {
        for (auto& c : ch) c.scale = scale;
    }

    // And refresh the group.
    Group::refresh(full);
}

void CardStacks::set_overlay(Card::Overlay new_value) {
    for (auto& c : stacks()) c.overlay = new_value;
}

TRIVIAL_CACHING_SETTER(CardStacks, bool, autoscale);
TRIVIAL_CACHING_SETTER(CardStacks, i32, max_width);
TRIVIAL_CACHING_SETTER(CardStacks, Scale, scale);
