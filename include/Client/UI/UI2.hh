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

/// The position of an element.
///
/// This class’s main purpose is to abstract away centering and anchoring
/// calculations.
struct Position {
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

/// Size policy of an element.
struct SizePolicy {
    enum : i32 {

    };

    /// These may be fixed values, or special, dynamic values.
    i32 xval{};
    i32 yval{};

    /// Create a fixed-size element.
    SizePolicy(i32 x, i32 y) : xval(x), yval(y) {}
    SizePolicy(Size size) : xval(size.wd), yval(size.ht) {}

    /// Get this as a fixed size.
    auto as_fixed() const -> Size {
        if (not fixed()) Log("Warning: Non-fixed size requested as fixed");
        return {xval, yval};
    }

    /// Whether this element has a fixed size.
    auto fixed() const -> bool { return xval >= 0 and yval >= 0; }
};

/// Controls how elements are laid out.
enum struct LayoutPolicy {
    /// Elements are packed at the start of the container.
    Packed,
};

/// Style data of an element.
struct Style {
    Colour background = Colour::Transparent;
    i32 border_radius = 0;
    SizePolicy size = {0, 0};
    LayoutPolicy layout = LayoutPolicy::Packed;
    bool vertical = false;
};

/// The root of the UI element hierarchy.
class Element {
    LIBBASE_IMMOVABLE(Element);

    /// The computed bounding box of this element, relative to its parent.
    Size computed_size = {};
    xy computed_pos = {};

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
    auto as() -> T& {
        auto ptr = cast<T>();
        Assert(ptr, "Failed to cast widget to desired type!");
        return *ptr;
    }

    /// Cast this to a certain type, returning nullptr on failure.
    template <std::derived_from<Element> T>
    auto cast() -> T* { return dynamic_cast<T*>(this); }

    /// Iterate over the children of this element.
    template <std::derived_from<Element> CastTo = Element>
    auto children() { return elements | vws::transform(&Element::as<CastTo>); }

    /*
    /// Check whether an element is a parent of this element.
    bool has_parent(Element* other);
    */

    /// Check if a widget has a certain type.
    template <std::derived_from<Element>... Ts>
    bool is() { return (dynamic_cast<Ts*>(this) or ...); }

    /*
    /// Check if this widget is currently being hovered over.
    bool is_hovered() const;

    /// Check if this widget is currently selected.
    bool is_selected() const;
    */

    /// Iterate over the parents of this widget, bottom to top.
    template <std::derived_from<Element> Type = Element>
    auto parents() -> std::generator<Type*> {
        for (auto* p = parent; p; p = p->parent)
            if (auto* el = dynamic_cast<Type*>(p))
                co_yield el;
    }

    /*
    /// Unselect the element.
    void unselect();*/

    /// Get all visible elements.
    auto visible_elements() {
        return elements | vws::filter([](auto& e) { return e.visible; });
    }

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
    void BuildPackedLayout();
    void RecomputeLayout();
};

class Screen : public Element {
    using Element::draw;

public:
    Renderer& renderer;

    /// Create a new screen.
    explicit Screen(Renderer& r) : Element(nullptr), renderer(r) {}

    /// Create a new element.
    template <std::derived_from<Element> El, typename... Args>
    auto create(Args&&... args) -> El& {
        return CreateImpl<El>(std::forward<Args>(args)...);
    }

    /// Draw the screen.
    void draw() { draw(renderer); }
};
} // namespace pr::client::ui

#endif // PRESCRIPTIVISM_UI_UI2_HH
