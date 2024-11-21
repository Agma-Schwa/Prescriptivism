#ifndef PRESCRIPTIVISM_CLIENT_UI_HH
#define PRESCRIPTIVISM_CLIENT_UI_HH

#include <Client/Render/GL.hh>
#include <Client/Render/Render.hh>

#include <Shared/Cards.hh>
#include <Shared/Constants.hh>
#include <Shared/Utils.hh>

#include <base/Base.hh>
#include <base/Text.hh>
#include <SDL3/SDL.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>

// Define a setter that updates the property value if it is
// different, and, if so, also tells the element to refresh
// itself on the next frame.
#define TRIVIAL_CACHING_SETTER(class, type, property, ...) \
    void class ::set_##property(type new_value) {          \
        if (_##property == new_value) return;              \
        _##property = new_value;                           \
        needs_refresh = true;                              \
        __VA_ARGS__;                                       \
    }

#define CACHING_SETTER(class, type, name, target, ...) \
    void class ::set_##name(type new_value) {          \
        if (target == new_value) return;               \
        target = new_value;                            \
        needs_refresh = true;                          \
        __VA_ARGS__;                                   \
    }

namespace pr::client {
struct Position;
class Element;
class Button;
class Screen;
class TextEdit;
class InputSystem;
class Label;
class Throbber;
class Image;
class Card;
class Widget;
class CardStacks;
class Group;
class Player;

enum class Anchor : u8;
enum class Selectable : u8;
using Hoverable = Selectable;

void InitialiseUI(Renderer& r);
} // namespace pr::client

namespace pr::client {
/// Intermediate widget that consists of a box that wraps some text.
class TextBox;

/// Container for widgets.
class WidgetHolder;

/// Result of hovering over or selecting a widget.
struct SelectResult;
using HoverResult = SelectResult;
} // namespace pr::client

/// An anchor point for positioning elements.
enum class pr::client::Anchor : base::u8 {
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    NorthWest,
    Center,
    Default = SouthWest,
};

/// Selection or hover behaviour.
enum class pr::client::Selectable : base::u8 {
    /// Can be selected or hovered over.
    Yes,

    /// Cannot be selected or hovered over.
    No,

    /// As 'No', but does not elements below it from being selected.
    Transparent,
};

struct pr::client::SelectResult {
    /// If non-null, the widget that was hovered over or selected.
    Widget* widget;

    /// If true, we should not stop searching for widgets to hover
    /// over or select, even if we didn’t find a widget.
    bool transparent;

private:
    /// Should we keep searching for widgets to hover over or select?
    ComputedProperty(bool, keep_searching, not widget and transparent);

    SelectResult(Widget* w, bool t) : widget(w), transparent(t) {}

public:
    static auto No(Selectable s) { return SelectResult{nullptr, s == Selectable::Transparent}; }
    static auto Yes(Widget* w) { return SelectResult{w, false}; }
    static auto TakeIf(Widget* w, Selectable s) { return s == Selectable::Yes ? Yes(w) : No(s); }
};

/// The position of an element.
///
/// This class’s main purpose is to abstract away centering and anchoring
/// calculations.
struct pr::client::Position {
    static constexpr i32 Centered = std::numeric_limits<i32>::min();

    /// Base position.
    xy base;

    /// Separate adjustment fields since the position may be centered,
    /// in which case adjustments can only be applied when the position
    /// is computed. These are always relative to the base position.
    i16 xadjust{};
    i16 yadjust{};

    /// Anchor of the position.
    Anchor anchor = Anchor::Default;

    constexpr Position() = default;
    constexpr Position(xy base, Anchor a = Anchor::Default) : base(base), anchor(a) {}
    constexpr Position(i32 x, i32 y, Anchor a = Anchor::Default) : base(x, y), anchor(a) {}
    constexpr Position(Axis a, i32 axis_value, i32 other, Anchor anchor = Anchor::Default)
        : base(a == Axis::X ? axis_value : other, a == Axis::Y ? axis_value : other), anchor(anchor) {}

