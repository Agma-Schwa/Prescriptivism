#ifndef PRESCRIPTIVISM_UI_UI2_HH
#define PRESCRIPTIVISM_UI_UI2_HH

#include <Client/Render/Render.hh>

#include <base/Macros.hh>
#include <base/Properties.hh>

#include <numeric>

namespace pr::client::ui {
class Element;
class Screen;

constexpr Colour InactiveButtonColour{55, 55, 55, 255};
constexpr Colour DefaultButtonColour{36, 36, 36, 255};
constexpr Colour HoverButtonColour{23, 23, 23, 255};

/// An anchor point for positioning elements.
enum class Anchor : u8 {
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

/// Hover behaviour.
enum class Hoverable : u8 {
    /// Can be hovered over.
    Yes,

    /// Cannot be hovered over.
    No,

    /// As 'No', but does not elements below it from being hovered.
    Transparent,
};

/// Current state of the mouse buttons.
struct MouseState {
    xy pos{};
    bool left{};
    bool right{};
    bool middle{};
};

/// Size policy of an element.
struct SizePolicy {
    /// Negative values are special.
    enum V : i32 {
        /// Used to indicate that a dimension is computed dynamically during
        /// the refresh step.
        Computed = -1,

        /// First dynamic layout.
        DynamicLayoutStart = -100,

        /// Used to indicate that this element should fill all available space.
        Fill = DynamicLayoutStart - 0,
    };

    /// These may be fixed values, or special, dynamic values.
    i32 xval{};
    i32 yval{};

    /// Create a fixed-size element.
    SizePolicy(V v) : xval(+v), yval(+v) {}
    SizePolicy(i32 x, i32 y) : xval(x), yval(y) {}
    SizePolicy(Size size) : xval(size.wd), yval(size.ht) {}

    /// Dynamic size policies.
    static auto ComputedSize() -> SizePolicy { return Computed; }

    /// Check if this has a computed dimension.
    [[nodiscard]] bool is_partially_computed() const { return xval == Computed or yval == Computed; }

    /// Check if the size of this element must be computed when it is laid out.
    [[nodiscard]] bool is_partially_dynamic() const { return is_dynamic(Axis::X) or is_dynamic(Axis::Y); }
    [[nodiscard]] bool is_dynamic(Axis a) const { return operator[](a) <= DynamicLayoutStart; }

    /// Check if the size of this element never changes.
    [[nodiscard]] bool is_fixed() const { return is_fixed(Axis::X) and is_fixed(Axis::Y); }
    [[nodiscard]] bool is_fixed(Axis a) const { return operator[](a) >= 0; }

    /// Check if the size of this element can be computed statically, i.e. without
    /// taking into account other elements in the surrounding layout.
    [[nodiscard]] bool is_static() const { return not is_partially_dynamic(); }

    [[nodiscard]] auto operator[](Axis a) -> i32& { return a == Axis::X ? xval : yval; }
    [[nodiscard]] auto operator[](Axis a) const -> i32 { return a == Axis::X ? xval : yval; }
};

/// Controls how elements are laid out.
struct Layout {
    enum struct Policy {
        /// Elements are packed at the start of the container.
        ///
        /// [xxx      ]
        Packed,

        /// Elements are packed at the center of the container.
        ///
        /// [   xxx   ]
        PackedCenter,

        /// All elements are placed in the same position.
        ///
        /// This and OverlapCenter can be used to effectively disable
        /// layout along one axis.
        ///
        /// [x        ]
        ///  ^ all 3 'x's are here
        Overlap,

        /// All elements are placed in the same position, but centered
        /// in the middle of the container.
        ///
        /// [    x    ]
        ///      ^ all 3 'x's are here
        OverlapCenter,
    };

    using enum Policy;

    /// Layout policy (see above).
    Policy policy = Packed;

    /// Gap between each pair of elements; can be negative to cause
    /// them to overlap.
    i32 gap = 0;

    /// Lay out elements in reverse order. By default, elements are
    /// arranged left to right or top to bottom.
    bool reverse = false;

    [[nodiscard]] bool is_centered() const {
        return policy == PackedCenter or policy == OverlapCenter;
    }
};

/// Style data of an element.
///
/// TODO: Changing a style needs to refresh the layout, but we’ll
///       do that once we start caching and reusing styles.
///
/// TODO: Caching styles, as follows:
///
///   - Never expose a mutable style, always return a const& from the element.
///   - Store shared_ptrs to a style.
///   - When a style is changed (create a copy, and cascade the change down).
///   - It’s important not to override any properties the children have already
///     set while doing this, so add a bitset w/ a bit for every property that
///     indicates whether this child has overridden that property.
///   - Then, visit each child transitively and override the property.
///   - Also have a bit in the bitset to stop propagation (a la shadow dom).
struct Style {
    /// The background colour of this element.
    Colour background = Colour::Transparent;

