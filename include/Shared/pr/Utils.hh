#ifndef PRESCRIPTIVISM_SHARED_UTILS_HH
#define PRESCRIPTIVISM_SHARED_UTILS_HH

#define PR_SERIALISE(...)                                   \
    void serialise(::pr::ser::Writer& buf) const { buf(__VA_ARGS__); } \
    void deserialise(::pr::ser::Reader& buf) { buf(__VA_ARGS__); }


#endif //PRESCRIPTIVISM_SHARED_UTILS_HH
