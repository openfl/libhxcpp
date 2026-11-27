# How a script calls the host

A cppia script calls a function of a host class in one of two ways. Which one is decided once, when the script loads.

**Typed.** The host has an entry for the function. The entry reads the arguments from the script's stack as they are, an Int as an Int and a Float as a Float, calls the function, and writes the result back. Nothing is looked up when the call runs, and nothing is allocated for it.

**By name.** For a member function, cppia asks the object for the function by its name each time the call runs, as `Reflect.field` does, which makes a closure. For a static function it keeps the function it found when the script loaded. Either way each argument is passed as a `Dynamic`, so a Float is allocated for the call. The result is the same, and the call costs many times more.

## Which calls are typed

- A function of a public host class. With `-D scriptable` the Haxe compiler writes a script function for each one.
- A function of a private host class, or of the class behind an abstract. The Haxe compiler writes no script functions for these, yet a script reaches them often: the inline functions of a public abstract or class put calls to them into the script. `openfl.Vector` is such an abstract, and `vector[i] = x` in a script is a call to `set` on a private class. The runtime makes an entry for each of their functions itself, from the function's C++ signature (`include/hx/ScriptCall.h`), by the rules the Haxe compiler uses for its own.
- A static function of `Math`, which the runtime implements itself.

## Which calls are still made by name

- A call through a value typed as a private interface.
- A function whose name is overloaded in C++, which only hand-written and extern classes have.
- A function that takes an argument by C++ reference.
- A call on a `Dynamic` value or through `Reflect`, as in native code.

Reading or writing a field of a private class is not a call, and none of this changes it.

## Finding them

`__scriptable_cppia_calls_by_name()` returns the host functions that the scripts loaded so far call by name, each as `package.Class.function`, sorted:

```haxe
var byName:Array<String> = untyped __global__.__scriptable_cppia_calls_by_name();
```

A function is listed when a script calls it on a value typed as a host class, or as a static of one, and cppia found nothing typed to link the call to. Calls on `Dynamic` values are not listed. An empty list means every such call is typed.

## What it costs the host

Entries are made only in a host built with `-D scriptable`, and only for classes that have no script functions. Each is one small function and one static record, linked into a list as the program starts. A program built without `-D scriptable` has none of it.
