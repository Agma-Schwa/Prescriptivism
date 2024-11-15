#ifndef PRESCRIPTIVISM_CLIENT_UI_MACROS_HH
#define PRESCRIPTIVISM_CLIENT_UI_MACROS_HH

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

#endif // PRESCRIPTIVISM_CLIENT_UI_MACROS_HH
