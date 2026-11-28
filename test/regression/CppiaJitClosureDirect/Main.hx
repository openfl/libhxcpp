// Under the JIT, a script calling one of its closures whose function takes the
// arguments and gives the result as they are, and is compiled, calls its code
// directly, as a call of a known function does. Anything else goes the general
// way.
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
class CompiledCounter {
	public var total = 0;

	public function new() {}

	public function add(x:Int):Int {
		total += x;
		return total;
	}

	public function addFloat(x:Float):Int {
		total += Std.int(x * 2);
		return total;
	}
}

class Compiled {
	static function hostLike(x:Int):Int
		return x * 100;

	public static function run() {
		// One call site, many kinds of function value
		var counter = new CompiledCounter();
		var offset = 5;
		var functions:Array<Dynamic> = [
			(x:Int) -> x + offset,
			(x:Float) -> x / 2,
			(x:Int) -> "s" + x,
			(x:Dynamic) -> x,
			counter.add,
			counter.addFloat,
			hostLike,
			Std.string,
			Math.abs,
			(x:Int, ?y:Int = 3) -> x * y,
			null,
		];
		var results = [];
		for (round in 0...3)
			for (f in functions) {
				var call:Int->Dynamic = f;
				results.push(try Std.string(call(round + 1)) catch (e:Dynamic) "throws");
			}
		Sys.println('one call site ${results.join(",")}');

		var asInts = [];
		for (round in 0...2)
			for (f in [(x:Int) -> x + 1, (x:Int) -> x * 3, counter.add, hostLike]) {
				var call:Int->Int = f;
				asInts.push(call(round + 10));
			}
		Sys.println('Int results $asInts ${counter.total}');

		// Captured variables of each type, read and changed
		var i = 1;
		var f = 0.5;
		var s = "a";
		var b = false;
		var o = [1];
		var change = (k:Int) -> {
			i += k;
			f *= 2;
			s += k;
			b = !b;
			o.push(k);
			return i;
		};
		var last = 0;
		for (k in 0...4)
			last = change(k);
		Sys.println('captured $last $i $f $s $b $o');

		// Recursion through a closure that calls itself through a captured variable
		var depth:Int->Int = null;
		depth = (n:Int) -> {
			var a = n * 2;
			var c = n + 1;
			return n == 0 ? 0 : 1 + depth(n - 1) + (a - 2 * n) + (c - n - 1);
		};
		Sys.println('recursion ${depth(500)}');

		// A throw from a closure called directly, inside and outside a try
		var thrower = (x:Int) -> {
			if (x % 3 == 2)
				throw "at " + x;
			return x;
		};
		var caught = [];
		for (k in 0...6)
			try {
				caught.push(Std.string(thrower(k)));
			} catch (e:String) {
				caught.push(e);
			}
		var guarded = (x:Int) -> {
			try {
				return thrower(x);
			} catch (e:String) {
				return -x;
			}
		};
		Sys.println('throws $caught ${guarded(4)} ${guarded(5)}');

		// Closures made in a loop, each with its own captured value
		var made = [for (k in 0...5) (x:Int) -> x * k + offset];
		var sum = 0;
		for (round in 0...3)
			for (m in made)
				sum += m(round);
		Sys.println('made in a loop $sum');

		// A bound method held as a value, called many times
		var bound = counter.add;
		for (k in 0...100)
			bound(k);
		Sys.println('bound ${counter.total}');
	}
}
