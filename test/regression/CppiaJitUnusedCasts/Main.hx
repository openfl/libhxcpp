// Under the JIT, a cast to a class or interface whose value goes unused, as a
// statement, checked nothing, so a cast that fails ran on, where the
// interpreter and compiled code throw. Haxe's TestCasts.testInterfaces makes
// such casts in closures, which the JIT compiles since local functions are
// compiled.
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
interface CompiledFirst {}
interface CompiledSecond {}

class CompiledPlain implements CompiledFirst {
	public function new() {}
}

class CompiledBoth extends CompiledPlain implements CompiledSecond {
	public function new() {
		super();
	}
}

class Compiled {
	static function attempt(label:String, f:Void->Void) {
		try {
			f();
			Sys.println('$label passes');
		} catch (e:Dynamic) {
			Sys.println('$label throws');
		}
	}

	// A cast whose value goes unused, as a statement of a method
	static function toSecond(o:Dynamic) {
		cast(o, CompiledSecond);
	}

	static function toBoth(o:Dynamic) {
		cast(o, CompiledBoth);
	}

	public static function run() {
		var plain = new CompiledPlain();
		var both = new CompiledBoth();
		attempt("plain to first in a closure", function() cast(plain, CompiledFirst));
		attempt("plain to second in a closure", function() cast(plain, CompiledSecond));
		attempt("both to second in a closure", function() cast(both, CompiledSecond));
		attempt("plain to both in a closure", function() cast(plain, CompiledBoth));
		attempt("plain to second in a method", () -> toSecond(plain));
		attempt("both to second in a method", () -> toSecond(both));
		attempt("plain to both in a method", () -> toBoth(plain));
		attempt("both to both in a method", () -> toBoth(both));
		attempt("plain to second, used", function() {
			var s:CompiledSecond = cast(plain, CompiledSecond);
			Sys.println('used ${s == null}');
		});
		var after = 0;
		attempt("a statement after a failed cast", function() {
			cast(plain, CompiledSecond);
			after++;
		});
		Sys.println('ran on past it $after');
	}
}
