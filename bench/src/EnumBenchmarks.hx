package;

enum Chain {
	End;
	Link(next:Chain);
	Pair(left:Chain, right:Int);
}

/** Std.string on a deeply nested enum, as a debug print of a list or syntax tree. **/
class EnumFormat extends Benchmark {
	static inline var DEPTH = 2048;
	static inline var REPS = 16;

	var value:Chain;

	public function new() {
		super("enum_format", "enum values", DEPTH * REPS);
	}

	override public function setup() {
		value = End;
		for (i in 0...DEPTH)
			value = (i & 1) == 0 ? Link(value) : Pair(value, i);
	}

	override public function run():Int {
		var sum = 0;
		for (r in 0...REPS) {
			var s = Std.string(value);
			var mid:Int = s.charCodeAt(s.length >> 1);
			sum = (sum * 31 + s.length + mid) | 0;
		}
		return sum;
	}
}

enum Shape {
	Square(side:Int);
	Circle(radius:Int);
}

enum Colour {
	Red;
	Green;
}

enum Signal {
	Stop;
	Go(speed:Int);
}

/**
	Std.isOfType(value, SomeEnum) on enum values held as Dynamic, half of them
	of that enum and half of another. Before libhxcpp #491 every enum value was
	of every enum, so the answers are kept in a field and the checksum counts
	the checks: a base from before then does the same work.
**/
class EnumIsOfType extends Benchmark {
	static inline var COUNT = 1 << 20;

	var values:Array<Dynamic>;

	public var found = 0;

	public function new() {
		super("enum_is_of_type", "checks", COUNT);
	}

	override public function setup() {
		values = [];
		for (i in 0...256)
			values.push((i & 1) == 0 ? (Square(i) : Dynamic) : (Red : Dynamic));
	}

	override public function run():Int {
		var found = 0;
		for (i in 0...COUNT) {
			if (Std.isOfType(values[i & 255], Shape))
				found++;
		}
		this.found = found;
		return COUNT;
	}
}

/**
	A thrown enum value and typed catches of three enums, the value's own
	caught by the second clause. Before libhxcpp #491 the first clause caught
	it, so the sum is kept in a field and the checksum counts the throws, as in
	EnumIsOfType.
**/
class EnumTypedCatch extends Benchmark {
	static inline var COUNT = 4096;

	var values:Array<Dynamic>;

	public var sum = 0;

	public function new() {
		super("enum_typed_catch", "throws", COUNT);
	}

	override public function setup() {
		values = [for (i in 0...16) Go(i)];
	}

	override public function run():Int {
		var sum = 0;
		for (i in 0...COUNT) {
			try {
				throw values[i & 15];
			} catch (e:Shape) {
				sum += 1000;
			} catch (e:Signal) {
				sum += switch (e) {
					case Go(speed): speed;
					case Stop: 0;
				}
			} catch (e:Colour) {
				sum += 2000;
			}
		}
		this.sum = sum;
		return COUNT;
	}
}

class Thrown1 {
	public var value:Int;

	public function new(value:Int) {
		this.value = value;
	}
}

class Thrown2 {
	public var value:Int;

	public function new(value:Int) {
		this.value = value;
	}
}

/** The same with two class types, to show catches of classes keep their cost. **/
class ClassTypedCatch extends Benchmark {
	static inline var COUNT = 4096;

	var values:Array<Dynamic>;

	public function new() {
		super("class_typed_catch", "throws", COUNT);
	}

	override public function setup() {
		values = [for (i in 0...16) new Thrown2(i)];
	}

	override public function run():Int {
		var sum = 0;
		for (i in 0...COUNT) {
			try {
				throw values[i & 15];
			} catch (e:Thrown1) {
				sum += 1000;
			} catch (e:Thrown2) {
				sum += e.value;
			}
		}
		return sum;
	}
}