    /// Create a position that is centered horizontally or vertically.
    static constexpr auto HCenter(i32 y, Anchor a = Anchor::Default) -> Position { return {Centered, y, a}; }
    static constexpr auto VCenter(i32 x, Anchor a = Anchor::Default) -> Position { return {x, Centered, a}; }
    static constexpr auto Center(Anchor a = Anchor::Default) -> Position { return {Centered, Centered, a}; }
    static constexpr auto Center(Axis a, i32 other, Anchor anchor = Anchor::Default) -> Position {
        return a == Axis::X ? HCenter(other, anchor) : VCenter(other, anchor);
    }

    /// Create a position from another position, but center it.
    static constexpr auto Center(Position pos) -> Position { return HCenter(VCenter(pos)); }
    static constexpr auto HCenter(Position pos) -> Position { return pos.base.x = Centered, pos; }
    static constexpr auto VCenter(Position pos) -> Position { return pos.base.y = Centered, pos; }

    /// Anchor the position.
    constexpr auto anchor_to(Anchor a) -> Position& {
        anchor = a;
        return *this;
    }

    /// Offset horizontally.
    constexpr auto hoffset(i32 offset) -> Position& {
        xadjust += std::saturate_cast<i16>(offset);
        return *this;
    }

    /// Resolve centering and anchors relative to the parent box.
    auto resolve(AABB parent_box, Size object_size) -> xy;
    auto resolve(Size parent_size, Size object_size) -> xy;

    /// Offset vertically.
    constexpr auto voffset(i32 offset) -> Position& {
        yadjust += std::saturate_cast<i16>(offset);
        return *this;
    }
};

/// User input handler.
class pr::client::InputSystem {
    struct MouseState {
        xy pos{};
        bool left{};
        bool right{};
        bool middle{};
    };

    struct Event {
        SDL_Keycode key;
        SDL_Keymod mod;
    };

    bool was_selected = false;

public:
    Renderer& renderer;
    std::u32string text_input;
    std::vector<Event> kb_events;
    MouseState mouse;
    bool quit = false;

    InputSystem(Renderer& r) : renderer(r) {}

    void game_loop(std::function<void()> tick);
    void process_events();
    void update_selection(bool is_element_selected);
};

/// The root of the UI element hierarchy.
class pr::client::Element {
    /// The bounding box of this element, *relative* to its parent.
    Readonly(AABB, bounding_box);

protected:
    Element() = default;

public:
    virtual ~Element() = default;

    /// Cast this to a certain type, asserting on failure.
    template <std::derived_from<Element> T>
    auto as() -> T& {
        auto ptr = cast<T>();
        Assert(ptr, "Failed to cast widget to desired type!");
        return *ptr;
    }

    /// Cast this to a certain type, returning nullptr on failure.
    template <std::derived_from<Element> T>
    auto cast() -> T* { return dynamic_cast<T*>(this); }

    /// Check if a widget has a certain type.
    template <std::derived_from<Element>... Ts>
    bool is() { return (dynamic_cast<Ts*>(this) or ...); }

    /// Draw this element.
    virtual void draw(Renderer& r) = 0;

protected:
    /// This is mainly intended to be used by Screen; widgets should
    /// generally use UpdateBoundingBox() instead.
    void SetBoundingBox(AABB aabb);
};

/// Element that is not a screen.
class pr::client::Widget : public Element {
    LIBBASE_IMMOVABLE(Widget);

    friend Element;
    friend Screen;

    Readonly(Element&, parent);
    Property(bool, needs_refresh, true);

    /// The scaled bounding box, used so translations when pushing a
    /// matrix center objects correctly according to their scaled size.
    /// Do NOT use this for anything else.
    AABB scaled_bounding_box;

public:
    bool hovered  : 1 = false; ///< Element is being hovered.
    bool selected : 1 = false; ///< Element is selected.
    bool visible  : 1 = true;  ///< Element is rendered, ticked, and interactable.

    Selectable selectable = Selectable::No;
    Hoverable hoverable = Hoverable::Yes;

    /// Position of the widget relative to its parent.
    Position pos;

    /// Scale this widget (and its children) by a factor.
    Property(f32, ui_scale, 1);

protected:
    explicit Widget(Element* parent, Position pos = {});

public:
    /// Event handler for when the mouse is clicked on this element.
    virtual void event_click(InputSystem&) {}

    /// Event handler for when a selected element is given text input.
    virtual void event_input(InputSystem&) {}

    /// Check whether an element is a parent of this widget.
    bool has_parent(Element* other);