    /// The overlay colour of this element.
    Colour overlay = Colour::Transparent;

    /// The text colour of this element.
    Colour text_colour = Colour::White;

    /// The border radius of the background.
    i32 border_radius = 0;

    /// Z-order relative to other elements in the same group; positive
    /// means in front.
    i32 z = 0;

    /// The intended size of this element, which may be either
    /// static or dynamic.
    SizePolicy size = {0, 0};

    /// Layout along the X axis.
    ByAxis<Layout> layout{{}, {Layout::OverlapCenter}};

    /// The total gap on both axes.
    Size gap() { return Size{layout.x.gap, layout.y.gap}; }

    /// Set the layout to be horizontal, with a gap.
    ///
    /// Diagonal layouts can be achieved by setting a non-overlapped
    /// layout for both axes.
    auto layout_horizontal(i32 gap = 0, Layout::Policy p = Layout::PackedCenter) -> Style& {
        layout.x.policy = p;
        layout.x.gap = gap;
        layout.y.policy = Layout::OverlapCenter;
        return *this;
    }

    /// Set the layout to be vertical, with a gap.
    ///
    /// \see layout_horizontal()
    auto layout_vertical(i32 gap = 0, Layout::Policy p = Layout::PackedCenter) -> Style& {
        layout.y.policy = p;
        layout.y.gap = gap;
        layout.x.policy = Layout::OverlapCenter;
        return *this;
    }
};

/// The root of the UI element hierarchy.
class Element {
    LIBBASE_IMMOVABLE(Element);

protected:
    /// The computed bounding box of this element, relative to its parent.
    Size computed_size = {};
    xy computed_pos = {};

private:
    /// The parent of this element. This is null for the root element.
    Readonly(Element*, parent);

    /// Used to scale this widget (and its children) by a factor.
    Property(f32, ui_scale, 1);

    /// The children of this element.
    StableVector<Element> elements;

public:
    /// Style of this element.
    Style style;

private:
    /// Whether the element can  hovered over.
    Hoverable hoverable : 2 = Hoverable::Yes;

    /// Whether the element’s layout needs to be recomputed, e.g. because
    /// its size changed or it gained a child that needs to be laid out.
    bool layout_changed : 1 = true;

    /// The mouse is currently on this element.
    bool under_mouse : 1 = false;

protected:
    /// Whether the element can be focused.
    bool focusable : 1 = false;

    /// Element is rendered, ticked, and interactable.
    bool visible : 1 = true;

public:
    explicit Element(Element* parent) : _parent(parent) {}

protected:
    /// Create an element with this as its parent.
    template <std::derived_from<Element> El, typename... Args>
    auto CreateImpl(Args&&... args) -> El& {
        layout_changed = true;
        return elements.emplace_back<El>(this, std::forward<Args>(args)...);
    }

public:
    virtual ~Element() = default;

    /// Cast this to a certain type, asserting on failure.
    template <std::derived_from<Element> T>
    [[nodiscard]] auto as() -> T& {
        auto ptr = cast<T>();
        Assert(ptr, "Failed to cast widget to desired type!");
        return *ptr;
    }

    /// Get the element’s bounding box.
    [[nodiscard]] auto box() const -> AABB { return {computed_pos, computed_size}; }

    /// Cast this to a certain type, returning nullptr on failure.
    template <std::derived_from<Element> T>
    [[nodiscard]] auto cast() -> T* { return dynamic_cast<T*>(this); }

    /// Iterate over the children of this element.
    template <std::derived_from<Element> CastTo = Element>
    [[nodiscard]] auto children() { return elements | vws::transform(&Element::as<CastTo>); }

    /// Create a new element.
    template <std::derived_from<Element> El, typename... Args>
    auto create(Args&&... args) -> El& {
        return CreateImpl<El>(std::forward<Args>(args)...);
    }

    /// Print the UI hierarchy.
    void dump();

    /// Focus this element.
    ///
    /// This does not perform any checks as to whether the element is
    /// supposed to be focusable.
    void focus();

    /*
    /// Check whether an element is a parent of this element.
    bool has_parent(Element* other);
    */

