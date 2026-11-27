// Under the JIT, reading an element of an array of objects as an object loads
// it in place, as reading it as anything else does, instead of calling
// __GetItem through a helper. Arrays of Ints, Bools, Floats and Strings still
// box their elements as __GetItem does.
// The same cases run as compiled code, then as a script with the JIT off and on,
// and print the same lines each time.
function main() {
	Sys.println("-- compiled");
	Compiled.run();
	var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		Sys.println(jit ? "-- script, JIT on" : "-- script, JIT off");
		cpp.cppia.Host.enableJit(jit);
		try {
			cpp.cppia.Host.runFile(script);
		} catch (e:Dynamic) {
			Sys.println("error: " + e);
		}
	}
}

// The same cases as Script.hx, compiled into the host
class CompiledItem {
	public var value:Int;
	public var next:CompiledItem;

	public function new(value:Int, next:CompiledItem) {
		this.value = value;
		this.next = next;
	}
}

class Compiled {
	static function kind(value:Dynamic):String {
		return if (value == null) "null" else if (Std.isOfType(value, Bool)) "Bool" else if (Std.isOfType(value, Int)) "Int" else
			if (Std.isOfType(value, Float)) "Float" else if (Std.isOfType(value, String)) "String" else if (Std.isOfType(value, CompiledItem))
				"object" else if (Std.isOfType(value, Array)) "Array" else "other";
	}

	public static function run() {
		var items = [new CompiledItem(1, null), null, new CompiledItem(3, new CompiledItem(4, null))];
		var indices = [0, 1, 2, 3, 7, -1, -100, 0x7fffffff, 0x80000000];
		var read = [];
		for (i in indices) {
			var item = items[i];
			read.push(item == null ? "null" : Std.string(item.value));
		}
		Sys.println('items $read');
		Sys.println('fields ${items[0].value} ${items[2].next.value} ${items[1] == null} ${items[2].next.next == null} ${kind(items[2])}');
		var asDynamic:Dynamic = items[2];
		Sys.println('as Dynamic ${asDynamic.value} ${kind(asDynamic)} ${kind(items[5])}');

		var mixed:Array<Dynamic> = [7, true, 2.5, 3.0, "s", null, [1, 2], new CompiledItem(9, null), 100000];
		Sys.println('Dynamic elements ${[for (i in 0...mixed.length + 2) kind(mixed[i])]}');
		var first:Dynamic = mixed[0];
		var flag:Dynamic = mixed[1];
		Sys.println('Dynamic values $first $flag ${mixed[2]} ${mixed[3]} ${mixed[4]} ${mixed[8]}');

		var nested = [[1, 2], [3], []];
		Sys.println('nested ${nested[0][1]} ${nested[1][0]} ${nested[2].length} ${nested[3] == null} ${kind(nested[1])}');

		var maybe:Array<Null<Int>> = [1, null, 300];
		Sys.println('Null<Int> ${[for (i in 0...4) kind(maybe[i])]} ${maybe[0]} ${maybe[2]}');

		var ints = [5, 600];
		var bools = [true, false];
		var strings = ["a", null];
		var floats = [1.5, 2.0];
		Sys.println('boxed elements ${kind(ints[0])} ${kind(ints[1])} ${kind(ints[2])} ${kind(bools[0])} ${kind(bools[1])} ${kind(strings[0])} ${kind(strings[1])} ${kind(floats[0])} ${kind(floats[1])}');

		var growing:Array<CompiledItem> = [];
		var sum = 0;
		for (i in 0...5000) {
			growing.push(new CompiledItem(i, i > 0 ? growing[i - 1] : null));
			sum += growing[i].value + (growing[i].next == null ? 0 : growing[i].next.value);
		}
		growing.resize(10);
		Sys.println('growing $sum ${growing[9].value} ${growing[10] == null} ${growing.length}');

		var chain = 0;
		var walk = growing[9];
		while (walk != null) {
			chain++;
			walk = walk.next;
		}
		Sys.println('chain $chain');
	}
}
