#include <opaque.hpp>

struct opaque {
    int value;
};

opaque* opaque_create(int value) {
    return new opaque { value };
}

int opaque_value(opaque* o) {
    return o->value;
}
