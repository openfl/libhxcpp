#pragma once

// Only declared here, so code that includes this header can hold a pointer to
// it but can't instantiate anything that needs its size
struct opaque;

opaque* opaque_create(int value);
int opaque_value(opaque* o);
