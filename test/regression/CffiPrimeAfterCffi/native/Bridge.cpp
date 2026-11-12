#define IMPLEMENT_API
#include <hx/CFFI.h>
#include <hx/CFFIPrime.h>
HxString afterEcho(HxString value) { return value; }
DEFINE_PRIME1(afterEcho);
HxString afterMakeText() { return alloc_hxs_utf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",9); }
DEFINE_PRIME0(afterMakeText);