    /// Determine which of our children is being hovered, if any. Widgets
    /// that don’t have children can just return themselves.
    ///
    /// The input is the mouse position relative to the parent element.
    virtual auto hovered_child(xy rel_pos) -> HoverResult;

    /// Get the widget’s parent screen
    auto parent_screen() -> Screen&;

    /// Resolve the position of this element.
    auto position() -> xy;

    /// Set the origin and scale used to draw the children of this widget;
    /// this *must* be called before any children are drawn, otherwise, the
    /// children’s positions will be incorrect.
    ///
    /// Prefer this over calling Renderer::push_matrix() directly; otherwise,
    /// elements may end up centered incorrectly.
    ///
    /// \see Renderer::push_matrix().
    auto PushTransform(Renderer& r) -> Renderer::MatrixRAII;

    /// Recompute bounding box etc.
    virtual void refresh(Renderer&) {}

    /// Determine which of our children is being selected, if any. Widgets
    /// that don’t have children can just return themselves.
    virtual auto selected_child(xy rel_pos) -> SelectResult;

    /// Unselect the element.
    void unselect();

protected:
    void UpdateBoundingBox(Size sz);

private:
    void unselect_impl(Screen& parent);
};

class pr::client::WidgetHolder {
protected:
    /// List of children.
    std::vector<std::unique_ptr<Widget>> widgets;

public:
    virtual ~WidgetHolder() = default;

    /// Iterate over the children of this element.
    template <typename CastTo = Widget>
    auto children(this auto&& self) {
        return std::forward_like<decltype(self)>(self.widgets) //
             | vws::transform([](auto& w) -> CastTo& { return static_cast<CastTo&>(*w); });
    }

    /// Get a widget by index.
    auto index_of(Widget& c) -> std::optional<u32>;

    /// Remove a widget. This asserts if the widget is not in the list.
    void remove(Widget& w);

    /// Get all visible elements.
    auto visible_elements() {
        return children() | vws::filter([](auto& e) { return e.visible; });
    }
};

/// A screen that displays elements and controls user
/// interaction. A screen is an element mainly so we
/// can set it as the parent of an element.
class pr::client::Screen : public Element
    , public WidgetHolder {
    LIBBASE_IMMOVABLE(Screen);

    friend void Widget::unselect_impl(Screen&);

    /// Previous size so we don’t refresh every frame.
    Size prev_size = {};

protected:
    /// The selected element.
    Widget* selected_element = nullptr;

    /// The hovered element.
    Widget* hovered_element = nullptr;

    /// Delete all children of this screen.
    void DeleteAllChildren();

public:
    Screen() = default;

    /// Create an element with this as its parent.
    template <std::derived_from<Widget> El, typename... Args>
    auto Create(Args&&... args) -> El& {
        auto el = std::make_unique<El>(this, std::forward<Args>(args)...);
        auto& ref = *el;
        widgets.push_back(std::move(el));
        return ref;
    }

    /// Code to run to reset a screen when it is entered.
    virtual void on_entered() {}

    /// Core to run when this screen is refreshed, before refreshing
    /// any child widgets.
    virtual void on_refresh(Renderer&) {}

    /// Render this screen.
    ///
    /// The default renderer renders all UI elements that are part of
    /// this screen.
    void draw(Renderer& r) override;

    /// Refresh all element positions.
    ///
    /// This recomputes the position of each element after the screen
    /// has been resized and before ticking/rendering.
    void refresh(Renderer& r);

    /// Tick this screen.
    ///
    /// The default tick handler visits all elements and performs UI
    /// interactions on them; thus, calling this handler is recommended
    /// if you override it.
    virtual void tick(InputSystem& input);
};

/// Label that supports reflowing text automatically.
class pr::client::Label : public Widget {
    Readonly(Text, text);

    /// Whether the text should reflow onto multiple lines if it
    /// exceeds a maximum width.
    ComputedProperty(Reflow, reflow, text.reflow);

    /// The maximum width of this text.
    Property(i32, max_width, std::numeric_limits<i32>::max());

    /// If set, the text will be centered vertically at the baseline
    /// within this height.
    Property(i32, fixed_height, 0);

    /// The font size of the text.
    ComputedProperty(FontSize, font_size, text.font_size);

    /// The alignment of the text.
    ComputedProperty(TextAlign, align, text.align);

public:
    /// The colour of the label.
    Colour colour = Colour::White;

