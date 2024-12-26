#ifndef PRESCRIPTIVISM_UI_UI2_HH
#define PRESCRIPTIVISM_UI_UI2_HH

#include <Client/Render/Render.hh>

#include <base/Macros.hh>
#include <base/Properties.hh>

#include <numeric>

namespace pr::client::ui {
class Element;
class Screen;

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

/// Selection or hover behaviour.
enum class Selectable : u8 {
    /// Can be selected or hovered over.
    Yes,

    /// Cannot be selected or hovered over.
    No,

    /// As 'No', but does not elements below it from being selected.
    Transparent,
};

using Hoverable = Selectable;

/// Size policy of an element.
struct SizePolicy {
    /// Negative values are special.
    enum struct V : i32 {
        /// A label that resizes based on its text content.
        Computed = -1,
    };

    /// These may be fixed values, or special, dynamic values.
    i32 xval{};
    i32 yval{};

    /// Create a fixed-size element.
    SizePolicy(V v) : xval(+v), yval(+v) {}
    SizePolicy(i32 x, i32 y) : xval(x), yval(y) {}
    SizePolicy(Size size) : xval(size.wd), yval(size.ht) {}

    /// Dynamic size policies.
    static auto Computed() -> SizePolicy { return V::Computed; }

    /// Get this as a fixed size.
    [[nodiscard]] Size as_fixed() const {
        if (not is_fixed()) Log("Warning: Non-fixed size requested as fixed");
        return {xval, yval};
    }

    /// Whether this element has a fixed size.
    [[nodiscard]] bool is_fixed() const { return xval >= 0 and yval >= 0; }
    [[nodiscard]] bool is_fixed(Axis a) const { return operator[](a) >= 0; }

    /// Whether this element has a special dynamic size.
    [[nodiscard]] bool is_computed() const {
        return xval == +V::Computed or yval == +V::Computed;
    }

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
struct Style {
    /// The background colour of this element.
    Colour background = Colour::Transparent;

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
    Layout horizontal;

    /// Layout along the Y axis.
    Layout vertical = {Layout::OverlapCenter};

    /// The total gap on both axes.
    Size gap() { return Size{horizontal.gap, vertical.gap}; }

    /// Set the layout to be horizontal, with a gap.
    ///
    /// Diagonal layouts can be achieved by setting a non-overlapped
    /// layout for both axes.
    auto layout_horizontal(i32 gap = 0, Layout::Policy p = Layout::PackedCenter) -> Style& {
        horizontal.policy = p;
        horizontal.gap = gap;
        vertical.policy = Layout::OverlapCenter;
        return *this;
    }

    /// Set the layout to be vertical, with a gap.
    ///
    /// \see layout_horizontal()
    auto layout_vertical(i32 gap = 0, Layout::Policy p = Layout::PackedCenter) -> Style& {
        vertical.policy = p;
        vertical.gap = gap;
        horizontal.policy = Layout::OverlapCenter;
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
    /// Whether the element can be selected or hovered over.
    Selectable selectable : 2 = Selectable::No;
    Hoverable hoverable   : 2 = Hoverable::Yes;

    /// Whether the element’s layout needs to be recomputed, e.g. because
    /// its size changed or it gained a child that needs to be laid out.
    bool layout_changed : 1 = true;

    /// Element is rendered, ticked, and interactable.
    bool visible : 1 = true;

    /// The mouse is currently on this element.
    bool under_mouse : 1 = false;

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
    virtual void event_click() {}

    /// Event handler for when the mouse enters this element.
    ///
    /// \param rel_pos Mouse position relative to the parent element.
    virtual void event_mouse_enter(xy rel_pos) {}

    /// Event handler for when the mouse leaves this element.
    virtual void event_mouse_leave() {}

    /// Event handler for when a selected element is given text input.
    virtual void event_input(std::string_view text) {}

    /// Event handler for when the parent element is resized.
    virtual void event_resize() {}

    /// Refresh the element.
    virtual void refresh();

private:
    void BuildLayout(Layout l, Axis a, i32 total_extent, i32 max_extent);
    void RecomputeLayout();
};

/// A text element.
class Label : public Element {
    Text text;

public:
    explicit Label(
        Element* parent,
        std::string_view contents,
        FontSize sz,
        TextStyle text_style = TextStyle::Regular
    );

    void draw(Renderer& r) override;
    void refresh() override;
};

class Screen : public Element {
    using Element::draw;

public:
    Renderer& renderer;

    /// Create a new screen.
    explicit Screen(Renderer& r) : Element(nullptr), renderer(r) {}

    /// Draw the screen.
    void draw() { draw(renderer); }
};
} // namespace pr::client::ui

#endif // PRESCRIPTIVISM_UI_UI2_HH
