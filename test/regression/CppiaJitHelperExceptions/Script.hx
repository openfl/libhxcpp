// libhxcpp #465: exceptions from the native helpers jitted code calls.
// - Some helpers caught the exception into the context, but the jitted code
//   did not check for it, and ran on past the throw. The exception was held
//   only by the context, which the collector did not mark, so allocating
//   before the catch could free it.
// - Others did not catch at all, so the exception unwound through the jitted
//   frames, past the script's catch.
// Each case catches in its own function, interpreted and under the JIT, and
// counts the statements that ran after the throw.
class Thrower {
	public function new() {}

	public function toString():String
		throw "toString threw";
}

class Script {
	static var ranOn = 0;

	static function fieldAdd():String {
		try {
			var d:Dynamic = {s: "a"};
			var t:Dynamic = new Thrower();
			d.s += t;
			ranOn++;
			var parts = new Array<String>();
			for (i in 0...100000)
				parts.push("x" + i);
			ranOn += parts.length > 0 ? 1 : 0;
			return "no exception";
		} catch (e:Dynamic) {
			return "caught " + e;
		}
	}

	static function dynamicAdd():String {
		try {
			var d:Dynamic = "x";
			var t:Dynamic = new Thrower();
			d += t;
			ranOn++;
			return "no exception";
		} catch (e:Dynamic) {
			return "caught " + e;
		}
	}

	static function dynamicIndexAdd():String {
		try {
			var d:Dynamic = ["x"];
			var t:Dynamic = new Thrower();
			d[0] += t;
			ranOn++;
			return "no exception";
		} catch (e:Dynamic) {
			return "caught " + e;
		}
	}

	static function main() {
		for (round in 0...3)
			Sys.println("field += throwing toString, then allocating: " + fieldAdd());
		Sys.println("Dynamic += throwing toString: " + dynamicAdd());
		Sys.println("Dynamic index += throwing toString: " + dynamicIndexAdd());
		Sys.println("statements run after a throw: " + ranOn);
	}
}