    explicit Label(Element* parent, Text text, Position pos);
    explicit Label(Element* parent, std::string_view text, FontSize sz, Position pos);

    void draw(Renderer& r) override;
    void refresh(Renderer& r) override;
    void update_text(std::string_view new_text);
};

class pr::client::TextBox : public Widget {
protected:
    Text label;
    std::optional<Text> placeholder;
    i32 padding{};
    i32 min_wd{};
    i32 min_ht{};
    i32 cursor_offs = -1; // Cursor offset from the start of the text; -1 to disable.

    explicit TextBox(
        Element* parent,
        Text label,
        std::optional<Text> placeholder,
        Position pos,
        i32 padding,
        i32 min_wd = 0,
        i32 min_ht = 0
    );

    auto TextPos(const Text& text) -> xy;

public:
    void draw(Renderer& r) override;
    void refresh(Renderer& r) override;
    void update_text(std::string_view new_text);
    void update_text(Text new_text);

protected:
    void draw(Renderer& r, Colour text_colour);
};

class pr::client::Button : public TextBox {
public:
    std::function<void()> on_click{};

    explicit Button(
        Element* parent,
        std::string_view label,
        Position pos,
        Font& font = Renderer::current().font(FontSize::Medium),
        i32 padding = 0,
        i32 min_wd = 125,
        i32 min_ht = 0
    );

    explicit Button(
        Element* parent,
        std::string_view label,
        Position pos,
        std::function<void()> click_handler
    );

    void event_click(InputSystem&) override;
    void draw(Renderer& r) override;
};

class pr::client::TextEdit : public TextBox {
    struct Selection {
        i32 start;
        i32 end;
    };

    /// The actual contents of the text edit, which can be different
    /// from the label of the underlying TextBox in case this is a
    /// password input box.
    std::u32string text;

    /// Clusters for the *shaped* text; used mainly for cursor positioning.
    std::vector<TextCluster> clusters;

    /// Whether the text has changed.
    bool dirty = false;

    /// Whether text should be replaced with '•'.
    bool hide_text = false;

    /// The cursor index.
    i32 cursor = 0;

    /// Used to inhibit blinking during typing.
    u32 no_blink_ticks = 0;

    /// To be used once selecting text has been implemented, if ever.
    [[maybe_unused]] Selection sel{};

public:
    TextEdit(
        Element* parent,
        Position pos,
        std::string_view placeholder,
        Font& font = Renderer::current().font(FontSize::Medium),
        i32 padding = 0,
        bool hide_text = false,
        i32 min_wd = 250,
        i32 min_ht = 0
    );

    void draw(Renderer& r) override;
    void event_click(InputSystem& input) override;
    void event_input(InputSystem& input) override;
    void set_hide_text(bool hide);
    auto value() -> std::string;
    void value(std::u32string new_text);
};

class pr::client::Throbber : public Widget {
    static constexpr f32 R = 20; // Radius of the throbber.

    VertexArrays vao;

public:
    Throbber(Element* parent, Position pos);

    void draw(Renderer& r) override;
};

class pr::client::Image : public Widget {
    Property(DrawableTexture*, texture, nullptr);

    /// The size of the image. If x or y is 0, they are set from the texture.
    Property(Size, fixed_size, {});

public:
    Image(Element* parent, Position pos) : Widget(parent, pos) {}

    void draw(Renderer& r) override;
    void refresh(Renderer&) override;
};

class pr::client::Card : public Widget {
public:
    /// The scale of the card; this determines how large it is.
    enum Scale : u8 {
        OtherPlayer,
        Field,
        Hand,
        Preview,
        NumScales
    };

    /// This handles overlay effects, such as greying it out or adding
    /// a white overlay.
    enum struct Overlay : u8 {
        /// Used for most cards.
        Default,

        /// A card that can not be interacted with, e.g. because it’s not
        /// our turn, because there are no valid targets, or because it has
        /// no valid targets.
        Inactive,
    };

    /// Card variants that are used in specific circumstances.
    enum struct Variant : u8 {
        /// Used for most cards.
        Regular,

        /// Used for the topmost card in a full stack.
        FullStackTop,

        /// Used for sounds that have been removed from a word.
        Ghost,

        /// Used for sounds that have been added to a word.
        Added,
    };

