#ifndef PR_CLIENT_UI_ANIMATIONS_HH
#define PR_CLIENT_UI_ANIMATIONS_HH

#include <Client/UI/Effect.hh>

namespace pr::client {
class Group;
class Widget;
class RemoveGroupElement;
}

/// Animation that visually removes an element from the group by
/// sliding the elements to the left and right of it closer together.
///
/// Note: This does NOT actually remove the element from the group.
class pr::client::RemoveGroupElement final : public Animation {
    static constexpr auto Duration = 500ms;

    Renderer& r;
    Widget& el;
    Group& group;
    Size start_size, end_size;

public:
    explicit RemoveGroupElement(Renderer& r, Widget& el);

private:
    void Tick();
};

#endif // PR_CLIENT_UI_ANIMATIONS_HH
