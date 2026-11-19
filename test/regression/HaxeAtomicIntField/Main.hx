import haxe.atomic.AtomicInt;

// An AtomicInt kept in a field must outlive collections. Haxe 4's own
// AtomicInt is a cpp.Pointer into a one-element array nothing else keeps, so
// the collector freed the array and the atomic read whatever reused its
// memory (HaxeFoundation/hxcpp#1225). A pointer boxed in a Dynamic field
// must keep what it points into the same way
class Holder {
	public var count:AtomicInt;
	public var boxed:Dynamic;

	public function new() {}
}

class Main {
	static inline var COUNT = 10000;

	static var holders:Array<Holder>;

	static function fill() {
		// The atomics and arrays are made in passes of their own, so the lines
		// they are in hold nothing else that lives
		for (i in 0...COUNT)
			holders[i].count = new AtomicInt(i);
		var arrays = [for (i in 0...COUNT) [i * 2]];
		for (i in 0...COUNT) {
			var pointer:cpp.Pointer<Int> = cpp.Pointer.ofArray(arrays[i]);
			holders[i].boxed = pointer;
		}
	}

	static function churn() {
		var junk = [];
		for (i in 0...200000)
			junk.push([0x5a5a5a5a]);
		return junk.length;
	}

	static function main() {
		holders = [for (i in 0...COUNT) new Holder()];
		fill();
		for (round in 0...4) {
			cpp.vm.Gc.run(true);
			churn();
		}
		cpp.vm.Gc.run(true);

		var wrongAtomic = 0;
		var wrongBoxed = 0;
		for (i in 0...COUNT) {
			var h = holders[i];
			if (h.count.add(1) != i || h.count.load() != i + 1)
				wrongAtomic++;
			var pointer:cpp.Pointer<Int> = h.boxed;
			if (pointer.value != i * 2)
				wrongBoxed++;
		}
		Sys.println('atomics wrong: $wrongAtomic');
		Sys.println('boxed pointers wrong: $wrongBoxed');
	}
}