    static constexpr Size CardSize[NumScales] = {Size{70, 100}, Size{140, 200}, Size{280, 400}, Size{420, 600}};
    static constexpr u16 Padding[NumScales] = {2, 3, 5, 7};
    static constexpr u16 InnerBorder[NumScales] = {1, 1, 2, 3};

    // The border is slightly uneven; this is because of an optical illusion
    // that makes vertical lines appear thinner than horizontal lines, so we
    // compensate for that here, by making the X border wider than the Y border.
    static constexpr Size Border[NumScales] = {{5, 4}, {9, 8}, {17, 16}, {25, 24}};
    static constexpr i32 BorderRadius[NumScales] = {5, 10, 20, 30};
    static constexpr f32 IconScale[NumScales] = {.25f, .5f, 1.f, 2.f};

private:
    static constexpr FontSize CodeSizes[NumScales] = {FontSize::Normal, FontSize::Medium, FontSize::Huge, FontSize::Title};
    static constexpr FontSize NameSizes[NumScales] = {FontSize::Small, FontSize::Normal, FontSize::Medium, FontSize::Large};
    static constexpr FontSize SoundDescriptionSizes[NumScales] = {FontSize::Normal, FontSize::Medium, FontSize::Large, FontSize::Huge};
    static constexpr FontSize PowerDescriptionSizes[NumScales] = {FontSize::Small, FontSize::Normal, FontSize::Intermediate, FontSize::Medium};
    static constexpr FontSize MiddleSizes[NumScales] = {FontSize::Medium, FontSize::Huge, FontSize::Title, FontSize::Gargantuan};

    Property(CardId, id, CardId::$$Count);
    Property(Scale, scale, Field);
    u8 count{};
    bool needs_full_refresh = true; // Set initially so we recalculate everything.
    Label code;
    Label name;
    Label middle;
    Label description;
    Colour outline_colour;
    Colour alternate_colour;
    Image image;

public:
    Overlay overlay = Overlay::Default;
    Variant variant = Variant::Regular;

    Card(Element* parent, Position pos);

    void draw(Renderer& r) override;
    void refresh(Renderer&) override;

private:
    void DrawChildren(Renderer& r);
};

/// A group of widgets, arranged horizontally or vertically.
///
/// Besides holding and aligning widgets, a group is also the only
/// widget whose children can be accessed, altered, and removed from
/// the outside.
///
/// The children of any other widget MUST NOT be altered from outside
/// that widget, as only it knows how to deal with changes to it (e.g.
/// refreshing, drawing, etc.).
class pr::client::Group : public Widget
    , public WidgetHolder {
    /// The intended gap size; 0 means no gap; the layout algorithm will
    /// try to make gaps as large as possible without exceeding this size.
    // FIXME: Rename to just 'gap'.
    Property(i32, max_gap, 10);

    /// Whether the elements of this group should be laid out vertically.
    Property(bool, vertical, false);

    /// The positioning of the children within this element on the
    /// primary axis (i.e. the layout direction). By default, elements
    /// are centered on the axis on which they are laid out.
    Property(i32, alignment, Position::Centered);

public:
    Group(Element* parent, Position pos) : Widget(parent, pos) {}

    /// Create an element with this as its parent.
    template <std::derived_from<Widget> El, typename... Args>
    auto create(Args&&... args) -> El& {
        auto el = new El(this, std::forward<Args>(args)...);
        widgets.emplace_back(el);
        needs_refresh = true;
        return *el;
    }

    /// Remove all children from this group.
    void clear();

    /// Check if this group contains an element.
    auto contains(Widget* c) -> bool { return &c->parent == this; }

    /// Make all children of this group selectable.
    void make_selectable(Selectable new_value = Selectable::Yes);

    /// Swap two elements of the group.
    void swap(Widget* a, Widget* b);

    void draw(Renderer& r) override;
    auto hovered_child(xy rel_pos) -> HoverResult override;
    void refresh(Renderer&) override;
    auto selected_child(xy rel_pos) -> SelectResult override;

private:
    auto HoverSelectHelper(
        xy rel_pos,
        auto (Widget::*accessor)(xy)->SelectResult,
        Selectable Widget::* property
    ) -> SelectResult;
};

