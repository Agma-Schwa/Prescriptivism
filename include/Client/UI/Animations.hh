#ifndef PR_CLIENT_UI_ANIMATIONS_HH
#define PR_CLIENT_UI_ANIMATIONS_HH

#include <Client/UI/Effect.hh>

namespace pr::client {
class RemoveGroupElement;
}

class pr::client::RemoveGroupElement final : public Animation {
    static constexpr auto Duration = 500ms;

public:
    /*RemoveGroupElement(Group& g) : Animation(&RemoveGroupElement::Tick, Duration) {}*/

private:
    void Tick();
    void draw(Renderer& r) override;
};

#endif //PR_CLIENT_UI_ANIMATIONS_HH
