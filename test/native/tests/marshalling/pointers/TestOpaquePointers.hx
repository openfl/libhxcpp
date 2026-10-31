package tests.marshalling.pointers;

import utest.Assert;
import utest.Test;

@:semantics(value)
@:include('opaque.hpp')
@:cpp.PointerType({ type : 'opaque', namespace : [] })
private extern class Opaque {}

@:include('opaque.hpp')
private extern class OpaqueApi {
    @:native('opaque_create')
    static function create(value : Int) : Opaque;

    @:native('opaque_value')
    static function value(o : Opaque) : Int;
}

private class OpaqueHolder {
    public final opaque : Opaque;

    public function new(opaque) {
        this.opaque = opaque;
    }
}

class TestOpaquePointers extends Test {
    function test_class_field() {
        final h = new OpaqueHolder(OpaqueApi.create(42));

        Assert.equals(42, OpaqueApi.value(h.opaque));
    }

    function test_reflection_construction() {
        final h = new OpaqueHolder(OpaqueApi.create(7));
        final copy = Type.createInstance(OpaqueHolder, [ h.opaque ]);

        Assert.isTrue(copy.opaque == h.opaque);
        Assert.equals(7, OpaqueApi.value(copy.opaque));
    }

    function test_reflection_construction_with_null() {
        final copy = Type.createInstance(OpaqueHolder, [ null ]);

        Assert.isTrue(copy.opaque == null);
    }
}