/// A group of widgets, each of which are a stack of cards
/// positioned on top of each other.
class pr::client::CardStacks final : public Group {
    using Scale = Card::Scale;

public:
    enum struct SelectionMode : u8 {
        Stack, ///< Select an entire stack.
        Card,  ///< Select individual cards in a stack.
        Top,   ///< Select the topmost card in each stack.
    };

    class Stack final : public Group {
        friend CardStacks;
        friend Group;

        /// The common scale of all cards in this stack.
        Property(Scale, scale, Scale::Field);

        /// Set the overlay for all cards of this stack.
        Writeonly(Card::Overlay, overlay);

        /// Get the parent stacks.
        ComputedReadonly(CardStacks&, parent, Widget::get_parent().as<CardStacks>());

        /// Get the topmost card.
        ComputedReadonly(Card&, top, static_cast<Card&>(*widgets.back()));

        /// Whether this stack is full.
        ComputedReadonly(bool, full, widgets.size() == constants::MaxSoundStackSize);

        Stack(Element* parent, Position pos) : Group(parent, pos) {
            vertical = true;
        }

    public:
        /// Whether this stack is locked.
        bool locked = false;

        /// Get the Id of a card in this stack.
        auto operator[](usz i) -> CardId {
            Assert(i < widgets.size(), "Index out of bounds!");
            return static_cast<Card&>(*widgets[i]).id;
        }

        auto cards() { return children<Card>(); }
        auto index_of(Card& c) -> std::optional<u32> { return Group::index_of(c); }
        void push(CardId card);

        void draw(Renderer& r) override;
        void refresh(Renderer&) override;

        /// Validation API.
        bool stack_is_locked() const { return locked; }
        bool stack_is_full() const { return full; }
    };

    static constexpr i32 CardGaps[Scale::NumScales]{5, 10, 20};

private:
    /// The scale that is set for all cards in the group.
    Property(Scale, scale, Scale::Field);

    /// The maximum width of the entire group. Ignored if 0.
    Property(i32, max_width, 0);

    /// Whether the group should scale to the next available
    /// card size if possible.
    Property(bool, autoscale, false);

public:
    /// The owner of this stack, if any.
    Player* owner = nullptr;

    /// How the group should handle selections.
    SelectionMode selection_mode = SelectionMode::Stack;

    CardStacks(Element* parent, Position pos) : CardStacks(parent, pos, {}) {}
    CardStacks(Element* parent, Position pos, std::span<CardId> cards) : Group(parent, pos) {
        for (auto c : cards) add_stack(c);
    }

    /// Get a stack in this stack group.
    auto operator[](usz i) -> Stack& {
        Assert(i < widgets.size(), "Index out of bounds!");
        return static_cast<Stack&>(*widgets[i]);
    }

    /// Add a stack of cards containing a single card with the given Id.
    void add_stack(CardId c);

    /// Get a range containing the Ids of all topmost cards in each stack.
    auto ids() {
        return children<Stack>()               //
             | vws::transform(&Stack::get_top) //
             | vws::transform(&Card::get_id);
    }

    /// Get a stack by index.
    ///
    /// This has the same behaviour as Group::index_of(), except that it
    /// only allows passing in stacks.
    auto index_of(Stack& c) -> std::optional<u32> { return Group::index_of(c); }

    /// Get a range of all the topmost cards in each stack.
    auto top_cards() { return children<Stack>() | vws::transform(&Stack::get_top); }

    /// Remove a stack from this group.
    void remove(Stack& s) { Group::remove(s); }

    /// Set the display state for all children.
    void set_overlay(Card::Overlay state);

    /// Get the card stacks in this group.
    auto stacks(this auto&& self) {
        return FWD(self).template children<Stack>();
    }

    auto selected_child(xy rel_pos) -> SelectResult override;
    void refresh(Renderer&) override;
};

template <>
struct std::formatter<pr::client::Position> : std::formatter<std::string> {
    template <typename FormatContext>
    auto format(pr::client::Position pos, FormatContext& ctx) const {
        return std::formatter<std::string>::format(
            std::format(
                "Position(base={}, xadjust={}, yadjust={}, anchor={})",
                pos.base,
                pos.xadjust,
                pos.yadjust,
                std::to_underlying(pos.anchor)
            ),
            ctx
        );
    }
};

#endif // PRESCRIPTIVISM_CLIENT_UI_HH
