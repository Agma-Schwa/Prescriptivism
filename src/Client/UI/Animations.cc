#include <Client/UI/Animations.hh>
#include <Client/UI/UI.hh>

using namespace pr;
using namespace pr::client;

RemoveGroupElement::RemoveGroupElement(Renderer& r, Widget& el)
    : Animation{&RemoveGroupElement::Tick, Duration}, r{r}, el{el}, group{el.parent.as<Group>()} {
    prevent_user_input = true;
    el.selectable = Selectable::Transparent;
    el.visible = false;
    start_size = el.bounding_box.size();
    end_size = 0;

    // If the gap is negative, i.e. the elements overlap, then we want
    // to shrink this until its size is equal to the gap.
    if (group.gap < 0) (group.vertical ? end_size.ht : end_size.wd) += group.gap;
}

void RemoveGroupElement::Tick() {
    if (dt() > .5) asm volatile ("int3");
    auto sz = lerp_smooth(start_size, end_size, dt());
    Log("Before: {} After: {}", el.bounding_box.size(), sz);
    el.UpdateBoundingBox(sz);

    // FIXME: Hack. Just relayout the group instead.
    group.refresh(r, true);
}