    /// Check if a widget has a certain type.
    template <std::derived_from<Element>... Ts>
    [[nodiscard]] bool is() { return (dynamic_cast<Ts*>(this) or ...); }

    /*
    /// Check if this widget is currently being hovered over.
    bool is_hovered() const;

    /// Check if this widget is currently selected.
    bool is_selected() const;
    */

    /// Get the widget’s parent screen
    auto parent_screen() -> Screen&;

    /// Iterate over the parents of this widget, bottom to top.
    template <std::derived_from<Element> Type = Element>
    [[nodiscard]] auto parents() -> std::generator<Type*> {
        for (auto* p = parent; p; p = p->parent)
            if (auto* el = dynamic_cast<Type*>(p))
                co_yield el;
    }

    /*
    /// Unselect the element.
    void unselect();*/

    /// Get all visible elements.
    [[nodiscard]] auto visible_elements() {
        return elements | vws::filter([](auto& e) { return e.visible; });
    }

    /// Get the element’s z order.
    ///
    /// This only applies to elements in the same group, e.g. if A, B, and
    /// C have increasing Z orders, and A and B are in the same group, and C
    /// is a child of A, then C will still be drawn below B—even though it
    /// has a larger z order—because the z order of its parent A in the same
    /// group as B is less than B’s z order.
    [[nodiscard]] auto z_order() const -> i32 { return style.z; }

    /// Draw this element.
    virtual void draw(Renderer& r);

    /// Event handler for when the mouse is clicked on this element.
    ///
    /// \return Whether the click should stop propagating.
    virtual bool event_click() { return false; }

    /// Event handler for when this element gains focus.
    ///
    /// Even for elements that are focused by being clicked on (which is
    /// most elements), prefer to implement any actions that should happen
    /// on focus in here, as there may be other ways to select an element
    /// besides clicking.
    virtual void event_focus_gained() {}

    /// Event handler for when this element loses focus.
    virtual void event_focus_lost() {}

    /// Event handler for when the mouse enters this element.
    virtual void event_mouse_enter() {}

    /// Event handler for when the mouse leaves this element.
    virtual void event_mouse_leave() {}

    /// Event handler for when a selected element is given text input.
    virtual void event_input(std::string_view) {}

    /// Event handler for when the parent element is resized.
    virtual void event_resize() {}

    /// Get the name of this element.
    virtual auto name() const -> std::string_view  { return "Element"; }

    /// Refresh the element.
    virtual void refresh();

    /// Tick the element.
    virtual void tick(MouseState& mouse, xy rel_pos);

private:
    void BuildLayout(Layout l, Axis a, i32 total_extent, i32 max_static_extent, i32 dynamic_els);
    void dump_impl(i32 indent);
    void tick_mouse(MouseState& mouse, xy rel_pos);
    void recompute_layout();
};

/// Single-line text element.
class TextElement : public Element {
protected:
    Text text;

    TextElement(
        Element* parent,
        std::string_view contents,
        FontSize sz,
        TextStyle text_style = TextStyle::Regular
    );

public:
    void draw(Renderer& r) override;
    void refresh() override;

protected:
    void RefreshImpl(const Text& text);
    void DrawCenteredText(Renderer& r, const Text& text, Colour colour);
};

/// A text element.
class Label : public TextElement {
public:
    explicit Label(
        Element* parent,
        std::string_view contents,
        FontSize sz,
        TextStyle text_style = TextStyle::Regular
    ) : TextElement(parent, contents, sz, text_style) {}

    auto name() const -> std::string_view override { return "Label"; }
};

class Screen : public Element {
    using Element::draw;
    using Element::tick;

    /// The currently focused element, if any.
    Property(Element*, active_element, nullptr);

public:
    Renderer& renderer;

    /// Create a new screen.
    explicit Screen(Renderer& r) : Element(nullptr), renderer(r) {}

    /// Draw the screen.
    void draw() { draw(renderer); }

    /// Tick the screen.
    void tick(MouseState& mouse) { tick(mouse, mouse.pos); }

    auto name() const -> std::string_view override {  return "Screen"; }

    bool event_click() override;
};

/// A single-line text editor.
class TextEdit : public TextElement {
    Text placeholder;

public:
    TextEdit(Element* parent, FontSize sz, TextStyle = TextStyle::Regular);

    void draw(Renderer& r) override;
    bool event_click() override;
    void event_focus_gained() override;
    void event_focus_lost() override;
    auto name() const -> std::string_view override { return "TextEdit"; }
    void refresh() override;
};

} // namespace pr::client::ui

#endif // PRESCRIPTIVISM_UI_UI2_HH
