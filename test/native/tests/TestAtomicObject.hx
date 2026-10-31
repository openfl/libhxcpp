package tests;

import haxe.atomic.AtomicObject;
import utest.Assert;
import utest.Test;

// C++ has haxe.atomic.AtomicObject from the Haxe 5 that builds it on
// hx::atomic::AtomicObject
class TestAtomicObject extends Test {
    static function churn() {
        var kept = 0;
        for (i in 0...50000)
            kept += [i, i + 1].length;
        cpp.vm.Gc.run(false);
        cpp.vm.Gc.run(true);
    }

    function test_operations() {
        final o = new AtomicObject<Array<Int>>([1, 2, 3]);
        final first = o.load();

        Assert.same([1, 2, 3], first);
        // A failed exchange returns the value it found and changes nothing
        Assert.equals(first, o.compareExchange([1, 2, 3], [9]));
        Assert.equals(first, o.load());

        final second = [4, 5];
        Assert.equals(first, o.compareExchange(first, second));
        Assert.equals(second, o.load());

        final third = [6];
        Assert.equals(second, o.exchange(third));
        final fourth = [7];
        Assert.equals(fourth, o.store(fourth));
        Assert.equals(fourth, o.load());
    }

    function test_strings_compare_by_value() {
        final o = new AtomicObject<String>("a" + Std.string(1));

        Assert.equals("a1", o.compareExchange("a1", "b"));
        Assert.equals("b", o.load());
    }

    // Values only an AtomicObject holds survive collections, including values
    // stored after the AtomicObject itself was collected into the old generation
    function test_values_survive_collections() {
        final o = new AtomicObject<Array<Int>>([1, 2, 3]);
        churn();
        Assert.same([1, 2, 3], o.load());

        o.compareExchange(o.load(), [4, 5]);
        churn();
        Assert.same([4, 5], o.load());

        o.exchange([6]);
        churn();
        Assert.same([6], o.load());

        o.store([7]);
        churn();
        Assert.same([7], o.load());
    }
}
